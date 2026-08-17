#include "FFmpegVideoParser.h"

/**
 * @file FFmpegVideoParser.cpp
 * @brief FFmpeg 解析实现
 *
 * 本文件按“流水线分层”组织，阅读顺序建议：
 *   [1] 错误工具
 *   [2] 生命周期 open/close
 *   [3] 容器层：打开文件 + find_stream_info
 *   [4] 元数据层：把 AV* 转成 MediaInfo
 *   [5] 解码层：抽首帧 + 像素格式转换
 *
 * FFmpeg API 注释：[来源库] 函数名：作用。下一行写形参。
 */

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

#include <QFileInfo>

// ============================================================================
// [1] 构造 / 析构 / 错误工具
// ============================================================================

FFmpegVideoParser::FFmpegVideoParser() = default;

FFmpegVideoParser::~FFmpegVideoParser()
{
    close();
}

void FFmpegVideoParser::setError(const QString &message)
{
    m_lastError = message;
}

QString FFmpegVideoParser::avErrorToString(int errnum)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
    // [libavutil] av_strerror：错误码 → 可读字符串。
    // 形参：errnum=负错误码；errbuf/errbuf_size=输出缓冲。
    av_strerror(errnum, buf, sizeof(buf));
    return QString::fromUtf8(buf);
}

QString FFmpegVideoParser::lastError() const
{
    return m_lastError;
}

bool FFmpegVideoParser::isOpen() const
{
    return m_opened;
}

MediaInfo FFmpegVideoParser::mediaInfo() const
{
    return m_mediaInfo;
}

// ============================================================================
// [2] 生命周期
// ============================================================================

bool FFmpegVideoParser::open(const QString &filePath)
{
    close();
    m_lastError.clear();

    if (filePath.isEmpty()) {
        setError(QStringLiteral("filePath is empty"));
        return false;
    }

    // 分层调用：容器 -> 流探测 -> 元数据汇总
    if (!openContainer(filePath)) {
        close();
        return false;
    }
    if (!probeStreams()) {
        close();
        return false;
    }
    if (!buildMediaInfo()) {
        close();
        return false;
    }

    m_opened = true;
    return true;
}

void FFmpegVideoParser::close()
{
    closeVideoDecoder();

    if (m_formatCtx) {
        // [libavformat] avformat_close_input：关文件并释放上下文，指针置空。
        // 形参：s=AVFormatContext**。
        avformat_close_input(&m_formatCtx);
        m_formatCtx = nullptr;
    }

    m_mediaInfo = MediaInfo{};
    m_activeVideoStream = -1;
    m_opened = false;
}

// ============================================================================
// [3] 容器层：打开输入 + 读取流信息
// ============================================================================

bool FFmpegVideoParser::openContainer(const QString &filePath)
{
    const QByteArray pathBytes = filePath.toUtf8();

    AVFormatContext *fmt = nullptr;
    // [libavformat] avformat_open_input：打开文件、识别容器、创建 AVFormatContext。
    // 形参：ps=输出上下文；url=路径；fmt=强制格式(nullptr=探测)；options=打开选项。
    const int openRet = avformat_open_input(&fmt, pathBytes.constData(), nullptr, nullptr);
    if (openRet < 0) {
        setError(QStringLiteral("avformat_open_input failed: %1").arg(avErrorToString(openRet)));
        return false;
    }

    m_formatCtx = fmt;
    m_mediaInfo.filePath = filePath;

    const QFileInfo fi(filePath);
    if (fi.exists()) {
        m_mediaInfo.fileSize = fi.size();
    }
    return true;
}

bool FFmpegVideoParser::probeStreams()
{
    // [libavformat] avformat_find_stream_info：读一段数据，填各流 codecpar/时长/帧率。
    // 形参：ic=已打开上下文；options=每流选项(nullptr=默认)。
    const int ret = avformat_find_stream_info(m_formatCtx, nullptr);
    if (ret < 0) {
        setError(QStringLiteral("avformat_find_stream_info failed: %1").arg(avErrorToString(ret)));
        return false;
    }
    return true;
}

// ============================================================================
// [4] 元数据层：AV* -> MediaInfo / MediaStreamInfo
// ============================================================================

