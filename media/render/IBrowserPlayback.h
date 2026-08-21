#ifndef IBROWSERPLAYBACK_H
#define IBROWSERPLAYBACK_H

/**
 * @file IBrowserPlayback.h
 * @brief 浏览器/HTML5 播放能力（Bridge 抽象层）
 *
 * 与 IVideoRenderer 正交：帧渲染后端走 present()，浏览器后端走本接口。
 * MainWindow 通过 dynamic_cast<IBrowserPlayback*> 识别，无需引入任何 CEF 头文件。
 */

#include <QtGlobal>

class QString;

class IBrowserPlayback
{
public:
    virtual ~IBrowserPlayback() = default;

    /** CEF 运行时是否已就绪（SDK 已链接且 Initialize 成功） */
    virtual bool browserAvailable() const = 0;

    /** 在浏览器中加载本地媒体（file://） */
    virtual bool openMedia(const QString &filePath) = 0;

    virtual void playMedia() = 0;
    virtual void pauseMedia() = 0;
    virtual void stopMedia() = 0;
    virtual void seekMedia(qint64 positionMs) = 0;

    /** 当前播放位置（毫秒）；浏览器端无法精确查询时由实现估算 */
    virtual qint64 browserPositionMs() const = 0;

    /** 由实现在 tick 时上报进度（可选，供 MainWindow 刷新进度条） */
    virtual void pollBrowserPosition() = 0;
};

#endif // IBROWSERPLAYBACK_H
