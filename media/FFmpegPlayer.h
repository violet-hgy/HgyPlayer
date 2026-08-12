#ifndef FFMPEGPLAYER_H
#define FFMPEGPLAYER_H

/**
 * @file FFmpegPlayer.h
 * @brief 专用播放器类（与 UI 解耦）
 *
 * 本类是播放内核的唯一对外入口。MainWindow 等 UI 只应调用本类 API，
 * 并通过信号刷新界面，不要在窗口类里写 demux / 解码 / 时钟逻辑。
 *
 * 内部结构（实现见 FFmpegPlayer.cpp）：
 *   FFmpegPlayer（主线程门面）
 *     ├── 状态机：Stopped / Playing / Paused
 *     ├── 主线程音频：QAudioSink + PCM 泵
 *     └── PlayerWorker（解码线程）
 *          ├── openFile / buildInfo …… 打开容器、解析流信息
 *          ├── runLoop ………………… demux（av_read_frame）
 *          ├── decodeVideo …………… 解码视频并转换画面
 *          ├── decodeAudio …………… 解码音频并重采样
 *          ├── updateClock / waitForPts … 媒体时钟同步
 *          └── performSeek_l ……… Seek + flush
 *
 * 线程模型：
 * - open / play / pause / seek / stop：调用线程（建议 UI 线程）
 * - demux / decode / 同步等待：Worker 线程
 * - frameReady：队列连接回 UI 线程显示
 */

#include "MediaTypes.h"

#include <QImage>
#include <QObject>
#include <QString>

class FFmpegPlayerPrivate;

/**
 * @brief FFmpeg 媒体播放器封装
 *
 * 提供打开、播放、暂停、停止、Seek；输出视频帧与播放进度信号。
 */
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
     * @brief 打开媒体文件：解析容器/流信息并创建解码器（不会自动播放）
     * @return false 时可通过 lastError() 查看原因
     */
    bool open(const QString &filePath);

    /** @brief 关闭文件并停止播放，释放解码资源 */
    void close();

    bool isOpen() const;

    // ============================================================
    // 播放控制
    // ============================================================

    /** @brief 开始或继续播放 */
    void play();

    /** @brief 暂停（保持当前位置） */
    void pause();

    /** @brief 停止并回到 0 位置（保持文件打开） */
    void stop();

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
    qint64 positionMs() const;   ///< 当前播放位置（媒体时钟，毫秒）
    qint64 durationMs() const;   ///< 总时长，未知则为 -1
    MediaInfo mediaInfo() const; ///< open() 时解析得到的媒体摘要
    QString lastError() const;

signals:
    /** 解码并转换完成的一帧画面（RGB32），供 UI 显示 */
    void frameReady(const QImage &frame, qint64 ptsMs);

    void positionChanged(qint64 positionMs);
    void stateChanged(FFmpegPlayer::State state);
    void errorOccurred(const QString &message);
    void playbackFinished();

private:
    FFmpegPlayerPrivate *d = nullptr;
};

#endif // FFMPEGPLAYER_H