static qint64 ptsToMs(qint64 ts, AVRational timeBase)
{
    if (ts == AV_NOPTS_VALUE) {
        return -1;
    }
    // [libavutil] av_rescale_q：按有理数换时间基。形参：a=时间戳；bq=源时间基；cq=1/1000 秒。
    return av_rescale_q(ts, timeBase, AVRational{1, 1000});
}

static QString mediaTypeName(AVMediaType type)
{
    switch (type) {
    case AVMEDIA_TYPE_VIDEO: return QStringLiteral("video");
    case AVMEDIA_TYPE_AUDIO: return QStringLiteral("audio");
    case AVMEDIA_TYPE_SUBTITLE: return QStringLiteral("subtitle");
    case AVMEDIA_TYPE_DATA: return QStringLiteral("data");
    case AVMEDIA_TYPE_ATTACHMENT: return QStringLiteral("attachment");
    default: return QStringLiteral("unknown");
    }
}

MediaStreamInfo FFmpegVideoParser::buildStreamInfo(int streamIndex) const
{
    MediaStreamInfo info;
    if (!m_formatCtx || streamIndex < 0 || streamIndex >= static_cast<int>(m_formatCtx->nb_streams)) {
        return info;
    }

    AVStream *st = m_formatCtx->streams[streamIndex];
    const AVCodecParameters *par = st->codecpar;

    info.index = st->index;
    info.mediaType = mediaTypeName(par->codec_type);
    info.bitrate = par->bit_rate;

    // [libavcodec] avcodec_find_decoder：按 codec_id 找解码器。形参：id。找不到再查名字。
    if (const AVCodec *codec = avcodec_find_decoder(par->codec_id)) {
        info.codecName = QString::fromUtf8(codec->name ? codec->name : "");
        info.codecLongName = QString::fromUtf8(codec->long_name ? codec->long_name : "");
    } else if (const char *name = avcodec_get_name(par->codec_id)) {
        info.codecName = QString::fromUtf8(name);
    }

    // 流时长优先用 stream->duration；无效则回退到容器时长
    if (st->duration != AV_NOPTS_VALUE) {
        info.durationMs = ptsToMs(st->duration, st->time_base);
    } else if (m_formatCtx->duration != AV_NOPTS_VALUE) {
        info.durationMs = m_formatCtx->duration / (AV_TIME_BASE / 1000);
    }

    if (par->codec_type == AVMEDIA_TYPE_VIDEO) {
        info.width = par->width;
        info.height = par->height;

        // avg_frame_rate 比 r_frame_rate 更接近“平均播放帧率”
        if (st->avg_frame_rate.den != 0) {
            // [libavutil] av_q2d：AVRational → double（fps）。
            info.frameRate = av_q2d(st->avg_frame_rate);
        } else if (st->r_frame_rate.den != 0) {
            info.frameRate = av_q2d(st->r_frame_rate);
        }

        // [libavutil] av_get_pix_fmt_name：像素格式枚举 → "yuv420p"。
        if (const char *pix = av_get_pix_fmt_name(static_cast<AVPixelFormat>(par->format))) {
            info.pixelFormat = QString::fromUtf8(pix);
        }
    } else if (par->codec_type == AVMEDIA_TYPE_AUDIO) {
        info.sampleRate = par->sample_rate;
        info.channels = par->ch_layout.nb_channels;

        // [libavutil] av_get_sample_fmt_name：采样格式枚举 → "fltp"。
        if (const char *sf = av_get_sample_fmt_name(static_cast<AVSampleFormat>(par->format))) {
            info.sampleFormat = QString::fromUtf8(sf);
        }

        char layoutBuf[128] = {0};
        // [libavutil] av_channel_layout_describe：声道布局 → "stereo" 等。
        // 形参：ch_layout；buf/buf_size=输出。
        if (av_channel_layout_describe(&par->ch_layout, layoutBuf, sizeof(layoutBuf)) >= 0) {
            info.channelLayout = QString::fromUtf8(layoutBuf);
        }
    }

    return info;
}

