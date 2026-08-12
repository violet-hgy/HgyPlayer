#include "FFmpegPlayer.h"

/**
 * @file FFmpegPlayer.cpp
 * @brief 播放器实现
 *
 * 音频为什么要放在主线程：
 * - QAudioSink 依赖线程事件循环；Worker 的 runLoop() 会占满线程，导致无声
 * - 因此：Worker 只负责解码出 PCM，通过队列交给主线程的 QAudioSink 播放
 *
 * 重采样为什么要“首帧懒加载”：
 * - 打开文件时 codec 的 sample_fmt / ch_layout 可能仍是 NONE
 * - 必须等第一帧 AVFrame 才能正确初始化 swr，否则会静默关掉音频
 */

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QElapsedTimer>
#include <QMediaDevices>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QTimer>
#include <QThread>
#include <QWaitCondition>

#include <atomic>
#include <cmath>
#include <cstdint>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libavutil/rational.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace {

QString avErr(int err)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(err, buf, sizeof(buf));
    return QString::fromUtf8(buf);
}

qint64 tsToMs(qint64 ts, AVRational tb)
{
    if (ts == AV_NOPTS_VALUE) {
        return -1;
    }
    return av_rescale_q(ts, tb, AVRational{1, 1000});
}

MediaStreamInfo makeStreamInfo(AVFormatContext *fmt, int index)
{
    MediaStreamInfo info;
    if (!fmt || index < 0 || index >= static_cast<int>(fmt->nb_streams)) {
        return info;
    }

    AVStream *st = fmt->streams[index];
    const AVCodecParameters *par = st->codecpar;
    info.index = st->index;

    switch (par->codec_type) {
    case AVMEDIA_TYPE_VIDEO: info.mediaType = QStringLiteral("video"); break;
    case AVMEDIA_TYPE_AUDIO: info.mediaType = QStringLiteral("audio"); break;
    case AVMEDIA_TYPE_SUBTITLE: info.mediaType = QStringLiteral("subtitle"); break;
    default: info.mediaType = QStringLiteral("unknown"); break;
    }

    if (const AVCodec *c = avcodec_find_decoder(par->codec_id)) {
        info.codecName = QString::fromUtf8(c->name ? c->name : "");
        info.codecLongName = QString::fromUtf8(c->long_name ? c->long_name : "");
    }
    info.bitrate = par->bit_rate;
    if (st->duration != AV_NOPTS_VALUE) {
        info.durationMs = tsToMs(st->duration, st->time_base);
    }

    if (par->codec_type == AVMEDIA_TYPE_VIDEO) {
        info.width = par->width;
        info.height = par->height;
        if (st->avg_frame_rate.den) {
            info.frameRate = av_q2d(st->avg_frame_rate);
        }
        if (const char *pix = av_get_pix_fmt_name(static_cast<AVPixelFormat>(par->format))) {
            info.pixelFormat = QString::fromUtf8(pix);
        }
    } else if (par->codec_type == AVMEDIA_TYPE_AUDIO) {
        info.sampleRate = par->sample_rate;
        info.channels = par->ch_layout.nb_channels;
        if (const char *sf = av_get_sample_fmt_name(static_cast<AVSampleFormat>(par->format))) {
            info.sampleFormat = QString::fromUtf8(sf);
        }
    }
    return info;
}

/** 线程安全 PCM 队列：解码线程写入，主线程读出 */
class PcmFifo
{
public:
    void push(const QByteArray &bytes)
    {
        if (bytes.isEmpty()) {
            return;
        }
        QMutexLocker lock(&m_mutex);
        // 防止暂停/卡顿时无限积压（约保留 2 秒 @48k stereo s16）
        constexpr int kMaxBytes = 48000 * 2 * 2 * 2;
        if (m_data.size() + bytes.size() > kMaxBytes) {
            const int overflow = m_data.size() + bytes.size() - kMaxBytes;
            m_data.remove(0, qMin(overflow, m_data.size()));
        }
        m_data.append(bytes);
    }

    QByteArray pop(int maxBytes)
    {
        QMutexLocker lock(&m_mutex);
        const int n = qMin(maxBytes, m_data.size());
        QByteArray out = m_data.left(n);
        m_data.remove(0, n);
        return out;
    }

    void clear()
    {
        QMutexLocker lock(&m_mutex);
        m_data.clear();
    }

    int size() const
    {
        QMutexLocker lock(&m_mutex);
        return m_data.size();
    }

private:
    mutable QMutex m_mutex;
    QByteArray m_data;
};

} // namespace

// ============================================================================
// PlayerWorker
// ============================================================================

