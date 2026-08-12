#include "mainwindow.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPixmap>
#include <QSplitter>
#include <QVBoxLayout>
#include <QWidget>

#include <climits>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_player(new FFmpegPlayer(this))
{
    setWindowTitle(QStringLiteral("HgyPlayer"));
    resize(1100, 700);

    auto *central = new QWidget(this);
    setCentralWidget(central);

    m_openBtn = new QPushButton(QStringLiteral("打开"), central);
    m_playBtn = new QPushButton(QStringLiteral("播放"), central);
    m_stopBtn = new QPushButton(QStringLiteral("停止"), central);
    m_playBtn->setEnabled(false);
    m_stopBtn->setEnabled(false);

    m_seekSlider = new QSlider(Qt::Horizontal, central);
    m_seekSlider->setRange(0, 0);
    m_seekSlider->setEnabled(false);

    m_timeLabel = new QLabel(QStringLiteral("00:00 / 00:00"), central);
    m_timeLabel->setMinimumWidth(110);

    m_videoLabel = new QLabel(QStringLiteral("打开文件后点击播放"), central);
    m_videoLabel->setAlignment(Qt::AlignCenter);
    m_videoLabel->setMinimumSize(640, 360);
    m_videoLabel->setStyleSheet(QStringLiteral("QLabel { background: #111111; color: #cccccc; }"));

    m_infoEdit = new QPlainTextEdit(central);
    m_infoEdit->setReadOnly(true);
    m_infoEdit->setPlaceholderText(QStringLiteral("媒体信息"));

    auto *btnRow = new QHBoxLayout;
    btnRow->addWidget(m_openBtn);
    btnRow->addWidget(m_playBtn);
    btnRow->addWidget(m_stopBtn);
    btnRow->addWidget(m_seekSlider, 1);
    btnRow->addWidget(m_timeLabel);

    auto *splitter = new QSplitter(Qt::Horizontal, central);
    splitter->addWidget(m_videoLabel);
    splitter->addWidget(m_infoEdit);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);

    auto *layout = new QVBoxLayout(central);
    layout->addLayout(btnRow);
    layout->addWidget(splitter, 1);

    connect(m_openBtn, &QPushButton::clicked, this, &MainWindow::onOpen);
    connect(m_playBtn, &QPushButton::clicked, this, &MainWindow::onPlayPause);
    connect(m_stopBtn, &QPushButton::clicked, this, &MainWindow::onStop);

    connect(m_seekSlider, &QSlider::sliderPressed, this, &MainWindow::onSeekPressed);
    connect(m_seekSlider, &QSlider::sliderReleased, this, &MainWindow::onSeekReleased);
    connect(m_seekSlider, &QSlider::valueChanged, this, &MainWindow::onSeekMoved);

    // UI 只订阅播放器信号，不介入 demux/解码/时钟
    connect(m_player, &FFmpegPlayer::frameReady, this, &MainWindow::onFrameReady);
    connect(m_player, &FFmpegPlayer::positionChanged, this, &MainWindow::onPositionChanged);
    connect(m_player, &FFmpegPlayer::stateChanged, this, &MainWindow::onStateChanged);
    connect(m_player, &FFmpegPlayer::errorOccurred, this, &MainWindow::onPlayerError);
    connect(m_player, &FFmpegPlayer::playbackFinished, this, &MainWindow::onPlaybackFinished);
}

MainWindow::~MainWindow() = default;

void MainWindow::onOpen()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("选择视频文件"),
        QString(),
        QStringLiteral("Media Files (*.mp4 *.mkv *.avi *.mov *.flv *.ts *.m4v *.mp3 *.wav);;All Files (*.*)"));
    if (path.isEmpty()) {
        return;
    }

    m_player->stop();

    // 打开/解析媒体信息全部由播放器完成
    if (!m_player->open(path)) {
        QMessageBox::warning(this, QStringLiteral("打开失败"), m_player->lastError());
        setTransportEnabled(false);
        return;
    }

    showMediaSummary(m_player->mediaInfo());

    const qint64 duration = qMax<qint64>(0, m_player->durationMs());
    m_seekSlider->setRange(0, static_cast<int>(qMin(duration, static_cast<qint64>(INT_MAX))));
    m_seekSlider->setValue(0);
    m_seekSlider->setEnabled(duration > 0);
    setTransportEnabled(true);
    m_playBtn->setText(QStringLiteral("播放"));
    m_videoLabel->setText(QStringLiteral("已加载，点击播放"));
    updateTimeLabel();
}

void MainWindow::onPlayPause()
{
    if (!m_player->isOpen()) {
        return;
    }
    if (m_player->state() == FFmpegPlayer::State::Playing) {
        m_player->pause();
    } else {
        m_player->play();
    }
}

