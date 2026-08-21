#include "mainwindow.h"

#include "FFmpegVideoParser.h"
#include "render/D3D11SharedDevice.h"
#include "render/IBrowserPlayback.h"
#include "render/VideoRendererFactory.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QMetaObject>
#include <QSplitter>
#include <QVBoxLayout>
#include <QWidget>

#include <climits>
#include <memory>

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

    m_rendererCombo = new QComboBox(central);
    m_rendererCombo->addItem(QStringLiteral("QImage"), int(IVideoRenderer::Backend::QImage));
    m_rendererCombo->addItem(QStringLiteral("OpenGL (Qt)"), int(IVideoRenderer::Backend::OpenGL));
#ifdef Q_OS_WIN
    m_rendererCombo->addItem(QStringLiteral("OpenGL"), int(IVideoRenderer::Backend::OpenGLNative));
    m_rendererCombo->addItem(QStringLiteral("D3D11"), int(IVideoRenderer::Backend::D3D11));
    m_rendererCombo->addItem(QStringLiteral("D3D11 硬解"), int(IVideoRenderer::Backend::D3D11Hw));
    m_rendererCombo->addItem(QStringLiteral("CEF (Browser)"), int(IVideoRenderer::Backend::Cef));
#endif
    m_rendererCombo->setToolTip(QStringLiteral("视频渲染后端"));

    m_seekSlider = new QSlider(Qt::Horizontal, central);
    m_seekSlider->setRange(0, 0);
    m_seekSlider->setEnabled(false);

    m_timeLabel = new QLabel(QStringLiteral("00:00 / 00:00"), central);
    m_timeLabel->setMinimumWidth(110);

    m_videoHost = new QWidget(central);
    auto *videoLayout = new QVBoxLayout(m_videoHost);
    videoLayout->setContentsMargins(0, 0, 0, 0);
    videoLayout->setSpacing(0);

    m_infoEdit = new QPlainTextEdit(central);
    m_infoEdit->setReadOnly(true);
    m_infoEdit->setPlaceholderText(QStringLiteral("媒体信息"));

    auto *btnRow = new QHBoxLayout;
    btnRow->addWidget(m_openBtn);
    btnRow->addWidget(m_playBtn);
    btnRow->addWidget(m_stopBtn);
    btnRow->addWidget(m_rendererCombo);
    btnRow->addWidget(m_seekSlider, 1);
    btnRow->addWidget(m_timeLabel);

    m_splitter = new QSplitter(Qt::Horizontal, central);
    m_splitter->addWidget(m_videoHost);
    m_splitter->addWidget(m_infoEdit);
    m_splitter->setStretchFactor(0, 3);
    m_splitter->setStretchFactor(1, 1);

    auto *layout = new QVBoxLayout(central);
    layout->addLayout(btnRow);
    layout->addWidget(m_splitter, 1);

    // 默认 QImage 后端
    switchRenderer(IVideoRenderer::Backend::QImage);

    connect(m_openBtn, &QPushButton::clicked, this, &MainWindow::onOpen);
    connect(m_playBtn, &QPushButton::clicked, this, &MainWindow::onPlayPause);
    connect(m_stopBtn, &QPushButton::clicked, this, &MainWindow::onStop);
    connect(m_rendererCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            &MainWindow::onRendererChanged);

    connect(m_seekSlider, &QSlider::sliderPressed, this, &MainWindow::onSeekPressed);
    connect(m_seekSlider, &QSlider::sliderReleased, this, &MainWindow::onSeekReleased);
    connect(m_seekSlider, &QSlider::valueChanged, this, &MainWindow::onSeekMoved);

    connect(m_player, &FFmpegPlayer::frameReady, this, &MainWindow::onFrameReady);
    connect(m_player, &FFmpegPlayer::gpuFrameReady, this, &MainWindow::onGpuFrameReady);
    connect(m_player, &FFmpegPlayer::positionChanged, this, &MainWindow::onPositionChanged);
    connect(m_player, &FFmpegPlayer::stateChanged, this, &MainWindow::onStateChanged);
    connect(m_player, &FFmpegPlayer::errorOccurred, this, &MainWindow::onPlayerError);
    connect(m_player, &FFmpegPlayer::playbackFinished, this, &MainWindow::onPlaybackFinished);

    m_browserPositionTimer.setInterval(250);
    connect(&m_browserPositionTimer, &QTimer::timeout, this, &MainWindow::onBrowserPositionTick);
}

