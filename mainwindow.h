#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "FFmpegPlayer.h"
#include "GpuVideoFrame.h"
#include "MediaTypes.h"
#include "render/IBrowserPlayback.h"
#include "render/IVideoRenderer.h"

#include <QComboBox>
#include <QLabel>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QTimer>

#include <memory>

class QSplitter;

/**
 * @brief 主窗口（仅 UI）
 *
 * 职责：布局、按钮/进度条交互、把用户操作转发给 FFmpegPlayer，
 * 并根据播放器信号刷新画面与状态。
 * 画面绘制交给 IVideoRenderer（QImage / OpenGL / D3D11）。
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
    void onRendererChanged(int index);

    void onFrameReady(const QImage &frame, qint64 ptsMs);
    void onGpuFrameReady(GpuVideoFrame frame);
    void onPositionChanged(qint64 ms);
    void onStateChanged(FFmpegPlayer::State state);
    void onPlayerError(const QString &message);
    void onPlaybackFinished();
    void presentVideoFrame();
    void onBrowserPositionTick();

private:
    IBrowserPlayback *browserPlayback() const;
    bool usesBrowserPlayback() const;
    bool isMediaLoaded() const;
    void openMediaAtPath(const QString &path);
    void showMediaSummary(const MediaInfo &info);
    void updateTimeLabel();
    void setTransportEnabled(bool enabled);
    bool switchRenderer(IVideoRenderer::Backend backend);
    void bindPlayerToRenderer();
    static QString formatTime(qint64 ms);

private:
    FFmpegPlayer *m_player = nullptr;

    QWidget *m_videoHost = nullptr; ///< 渲染器 widget 的容器
    std::unique_ptr<IVideoRenderer> m_renderer;

    QPlainTextEdit *m_infoEdit = nullptr;
    QPushButton *m_openBtn = nullptr;
    QPushButton *m_playBtn = nullptr;
    QPushButton *m_stopBtn = nullptr;
    QComboBox *m_rendererCombo = nullptr;
    QSlider *m_seekSlider = nullptr;
    QLabel *m_timeLabel = nullptr;
    QSplitter *m_splitter = nullptr;

    bool m_sliderPressed = false;

    QImage m_latestFrame;
    GpuVideoFrame m_latestGpu;
    bool m_presentScheduled = false;

    QString m_openMediaPath;
    qint64 m_browserDurationMs = -1;
    bool m_browserPlaying = false;
    QTimer m_browserPositionTimer;
};

#endif // MAINWINDOW_H
