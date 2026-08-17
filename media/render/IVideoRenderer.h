#ifndef IVIDEORENDERER_H
#define IVIDEORENDERER_H

/**
 * @file IVideoRenderer.h
 * @brief 视频渲染抽象接口
 *
 * 播放器产出 QImage（软解）或 GpuVideoFrame（D3D11 硬解零拷贝）；
 * 具体如何画到屏幕由渲染后端决定：
 *   - QImage：QLabel + QPixmap（软件缩放）
 *   - OpenGL：GPU 纹理上传 + 着色器绘制
 *   - D3D11：Direct3D 11 交换链（可接 CPU 图或硬解 GPU 帧）
 */

#include "GpuVideoFrame.h"

#include <QImage>
#include <QString>
#include <memory>

class QWidget;
class D3D11SharedDevice;

class IVideoRenderer
{
public:
    enum class Backend {
        QImage,
        OpenGL,
        D3D11,
        D3D11Hw ///< D3D11 显示 + D3D11VA 硬解零拷贝
    };

    virtual ~IVideoRenderer() = default;

    /** 嵌入布局用的显示控件（生命周期由渲染器持有） */
    virtual QWidget *widget() = 0;

    virtual Backend backend() const = 0;
    virtual QString name() const = 0;

    /** 显示一帧 CPU 图（调用线程应为 UI 线程） */
    virtual void present(const QImage &frame) = 0;

    /** 显示一帧 GPU 图；默认忽略（仅 D3D11 硬解路径实现） */
    virtual void presentGpu(const GpuVideoFrame &frame)
    {
        Q_UNUSED(frame);
    }

    virtual bool supportsGpuFrames() const { return false; }

    /** D3D11 渲染器返回与解码器共用的设备；其它后端为空 */
    virtual std::shared_ptr<D3D11SharedDevice> d3d11SharedDevice() const { return {}; }

    /** 清空画面，显示占位提示 */
    virtual void clear(const QString &placeholder = QString()) = 0;
};

#endif // IVIDEORENDERER_H
