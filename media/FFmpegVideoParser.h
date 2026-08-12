#ifndef FFMPEGVIDEOPARSER_H
#define FFMPEGVIDEOPARSER_H

/**
 * @file FFmpegVideoParser.h
 * @brief 基于 FFmpeg 的媒体解析器（打开容器、读取元数据、可选抽帧）
 *
 * 分层说明（从上到下）：
 * 1. 对外 API（本头文件）—— 业务 / UI 只调用这里
 * 2. 数据结构（MediaTypes.h）—— 与 FFmpeg 解耦的结果模型
 * 3. 实现层（FFmpegVideoParser.cpp）—— 封装 demux / decode
 *
 * 典型用法：
 * @code
 * FFmpegVideoParser parser;
 * if (!parser.open("D:/demo.mp4")) {
 *     qWarning() << parser.lastError();
 *     return;
 * }
 * const MediaInfo info = parser.mediaInfo();
 * qDebug() << info.formatName << info.durationMs;
 * QImage thumb = parser.extractFirstVideoFrame();
 * parser.close();
 * @endcode
 *
 * 线程注意：
 * - 单个实例不要跨线程同时调用
 * - 若要在工作线程解析，请为每个线程创建独立实例
 */

#include "MediaTypes.h"

#include <QImage>
#include <QString>

// 前置声明：把 FFmpeg 不透明指针留在实现里，头文件保持干净
struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct SwsContext;

class FFmpegVideoParser
{
public:
    FFmpegVideoParser();
    ~FFmpegVideoParser();

    FFmpegVideoParser(const FFmpegVideoParser &) = delete;
    FFmpegVideoParser &operator=(const FFmpegVideoParser &) = delete;

    // ============================================================
    // 生命周期
    // ============================================================

    /**
     * @brief 打开媒体文件并解析容器 / 流信息
     * @param filePath 本地路径（UTF-8 友好，Windows 下会做本地化转换）
     * @return true = 打开并 probe 成功
     */
    bool open(const QString &filePath);

    /** @brief 释放全部 FFmpeg 资源；可重复调用 */
    void close();

    bool isOpen() const;

    // ============================================================
    // 解析结果
    // ============================================================

    /** @brief 获取最近一次 open() 填充的媒体信息副本 */
    MediaInfo mediaInfo() const;

    QString lastError() const;

    // ============================================================
    // 可选能力：解码首帧（用于缩略图 / 验证解码链路）
    // ============================================================

    /**
     * @brief 从首选视频流解码第一帧，并转换为 QImage(RGB32)
     * @return 成功返回有效图像；失败返回空 QImage，错误写入 lastError()
     *
     * 说明：
     * - 不改动已解析的 MediaInfo
     * - 内部会临时创建解码器与 swscale 上下文，函数返回后释放
     */
    QImage extractFirstVideoFrame();

private:
    // ---- 实现分层（细节见 .cpp）----
    bool openContainer(const QString &filePath);
    bool probeStreams();
    bool buildMediaInfo();
    MediaStreamInfo buildStreamInfo(int streamIndex) const;

    bool openVideoDecoder();
    void closeVideoDecoder();
    QImage convertFrameToQImage(const AVFrame *frame) const;

    void setError(const QString &message);
    static QString avErrorToString(int errnum);

private:
    // 容器层（demux）
    AVFormatContext *m_formatCtx = nullptr;

    // 解码层（仅抽帧时使用）
    AVCodecContext *m_videoCodecCtx = nullptr;
    int m_activeVideoStream = -1;

    // 结果与错误
    MediaInfo m_mediaInfo;
    QString m_lastError;
    bool m_opened = false;
};

#endif // FFMPEGVIDEOPARSER_H
