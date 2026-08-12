#ifndef MEDIATYPES_H
#define MEDIATYPES_H

/**
 * @file MediaTypes.h
 * @brief 媒体解析结果的纯数据结构（不依赖 FFmpeg 头文件）
 *
 * 设计意图：
 * - UI / 业务层只依赖本文件，避免把 FFmpeg C API 泄漏到整个工程
 * - VideoParser 负责填充这些结构体
 */

#include <QString>
#include <QVector>
#include <QtGlobal>

/**
 * @brief 单条流的基础信息（视频 / 音频 / 字幕等）
 */
struct MediaStreamInfo
{
    int index = -1;                 ///< 在容器中的流下标（AVStream::index）
    QString mediaType;              ///< "video" / "audio" / "subtitle" / "data" / "unknown"
    QString codecName;              ///< 编码器短名，例如 h264、aac
    QString codecLongName;          ///< 编码器可读全名
    qint64 bitrate = 0;             ///< 码率（bps），未知时为 0
    qint64 durationMs = -1;         ///< 流时长（毫秒），未知时为 -1

    // ---- 视频专属（mediaType == "video" 时有效）----
    int width = 0;
    int height = 0;
    double frameRate = 0.0;         ///< 估算帧率（fps）
    QString pixelFormat;            ///< 像素格式名，例如 yuv420p

    // ---- 音频专属（mediaType == "audio" 时有效）----
    int sampleRate = 0;             ///< 采样率（Hz）
    int channels = 0;               ///< 声道数
    QString sampleFormat;           ///< 采样格式名，例如 fltp
    QString channelLayout;          ///< 声道布局描述，例如 stereo
};

/**
 * @brief 整个媒体文件（容器）的汇总信息
 */
struct MediaInfo
{
    QString filePath;               ///< 打开时使用的路径
    QString formatName;             ///< 容器短名，例如 mp4、matroska
    QString formatLongName;         ///< 容器可读全名
    qint64 durationMs = -1;         ///< 总时长（毫秒）
    qint64 bitrate = 0;             ///< 总体码率（bps）
    qint64 fileSize = 0;            ///< 文件大小（字节），未知时为 0

    int videoStreamIndex = -1;      ///< 首选视频流下标，无视频则为 -1
    int audioStreamIndex = -1;      ///< 首选音频流下标，无音频则为 -1

    QVector<MediaStreamInfo> streams; ///< 全部流列表
};

#endif // MEDIATYPES_H
