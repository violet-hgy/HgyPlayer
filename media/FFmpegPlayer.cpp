#include "FFmpegPlayer.h"
#include "GpuVideoFrame.h"
#include "render/D3D11SharedDevice.h"

/**
 * @file FFmpegPlayer.cpp
 * @brief 专用播放器实现（FFmpegPlayer 门面 + PlayerWorker 解码线程）
 *
 * 本文件集中全部播放内核；MainWindow 不得复制 demux/解码/时钟逻辑。
 *
 * 模块划分：
 *   [解析] openFile / openCodec / buildInfo …… 打开容器、找流、建解码器、填 MediaInfo
 *   [解复用] runLoop / av_read_frame …… 按包从容器读数据
 *   [画面] decodeVideo / convertVideoFrame …… 解码视频并转成 QImage
 *   [音频] decodeAudio / ensureSwrFromFrame …… 解码并重采样为 PCM
 *   [时钟] updateClock / waitForPts …… 媒体时钟与按 PTS 等待显示
 *   [Seek] requestSeek / performSeek_l / applySeek …… 跳转并 flush
 *   [主线程音频] startAudioDevice / pumpAudio …… QAudioSink 出声
 *
 * 音频为什么要放在主线程：
 * - QAudioSink 依赖线程事件循环；Worker 的 runLoop() 会占满线程，导致无声
 * - 因此：Worker 只负责解码出 PCM，通过队列交给主线程的 QAudioSink 播放
 *
 * 重采样为什么要“首帧懒加载”：
 * - 打开文件时 codec 的 sample_fmt / ch_layout 可能仍是 NONE
 * - 必须等第一帧 AVFrame 才能正确初始化 swr，否则会静默关掉音频
 *
 * FFmpeg API 注释约定（学习用）：
 *   [来源库] 函数名：作用。
 *   形参：a=...；b=...。返回：...
 * 来源库：libavformat / libavcodec / libavutil / libswscale / libswresample
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
#include <memory>

extern "C" {
#include <libavcodec/avcodec.h>       // 解码器
#include <libavformat/avformat.h>     // 容器 / demux
#include <libavutil/channel_layout.h> // 声道布局
#include <libavutil/error.h>          // av_strerror
#include <libavutil/frame.h>          // AVFrame
#include <libavutil/hwcontext.h>      // 硬解设备缓冲
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>        // 像素格式名
#include <libavutil/rational.h>       // AVRational / av_rescale_q
#include <libavutil/samplefmt.h>      // 采样格式名
#include <libswresample/swresample.h> // 音频重采样
#include <libswscale/swscale.h>       // 图像缩放 / 像素转换
}

#ifdef Q_OS_WIN
#include <libavutil/hwcontext_d3d11va.h>
#endif

namespace {

/** FFmpeg 错误码转可读字符串 */
QString avErr(int err)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
    // [libavutil] av_strerror：把负错误码写成英文短句。
    // 形参：errnum=FFmpeg 返回值（通常 <0）；errbuf=输出缓冲；errbuf_size=缓冲字节数。
    av_strerror(err, buf, sizeof(buf));
    return QString::fromUtf8(buf);
}

/** [时钟] 时间基时间戳 → 毫秒 */
qint64 tsToMs(qint64 ts, AVRational tb)
{
    if (ts == AV_NOPTS_VALUE) {
        return -1;
    }
    // [libavutil] av_rescale_q：按有理数换时间基，避免浮点误差。
    // 形参：a=时间戳；bq=源时间基（流的 time_base）；cq=目标时间基（这里是 1/1000 秒）。
    return av_rescale_q(ts, tb, AVRational{1, 1000});
}

