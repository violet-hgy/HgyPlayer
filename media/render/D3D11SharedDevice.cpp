#include "D3D11SharedDevice.h"

#ifdef Q_OS_WIN

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <d3d11.h>
#include <d3d11_4.h>
#include <wrl/client.h>

#include <mutex>
#include <vector>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
}
#include <libavutil/hwcontext_d3d11va.h>

using Microsoft::WRL::ComPtr;

struct D3D11TextureHolder {
    ComPtr<ID3D11Texture2D> tex;
};

struct D3D11SharedDevice::Impl {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> ctx;
    ComPtr<ID3D11VideoDevice> videoDevice;
    ComPtr<ID3D11VideoContext> videoCtx;
    ComPtr<ID3D11VideoProcessorEnumerator> vpEnum;
    ComPtr<ID3D11VideoProcessor> vp;
    int vpW = 0;
    int vpH = 0;
    DXGI_FORMAT vpInFormat = DXGI_FORMAT_UNKNOWN;

    AVBufferRef *avHw = nullptr;
    std::recursive_mutex mutex;
    std::vector<std::shared_ptr<D3D11TextureHolder>> pool;

    static void lockCb(void *p)
    {
        static_cast<Impl *>(p)->mutex.lock();
    }
    static void unlockCb(void *p)
    {
        static_cast<Impl *>(p)->mutex.unlock();
    }

    ~Impl()
    {
        pool.clear();
        vp.Reset();
        vpEnum.Reset();
        if (avHw) {
            // [libavutil] av_buffer_unref：释放硬解设备缓冲。
            av_buffer_unref(&avHw);
        }
    }

