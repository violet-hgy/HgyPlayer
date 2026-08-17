#ifndef D3D11VIDEORENDERER_H
#define D3D11VIDEORENDERER_H

#include "IVideoRenderer.h"

#ifdef Q_OS_WIN

#include "D3D11SharedDevice.h"

#include <QPointer>
#include <QWidget>
#include <memory>

/**
 * @brief Direct3D 11 渲染
 *
 * 与硬解共用 D3D11SharedDevice：软解走 QImage 上传，硬解走 GpuVideoFrame（无 CPU 像素）。
 */
class D3D11VideoRenderer : public IVideoRenderer
{
public:
    explicit D3D11VideoRenderer(QWidget *parent = nullptr);
    ~D3D11VideoRenderer() override;

    QWidget *widget() override;
    Backend backend() const override { return m_backend; }
    QString name() const override;

    void setBackendKind(Backend kind);

    void present(const QImage &frame) override;
    void presentGpu(const GpuVideoFrame &frame) override;
    bool supportsGpuFrames() const override { return true; }
    std::shared_ptr<D3D11SharedDevice> d3d11SharedDevice() const override { return m_gpu; }
    void clear(const QString &placeholder = QString()) override;

private:
    class RenderWindow;

    Backend m_backend = Backend::D3D11;
    std::shared_ptr<D3D11SharedDevice> m_gpu;
    RenderWindow *m_window = nullptr;
    QPointer<QWidget> m_container;
};

#endif // Q_OS_WIN

#endif // D3D11VIDEORENDERER_H