/** [解析] 从 AVStream 填充一条与 FFmpeg 解耦的 MediaStreamInfo */
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

    // [libavcodec] avcodec_find_decoder：按 codec_id 找软解实现（h264/aac 等）。
    // 形参：id=流里记录的编码器 ID。返回：解码器描述，找不到为 nullptr。
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
            // [libavutil] av_q2d：AVRational → double（这里把帧率分数换成 fps）。
            // 形参：a=分子/分母有理数。
            info.frameRate = av_q2d(st->avg_frame_rate);
        }
        // [libavutil] av_get_pix_fmt_name：像素格式枚举 → "yuv420p" 这类名字。
        // 形参：pix_fmt=AVPixelFormat。
        if (const char *pix = av_get_pix_fmt_name(static_cast<AVPixelFormat>(par->format))) {
            info.pixelFormat = QString::fromUtf8(pix);
        }
    } else if (par->codec_type == AVMEDIA_TYPE_AUDIO) {
        info.sampleRate = par->sample_rate;
        info.channels = par->ch_layout.nb_channels;
        // [libavutil] av_get_sample_fmt_name：采样格式枚举 → "fltp" 等。
        // 形参：sample_fmt=AVSampleFormat。
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
// PlayerWorker：解码线程对象（demux / 解码 / 时钟 / Seek）
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

    /** [音频] 设置主线程音频设备的目标采样率/声道，供 swr 输出对齐 */
    void setOutputAudioFormat(int sampleRate, int channels)
    {
        QMutexLocker lock(&m_mutex);
        m_outSampleRate = sampleRate > 0 ? sampleRate : 48000;
        m_outChannels = channels > 0 ? channels : 2;
        // 输出格式变化后，强制下一帧重建 swr
        if (m_swr) {
            // [libswresample] swr_free：释放重采样上下文并把指针置空。
            // 形参：s=SwrContext**。
            swr_free(&m_swr);
        }
    }

    /** [控制] 请求暂停：冻结媒体时钟基准 */
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

    /** [控制] 请求继续：重启墙钟，与当前媒体时钟对齐 */
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

    /**
     * [Seek] 播放中异步请求跳转（任意线程可调）
     * 只置标志；真正执行在 runLoop → consumeCommands → performSeek_l
     */
    void requestSeek(qint64 ms)
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
        m_seekTargetMs = ms;
        m_seekReq.store(true);
        m_wait.wakeAll();
    }

    /** [控制] 请求中止当前播放循环 */
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
    bool hardwareDecodeActive() const { return m_hwActive; }

    void setD3D11Device(std::shared_ptr<D3D11SharedDevice> dev)
    {
        m_d3d = std::move(dev);
    }