void MainWindow::onStop()
{
    m_player->stop();
    m_seekSlider->setValue(0);
    updateTimeLabel();
}

void MainWindow::onSeekPressed()
{
    m_sliderPressed = true;
}

void MainWindow::onSeekReleased()
{
    if (!m_player->isOpen()) {
        m_sliderPressed = false;
        return;
    }

    m_sliderPressed = false;
    m_player->seek(m_seekSlider->value());
    updateTimeLabel();
}

void MainWindow::onSeekMoved(int value)
{
    if (m_sliderPressed) {
        // 拖动中只预览时间文字，松手后再真正 seek
        m_timeLabel->setText(QStringLiteral("%1 / %2")
                                 .arg(formatTime(value), formatTime(m_player->durationMs())));
    }
}

void MainWindow::onFrameReady(const QImage &frame, qint64)
{
    if (frame.isNull()) {
        return;
    }
    m_videoLabel->setPixmap(QPixmap::fromImage(frame).scaled(
        m_videoLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void MainWindow::onPositionChanged(qint64 ms)
{
    // seek 过期进度过滤已在 FFmpegPlayer 内部完成，此处只刷新 UI
    if (!m_sliderPressed && m_seekSlider->isEnabled()) {
        m_seekSlider->blockSignals(true);
        const qint64 maxPos = m_seekSlider->maximum();
        const qint64 clamped = qBound(qint64(0), ms, maxPos);
        m_seekSlider->setValue(static_cast<int>(clamped));
        m_seekSlider->blockSignals(false);
    }
    updateTimeLabel();
}

void MainWindow::onStateChanged(FFmpegPlayer::State state)
{
    switch (state) {
    case FFmpegPlayer::State::Playing:
        m_playBtn->setText(QStringLiteral("暂停"));
        break;
    case FFmpegPlayer::State::Paused:
        m_playBtn->setText(QStringLiteral("继续"));
        break;
    case FFmpegPlayer::State::Stopped:
        m_playBtn->setText(QStringLiteral("播放"));
        break;
    }
}

void MainWindow::onPlayerError(const QString &message)
{
    QMessageBox::warning(this, QStringLiteral("播放错误"), message);
}

void MainWindow::onPlaybackFinished()
{
    m_playBtn->setText(QStringLiteral("播放"));
}

void MainWindow::showMediaSummary(const MediaInfo &info)
{
    QString text;
    text += QStringLiteral("文件: %1\n").arg(info.filePath);
    text += QStringLiteral("容器: %1 (%2)\n").arg(info.formatName, info.formatLongName);
    text += QStringLiteral("时长: %1\n").arg(formatTime(info.durationMs));
    text += QStringLiteral("码率: %1 bps\n").arg(info.bitrate);
    text += QStringLiteral("视频流: %1, 音频流: %2\n\n")
                .arg(info.videoStreamIndex)
                .arg(info.audioStreamIndex);

    for (const MediaStreamInfo &s : info.streams) {
        text += QStringLiteral("---- stream #%1 [%2] ----\n").arg(s.index).arg(s.mediaType);
        text += QStringLiteral("codec: %1\n").arg(s.codecName);
        if (s.mediaType == QLatin1String("video")) {
            text += QStringLiteral("%1x%2 @ %3 fps\n")
                        .arg(s.width)
                        .arg(s.height)
                        .arg(s.frameRate, 0, 'f', 2);
        } else if (s.mediaType == QLatin1String("audio")) {
            text += QStringLiteral("%1 Hz, %2 ch\n").arg(s.sampleRate).arg(s.channels);
        }
        text += QLatin1Char('\n');
    }
    m_infoEdit->setPlainText(text);
}

void MainWindow::updateTimeLabel()
{
    const qint64 pos = m_sliderPressed ? m_seekSlider->value() : m_player->positionMs();
    m_timeLabel->setText(QStringLiteral("%1 / %2")
                             .arg(formatTime(pos), formatTime(m_player->durationMs())));
}

void MainWindow::setTransportEnabled(bool enabled)
{
    m_playBtn->setEnabled(enabled);
    m_stopBtn->setEnabled(enabled);
    if (!enabled) {
        m_seekSlider->setEnabled(false);
    }
}

QString MainWindow::formatTime(qint64 ms)
{
    if (ms < 0) {
        return QStringLiteral("--:--");
    }
    const qint64 totalSec = ms / 1000;
    const qint64 h = totalSec / 3600;
    const qint64 m = (totalSec % 3600) / 60;
    const qint64 s = totalSec % 60;
    if (h > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(h)
            .arg(m, 2, 10, QChar('0'))
            .arg(s, 2, 10, QChar('0'));
    }
    return QStringLiteral("%1:%2").arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
}
