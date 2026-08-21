#ifndef CEFVIDEORENDERER_H
#define CEFVIDEORENDERER_H

/**
 * @file CefVideoRenderer.h
 * @brief 浏览器渲染后端（Adapter：IVideoRenderer + IBrowserPlayback）
 *
 * - Adapter：把 CEF 浏览器适配到 IVideoRenderer::widget()/clear()
 * - Bridge：播放控制委托 ICefBrowserHost + HTML5 <video>
 * - PImpl：Private 结构体隐藏全部 CEF 相关成员
 *
 * present()/presentGpu() 为空操作；实际画面由 Chromium 合成。
 */

#include "IBrowserPlayback.h"
#include "IVideoRenderer.h"

#include <memory>

class CefVideoRenderer : public IVideoRenderer, public IBrowserPlayback
{
public:
    explicit CefVideoRenderer(QWidget *parent = nullptr);
    ~CefVideoRenderer() override;

    QWidget *widget() override;
    Backend backend() const override { return Backend::Cef; }
    QString name() const override { return QStringLiteral("CEF (Browser)"); }

    void present(const QImage &frame) override;
    void presentGpu(const GpuVideoFrame &frame) override;
    void clear(const QString &placeholder = QString()) override;

    bool browserAvailable() const override;
    bool openMedia(const QString &filePath) override;
    void playMedia() override;
    void pauseMedia() override;
    void stopMedia() override;
    void seekMedia(qint64 positionMs) override;
    qint64 browserPositionMs() const override;
    void pollBrowserPosition() override;

private:
    struct Private;
    std::unique_ptr<Private> d;
};

#endif // CEFVIDEORENDERER_H
