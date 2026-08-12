#ifndef FFMPEGPLAYER_H
#define FFMPEGPLAYER_H

/**
 * @file FFmpegPlayer.h
 * @brief 基于 FFmpeg 的简易播放器（播放 / 暂停 / Seek）
 *
 * 分层：
 *   UI 层 ──调用──> FFmpegPlayer（本头文件，主线程 API）
 *                      │
 *                      ├── 状态机：Stopped / Playing / Paused
 *                      ├── 信号：frameReady / positionChanged / ...
 *                      └── 内部 Worker（解码线程，见 .cpp）
 *                               ├── Demux：av_read_frame
 *                               ├── Decode：视频 + 音频
 *                               ├── Sync：以“媒体时钟”对齐显示
 *                               └── Seek：flush + av_seek_frame
 *
 * 线程模型：
 * - open/play/pause/seek/stop 在调用线程（建议 UI 线程）
 * - 解码与音视频输出调度在独立 Worker 线程
 * - frameReady 通过队列连接抛回 UI 线程刷新画面
 */

#include "MediaTypes.h"

#include <QImage>
#include <QObject>
#include <QString>

class FFmpegPlayerPrivate;

class FFmpegPlayer : public QObject
{
    Q_OBJECT

public:
    enum class State {
        Stopped, ///< 未播放（已打开或未打开）
        Playing, ///< 正在播放
        Paused   ///< 已暂停（可继续）
    };
    Q_ENUM(State)

    explicit FFmpegPlayer(QObject *parent = nullptr);
    ~FFmpegPlayer() override;

    FFmpegPlayer(const FFmpegPlayer &) = delete;
    FFmpegPlayer &operator=(const FFmpegPlayer &) = delete;

    // ============================================================
    // 生命周期
    // ============================================================

    /**
     * @brief 打开媒体文件（不会自动播放）
     * @return false 时可通过 lastError() 查看原因
     */
    bool open(const QString &filePath);

    /** @brief 关闭文件并停止播放，释放解码资源 */
    void close();

    bool isOpen() const;

    // ============================================================
    // 播放控制
    // ============================================================

    void play();
    void pause();
    void stop(); ///< 停止并回到 0 位置（保持文件打开）

    /**
     * @brief 跳转到指定时间
     * @param positionMs 目标位置（毫秒），会夹紧到 [0, duration]
     *
     * 可在 Playing / Paused 状态下调用；Stopped 且已 open 时也会生效。
     */
    void seek(qint64 positionMs);

    // ============================================================
    // 只读状态
    // ============================================================

    State state() const;
    qint64 positionMs() const;   ///< 当前播放位置（媒体时钟）
    qint64 durationMs() const;   ///< 总时长，未知则为 -1
    MediaInfo mediaInfo() const;
    QString lastError() const;

signals:
    /** 解码出一帧可显示图像（已转为 RGB32） */
    void frameReady(const QImage &frame, qint64 ptsMs);

    void positionChanged(qint64 positionMs);
    void stateChanged(FFmpegPlayer::State state);
    void errorOccurred(const QString &message);
    void playbackFinished();

private:
    FFmpegPlayerPrivate *d = nullptr;
};

#endif // FFMPEGPLAYER_H
