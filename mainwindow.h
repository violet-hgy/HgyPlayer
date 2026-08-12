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
 * @brief 播放器窗口：打开 / 播放 / 暂停 / 停止 / Seek
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
    static QString formatTime(qint64 ms);

private:
    FFmpegPlayer *m_player = nullptr;

    QLabel *m_videoLabel = nullptr;
    QPlainTextEdit *m_infoEdit = nullptr;
    QPushButton *m_openBtn = nullptr;
    QPushButton *m_playBtn = nullptr;
    QPushButton *m_stopBtn = nullptr;
    QSlider *m_seekSlider = nullptr;
    QLabel *m_timeLabel = nullptr;

    bool m_sliderPressed = false;
    qint64 m_pendingSeekMs = -1; ///< >=0 表示正在等待 seek 生效，忽略过期进度
};

#endif // MAINWINDOW_H