class PlayerWorker : public QObject
{
    Q_OBJECT

public:
    explicit PlayerWorker(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    ~PlayerWorker() override
    {
        requestAbort();
        closeFile();
    }

    void setOutputAudioFormat(int sampleRate, int channels)
    {
        QMutexLocker lock(&m_mutex);
        m_outSampleRate = sampleRate > 0 ? sampleRate : 48000;
        m_outChannels = channels > 0 ? channels : 2;
        // 输出格式变化后，强制下一帧重建 swr
        if (m_swr) {
            swr_free(&m_swr);
        }
    }

    void requestPause()
    {
        QMutexLocker lock(&m_mutex);
        if (!m_running) {
            return;
        }
        m_paused = true;
        m_clockBaseMs = m_clockMs.load();
        emit stateChanged(FFmpegPlayer::State::Paused);
    }

    void requestResume()
    {
        QMutexLocker lock(&m_mutex);
        if (!m_running) {
            return;
        }
        m_paused = false;
        m_clockBaseMs = m_clockMs.load();
        m_wallTimer.restart();
        m_wait.wakeAll();
        emit stateChanged(FFmpegPlayer::State::Playing);
    }

    void requestSeek(qint64 ms)
    {
        // 任意线程可调：只置标志。播放循环里的 consumeCommands() 会真正执行 seek。
        QMutexLocker lock(&m_mutex);
        if (!m_fmt) {
            return;
        }
        if (m_durationMs > 0) {
            ms = qBound(0LL, ms, m_durationMs);
        } else {
            ms = qMax(0LL, ms);
        }
        m_seekTargetMs = ms;
        m_seekReq.store(true);
        m_wait.wakeAll();
    }

    void requestAbort()
    {
        QMutexLocker lock(&m_mutex);
        m_abort = true;
        m_paused = false;
        m_wait.wakeAll();
    }

    MediaInfo mediaInfo() const { return m_info; }
    QString lastError() const { return m_lastError; }
    qint64 durationMs() const { return m_durationMs; }
    qint64 positionMs() const { return m_clockMs.load(); }
    bool isRunning() const { return m_running.load(); }
    bool hasAudio() const { return m_audioStream >= 0 && m_adec != nullptr; }

public slots:
    bool openFile(const QString &path)
    {
        closeFile();
        m_lastError.clear();

        AVFormatContext *fmt = nullptr;
        const QByteArray pathBytes = path.toUtf8();
        int ret = avformat_open_input(&fmt, pathBytes.constData(), nullptr, nullptr);
        if (ret < 0) {
            m_lastError = QStringLiteral("open_input: %1").arg(avErr(ret));
            return false;
        }
        ret = avformat_find_stream_info(fmt, nullptr);
        if (ret < 0) {
            avformat_close_input(&fmt);
            m_lastError = QStringLiteral("find_stream_info: %1").arg(avErr(ret));
            return false;
        }

        m_fmt = fmt;
        m_videoStream = av_find_best_stream(m_fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        m_audioStream = av_find_best_stream(m_fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        if (m_videoStream < 0 && m_audioStream < 0) {
            m_lastError = QStringLiteral("no audio/video stream");
            closeFile();
            return false;
        }

        if (m_videoStream >= 0 && !openCodec(m_videoStream, &m_vdec)) {
            closeFile();
            return false;
        }
        if (m_audioStream >= 0 && !openCodec(m_audioStream, &m_adec)) {
            emit errorOccurred(QStringLiteral("audio decoder open failed, play video only"));
            m_audioStream = -1;
            m_adec = nullptr;
        }
        // 注意：这里不再 init swr，等首帧

        buildInfo(path);

        m_packet = av_packet_alloc();
        m_vframe = av_frame_alloc();
        m_aframe = av_frame_alloc();
        if (!m_packet || !m_vframe || !m_aframe) {
            m_lastError = QStringLiteral("alloc packet/frame failed");
            closeFile();
            return false;
        }

        m_eof = false;
        m_clockMs.store(0);
        m_durationMs = m_info.durationMs;
        return true;
    }

    void closeFile()
    {
        if (m_swr) {
            swr_free(&m_swr);
        }
        if (m_sws) {
            sws_freeContext(m_sws);
            m_sws = nullptr;
        }
        av_frame_free(&m_vframe);
        av_frame_free(&m_aframe);
        av_packet_free(&m_packet);
        avcodec_free_context(&m_vdec);
        avcodec_free_context(&m_adec);
        if (m_fmt) {
            avformat_close_input(&m_fmt);
        }

        m_videoStream = -1;
        m_audioStream = -1;
        m_info = MediaInfo{};
        m_durationMs = -1;
        m_eof = false;
        m_abort = false;
        m_paused = false;
        m_seekReq = false;
        m_discardUntilPtsMs = -1;
        emit audioReset();
    }

    void startPlayback()
    {
        if (!m_fmt || m_running.load()) {
            return;
        }

        {
            QMutexLocker lock(&m_mutex);
            m_abort = false;
            m_paused = false;
            if (m_eof || (m_durationMs > 0 && m_clockMs.load() >= m_durationMs - 50)) {
                performSeek_l(0);
            }
            m_eof = false;
            m_running = true;
            m_clockBaseMs = m_clockMs.load();
            m_wallTimer.restart();
        }

        emit stateChanged(FFmpegPlayer::State::Playing);
        runLoop();
        m_running = false;

        if (!m_abort.load() && m_eof) {
            emit playbackFinished();
        }
        if (!m_abort.load()) {
            emit stateChanged(FFmpegPlayer::State::Stopped);
        }
    }

    void stopPlayback()
    {
        requestAbort();
        while (m_running.load()) {
            QThread::msleep(5);
        }

        if (m_fmt) {
            QMutexLocker lock(&m_mutex);
            performSeek_l(0);
            m_clockMs.store(0);
            m_clockBaseMs = 0;
            m_eof = false;
            m_abort = false;
            emit positionChanged(0);
        }
        emit audioReset();
        emit stateChanged(FFmpegPlayer::State::Stopped);
    }

    /** 始终在 Worker 线程调用：处理 seek（播放中置标志由循环消费；空闲则立即执行） */
    void applySeek(qint64 ms)
    {
        QMutexLocker lock(&m_mutex);
        if (!m_fmt) {
            return;
        }
        if (m_durationMs > 0) {
            ms = qBound(0LL, ms, m_durationMs);
        } else {
            ms = qMax(0LL, ms);
        }

        if (m_running) {
            m_seekReq = true;
            m_seekTargetMs = ms;
            m_wait.wakeAll();
        } else {
            performSeek_l(ms);
            m_seekReq = false;
        }
    }

    void shutdown()
    {
        requestAbort();
        while (m_running.load()) {
            QThread::msleep(5);
        }
        closeFile();
    }

signals:
    void frameReady(const QImage &frame, qint64 ptsMs);
    void pcmReady(const QByteArray &pcm);
    void audioReset();
    void positionChanged(qint64 positionMs);
    void stateChanged(FFmpegPlayer::State state);
    void errorOccurred(const QString &message);
    void playbackFinished();

private:
    bool openCodec(int streamIndex, AVCodecContext **ctxOut)
    {
        AVStream *st = m_fmt->streams[streamIndex];
        const AVCodec *dec = avcodec_find_decoder(st->codecpar->codec_id);
        if (!dec) {
            m_lastError = QStringLiteral("decoder not found");
            return false;
        }
        AVCodecContext *ctx = avcodec_alloc_context3(dec);
        if (!ctx) {
            m_lastError = QStringLiteral("alloc codec ctx failed");
            return false;
        }
        int ret = avcodec_parameters_to_context(ctx, st->codecpar);
        if (ret < 0) {
            avcodec_free_context(&ctx);
            m_lastError = QStringLiteral("parameters_to_context: %1").arg(avErr(ret));
            return false;
        }
        // 略增解码线程，降低卡顿（可选）
        ctx->thread_count = 2;
        ret = avcodec_open2(ctx, dec, nullptr);
        if (ret < 0) {
            avcodec_free_context(&ctx);
            m_lastError = QStringLiteral("codec_open2: %1").arg(avErr(ret));
            return false;
        }
        *ctxOut = ctx;
        return true;
    }

    void buildInfo(const QString &path)
    {
        m_info = MediaInfo{};
        m_info.filePath = path;
        if (m_fmt->iformat) {
            m_info.formatName = QString::fromUtf8(m_fmt->iformat->name ? m_fmt->iformat->name : "");
            m_info.formatLongName =
                QString::fromUtf8(m_fmt->iformat->long_name ? m_fmt->iformat->long_name : "");
        }
        if (m_fmt->duration != AV_NOPTS_VALUE) {
            m_info.durationMs = m_fmt->duration / (AV_TIME_BASE / 1000);
        }
        m_info.bitrate = m_fmt->bit_rate;
        m_info.videoStreamIndex = m_videoStream;
        m_info.audioStreamIndex = m_audioStream;
        for (unsigned i = 0; i < m_fmt->nb_streams; ++i) {
            m_info.streams.push_back(makeStreamInfo(m_fmt, static_cast<int>(i)));
        }
    }

    /**
     * 用“真实音频帧”初始化/重建 swr。
     * 这是有声音的关键一步：不能再用可能为 NONE 的 codecCtx->sample_fmt。
     */
    bool ensureSwrFromFrame(const AVFrame *frame)
    {
        if (!frame) {
            return false;
        }

        const AVSampleFormat inFmt = static_cast<AVSampleFormat>(frame->format);
        const int inRate = frame->sample_rate > 0 ? frame->sample_rate : m_adec->sample_rate;
        if (inFmt == AV_SAMPLE_FMT_NONE || inRate <= 0 || frame->ch_layout.nb_channels <= 0) {
            return false;
        }

        // 已初始化且输入参数没变，则复用
        if (m_swr && m_swrInFmt == inFmt && m_swrInRate == inRate
            && m_swrInChannels == frame->ch_layout.nb_channels) {
            return true;
        }

        if (m_swr) {
            swr_free(&m_swr);
        }

        AVChannelLayout outLayout;
        av_channel_layout_default(&outLayout, m_outChannels);

        int ret = swr_alloc_set_opts2(&m_swr,
                                      &outLayout,
                                      AV_SAMPLE_FMT_S16,
                                      m_outSampleRate,
                                      &frame->ch_layout,
                                      inFmt,
                                      inRate,
                                      0,
                                      nullptr);
        av_channel_layout_uninit(&outLayout);
        if (ret < 0 || !m_swr) {
            emit errorOccurred(QStringLiteral("swr_alloc failed"));
            return false;
        }
        ret = swr_init(m_swr);
        if (ret < 0) {
            swr_free(&m_swr);
            emit errorOccurred(QStringLiteral("swr_init failed: %1").arg(avErr(ret)));
            return false;
        }

        m_swrInFmt = inFmt;
        m_swrInRate = inRate;
        m_swrInChannels = frame->ch_layout.nb_channels;
        return true;
    }

    void performSeek_l(qint64 targetMs)
    {
        const qint64 ts = av_rescale_q(targetMs, AVRational{1, 1000}, AVRational{1, AV_TIME_BASE});
        int ret = avformat_seek_file(m_fmt, -1, INT64_MIN, ts, INT64_MAX, 0);
        if (ret < 0) {
            ret = av_seek_frame(m_fmt, -1, ts, AVSEEK_FLAG_BACKWARD);
        }
        if (ret < 0) {
            emit errorOccurred(QStringLiteral("seek failed: %1").arg(avErr(ret)));
            return;
        }
        if (m_vdec) {
            avcodec_flush_buffers(m_vdec);
        }
        if (m_adec) {
            avcodec_flush_buffers(m_adec);
        }
        // seek 后重采样残留丢弃
        if (m_swr) {
            swr_convert(m_swr, nullptr, 0, nullptr, 0);
        }
        m_eof = false;
        m_clockMs.store(targetMs);
        m_clockBaseMs = targetMs;
        m_wallTimer.restart();
        // 丢弃 seek 点之前的帧，避免旧 PTS 把时钟拉回去导致长时间“假死”
        m_discardUntilPtsMs = targetMs;
        m_seekSerial++;
        emit audioReset();
        emit positionChanged(targetMs);
    }

    void updateClock(qint64 mediaPtsMs)
    {
        if (mediaPtsMs < 0) {
            return;
        }
        // seek 请求尚未执行时，不要再往外抛旧进度
        if (m_seekReq.load()) {
            return;
        }
        // 禁止用过期帧把时钟往回拨（seek 后的典型坑）
        const qint64 cur = m_clockMs.load();
        if (mediaPtsMs + 100 < cur) {
            return;
        }
        m_clockMs.store(mediaPtsMs);
        const qint64 wallBased = m_clockBaseMs + m_wallTimer.elapsed();
        if (std::llabs(mediaPtsMs - wallBased) > 350) {
            m_clockBaseMs = mediaPtsMs;
            m_wallTimer.restart();
        }
        emit positionChanged(mediaPtsMs);
    }

    QImage convertVideoFrame(AVFrame *frame)
    {
        if (!frame || frame->width <= 0 || frame->height <= 0) {
            return {};
        }
        m_sws = sws_getCachedContext(m_sws,
                                     frame->width,
                                     frame->height,
                                     static_cast<AVPixelFormat>(frame->format),
                                     frame->width,
                                     frame->height,
                                     AV_PIX_FMT_RGB32,
                                     SWS_BILINEAR,
                                     nullptr,
                                     nullptr,
                                     nullptr);
        if (!m_sws) {
            return {};
        }
        QImage img(frame->width, frame->height, QImage::Format_RGB32);
        uint8_t *dst[4] = {img.bits(), nullptr, nullptr, nullptr};
        int dstStride[4] = {static_cast<int>(img.bytesPerLine()), 0, 0, 0};
        sws_scale(m_sws, frame->data, frame->linesize, 0, frame->height, dst, dstStride);
        return img;
    }

    /**
     * @return true = 发生了 seek/abort，调用方应丢弃当前帧，不要 updateClock/显示
     */
    bool waitForPts(qint64 ptsMs)
    {
        const quint64 serialAtStart = m_seekSerial;
        if (ptsMs < 0) {
            return m_abort.load() || serialAtStart != m_seekSerial;
        }
        while (!m_abort.load()) {
            if (consumeCommands()) {
                return true;
            }
            // seek 发生在等待期间：当前帧已过期
            if (serialAtStart != m_seekSerial) {
                return true;
            }
            if (m_paused.load()) {
                QThread::msleep(10);
                continue;
            }
            const qint64 delay = ptsMs - (m_clockBaseMs + m_wallTimer.elapsed());
            if (delay <= 0) {
                return false;
            }
            QThread::msleep(static_cast<unsigned long>(qMin(delay, 15LL)));
        }
        return true;
    }

    bool consumeCommands()
    {
        QMutexLocker lock(&m_mutex);
        if (m_seekReq) {
            performSeek_l(m_seekTargetMs);
            m_seekReq = false;
            return false; // 不 abort，但 waitForPts 会靠 serial 丢帧
        }
        while (m_paused && !m_abort) {
            m_wait.wait(&m_mutex, 40);
            if (m_seekReq) {
                performSeek_l(m_seekTargetMs);
                m_seekReq = false;
            }
        }
        return m_abort.load();
    }

    bool decodeVideo(AVPacket *pkt)
    {
        int ret = avcodec_send_packet(m_vdec, pkt);
        if (ret < 0) {
            return true;
        }
        while (ret >= 0) {
            ret = avcodec_receive_frame(m_vdec, m_vframe);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            }
            if (ret < 0) {
                break;
            }

            qint64 ptsMs = -1;
            if (m_vframe->best_effort_timestamp != AV_NOPTS_VALUE) {
                ptsMs = tsToMs(m_vframe->best_effort_timestamp,
                               m_fmt->streams[m_videoStream]->time_base);
            }

            // seek 后跳过目标点之前的帧（从关键键帧解码上来的）
            if (m_discardUntilPtsMs >= 0 && ptsMs >= 0 && ptsMs + 50 < m_discardUntilPtsMs) {
                av_frame_unref(m_vframe);
                continue;
            }
            if (m_discardUntilPtsMs >= 0 && ptsMs >= m_discardUntilPtsMs - 50) {
                m_discardUntilPtsMs = -1;
            }

            if (waitForPts(ptsMs)) {
                av_frame_unref(m_vframe);
                // abort 或 seek：结束当前 packet 的解码，回到 demux 读新位置数据
                return true;
            }

            const QImage image = convertVideoFrame(m_vframe);
            av_frame_unref(m_vframe);
            if (!image.isNull()) {
                if (ptsMs >= 0) {
                    updateClock(ptsMs);
                }
                emit frameReady(image, ptsMs);
            }
        }
        return true;
    }

    bool decodeAudio(AVPacket *pkt)
    {
        int ret = avcodec_send_packet(m_adec, pkt);
        if (ret < 0) {
            return true;
        }
        while (ret >= 0) {
            ret = avcodec_receive_frame(m_adec, m_aframe);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            }
            if (ret < 0) {
                break;
            }

            if (!ensureSwrFromFrame(m_aframe)) {
                av_frame_unref(m_aframe);
                continue;
            }

            qint64 ptsMs = -1;
            if (m_aframe->best_effort_timestamp != AV_NOPTS_VALUE) {
                ptsMs = tsToMs(m_aframe->best_effort_timestamp,
                               m_fmt->streams[m_audioStream]->time_base);
            }

            const int srcRate = m_aframe->sample_rate > 0 ? m_aframe->sample_rate : m_swrInRate;
            const int maxOut =
                av_rescale_rnd(swr_get_delay(m_swr, srcRate) + m_aframe->nb_samples,
                               m_outSampleRate,
                               srcRate,
                               AV_ROUND_UP);
            QByteArray pcm;
            pcm.resize(maxOut * m_outChannels * static_cast<int>(sizeof(int16_t)));
            uint8_t *outPtr = reinterpret_cast<uint8_t *>(pcm.data());
            const int outSamples = swr_convert(m_swr,
                                               &outPtr,
                                               maxOut,
                                               (const uint8_t **)m_aframe->extended_data,
                                               m_aframe->nb_samples);
            av_frame_unref(m_aframe);
            if (outSamples <= 0) {
                continue;
            }
            pcm.resize(outSamples * m_outChannels * static_cast<int>(sizeof(int16_t)));

            if (m_videoStream < 0 && ptsMs >= 0) {
                if (waitForPts(ptsMs)) {
                    return true;
                }
                updateClock(ptsMs);
            }

            if (m_abort.load() || m_paused.load()) {
                return true;
            }
            emit pcmReady(pcm);
        }
        return true;
    }

    void runLoop()
    {
        while (!m_abort.load()) {
            if (consumeCommands()) {
                break;
            }
            if (m_paused.load()) {
                continue;
            }

            if (m_eof) {
                if (m_vdec) {
                    avcodec_send_packet(m_vdec, nullptr);
                    for (;;) {
                        const int fr = avcodec_receive_frame(m_vdec, m_vframe);
                        if (fr == AVERROR(EAGAIN) || fr == AVERROR_EOF || fr < 0) {
                            break;
                        }
                        qint64 ptsMs = -1;
                        if (m_vframe->best_effort_timestamp != AV_NOPTS_VALUE) {
                            ptsMs = tsToMs(m_vframe->best_effort_timestamp,
                                           m_fmt->streams[m_videoStream]->time_base);
                        }
                        waitForPts(ptsMs);
                        if (m_abort.load()) {
                            av_frame_unref(m_vframe);
                            break;
                        }
                        // seek 串号变化时丢弃尾帧 flush 中的过期帧
                        if (m_discardUntilPtsMs >= 0 && ptsMs >= 0 && ptsMs + 50 < m_discardUntilPtsMs) {
                            av_frame_unref(m_vframe);
                            continue;
                        }
                        const QImage image = convertVideoFrame(m_vframe);
                        av_frame_unref(m_vframe);
                        if (!image.isNull()) {
                            if (ptsMs >= 0) {
                                updateClock(ptsMs);
                            }
                            emit frameReady(image, ptsMs);
                        }
                        if (m_abort.load()) {
                            break;
                        }
                    }
                }
                break;
            }

            const int ret = av_read_frame(m_fmt, m_packet);
            if (ret == AVERROR_EOF) {
                m_eof = true;
                continue;
            }
            if (ret < 0) {
                emit errorOccurred(QStringLiteral("read_frame: %1").arg(avErr(ret)));
                break;
            }

            if (m_packet->stream_index == m_videoStream && m_vdec) {
                decodeVideo(m_packet);
            } else if (m_packet->stream_index == m_audioStream && m_adec) {
                decodeAudio(m_packet);
            }
            av_packet_unref(m_packet);
        }
    }

private:
    AVFormatContext *m_fmt = nullptr;
    AVCodecContext *m_vdec = nullptr;
    AVCodecContext *m_adec = nullptr;
    AVPacket *m_packet = nullptr;
    AVFrame *m_vframe = nullptr;
    AVFrame *m_aframe = nullptr;
    SwsContext *m_sws = nullptr;
    SwrContext *m_swr = nullptr;
    int m_videoStream = -1;
    int m_audioStream = -1;

    int m_outSampleRate = 48000;
    int m_outChannels = 2;
    AVSampleFormat m_swrInFmt = AV_SAMPLE_FMT_NONE;
    int m_swrInRate = 0;
    int m_swrInChannels = 0;

    QMutex m_mutex;
    QWaitCondition m_wait;
    std::atomic_bool m_abort{false};
    std::atomic_bool m_paused{false};
    std::atomic_bool m_running{false};
    std::atomic_bool m_seekReq{false};
    qint64 m_seekTargetMs = 0;
    qint64 m_discardUntilPtsMs = -1; ///< seek 后丢弃该 PTS 之前的帧
    std::atomic<quint64> m_seekSerial{0};
    bool m_eof = false;

    QElapsedTimer m_wallTimer;
    qint64 m_clockBaseMs = 0;
    std::atomic<qint64> m_clockMs{0};
    qint64 m_durationMs = -1;

    MediaInfo m_info;
    QString m_lastError;
};

// ============================================================================
// FFmpegPlayer：主线程音频设备 + Worker 门面
// ============================================================================

class FFmpegPlayerPrivate
{
public:
    QThread thread;
    PlayerWorker *worker = nullptr;
    FFmpegPlayer::State state = FFmpegPlayer::State::Stopped;
    MediaInfo info;
    QString lastError;
    qint64 durationMs = -1;
    std::atomic<qint64> positionMs{0};
    std::atomic<qint64> seekingToMs{-1}; ///< >=0 表示 UI/门面层正在等待 seek 对齐
    bool opened = false;

