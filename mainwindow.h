#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "FFmpegPlayer.h"
#include "MediaTypes.h"

#include <QLabel>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>

/**
 * @brief 主窗口（仅 UI）
 *
 * 职责：布局、按钮/进度条交互、把用户操作转发给 FFmpegPlayer，
 * 并根据播放器信号刷新画面与状态。
 * 不包含 demux / 解码 / 时钟同步等播放内核逻辑。
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onOpen();
    void onPlayPause();
    void onStop();
    void onSeekPressed();
    void onSeekReleased();
    void onSeekMoved(int value);

    void onFrameReady(const QImage &frame, qint64 ptsMs);
    void onPositionChanged(qint64 ms);
    void onStateChanged(FFmpegPlayer::State state);
    void onPlayerError(const QString &message);
    void onPlaybackFinished();

private:
    void showMediaSummary(const MediaInfo &info);
    void updateTimeLabel();
    void setTransportEnabled(bool enabled);
    static QString formatTime(qint64 ms);

private:
    FFmpegPlayer *m_player = nullptr; ///< 专用播放器封装，所有播放逻辑在其内部

    QLabel *m_videoLabel = nullptr;
    QPlainTextEdit *m_infoEdit = nullptr;
    QPushButton *m_openBtn = nullptr;
    QPushButton *m_playBtn = nullptr;
    QPushButton *m_stopBtn = nullptr;
    QSlider *m_seekSlider = nullptr;
    QLabel *m_timeLabel = nullptr;

    bool m_sliderPressed = false; ///< 拖动进度条时暂停用 position 回写滑块
};

#endif // MAINWINDOW_H