public slots:
    /**
     * [解析] 打开媒体文件
     * 步骤：open_input → find_stream_info → 选音视频流 → openCodec → buildInfo
     */
    bool openFile(const QString &path)
    {
        closeFile();
        m_lastError.clear();

        AVFormatContext *fmt = nullptr;
        const QByteArray pathBytes = path.toUtf8();
        // [libavformat] avformat_open_input：打开文件、识别容器、创建 AVFormatContext。
        // 形参：ps=输出上下文；url=路径；fmt=强制输入格式(nullptr=自动探测)；
        //       options=打开选项字典(nullptr=无)。返回：0 成功，<0 失败。
        int ret = avformat_open_input(&fmt, pathBytes.constData(), nullptr, nullptr);
        if (ret < 0) {
            m_lastError = QStringLiteral("open_input: %1").arg(avErr(ret));
            return false;
        }
        // [libavformat] avformat_find_stream_info：读一段数据，填各流 codecpar/时长/帧率。
        // 形参：ic=已打开的上下文；options=每流选项(nullptr=默认)。返回：>=0 成功。
        ret = avformat_find_stream_info(fmt, nullptr);
        if (ret < 0) {
            // [libavformat] avformat_close_input：关文件并释放上下文，指针置空。
            // 形参：s=AVFormatContext**。
            avformat_close_input(&fmt);
            m_lastError = QStringLiteral("find_stream_info: %1").arg(avErr(ret));
            return false;
        }

        m_fmt = fmt;
        // [libavformat] av_find_best_stream：按启发式挑最适合播放的那条流。
        // 形参：ic=上下文；type=VIDEO/AUDIO；wanted_stream_nb=-1 表示不指定；
        //       related_stream=-1；decoder_ret=可选输出解码器；flags=0。
        // 返回：流下标，失败为负错误码。
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

        // [libavcodec] av_packet_alloc：分配空压缩包（后续 av_read_frame 填数据）。
        // [libavutil] av_frame_alloc：分配空解码帧（后续 receive_frame 填像素/PCM）。
        // 形参：无。返回：堆对象，失败 nullptr；释放用 av_packet_free / av_frame_free。
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

    /** [解析] 关闭容器与解码器，释放全部 FFmpeg 资源 */
    void closeFile()
    {
        if (m_swr) {
            swr_free(&m_swr);
        }
        if (m_sws) {
            // [libswscale] sws_freeContext：释放像素转换上下文。
            // 形参：swsContext=SwsContext*（这里不是双重指针）。
            sws_freeContext(m_sws);
            m_sws = nullptr;
        }
        // 下列 free 都接受 **，内部置空，传入 nullptr 也安全。
        av_frame_free(&m_vframe);
        av_frame_free(&m_aframe);
        av_packet_free(&m_packet);
        // [libavcodec] avcodec_free_context：关解码器并释放 AVCodecContext。
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
        m_hwActive = false;
        emit audioReset();
    }

    /**
     * [控制] 启动播放：进入 demux/解码主循环（阻塞直到 abort 或 EOF）
     * 必须在 Worker 线程调用（QueuedConnection）
     */
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

    /** [控制] 停止播放并 Seek 回起点（文件保持打开） */
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

    /**
     * [Seek] Worker 线程槽：空闲时立即执行；播放中则转 requestSeek 标志
     * （播放中 Queued 槽可能迟迟进不来，故门面层对 isRunning 走 requestSeek）
     */
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

    /** [控制] 析构前：中止循环并关闭文件 */
    void shutdown()
    {
        requestAbort();
        while (m_running.load()) {
            QThread::msleep(5);
        }
        closeFile();
    }

signals:
    void frameReady(const QImage &frame, qint64 ptsMs); ///< [画面] 一帧可显示图像
    void gpuFrameReady(GpuVideoFrame frame);            ///< [画面] D3D11 硬解 GPU 帧
    void pcmReady(const QByteArray &pcm);               ///< [音频] 一段重采样后的 PCM
    void audioReset();                                  ///< [音频] Seek/停止时清空音频缓冲
    void positionChanged(qint64 positionMs);
    void stateChanged(FFmpegPlayer::State state);
    void errorOccurred(const QString &message);
    void playbackFinished();

private:
    /** [解析] 为指定流创建并打开解码器（视频或音频） */
    bool openCodec(int streamIndex, AVCodecContext **ctxOut)
    {
        AVStream *st = m_fmt->streams[streamIndex];
        // [libavcodec] avcodec_find_decoder：按流的 codec_id 找解码器。
        // 形参：id=AVCodecID。返回：解码器，找不到 nullptr。
        const AVCodec *dec = avcodec_find_decoder(st->codecpar->codec_id);
        if (!dec) {
            m_lastError = QStringLiteral("decoder not found");
            return false;
        }
        // [libavcodec] avcodec_alloc_context3：为该解码器分配运行上下文。
        // 形参：codec=解码器（可 nullptr，这里传入以便填默认值）。
        AVCodecContext *ctx = avcodec_alloc_context3(dec);
        if (!ctx) {
            m_lastError = QStringLiteral("alloc codec ctx failed");
            return false;
        }
        // [libavcodec] avcodec_parameters_to_context：把容器里的 codecpar 拷进解码器。
        // 形参：codec=目标上下文；par=流参数（宽高、采样率、extradata 等）。
        int ret = avcodec_parameters_to_context(ctx, st->codecpar);
        if (ret < 0) {
            avcodec_free_context(&ctx);
            m_lastError = QStringLiteral("parameters_to_context: %1").arg(avErr(ret));
            return false;
        }

        m_hwActive = false;
        const bool tryHw = m_d3d && st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO;
        if (tryHw) {
            AVBufferRef *hwRef = m_d3d->refHwDeviceCtx();
            if (hwRef) {
                ctx->hw_device_ctx = hwRef;
                // get_format：解码器问“你要哪种像素格式”；我们选 AV_PIX_FMT_D3D11。
                ctx->get_format = &PlayerWorker::hwGetFormat;
                ctx->thread_count = 1;      // 硬解必须单线程
                ctx->extra_hw_frames = 16;  // 额外 GPU 表面，避免显示还在用时解码器没表面
            }
        } else {
            ctx->thread_count = 2;
        }

        // [libavcodec] avcodec_open2：真正打开解码器（分配内部缓冲、校验参数）。
        // 形参：avctx=上下文；codec=解码器；options=打开选项(nullptr=默认)。
        // 返回：0 成功。硬解失败时下面会清 hw_device_ctx 再开一次软解。
        ret = avcodec_open2(ctx, dec, nullptr);
        if (ret < 0 && tryHw && ctx->hw_device_ctx) {
            // [libavutil] av_buffer_unref：引用计数 -1，到 0 时释放硬解设备。
            // 形参：buf=AVBufferRef**。
            av_buffer_unref(&ctx->hw_device_ctx);
            ctx->get_format = nullptr;
            ctx->thread_count = 2;
            emit errorOccurred(QStringLiteral("D3D11VA 硬解打开失败，回退软解: %1").arg(avErr(ret)));
            ret = avcodec_open2(ctx, dec, nullptr);
        }
        if (ret < 0) {
            avcodec_free_context(&ctx);
            m_lastError = QStringLiteral("codec_open2: %1").arg(avErr(ret));
            return false;
        }
        m_hwActive = ctx->hw_device_ctx != nullptr;
        *ctxOut = ctx;
        return true;
    }

    static AVPixelFormat hwGetFormat(AVCodecContext *, const AVPixelFormat *pix_fmts)
    {
        for (const AVPixelFormat *p = pix_fmts; *p != AV_PIX_FMT_NONE; ++p) {
            if (*p == AV_PIX_FMT_D3D11) {
                return AV_PIX_FMT_D3D11;
            }
        }
        return AV_PIX_FMT_NONE;
    }

    /** [解析] 根据已打开的 AVFormatContext 填充 MediaInfo（时长、码率、各流） */
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
     * [音频] 用“真实音频帧”初始化/重建 swr（重采样到设备格式）
     * 有声音的关键一步：不能用可能为 NONE 的 codecCtx->sample_fmt。
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
        // [libavutil] av_channel_layout_default：按声道数填默认布局（2 → stereo）。
        // 形参：ch_layout=输出布局；nb_channels=声道数。
        av_channel_layout_default(&outLayout, m_outChannels);

        // [libswresample] swr_alloc_set_opts2：按输入/输出规格创建重采样器。
        // 形参：ps=输出 SwrContext**；out_ch_layout/out_sample_fmt/out_sample_rate=设备侧；
        //       in_ch_layout/in_sample_fmt/in_sample_rate=解码帧侧；log_offset/log_ctx=日志。
        int ret = swr_alloc_set_opts2(&m_swr,
                                      &outLayout,
                                      AV_SAMPLE_FMT_S16,
                                      m_outSampleRate,
                                      &frame->ch_layout,
                                      inFmt,
                                      inRate,
                                      0,
                                      nullptr);
        // [libavutil] av_channel_layout_uninit：释放布局内部堆（与 default 配对）。
        av_channel_layout_uninit(&outLayout);
        if (ret < 0 || !m_swr) {
            emit errorOccurred(QStringLiteral("swr_alloc failed"));
            return false;
        }
        // [libswresample] swr_init：按上面规格初始化，失败则不能 convert。
        // 形参：s=SwrContext*。返回：0 成功。
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

    /**
     * [Seek] 真正执行跳转（调用方须已持有 m_mutex）
     * avformat_seek_file / av_seek_frame + flush 解码器/重采样，并重置时钟
     */
    void performSeek_l(qint64 targetMs)
    {
        // [libavutil] av_rescale_q：毫秒(1/1000) → AV_TIME_BASE(1/1000000)，供容器 seek。
        const qint64 ts = av_rescale_q(targetMs, AVRational{1, 1000}, AVRational{1, AV_TIME_BASE});
        // [libavformat] avformat_seek_file：把 demux 指针跳到目标时间附近的关键帧。
        // 形参：s=上下文；stream_index=-1 表示用 AV_TIME_BASE；
        //       min_ts/ts/max_ts=可接受时间范围；flags=0。
        int ret = avformat_seek_file(m_fmt, -1, INT64_MIN, ts, INT64_MAX, 0);
        if (ret < 0) {
            // [libavformat] av_seek_frame：旧式 seek。flags=AVSEEK_FLAG_BACKWARD 表示往前回关键帧。
            // 形参：s；stream_index=-1；timestamp=目标；flags。
            ret = av_seek_frame(m_fmt, -1, ts, AVSEEK_FLAG_BACKWARD);
        }
        if (ret < 0) {
            emit errorOccurred(QStringLiteral("seek failed: %1").arg(avErr(ret)));
            return;
        }
        if (m_vdec) {
            // [libavcodec] avcodec_flush_buffers：丢掉解码器内部已解/半解的帧（seek 后必须）。
            // 形参：avctx=解码器上下文。
            avcodec_flush_buffers(m_vdec);
        }
        if (m_adec) {
            avcodec_flush_buffers(m_adec);
        }
        // seek 后重采样残留丢弃
        if (m_swr) {
            // [libswresample] swr_convert：这里 in/out 都空，只冲掉内部 delay 缓冲。
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

    /**
     * [时钟] 用帧 PTS 更新媒体时钟，并在漂移过大时重锚墙钟
     * 同时发出 positionChanged 供 UI 刷新进度
     */
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

    /** [画面] 将解码后的 AVFrame 通过 swscale 转为 QImage(ARGB32/BGRA) */
    QImage convertVideoFrame(AVFrame *frame)
    {
        if (!frame || frame->width <= 0 || frame->height <= 0) {
            return {};
        }

        AVFrame *transferred = nullptr;
        const AVFrame *src = frame;
        if (frame->format == AV_PIX_FMT_D3D11) {
            transferred = av_frame_alloc();
            // [libavutil] av_hwframe_transfer_data：把 GPU 表面拷到 CPU 帧（硬解失败回退时用）。
            // 形参：dst=CPU 帧；src=D3D11 帧；flags=0。返回：0 成功。
            if (!transferred || av_hwframe_transfer_data(transferred, frame, 0) < 0) {
                av_frame_free(&transferred);
                return {};
            }
            src = transferred;
        }

        const AVPixelFormat srcFmt = static_cast<AVPixelFormat>(src->format);
        if (srcFmt == AV_PIX_FMT_NONE) {
            av_frame_free(&transferred);
            return {};
        }

        // [libswscale] sws_getCachedContext：按宽高/格式取（或重建）转换器，可复用上次的。
        // 形参：context=旧上下文(可 nullptr)；srcW/H/format=源；dstW/H/format=目标 BGRA；
        //       flags=算法(SWS_BILINEAR)；srcFilter/dstFilter/param=滤镜，这里全空。
        m_sws = sws_getCachedContext(m_sws,
                                     src->width,
                                     src->height,
                                     srcFmt,
                                     src->width,
                                     src->height,
                                     AV_PIX_FMT_BGRA,
                                     SWS_BILINEAR,
                                     nullptr,
                                     nullptr,
                                     nullptr);
        if (!m_sws) {
            av_frame_free(&transferred);
            return {};
        }
        QImage img(src->width, src->height, QImage::Format_ARGB32);
        if (img.isNull()) {
            av_frame_free(&transferred);
            return {};
        }
        uint8_t *dst[4] = {img.bits(), nullptr, nullptr, nullptr};
        int dstStride[4] = {static_cast<int>(img.bytesPerLine()), 0, 0, 0};
        // [libswscale] sws_scale：把 src 像素写进 dst（这里写 QImage 的 bits）。
        // 形参：c=上下文；srcSlice/srcStride=源平面；srcSliceY=起始行；srcSliceH=行数；
        //       dst/dstStride=目标平面。返回：输出高度。
        sws_scale(m_sws, src->data, src->linesize, 0, src->height, dst, dstStride);
        av_frame_free(&transferred);
        return img;
    }

    void outputVideoFrame(qint64 ptsMs)
    {
#ifdef Q_OS_WIN
        if (m_vframe->format == AV_PIX_FMT_D3D11 && m_d3d) {
            GpuVideoFrame gpu = m_d3d->convertDecoderFrame(m_vframe);
            gpu.ptsMs = ptsMs;
            if (gpu.isValid()) {
                if (ptsMs >= 0) {
                    updateClock(ptsMs);
                }
                emit gpuFrameReady(gpu);
                return;
            }
        }
#endif
        const QImage image = convertVideoFrame(m_vframe);
        if (!image.isNull()) {
            if (ptsMs >= 0) {
                updateClock(ptsMs);
            }
            emit frameReady(image, ptsMs);
        }
    }

    /**
     * [时钟] 按媒体时钟等待到帧应显示的 PTS
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

    /**
     * [控制] 在 demux/等待循环中消费暂停与 Seek 请求
     * @return true 表示应 abort 退出循环
     */
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

    /**
     * [画面] 解码一个视频 packet：收帧 → 按 PTS 等待 → 转 RGB → frameReady
     * @return 是否结束本 packet 的处理（含 abort/seek 打断）
     */
    bool decodeVideo(AVPacket *pkt)
    {
        // [libavcodec] avcodec_send_packet：把一包压缩数据送进解码器。
        // 形参：avctx=视频解码器；avpkt=压缩包（pkt=nullptr 表示 flush 尾帧）。
        // 返回：0 成功；AVERROR(EAGAIN)=先 receive；其它负值为失败。
        int ret = avcodec_send_packet(m_vdec, pkt);
        if (ret < 0) {
            return true;
        }
        while (ret >= 0) {
            // [libavcodec] avcodec_receive_frame：取出一帧已解码图像。
            // 形参：avctx；frame=输出 AVFrame（硬解时 format=D3D11，data[0]=纹理）。
            // 返回：0 有帧；EAGAIN=还要再 send；EOF=已冲完。
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
                // [libavutil] av_frame_unref：丢掉本帧缓冲，AVFrame 结构可复用。
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

            outputVideoFrame(ptsMs);
            av_frame_unref(m_vframe);
        }
        return true;
    }

    /**
     * [音频] 解码一个音频 packet：收帧 → 懒加载 swr → 重采样 → pcmReady
     * 纯音频文件时还会驱动时钟（waitForPts + updateClock）
     */
    bool decodeAudio(AVPacket *pkt)
    {
        // send/receive 与视频相同，只是输出是 PCM 样本而不是图像。
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
            // [libswresample] swr_get_delay：重采样器里还积着多少输入采样。
            // 形参：s=上下文；base=用输入采样率计数。
            // [libavutil] av_rescale_rnd：按采样率换算输出缓冲需要多少样本（向上取整）。
            const int maxOut =
                av_rescale_rnd(swr_get_delay(m_swr, srcRate) + m_aframe->nb_samples,
                               m_outSampleRate,
                               srcRate,
                               AV_ROUND_UP);
            QByteArray pcm;
            pcm.resize(maxOut * m_outChannels * static_cast<int>(sizeof(int16_t)));
            uint8_t *outPtr = reinterpret_cast<uint8_t *>(pcm.data());
            // [libswresample] swr_convert：输入帧 PCM → 设备格式 Int16 立体声。
            // 形参：s；out/out_count=输出缓冲及最大样本数；in/in_count=输入平面及样本数。
            // 返回：实际写出的输出样本数。
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

    /**
     * [解复用] 播放主循环：读包 → 分发到 decodeVideo / decodeAudio
     * EOF 后冲刷视频解码器残帧，然后退出
     */
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
                    // [libavcodec] avcodec_send_packet(pkt=nullptr)：输入结束，把内部残留帧吐出。
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
                        outputVideoFrame(ptsMs);
                        av_frame_unref(m_vframe);
                        if (m_abort.load()) {
                            break;
                        }
                    }
                }
                break;
            }

            // [libavformat] av_read_frame：从容器读下一个压缩包（视频或音频交错）。
            // 形参：s=上下文；pkt=输出包（须先 alloc）。返回：0 成功；AVERROR_EOF=文件结束。
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
            // [libavcodec] av_packet_unref：释放本包数据，结构复用给下一轮 read。
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
    std::shared_ptr<D3D11SharedDevice> m_d3d;
    bool m_hwActive = false;
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

/** [音频] 停止并销毁主线程 QAudioSink，清空 PCM 队列 */
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

/** [音频] 在主线程按设备格式打开 QAudioSink（Int16 / 立体声） */
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

/** [音频] 定时从 PCM 队列泵数据到 QAudioSink */
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
    // 构造：创建解码线程 Worker，并桥接信号到本门面 / 主线程音频
    d->worker = new PlayerWorker;
    d->worker->moveToThread(&d->thread);

    connect(d->worker, &PlayerWorker::frameReady, this, &FFmpegPlayer::frameReady);
    qRegisterMetaType<GpuVideoFrame>("GpuVideoFrame");
    connect(d->worker, &PlayerWorker::gpuFrameReady, this, &FFmpegPlayer::gpuFrameReady);
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
    // 析构：中止解码循环、关闭文件、停音频、退出 Worker 线程
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

/** [门面] 打开文件：转发到 Worker.openFile（解析容器/流/解码器） */
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

/** [门面] 关闭文件并释放解码资源 */
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

void FFmpegPlayer::setD3D11SharedDevice(std::shared_ptr<D3D11SharedDevice> device)
{
    if (d->worker->isRunning()) {
        stop();
    }
    QMetaObject::invokeMethod(d->worker, [this, device]() {
        d->worker->setD3D11Device(device);
    }, Qt::BlockingQueuedConnection);
}

bool FFmpegPlayer::hardwareDecodeActive() const
{
    return d->worker && d->worker->hardwareDecodeActive();
}

/** [门面] 播放：主线程开音频设备，再启动 Worker demux 循环 */
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

/** [门面] 暂停 */
void FFmpegPlayer::pause()
{
    if (d->state != State::Playing) {
        return;
    }
    d->worker->requestPause();
}

/** [门面] 停止并回到起点 */
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

/**
 * [门面] Seek
 * 播放中 Worker 卡在 runLoop 时 Queued 槽进不去，故 isRunning 时走 requestSeek 标志；
 * 空闲时投递 applySeek。seekingToMs 用于过滤 seek 完成前的过期 positionChanged。
 */
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
