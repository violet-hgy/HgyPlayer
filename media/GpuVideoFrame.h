#ifndef GPUVIDEOFRAME_H
#define GPUVIDEOFRAME_H

#include <memory>
#include <QMetaType>
#include <QtGlobal>

/**
 * @brief GPU 侧一帧（不经过 QImage / CPU 像素）
 *
 * D3D11 硬解路径：native 指向可着色器采样的 BGRA 纹理（由 VideoProcessor 从解码表面转出）。
 * 用 shared_ptr 管理引用，Queued 信号跨线程时纹理保持有效。
 */
struct GpuVideoFrame
{
    int width = 0;
    int height = 0;
    qint64 ptsMs = -1;
    std::shared_ptr<void> native; ///< D3D11TextureHolder*

    bool isValid() const { return width > 0 && height > 0 && native != nullptr; }
};

Q_DECLARE_METATYPE(GpuVideoFrame)

#endif // GPUVIDEOFRAME_H