    bool ensureProcessor(int w, int h, DXGI_FORMAT inFmt)
    {
        if (vp && vpW == w && vpH == h && vpInFormat == inFmt) {
            return true;
        }
        vp.Reset();
        vpEnum.Reset();
        vpW = vpH = 0;
        vpInFormat = DXGI_FORMAT_UNKNOWN;

        D3D11_VIDEO_PROCESSOR_CONTENT_DESC cd{};
        cd.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
        cd.InputWidth = UINT(w);
        cd.InputHeight = UINT(h);
        cd.OutputWidth = UINT(w);
        cd.OutputHeight = UINT(h);
        cd.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
        // [D3D11 Video] CreateVideoProcessorEnumerator：按宽高创建视频处理器枚举器。
        // 形参：pDesc=内容描述；ppEnum=输出。
        HRESULT hr = videoDevice->CreateVideoProcessorEnumerator(&cd, &vpEnum);
        if (FAILED(hr)) {
            return false;
        }

        UINT inCaps = 0;
        UINT outCaps = 0;
        // [D3D11 Video] CheckVideoProcessorFormat：查询某 DXGI 格式能否作输入/输出。
        // 形参：Format；pFlags=能力位（INPUT/OUTPUT）。
        vpEnum->CheckVideoProcessorFormat(inFmt, &inCaps);
        vpEnum->CheckVideoProcessorFormat(DXGI_FORMAT_B8G8R8A8_UNORM, &outCaps);
        if (!(inCaps & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT)
            || !(outCaps & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT)) {
            return false;
        }

        // [D3D11 Video] CreateVideoProcessor：创建处理器。形参：pEnum；RateConversionIndex=0；ppVideoProcessor。
        hr = videoDevice->CreateVideoProcessor(vpEnum.Get(), 0, &vp);
        if (FAILED(hr)) {
            return false;
        }

        RECT r{0, 0, w, h};
        // [D3D11 Video] VideoProcessorSetStream*：设置流为逐行、源/目标矩形为整帧。
        // 形参：pVP；StreamIndex=0；Enable；pRect。
        videoCtx->VideoProcessorSetStreamFrameFormat(vp.Get(), 0,
                                                     D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
        videoCtx->VideoProcessorSetStreamSourceRect(vp.Get(), 0, TRUE, &r);
        videoCtx->VideoProcessorSetStreamDestRect(vp.Get(), 0, TRUE, &r);
        videoCtx->VideoProcessorSetOutputTargetRect(vp.Get(), TRUE, &r);

        vpW = w;
        vpH = h;
        vpInFormat = inFmt;
        return true;
    }

    std::shared_ptr<D3D11TextureHolder> acquireBgra(int w, int h)
    {
        for (auto &slot : pool) {
            if (slot.use_count() == 1 && slot->tex) {
                D3D11_TEXTURE2D_DESC d{};
                slot->tex->GetDesc(&d);
                if (int(d.Width) == w && int(d.Height) == h) {
                    return slot;
                }
            }
        }
        D3D11_TEXTURE2D_DESC td{};
        td.Width = UINT(w);
        td.Height = UINT(h);
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        ComPtr<ID3D11Texture2D> tex;
        // [D3D11] CreateTexture2D：建 BGRA 纹理（既当 VP 输出 RT，又给着色器采样）。
        // 形参：pDesc；pInitialData=nullptr；ppTexture2D。
        if (FAILED(device->CreateTexture2D(&td, nullptr, &tex))) {
            return nullptr;
        }
        auto holder = std::make_shared<D3D11TextureHolder>();
        holder->tex = tex;
        pool.push_back(holder);
        return holder;
    }
};

std::shared_ptr<D3D11SharedDevice> D3D11SharedDevice::create()
{
    auto p = std::shared_ptr<D3D11SharedDevice>(new D3D11SharedDevice);
    if (!p->init()) {
        return nullptr;
    }
    return p;
}

bool D3D11SharedDevice::init()
{
    m = new Impl;
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
    D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    const D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };
    // [D3D11] D3D11CreateDevice：创建硬件设备和 immediate context（解码+渲染共用）。
    // 形参：pAdapter=nullptr 默认 GPU；DriverType=HARDWARE；Software=nullptr；
    //       Flags=BGRA+VIDEO；pFeatureLevels；FeatureLevels 个数；SDKVersion；
    //       ppDevice；pFeatureLevel=实际等级；ppImmediateContext。
    HRESULT hr = D3D11CreateDevice(nullptr,
                                   D3D_DRIVER_TYPE_HARDWARE,
                                   nullptr,
                                   flags,
                                   levels,
                                   ARRAYSIZE(levels),
                                   D3D11_SDK_VERSION,
                                   &m->device,
                                   &level,
                                   &m->ctx);
    if (FAILED(hr)) {
        flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        hr = D3D11CreateDevice(nullptr,
                               D3D_DRIVER_TYPE_HARDWARE,
                               nullptr,
                               flags,
                               levels,
                               ARRAYSIZE(levels),
                               D3D11_SDK_VERSION,
                               &m->device,
                               &level,
                               &m->ctx);
    }
    if (FAILED(hr)) {
        delete m;
        m = nullptr;
        return false;
    }

    m->device.As(&m->videoDevice);
    m->ctx.As(&m->videoCtx);

    ComPtr<ID3D11Multithread> mt;
    if (SUCCEEDED(m->ctx.As(&mt))) {
        // [D3D11] ID3D11Multithread::SetMultithreadProtected：允许解码线程与 Present 共用 context。
        // 形参：bMTProtect=TRUE。
        mt->SetMultithreadProtected(TRUE);
    }
    return true;
}

D3D11SharedDevice::~D3D11SharedDevice()
{
    delete m;
    m = nullptr;
}

ID3D11Device *D3D11SharedDevice::device() const
{
    return m ? m->device.Get() : nullptr;
}

ID3D11DeviceContext *D3D11SharedDevice::context() const
{
    return m ? m->ctx.Get() : nullptr;
}

void D3D11SharedDevice::lock()
{
    if (m) {
        m->mutex.lock();
    }
}

void D3D11SharedDevice::unlock()
{
    if (m) {
        m->mutex.unlock();
    }
}

