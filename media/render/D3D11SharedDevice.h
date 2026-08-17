#ifndef D3D11SHAREDDEVICE_H
#define D3D11SHAREDDEVICE_H

#include <QtGlobal>

#ifdef Q_OS_WIN

#include "GpuVideoFrame.h"

#include <memory>

struct AVFrame;
struct AVBufferRef;
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;

/**
 * @brief 解码器与渲染器共用的 D3D11 设备
 *
 * - 硬解（D3D11VA）和交换链必须在同一 ID3D11Device 上，才是零拷贝（无 CPU 搬像素）
 * - immediate context 非线程安全：FFmpeg lock 回调与 Present 共用一把递归锁
 *
 * 像素路径：解码表面(NV12) --VideoProcessor--> BGRA 纹理 --着色器--> 窗口
 * 全程在 GPU，没有 sws_scale / QImage / memcpy。
 */
class D3D11SharedDevice
{
public:
    static std::shared_ptr<D3D11SharedDevice> create();

    ~D3D11SharedDevice();

    D3D11SharedDevice(const D3D11SharedDevice &) = delete;
    D3D11SharedDevice &operator=(const D3D11SharedDevice &) = delete;

    ID3D11Device *device() const;
    ID3D11DeviceContext *context() const;

    void lock();
    void unlock();

    /** av_buffer_ref 一份 FFmpeg hw_device_ctx，调用方 av_buffer_unref */
    AVBufferRef *refHwDeviceCtx();

    /**
     * 将 D3D11VA 解码帧（array texture + index）转到可采样的 BGRA 纹理。
     * 须在 FFmpeg 解码锁之外调用；内部会 lock context。
     */
    GpuVideoFrame convertDecoderFrame(AVFrame *frame);

    static ID3D11Texture2D *textureFrom(const GpuVideoFrame &frame);

private:
    D3D11SharedDevice() = default;
    bool init();

    struct Impl;
    Impl *m = nullptr;
};

#endif // Q_OS_WIN

#endif // D3D11SHAREDDEVICE_H