bool FFmpegVideoParser::buildMediaInfo()
{
    if (!m_formatCtx) {
        setError(QStringLiteral("format context is null"));
        return false;
    }

    if (m_formatCtx->iformat) {
        m_mediaInfo.formatName = QString::fromUtf8(m_formatCtx->iformat->name
                                                       ? m_formatCtx->iformat->name
                                                       : "");
        m_mediaInfo.formatLongName = QString::fromUtf8(m_formatCtx->iformat->long_name
                                                           ? m_formatCtx->iformat->long_name
                                                           : "");
    }

    if (m_formatCtx->duration != AV_NOPTS_VALUE) {
        // AV_TIME_BASE = 1_000_000，因此 /1000 得到毫秒
        m_mediaInfo.durationMs = m_formatCtx->duration / (AV_TIME_BASE / 1000);
    }
    m_mediaInfo.bitrate = m_formatCtx->bit_rate;

    m_mediaInfo.streams.clear();
    m_mediaInfo.streams.reserve(static_cast<int>(m_formatCtx->nb_streams));

    for (unsigned i = 0; i < m_formatCtx->nb_streams; ++i) {
        m_mediaInfo.streams.push_back(buildStreamInfo(static_cast<int>(i)));
    }

    // [libavformat] av_find_best_stream：挑最适合播放的那条流。
    // 形参：ic；type=VIDEO/AUDIO；wanted_stream_nb=-1；related_stream=-1；decoder_ret；flags=0。
    m_mediaInfo.videoStreamIndex = av_find_best_stream(m_formatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (m_mediaInfo.videoStreamIndex < 0) {
        m_mediaInfo.videoStreamIndex = -1;
    }

    m_mediaInfo.audioStreamIndex = av_find_best_stream(m_formatCtx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (m_mediaInfo.audioStreamIndex < 0) {
        m_mediaInfo.audioStreamIndex = -1;
    }

    return true;
}

// ============================================================================
// [5] 解码层：打开视频解码器 -> 读包送解码器 -> 转 RGB QImage
// ============================================================================

bool FFmpegVideoParser::openVideoDecoder()
{
    closeVideoDecoder();

    if (!m_formatCtx || m_mediaInfo.videoStreamIndex < 0) {
        setError(QStringLiteral("no video stream available"));
        return false;
    }

    const int streamIndex = m_mediaInfo.videoStreamIndex;
    AVStream *st = m_formatCtx->streams[streamIndex];
    // [libavcodec] avcodec_find_decoder：按流 codec_id 找解码器。
    const AVCodec *decoder = avcodec_find_decoder(st->codecpar->codec_id);
    if (!decoder) {
        setError(QStringLiteral("decoder not found for codec_id=%1").arg(st->codecpar->codec_id));
        return false;
    }

    // [libavcodec] avcodec_alloc_context3：分配解码器运行上下文。形参：codec。
    AVCodecContext *codecCtx = avcodec_alloc_context3(decoder);
    if (!codecCtx) {
        setError(QStringLiteral("avcodec_alloc_context3 failed"));
        return false;
    }

    // [libavcodec] avcodec_parameters_to_context：把流参数拷进解码器。形参：codec；par。
    int ret = avcodec_parameters_to_context(codecCtx, st->codecpar);
    if (ret < 0) {
        avcodec_free_context(&codecCtx);
        setError(QStringLiteral("avcodec_parameters_to_context failed: %1").arg(avErrorToString(ret)));
        return false;
    }

    // [libavcodec] avcodec_open2：打开解码器。形参：avctx；codec；options=nullptr。
    ret = avcodec_open2(codecCtx, decoder, nullptr);
    if (ret < 0) {
        avcodec_free_context(&codecCtx);
        setError(QStringLiteral("avcodec_open2 failed: %1").arg(avErrorToString(ret)));
        return false;
    }

    m_videoCodecCtx = codecCtx;
    m_activeVideoStream = streamIndex;
    return true;
}

void FFmpegVideoParser::closeVideoDecoder()
{
    if (m_videoCodecCtx) {
        // [libavcodec] avcodec_free_context：关解码器并释放上下文。
        avcodec_free_context(&m_videoCodecCtx);
        m_videoCodecCtx = nullptr;
    }
    m_activeVideoStream = -1;
}

QImage FFmpegVideoParser::convertFrameToQImage(const AVFrame *frame) const
{
    if (!frame || frame->width <= 0 || frame->height <= 0) {
        return {};
    }

    const AVPixelFormat srcFmt = static_cast<AVPixelFormat>(frame->format);
    const AVPixelFormat dstFmt = AV_PIX_FMT_RGB32;

    // [libswscale] sws_getContext：创建像素转换器（抽帧用一次即丢，播放路径应缓存）。
    // 形参：srcW/H/format；dstW/H/format=RGB32；flags=SWS_BILINEAR；滤镜全空。
    SwsContext *sws = sws_getContext(frame->width,
                                     frame->height,
                                     srcFmt,
                                     frame->width,
                                     frame->height,
                                     dstFmt,
                                     SWS_BILINEAR,
                                     nullptr,
                                     nullptr,
                                     nullptr);
    if (!sws) {
        return {};
    }

    QImage image(frame->width, frame->height, QImage::Format_RGB32);
    uint8_t *dstSlices[4] = {image.bits(), nullptr, nullptr, nullptr};
    int dstStrides[4] = {static_cast<int>(image.bytesPerLine()), 0, 0, 0};

    // [libswscale] sws_scale：YUV → RGB 写入 QImage。形参：c；src/srcStride；srcSliceY/H；dst/dstStride。
    sws_scale(sws,
              frame->data,
              frame->linesize,
              0,
              frame->height,
              dstSlices,
              dstStrides);

    // [libswscale] sws_freeContext：释放转换器。
    sws_freeContext(sws);
    return image;
}

QImage FFmpegVideoParser::extractFirstVideoFrame()
{
    if (!m_opened) {
        setError(QStringLiteral("parser is not open"));
        return {};
    }

    if (!openVideoDecoder()) {
        return {};
    }

    // [libavcodec] av_packet_alloc / [libavutil] av_frame_alloc：空压缩包与空解码帧。
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    if (!packet || !frame) {
        av_packet_free(&packet);
        av_frame_free(&frame);
        closeVideoDecoder();
        setError(QStringLiteral("alloc packet/frame failed"));
        return {};
    }

    QImage result;
    bool gotFrame = false;

    // [libavformat] av_read_frame：读下一压缩包。形参：s；pkt。返回：>=0 成功。
    while (!gotFrame && av_read_frame(m_formatCtx, packet) >= 0) {
        if (packet->stream_index != m_activeVideoStream) {
            // [libavcodec] av_packet_unref：丢掉本包数据，结构复用。
            av_packet_unref(packet);
            continue;
        }

        // [libavcodec] avcodec_send_packet：送压缩包。pkt=nullptr 表示 flush。
        int ret = avcodec_send_packet(m_videoCodecCtx, packet);
        av_packet_unref(packet);
        if (ret < 0) {
            setError(QStringLiteral("avcodec_send_packet failed: %1").arg(avErrorToString(ret)));
            break;
        }

        while (ret >= 0) {
            // [libavcodec] avcodec_receive_frame：取一帧图像。EAGAIN=再 send；EOF=冲完。
            ret = avcodec_receive_frame(m_videoCodecCtx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            }
            if (ret < 0) {
                setError(QStringLiteral("avcodec_receive_frame failed: %1").arg(avErrorToString(ret)));
                break;
            }

            result = convertFrameToQImage(frame);
            av_frame_unref(frame);
            if (!result.isNull()) {
                gotFrame = true;
            } else {
                setError(QStringLiteral("convertFrameToQImage failed"));
            }
            break;
        }
    }

    // 有些文件开头只有音频包，读完文件仍无帧：尝试 flush 解码器
    if (!gotFrame) {
        avcodec_send_packet(m_videoCodecCtx, nullptr);
        if (avcodec_receive_frame(m_videoCodecCtx, frame) >= 0) {
            result = convertFrameToQImage(frame);
            gotFrame = !result.isNull();
            av_frame_unref(frame);
        }
        if (!gotFrame && m_lastError.isEmpty()) {
            setError(QStringLiteral("no video frame decoded"));
        }
    }

    av_packet_free(&packet);
    av_frame_free(&frame);
    closeVideoDecoder();

    // 抽帧会推进 demux 位置；seek 回开头
    if (m_formatCtx) {
        // [libavformat] av_seek_frame：跳到时间戳。flags=BACKWARD 回关键帧。形参：s；stream=-1；timestamp=0。
        av_seek_frame(m_formatCtx, -1, 0, AVSEEK_FLAG_BACKWARD);
    }

    return result;
}