MainWindow::~MainWindow()
{
    m_browserPositionTimer.stop();
    m_presentScheduled = false;
    m_latestFrame = QImage();
    m_latestGpu = {};
    if (m_player) {
        disconnect(m_player, nullptr, this, nullptr);
        m_player->stop();
    }
    // 播放器停干净后再拆渲染器，避免退出过程中仍 present 到半销毁的 D3D 窗口
    m_renderer.reset();
}

bool MainWindow::switchRenderer(IVideoRenderer::Backend backend)
{
    const QString reopenPath = isMediaLoaded() ? m_openMediaPath : QString();
    const qint64 reopenPos = usesBrowserPlayback() && browserPlayback()
                                 ? browserPlayback()->browserPositionMs()
                                 : m_player->positionMs();
    const bool wasPlaying = usesBrowserPlayback() ? m_browserPlaying
                                                  : m_player->state() == FFmpegPlayer::State::Playing;

    m_browserPositionTimer.stop();
    m_browserPlaying = false;
    if (auto *browser = browserPlayback()) {
        browser->stopMedia();
    }
    if (m_player->isOpen()) {
        m_player->stop();
    }

    auto next = VideoRendererFactory::create(backend, m_videoHost);
    if (!next || !next->widget()) {
        QMessageBox::warning(this,
                             QStringLiteral("渲染器"),
                             QStringLiteral("无法创建渲染后端：%1")
                                 .arg(VideoRendererFactory::backendName(backend)));
        return false;
    }

    QLayout *lay = m_videoHost->layout();
    if (lay) {
        while (QLayoutItem *item = lay->takeAt(0)) {
            delete item;
        }
    }

    m_renderer.reset();
    m_renderer = std::move(next);
    if (lay) {
        lay->addWidget(m_renderer->widget());
    }
    m_renderer->widget()->show();
    m_renderer->clear(QStringLiteral("打开文件后点击播放"));
    m_latestGpu = {};

    bindPlayerToRenderer();

    if (!reopenPath.isEmpty()) {
        openMediaAtPath(reopenPath);
        if (reopenPos > 0) {
            if (auto *browser = browserPlayback()) {
                browser->seekMedia(reopenPos);
            } else {
                m_player->seek(reopenPos);
            }
        }
        if (wasPlaying) {
            onPlayPause();
        }
    } else if (!m_latestFrame.isNull()) {
        m_renderer->present(m_latestFrame);
    }
    return true;
}

void MainWindow::bindPlayerToRenderer()
{
    std::shared_ptr<D3D11SharedDevice> dev;
    if (m_renderer && m_renderer->backend() == IVideoRenderer::Backend::D3D11Hw) {
        dev = m_renderer->d3d11SharedDevice();
    }
    m_player->setD3D11SharedDevice(dev);
}

void MainWindow::onRendererChanged(int index)
{
    if (index < 0 || !m_rendererCombo) {
        return;
    }
    const auto backend =
        static_cast<IVideoRenderer::Backend>(m_rendererCombo->itemData(index).toInt());
    if (m_renderer && m_renderer->backend() == backend) {
        return;
    }
    if (!switchRenderer(backend)) {
        // 回退到当前成功后端的下拉项
        for (int i = 0; i < m_rendererCombo->count(); ++i) {
            if (m_renderer
                && m_rendererCombo->itemData(i).toInt() == int(m_renderer->backend())) {
                m_rendererCombo->blockSignals(true);
                m_rendererCombo->setCurrentIndex(i);
                m_rendererCombo->blockSignals(false);
                break;
            }
        }
    }
}

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

    m_browserPositionTimer.stop();
    m_browserPlaying = false;
    if (auto *browser = browserPlayback()) {
        browser->stopMedia();
    }
    m_player->stop();

    openMediaAtPath(path);
}