AVBufferRef *D3D11SharedDevice::refHwDeviceCtx()
{
    if (!m || !m->device) {
        return nullptr;
    }
    if (!m->avHw) {
        // [libavutil] av_hwdevice_ctx_alloc：分配 D3D11VA 硬解设备缓冲（尚未绑定真实设备）。
        // 形参：type=AV_HWDEVICE_TYPE_D3D11VA。返回：AVBufferRef*，失败 nullptr。
        m->avHw = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
        if (!m->avHw) {
            return nullptr;
        }
        auto *hw = reinterpret_cast<AVHWDeviceContext *>(m->avHw->data);
        auto *d3d = static_cast<AVD3D11VADeviceContext *>(hw->hwctx);
        d3d->device = m->device.Get();
        d3d->device->AddRef();
        d3d->device_context = m->ctx.Get();
        d3d->device_context->AddRef();
        d3d->lock = &Impl::lockCb;
        d3d->unlock = &Impl::unlockCb;
        d3d->lock_ctx = m;
        // [libavutil] av_hwdevice_ctx_init：用上面填好的 ID3D11Device 完成硬解设备初始化。
        // 形参：ref=刚 alloc 的缓冲。返回：0 成功。
        if (av_hwdevice_ctx_init(m->avHw) < 0) {
            av_buffer_unref(&m->avHw);
            return nullptr;
        }
    }
    // [libavutil] av_buffer_ref：引用计数 +1，给解码器的 hw_device_ctx 用（调用方 unref）。
    return av_buffer_ref(m->avHw);
}

GpuVideoFrame D3D11SharedDevice::convertDecoderFrame(AVFrame *frame)
{
    GpuVideoFrame out;
    if (!m || !frame || frame->format != AV_PIX_FMT_D3D11 || !m->videoDevice || !m->videoCtx) {
        return out;
    }
    auto *src = reinterpret_cast<ID3D11Texture2D *>(frame->data[0]);
    const int index = static_cast<int>(reinterpret_cast<intptr_t>(frame->data[1]));
    if (!src || frame->width <= 0 || frame->height <= 0) {
        return out;
    }

    D3D11_TEXTURE2D_DESC srcDesc{};
    src->GetDesc(&srcDesc);

    std::lock_guard<std::recursive_mutex> guard(m->mutex);
    if (!m->ensureProcessor(frame->width, frame->height, srcDesc.Format)) {
        return out;
    }
    auto holder = m->acquireBgra(frame->width, frame->height);
    if (!holder) {
        return out;
    }

    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC ivd{};
    ivd.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    ivd.Texture2D.MipSlice = 0;
    ivd.Texture2D.ArraySlice = UINT(index);
    ComPtr<ID3D11VideoProcessorInputView> inView;
    // [D3D11 Video] CreateVideoProcessorInputView：把解码 array texture 的某一片绑成 VP 输入。
    // 形参：pResource=NV12 表面；pEnum；pDesc.ArraySlice=帧下标；ppView。
    if (FAILED(m->videoDevice->CreateVideoProcessorInputView(src, m->vpEnum.Get(), &ivd, &inView))) {
        return out;
    }

    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC ovd{};
    ovd.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    ComPtr<ID3D11VideoProcessorOutputView> outView;
    // [D3D11 Video] CreateVideoProcessorOutputView：BGRA 纹理作为 VP 输出。
    // 形参：pResource；pEnum；pDesc；ppView。
    if (FAILED(m->videoDevice->CreateVideoProcessorOutputView(holder->tex.Get(), m->vpEnum.Get(),
                                                              &ovd, &outView))) {
        return out;
    }

    D3D11_VIDEO_PROCESSOR_STREAM stream{};
    stream.Enable = TRUE;
    stream.pInputSurface = inView.Get();
    // [D3D11 Video] VideoProcessorBlt：GPU 上 NV12 → BGRA（零拷贝转换，不经 CPU）。
    // 形参：pVP；pView=输出；OutputFrame=0；StreamCount=1；pStreams。
    if (FAILED(m->videoCtx->VideoProcessorBlt(m->vp.Get(), outView.Get(), 0, 1, &stream))) {
        return out;
    }

    out.width = frame->width;
    out.height = frame->height;
    out.native = holder;
    return out;
}

ID3D11Texture2D *D3D11SharedDevice::textureFrom(const GpuVideoFrame &frame)
{
    if (!frame.native) {
        return nullptr;
    }
    return static_cast<D3D11TextureHolder *>(frame.native.get())->tex.Get();
}

#endif