    // 主线程音频
    QAudioSink *sink = nullptr;
    QIODevice *audioIo = nullptr;
    PcmFifo pcmFifo;
    QTimer *audioPump = nullptr;
    int outRate = 48000;
    int outChannels = 2;
};

static void stopAudioDevice(FFmpegPlayerPrivate *d)
{
    if (d->audioPump) {
        d->audioPump->stop();
    }
    d->pcmFifo.clear();
    if (d->sink) {
        d->sink->stop();
        delete d->sink;
        d->sink = nullptr;
    }
    d->audioIo = nullptr;
}

static bool startAudioDevice(FFmpegPlayerPrivate *d, QObject *parent)
{
    stopAudioDevice(d);

    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (device.isNull()) {
        return false;
    }

    QAudioFormat fmt = device.preferredFormat();
    // 统一成 Int16 + 立体声，便于 swr 输出
    fmt.setSampleFormat(QAudioFormat::Int16);
    if (fmt.channelCount() <= 0) {
        fmt.setChannelCount(2);
    }
    if (fmt.sampleRate() <= 0) {
        fmt.setSampleRate(48000);
    }
    // 强制 2 声道，避免设备 preferred 为 1/6/8 时复杂处理
    fmt.setChannelCount(2);

    if (!device.isFormatSupported(fmt)) {
        // 再试一次固定 48000
        fmt.setSampleRate(48000);
        fmt.setChannelCount(2);
        fmt.setSampleFormat(QAudioFormat::Int16);
    }

    d->outRate = fmt.sampleRate();
    d->outChannels = fmt.channelCount();

    d->sink = new QAudioSink(device, fmt, parent);
    d->sink->setBufferSize(d->outRate * d->outChannels * 2 * 2);
    d->audioIo = d->sink->start();
    if (!d->audioIo) {
        stopAudioDevice(d);
        return false;
    }

    if (!d->audioPump) {
        d->audioPump = new QTimer(parent);
        d->audioPump->setInterval(10);
    }
    return true;
}

static void pumpAudio(FFmpegPlayerPrivate *d)
{
    if (!d->sink || !d->audioIo) {
        return;
    }
    if (d->sink->state() == QAudio::SuspendedState) {
        return;
    }

    const int freeBytes = d->sink->bytesFree();
    if (freeBytes <= 0) {
        return;
    }
    const QByteArray chunk = d->pcmFifo.pop(freeBytes);
    if (!chunk.isEmpty()) {
        d->audioIo->write(chunk);
    }
}

FFmpegPlayer::FFmpegPlayer(QObject *parent)
    : QObject(parent)
    , d(new FFmpegPlayerPrivate)
{
    d->worker = new PlayerWorker;
    d->worker->moveToThread(&d->thread);

    connect(d->worker, &PlayerWorker::frameReady, this, &FFmpegPlayer::frameReady);
    connect(d->worker, &PlayerWorker::positionChanged, this, [this](qint64 ms) {
        // seek 已发出但 Worker 尚未跳转完成时，忽略旧进度，防止 UI 回跳
        const qint64 pending = d->seekingToMs.load();
        if (pending >= 0) {
            if (qAbs(ms - pending) > 900) {
                return;
            }
            d->seekingToMs.store(-1);
        }
        d->positionMs.store(ms);
        emit positionChanged(ms);
    });
    connect(d->worker, &PlayerWorker::stateChanged, this, [this](FFmpegPlayer::State s) {
        d->state = s;
        if (s == State::Paused && d->sink) {
            d->sink->suspend();
            if (d->audioPump) {
                d->audioPump->stop();
            }
        } else if (s == State::Playing && d->sink) {
            d->sink->resume();
            if (d->audioPump) {
                d->audioPump->start();
            }
        } else if (s == State::Stopped) {
            stopAudioDevice(d);
        }
        emit stateChanged(s);
    });
    connect(d->worker, &PlayerWorker::errorOccurred, this, [this](const QString &msg) {
        d->lastError = msg;
        emit errorOccurred(msg);
    });
    connect(d->worker, &PlayerWorker::playbackFinished, this, [this]() {
        stopAudioDevice(d);
        emit playbackFinished();
    });

    // PCM：解码线程 -> 主线程队列（Qt::QueuedConnection 自动跨线程）
    connect(d->worker, &PlayerWorker::pcmReady, this, [this](const QByteArray &pcm) {
        d->pcmFifo.push(pcm);
    });
    connect(d->worker, &PlayerWorker::audioReset, this, [this]() {
        d->pcmFifo.clear();
        if (d->sink) {
            d->sink->reset();
            d->audioIo = d->sink->start();
        }
    });

    d->audioPump = new QTimer(this);
    d->audioPump->setInterval(10);
    connect(d->audioPump, &QTimer::timeout, this, [this]() { pumpAudio(d); });

    d->thread.start();
}

FFmpegPlayer::~FFmpegPlayer()
{
    d->worker->requestAbort();
    QMetaObject::invokeMethod(d->worker, "shutdown", Qt::BlockingQueuedConnection);
    stopAudioDevice(d);
    d->thread.quit();
    d->thread.wait(5000);
    delete d->worker;
    d->worker = nullptr;
    delete d;
    d = nullptr;
}

bool FFmpegPlayer::open(const QString &filePath)
{
    if (d->state == State::Playing || d->state == State::Paused || d->worker->isRunning()) {
        stop();
    }

    bool ok = false;
    QMetaObject::invokeMethod(d->worker,
                              "openFile",
                              Qt::BlockingQueuedConnection,
                              Q_RETURN_ARG(bool, ok),
                              Q_ARG(QString, filePath));
    if (!ok) {
        d->lastError = d->worker->lastError();
        d->opened = false;
        return false;
    }

    d->info = d->worker->mediaInfo();
    d->durationMs = d->worker->durationMs();
    d->positionMs.store(0);
    d->opened = true;
    d->state = State::Stopped;
    d->lastError.clear();
    emit stateChanged(State::Stopped);
    emit positionChanged(0);
    return true;
}

void FFmpegPlayer::close()
{
    stop();
    QMetaObject::invokeMethod(d->worker, "closeFile", Qt::BlockingQueuedConnection);
    d->opened = false;
    d->info = MediaInfo{};
    d->durationMs = -1;
    d->positionMs.store(0);
}

bool FFmpegPlayer::isOpen() const
{
    return d->opened;
}

void FFmpegPlayer::play()
{
    if (!d->opened) {
        return;
    }
    if (d->state == State::Playing) {
        return;
    }
    if (d->state == State::Paused) {
        d->worker->requestResume();
        return;
    }

    // 先在主线程打开音频设备，再告诉 Worker 输出规格
    if (d->worker->hasAudio()) {
        if (!startAudioDevice(d, this)) {
            emit errorOccurred(QStringLiteral("无法打开音频输出设备（将尝试静音播放）"));
        } else {
            d->worker->setOutputAudioFormat(d->outRate, d->outChannels);
            d->audioPump->start();
        }
    }

    QMetaObject::invokeMethod(d->worker, "startPlayback", Qt::QueuedConnection);
}

void FFmpegPlayer::pause()
{
    if (d->state != State::Playing) {
        return;
    }
    d->worker->requestPause();
}

void FFmpegPlayer::stop()
{
    if (!d->opened) {
        d->state = State::Stopped;
        return;
    }
    d->seekingToMs.store(-1);
    d->worker->requestAbort();
    QMetaObject::invokeMethod(d->worker, "stopPlayback", Qt::BlockingQueuedConnection);
    stopAudioDevice(d);
    d->state = State::Stopped;
    d->positionMs.store(0);
}

void FFmpegPlayer::seek(qint64 positionMs)
{
    if (!d->opened) {
        return;
    }
    if (d->durationMs > 0) {
        positionMs = qBound(qint64(0), positionMs, d->durationMs);
    } else {
        positionMs = qMax(qint64(0), positionMs);
    }

    // 先对外公布目标进度，并屏蔽 Worker 尚未跳转前的旧 positionChanged
    d->seekingToMs.store(positionMs);
    d->positionMs.store(positionMs);
    emit positionChanged(positionMs);

    /**
     * 关键：播放中 Worker 卡在 runLoop，不会处理 Queued 槽。
     * - 正在跑：直接 requestSeek() 置标志，由循环内 consumeCommands() 执行
     * - 空闲时：投递 applySeek 到 Worker 线程执行 FFmpeg seek
     */
    if (d->worker->isRunning()) {
        d->worker->requestSeek(positionMs);
    } else {
        QMetaObject::invokeMethod(d->worker,
                                  "applySeek",
                                  Qt::QueuedConnection,
                                  Q_ARG(qint64, positionMs));
    }
}

FFmpegPlayer::State FFmpegPlayer::state() const
{
    return d->state;
}

qint64 FFmpegPlayer::positionMs() const
{
    return d->positionMs.load();
}

qint64 FFmpegPlayer::durationMs() const
{
    return d->durationMs;
}

MediaInfo FFmpegPlayer::mediaInfo() const
{
    return d->info;
}

QString FFmpegPlayer::lastError() const
{
    return d->lastError;
}

#include "FFmpegPlayer.moc"