void MainWindow::openMediaAtPath(const QString &path)
{
    FFmpegVideoParser parser;
    if (!parser.open(path)) {
        QMessageBox::warning(this, QStringLiteral("打开失败"), parser.lastError());
        setTransportEnabled(false);
        m_openMediaPath.clear();
        m_browserDurationMs = -1;
        return;
    }

    const MediaInfo info = parser.mediaInfo();
    m_openMediaPath = path;
    m_browserDurationMs = info.durationMs;
    showMediaSummary(info);

    const qint64 duration = qMax<qint64>(0, info.durationMs);
    m_seekSlider->setRange(0, static_cast<int>(qMin(duration, static_cast<qint64>(INT_MAX))));
    m_seekSlider->setValue(0);
    m_seekSlider->setEnabled(duration > 0);
    setTransportEnabled(true);
    m_playBtn->setText(QStringLiteral("播放"));
    m_latestFrame = QImage();
    m_latestGpu = {};

    if (auto *browser = browserPlayback()) {
        bool likelyUnsupportedCodec = false;
        for (const MediaStreamInfo &stream : info.streams) {
            if (stream.mediaType != QLatin1String("video")) {
                continue;
            }
            const QString codec = stream.codecName.toLower();
            if (codec.contains(QStringLiteral("hevc")) || codec.contains(QStringLiteral("h265"))
                || codec.contains(QStringLiteral("h.265"))) {
                likelyUnsupportedCodec = true;
                break;
            }
        }

        m_player->close();
        if (!browser->browserAvailable()) {
            if (m_renderer) {
                m_renderer->clear(QStringLiteral("CEF 未就绪，请查看上方错误提示"));
            }
        } else if (likelyUnsupportedCodec) {
            if (m_renderer) {
                m_renderer->clear(
                    QStringLiteral("Chromium HTML5 当前无法解码 HEVC(H.265) 视频。\n"
                                   "请切换到 D3D11 硬解 / OpenGL 后端播放该文件。"));
            }
        } else {
            if (!browser->openMedia(path)) {
                if (m_renderer) {
                    m_renderer->clear(QStringLiteral("CEF 媒体加载失败"));
                }
            }
        }
    } else {
        if (!m_player->open(path)) {
            QMessageBox::warning(this, QStringLiteral("打开失败"), m_player->lastError());
            setTransportEnabled(false);
            m_openMediaPath.clear();
            return;
        }
        if (m_renderer) {
            m_renderer->clear(QStringLiteral("已加载，点击播放"));
        }
    }
    updateTimeLabel();
}

void MainWindow::onPlayPause()
{
    if (!isMediaLoaded()) {
        return;
    }

    if (auto *browser = browserPlayback()) {
        if (m_browserPlaying) {
            browser->pauseMedia();
            m_browserPlaying = false;
            m_browserPositionTimer.stop();
            m_playBtn->setText(QStringLiteral("播放"));
        } else {
            browser->playMedia();
            m_browserPlaying = true;
            m_browserPositionTimer.start();
            m_playBtn->setText(QStringLiteral("暂停"));
        }
        updateTimeLabel();
        return;
    }

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
    m_browserPositionTimer.stop();
    m_browserPlaying = false;
    if (auto *browser = browserPlayback()) {
        browser->stopMedia();
    }
    m_player->stop();
    m_seekSlider->setValue(0);
    m_latestFrame = QImage();
    m_latestGpu = {};
    if (m_renderer) {
        m_renderer->clear(QStringLiteral("已停止"));
    }
    m_playBtn->setText(QStringLiteral("播放"));
    updateTimeLabel();
}

void MainWindow::onSeekPressed()
{
    m_sliderPressed = true;
}

void MainWindow::onSeekReleased()
{
    if (!isMediaLoaded()) {
        m_sliderPressed = false;
        return;
    }

    m_sliderPressed = false;
    if (auto *browser = browserPlayback()) {
        browser->seekMedia(m_seekSlider->value());
        updateTimeLabel();
        return;
    }

    m_player->seek(m_seekSlider->value());
    updateTimeLabel();
}

void MainWindow::onSeekMoved(int value)
{
    if (m_sliderPressed) {
        const qint64 duration = usesBrowserPlayback() ? m_browserDurationMs : m_player->durationMs();
        m_timeLabel->setText(QStringLiteral("%1 / %2")
                                 .arg(formatTime(value), formatTime(duration)));
    }
}

void MainWindow::onFrameReady(const QImage &frame, qint64)
{
    if (usesBrowserPlayback() || frame.isNull()) {
        return;
    }

    m_latestGpu = {};
    m_latestFrame = frame;
    if (m_presentScheduled) {
        return;
    }
    m_presentScheduled = true;
    QMetaObject::invokeMethod(this, &MainWindow::presentVideoFrame, Qt::QueuedConnection);
}

void MainWindow::onGpuFrameReady(GpuVideoFrame frame)
{
    if (usesBrowserPlayback() || !frame.isValid()) {
        return;
    }
    m_latestFrame = QImage();
    m_latestGpu = std::move(frame);
    if (m_presentScheduled) {
        return;
    }
    m_presentScheduled = true;
    QMetaObject::invokeMethod(this, &MainWindow::presentVideoFrame, Qt::QueuedConnection);
}

void MainWindow::presentVideoFrame()
{
    m_presentScheduled = false;
    if (!m_renderer) {
        return;
    }
    if (m_latestGpu.isValid() && m_renderer->supportsGpuFrames()) {
        m_renderer->presentGpu(m_latestGpu);
        return;
    }
    if (!m_latestFrame.isNull()) {
        m_renderer->present(m_latestFrame);
    }
}

void MainWindow::onPositionChanged(qint64 ms)
{
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
    updateTimeLabel();
}

void MainWindow::showMediaSummary(const MediaInfo &info)
{
    QString text;
    text += QStringLiteral("文件: %1\n").arg(info.filePath);
    text += QStringLiteral("容器: %1 (%2)\n").arg(info.formatName, info.formatLongName);
    text += QStringLiteral("时长: %1\n").arg(formatTime(info.durationMs));
    text += QStringLiteral("码率: %1 bps\n").arg(info.bitrate);
    text += QStringLiteral("视频流: %1, 音频流: %2\n").arg(info.videoStreamIndex).arg(info.audioStreamIndex);
    text += QStringLiteral("解码: %1\n\n")
                .arg(usesBrowserPlayback()
                         ? QStringLiteral("Chromium HTML5（CEF）")
                         : (m_player->hardwareDecodeActive()
                                ? QStringLiteral("D3D11VA 硬解（GPU 零拷贝）")
                                : QStringLiteral("软解")));

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
    qint64 pos = 0;
    qint64 duration = -1;
    if (auto *browser = browserPlayback()) {
        pos = m_sliderPressed ? m_seekSlider->value() : browser->browserPositionMs();
        duration = m_browserDurationMs;
    } else {
        pos = m_sliderPressed ? m_seekSlider->value() : m_player->positionMs();
        duration = m_player->durationMs();
    }
    m_timeLabel->setText(QStringLiteral("%1 / %2").arg(formatTime(pos), formatTime(duration)));
}

IBrowserPlayback *MainWindow::browserPlayback() const
{
    return dynamic_cast<IBrowserPlayback *>(m_renderer.get());
}

bool MainWindow::usesBrowserPlayback() const
{
    return browserPlayback() != nullptr;
}

bool MainWindow::isMediaLoaded() const
{
    return !m_openMediaPath.isEmpty();
}

void MainWindow::onBrowserPositionTick()
{
    auto *browser = browserPlayback();
    if (!browser) {
        return;
    }
    browser->pollBrowserPosition();
    if (!m_sliderPressed && m_seekSlider->isEnabled()) {
        const qint64 pos = browser->browserPositionMs();
        m_seekSlider->blockSignals(true);
        const qint64 clamped = qBound(qint64(0), pos, qint64(m_seekSlider->maximum()));
        m_seekSlider->setValue(static_cast<int>(clamped));
        m_seekSlider->blockSignals(false);
    }
    updateTimeLabel();
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
