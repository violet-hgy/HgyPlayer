#include "D3D11VideoRenderer.h"

#ifdef Q_OS_WIN

#include <QResizeEvent>
#include <QShowEvent>
#include <QSizePolicy>
#include <QWindow>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <cstring>
#include <memory>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;

namespace {

struct Vertex {
    float x, y, u, v;
};

static const char *kShaderSrc = R"(
cbuffer CB : register(b0) {
    float4 rect; // left, bottom, right, top in NDC
};
struct VSIn  { float2 pos : POSITION; float2 uv : TEXCOORD0; };
struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };
Texture2D tex0 : register(t0);
SamplerState samp0 : register(s0);

VSOut VSMain(VSIn i) {
    VSOut o;
    float2 p = float2(lerp(rect.x, rect.z, i.pos.x),
                      lerp(rect.w, rect.y, i.pos.y));
    o.pos = float4(p, 0, 1);
    o.uv = i.uv;
    return o;
}
float4 PSMain(VSOut i) : SV_TARGET {
    return tex0.Sample(samp0, i.uv);
}
)";

HWND hwndFromWinId(WId wid)
{
    return reinterpret_cast<HWND>(wid);
}

bool clientSizePx(HWND hwnd, int *w, int *h)
{
    if (!hwnd || !w || !h) {
        return false;
    }
    RECT rc{};
    if (!GetClientRect(hwnd, &rc)) {
        return false;
    }
    *w = rc.right - rc.left;
    *h = rc.bottom - rc.top;
    return *w > 0 && *h > 0;
}

} // namespace

/**
 * 自包含 D3D 窗口：帧、设备、交换链都在本对象内。
 * 不回调外部渲染器，避免退出时窗口事件打到已释放对象。
 */
class D3D11VideoRenderer::RenderWindow : public QWindow
{
public:
    explicit RenderWindow(std::shared_ptr<D3D11SharedDevice> gpu)
        : m_shared(std::move(gpu))
    {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        setSurfaceType(QSurface::Direct3DSurface);
#else
        setSurfaceType(QSurface::RasterSurface);
#endif
    }

    ~RenderWindow() override
    {
        releaseGpu();
    }

    void setFrame(const QImage &frame)
    {
        if (frame.isNull()) {
            return;
        }
        m_gpuFrame = {};
        m_gpuTexPtr = nullptr;
        m_frame = frame.convertToFormat(QImage::Format_RGBA8888);
        m_hasFrame = !m_frame.isNull();
        render();
    }

    void setGpuFrame(const GpuVideoFrame &frame)
    {
        if (!frame.isValid()) {
            return;
        }
        m_frame = QImage();
        m_gpuFrame = frame;
        m_hasFrame = true;
        render();
    }

    void setPlaceholder()
    {
        m_frame = QImage();
        m_gpuFrame = {};
        m_gpuTexPtr = nullptr;
        m_hasFrame = false;
        render();
    }

    void releaseGpu()
    {
        if (m_ctx) {
            m_ctx->ClearState();
            m_ctx->Flush();
        }
        m_srv.Reset();
        m_tex.Reset();
        m_rtv.Reset();
        m_swap.Reset();
        m_vb.Reset();
        m_cb.Reset();
        m_vs.Reset();
        m_ps.Reset();
        m_layout.Reset();
        m_sampler.Reset();
        m_rs.Reset();
        if (!m_shared) {
            m_ctx.Reset();
            m_device.Reset();
        }
        m_hwnd = nullptr;
        m_texW = m_texH = 0;
        m_swapW = m_swapH = 0;
    }

protected:
    void exposeEvent(QExposeEvent *) override
    {
        if (isExposed()) {
            render();
        }
    }

    void resizeEvent(QResizeEvent *) override
    {
        if (m_shared) {
            m_shared->lock();
        }
        m_swap.Reset();
        m_rtv.Reset();
        m_swapW = m_swapH = 0;
        if (m_shared) {
            m_shared->unlock();
        }
        if (isExposed()) {
            render();
        }
    }

private:
    bool ensureDevice()
    {
        if (!handle()) {
            create();
        }
        const HWND hwnd = hwndFromWinId(winId());
        if (!hwnd) {
            return false;
        }

        int cw = 0;
        int ch = 0;
        if (!clientSizePx(hwnd, &cw, &ch)) {
            return false;
        }

        if (!m_device) {
            if (m_shared && m_shared->device() && m_shared->context()) {
                m_device = m_shared->device();
                m_ctx = m_shared->context();
            } else {
                UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
                D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
                const D3D_FEATURE_LEVEL levels[] = {
                    D3D_FEATURE_LEVEL_11_0,
                    D3D_FEATURE_LEVEL_10_1,
                    D3D_FEATURE_LEVEL_10_0,
                };
                // [D3D11] D3D11CreateDevice：软解路径自建设备（无 VIDEO_SUPPORT）。
                // 形参：pAdapter=nullptr；DriverType=HARDWARE，失败再试 WARP；
                //       Flags=BGRA；pFeatureLevels；ppDevice；ppImmediateContext。
                HRESULT hr = D3D11CreateDevice(nullptr,
                                               D3D_DRIVER_TYPE_HARDWARE,
                                               nullptr,
                                               flags,
                                               levels,
                                               ARRAYSIZE(levels),
                                               D3D11_SDK_VERSION,
                                               &m_device,
                                               &level,
                                               &m_ctx);
                if (FAILED(hr)) {
                    hr = D3D11CreateDevice(nullptr,
                                           D3D_DRIVER_TYPE_WARP,
                                           nullptr,
                                           flags,
                                           levels,
                                           ARRAYSIZE(levels),
                                           D3D11_SDK_VERSION,
                                           &m_device,
                                           &level,
                                           &m_ctx);
                    if (FAILED(hr)) {
                        return false;
                    }
                }
            }
        }

        if (!m_vs && m_device) {
            ComPtr<ID3DBlob> vsBlob;
            ComPtr<ID3DBlob> psBlob;
            // [D3DCompiler] D3DCompile：HLSL → 字节码。
            // 形参：pSrcData/SrcDataSize=源码；pSourceName=nullptr；pDefines/pInclude=nullptr；
            //       pEntrypoint=VSMain/PSMain；pTarget=vs_4_0/ps_4_0；Flags1/2=0；ppCode；ppErrorMsgs。
            HRESULT hr = D3DCompile(kShaderSrc, strlen(kShaderSrc), nullptr, nullptr, nullptr,
                            "VSMain", "vs_4_0", 0, 0, &vsBlob, nullptr);
            if (FAILED(hr)) {
                return false;
            }
            hr = D3DCompile(kShaderSrc, strlen(kShaderSrc), nullptr, nullptr, nullptr,
                            "PSMain", "ps_4_0", 0, 0, &psBlob, nullptr);
            if (FAILED(hr)) {
                return false;
            }
            // [D3D11] CreateVertexShader：顶点着色器。形参：pShaderBytecode；BytecodeLength；pClassLinkage=nullptr；ppVertexShader。
            if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(),
                                                    vsBlob->GetBufferSize(), nullptr, &m_vs))) {
                return false;
            }
            // [D3D11] CreatePixelShader：像素着色器。形参：pShaderBytecode；BytecodeLength；pClassLinkage；ppPixelShader。
            if (FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(),
                                                   psBlob->GetBufferSize(), nullptr, &m_ps))) {
                return false;
            }

            const D3D11_INPUT_ELEMENT_DESC layoutDesc[] = {
                {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
            };
            // [D3D11] CreateInputLayout：顶点布局（POSITION+TEXCOORD）。形参：pInputElementDescs；NumElements；pShaderBytecodeWithInputSignature；BytecodeLength；ppInputLayout。
            if (FAILED(m_device->CreateInputLayout(layoutDesc, 2, vsBlob->GetBufferPointer(),
                                                   vsBlob->GetBufferSize(), &m_layout))) {
                return false;
            }

            const Vertex quad[] = {
                {0, 0, 0, 0},
                {1, 0, 1, 0},
                {0, 1, 0, 1},
                {1, 1, 1, 1},
            };
            D3D11_BUFFER_DESC bd{};
            bd.ByteWidth = sizeof(quad);
            bd.Usage = D3D11_USAGE_IMMUTABLE;
            bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            D3D11_SUBRESOURCE_DATA init{};
            init.pSysMem = quad;
            // [D3D11] CreateBuffer：顶点缓冲（四边形）。形参：pDesc；pInitialData；ppBuffer。
            if (FAILED(m_device->CreateBuffer(&bd, &init, &m_vb))) {
                return false;
            }

            bd = {};
            bd.ByteWidth = 16;
            bd.Usage = D3D11_USAGE_DYNAMIC;
            bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if (FAILED(m_device->CreateBuffer(&bd, nullptr, &m_cb))) {
                return false;
            }

            D3D11_SAMPLER_DESC sd{};
            sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            sd.MaxLOD = D3D11_FLOAT32_MAX;
            // [D3D11] CreateSamplerState：线性采样 + clamp。形参：pSamplerDesc；ppSamplerState。
            if (FAILED(m_device->CreateSamplerState(&sd, &m_sampler))) {
                return false;
            }

            D3D11_RASTERIZER_DESC rd{};
            rd.FillMode = D3D11_FILL_SOLID;
            rd.CullMode = D3D11_CULL_NONE;
            rd.DepthClipEnable = TRUE;
            // [D3D11] CreateRasterizerState：实心填充、不裁剪背面。形参：pRasterizerDesc；ppRasterizerState。
            if (FAILED(m_device->CreateRasterizerState(&rd, &m_rs))) {
                return false;
            }
        }

        if (m_hwnd != hwnd) {
            m_swap.Reset();
            m_rtv.Reset();
            m_hwnd = hwnd;
            m_swapW = m_swapH = 0;
        }

        if (!m_swap || m_swapW != cw || m_swapH != ch) {
            if (!recreateSwapChain(cw, ch)) {
                return false;
            }
        }
        return m_device && m_swap && m_rtv;
    }

    bool recreateSwapChain(int width, int height)
    {
        m_rtv.Reset();
        if (m_ctx) {
            // [D3D11] OMSetRenderTargets：解绑 RT，以便 ResizeBuffers。形参：NumViews=0；ppRTViews=nullptr；pDSV=nullptr。
            m_ctx->OMSetRenderTargets(0, nullptr, nullptr);
            m_ctx->Flush();
        }

        if (m_swap) {
            // [DXGI] IDXGISwapChain::ResizeBuffers：窗口客户区变化时改后备缓冲尺寸。
            // 形参：BufferCount=0 保持；Width/Height；NewFormat=UNKNOWN 保持；SwapChainFlags=0。
            const HRESULT hr = m_swap->ResizeBuffers(0, UINT(width), UINT(height),
                                                     DXGI_FORMAT_UNKNOWN, 0);
            if (FAILED(hr)) {
                m_swap.Reset();
            }
        }

        if (!m_swap) {
            ComPtr<IDXGIDevice> dxgiDev;
            if (FAILED(m_device.As(&dxgiDev))) {
                return false;
            }
            ComPtr<IDXGIAdapter> adapter;
            // [DXGI] IDXGIDevice::GetAdapter：设备所属适配器。形参：ppAdapter。
            if (FAILED(dxgiDev->GetAdapter(&adapter))) {
                return false;
            }
            ComPtr<IDXGIFactory> factory;
            // [DXGI] IDXGIAdapter::GetParent：取 IDXGIFactory 以便 CreateSwapChain。
            if (FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))) {
                return false;
            }

            // 子窗口用 bitblt；Flip 模型在嵌入 HWND 上经常无画面
            DXGI_SWAP_CHAIN_DESC scd{};
            scd.BufferCount = 1;
            scd.BufferDesc.Width = UINT(width);
            scd.BufferDesc.Height = UINT(height);
            scd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            scd.OutputWindow = m_hwnd;
            scd.SampleDesc.Count = 1;
            scd.Windowed = TRUE;
            scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
            // [DXGI] IDXGIFactory::CreateSwapChain：为 HWND 创建交换链（DISCARD=bitblt，嵌入子窗口更稳）。
            // 形参：pDevice；pDesc（OutputWindow=HWND）；ppSwapChain。
            if (FAILED(factory->CreateSwapChain(m_device.Get(), &scd, &m_swap))) {
                return false;
            }
            // [DXGI] MakeWindowAssociation：禁止 Alt+Enter 全屏切换。形参：WindowHandle；Flags=NO_ALT_ENTER。
            factory->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER);
        }

        ComPtr<ID3D11Texture2D> back;
        // [DXGI] GetBuffer：取后备缓冲纹理。形参：Buffer=0；riid=ID3D11Texture2D。
        if (FAILED(m_swap->GetBuffer(0, IID_PPV_ARGS(&back)))) {
            return false;
        }
        // [D3D11] CreateRenderTargetView：把后备缓冲绑成渲染目标。形参：pResource；pDesc=nullptr；ppRTView。
        if (FAILED(m_device->CreateRenderTargetView(back.Get(), nullptr, &m_rtv))) {
            return false;
        }
        m_swapW = width;
        m_swapH = height;
        return true;
    }

    bool uploadFrame()
    {
        const int fw = m_frame.width();
        const int fh = m_frame.height();
        if (fw <= 0 || fh <= 0) {
            return false;
        }

        if (!m_tex || m_texW != fw || m_texH != fh) {
            m_srv.Reset();
            m_tex.Reset();
            D3D11_TEXTURE2D_DESC td{};
            td.Width = UINT(fw);
            td.Height = UINT(fh);
            td.MipLevels = 1;
            td.ArraySize = 1;
            td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            td.SampleDesc.Count = 1;
            td.Usage = D3D11_USAGE_DYNAMIC;
            td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            // [D3D11] CreateTexture2D：CPU 可写动态纹理，用来上传 QImage。
            // 形参：pDesc（DYNAMIC + SHADER_RESOURCE + CPU_WRITE）；pInitialData=nullptr；ppTexture2D。
            if (FAILED(m_device->CreateTexture2D(&td, nullptr, &m_tex))) {
                return false;
            }
            // [D3D11] CreateShaderResourceView：纹理 → 像素着色器可采样资源。形参：pResource；pDesc=nullptr 整张；ppSRView。
            if (FAILED(m_device->CreateShaderResourceView(m_tex.Get(), nullptr, &m_srv))) {
                return false;
            }
            m_texW = fw;
            m_texH = fh;
        }

        D3D11_MAPPED_SUBRESOURCE mapped{};
        // [D3D11] Map：把动态纹理映射到 CPU 可写内存。形参：pResource；Subresource=0；MapType=WRITE_DISCARD；MapFlags=0；pMappedResource。
        if (FAILED(m_ctx->Map(m_tex.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            return false;
        }

        const int srcStride = m_frame.bytesPerLine();
        const int dstStride = int(mapped.RowPitch);
        // 只拷贝两边都合法的字节，避免 QImage 对齐 / D3D RowPitch 不一致时写穿堆
        const int rowBytes = qMin(fw * 4, qMin(srcStride, dstStride));
        const uchar *src = m_frame.constBits();
        auto *dst = static_cast<uchar *>(mapped.pData);
        if (src && dst && rowBytes > 0) {
            for (int y = 0; y < fh; ++y) {
                memcpy(dst + y * dstStride, src + y * srcStride, size_t(rowBytes));
            }
        }
        // [D3D11] Unmap：结束映射，GPU 才能再采样该纹理。形参：pResource；Subresource=0。
        m_ctx->Unmap(m_tex.Get(), 0);
        return true;
    }

    bool bindSource(int *fw, int *fh)
    {
        if (m_gpuFrame.isValid()) {
            ID3D11Texture2D *tex = D3D11SharedDevice::textureFrom(m_gpuFrame);
            if (!tex) {
                return false;
            }
            if (!m_srv || m_gpuTexPtr != tex) {
                m_srv.Reset();
                D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
                sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
                sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                sd.Texture2D.MipLevels = 1;
                // [D3D11] CreateShaderResourceView：硬解 BGRA 纹理直接当采样源（不再走 CPU 上传）。
                if (FAILED(m_device->CreateShaderResourceView(tex, &sd, &m_srv))) {
                    return false;
                }
                m_gpuTexPtr = tex;
            }
            *fw = m_gpuFrame.width;
            *fh = m_gpuFrame.height;
            return true;
        }
        if (!uploadFrame()) {
            return false;
        }
        *fw = m_texW;
        *fh = m_texH;
        return true;
    }

    void render()
    {
        if (!isExposed()) {
            return;
        }
        if (m_shared) {
            m_shared->lock();
        }
        const bool ok = ensureDevice();
        if (!ok) {
            if (m_shared) {
                m_shared->unlock();
            }
            return;
        }

        const float clearColor[4] = {0.07f, 0.07f, 0.07f, 1.0f};
        // [D3D11] OMSetRenderTargets：绑定交换链 RT。形参：NumViews=1；ppRenderTargetViews；pDepthStencilView=nullptr。
        m_ctx->OMSetRenderTargets(1, m_rtv.GetAddressOf(), nullptr);
        // [D3D11] ClearRenderTargetView：清成暗灰。形参：pRTV；ColorRGBA。
        m_ctx->ClearRenderTargetView(m_rtv.Get(), clearColor);

        D3D11_VIEWPORT vp{};
        vp.Width = float(m_swapW);
        vp.Height = float(m_swapH);
        vp.MaxDepth = 1.0f;
        // [D3D11] RSSetViewports：视口等于交换链像素尺寸。形参：NumViewports=1；pViewports。
        m_ctx->RSSetViewports(1, &vp);
        m_ctx->RSSetState(m_rs.Get());

        int fw = 0;
        int fh = 0;
        if (m_hasFrame && bindSource(&fw, &fh)) {
            const float vw = float(m_swapW);
            const float vh = float(m_swapH);
            const float scale = qMin(vw / float(fw), vh / float(fh));
            const float dw = float(fw) * scale;
            const float dh = float(fh) * scale;
            const float x0 = (vw - dw) * 0.5f;
            const float y0 = (vh - dh) * 0.5f;
            float rect[4] = {
                x0 / vw * 2.0f - 1.0f,
                1.0f - (y0 + dh) / vh * 2.0f,
                (x0 + dw) / vw * 2.0f - 1.0f,
                1.0f - y0 / vh * 2.0f,
            };

            D3D11_MAPPED_SUBRESOURCE cbMap{};
            if (SUCCEEDED(m_ctx->Map(m_cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &cbMap))) {
                memcpy(cbMap.pData, rect, sizeof(rect));
                m_ctx->Unmap(m_cb.Get(), 0);
            }

            UINT stride = sizeof(Vertex);
            UINT offset = 0;
            // [D3D11] 绘制管线：IA 顶点/拓扑 → VS/PS 着色器 → 常量缓冲/纹理/采样器 → Draw。
            m_ctx->IASetVertexBuffers(0, 1, m_vb.GetAddressOf(), &stride, &offset);
            m_ctx->IASetInputLayout(m_layout.Get());
            m_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
            m_ctx->VSSetShader(m_vs.Get(), nullptr, 0);
            m_ctx->PSSetShader(m_ps.Get(), nullptr, 0);
            m_ctx->VSSetConstantBuffers(0, 1, m_cb.GetAddressOf());
            m_ctx->PSSetShaderResources(0, 1, m_srv.GetAddressOf());
            m_ctx->PSSetSamplers(0, 1, m_sampler.GetAddressOf());
            // [D3D11] Draw：画三角形带 4 顶点（两个三角形铺满画面矩形）。形参：VertexCount=4；StartVertexLocation=0。
            m_ctx->Draw(4, 0);

            ID3D11ShaderResourceView *nullSrv = nullptr;
            m_ctx->PSSetShaderResources(0, 1, &nullSrv);
        }

        // [DXGI] IDXGISwapChain::Present：把后备缓冲送到窗口。形参：SyncInterval=0 不等 vsync；Flags=0。
        m_swap->Present(0, 0);
        if (m_shared) {
            m_shared->unlock();
        }
    }

    QImage m_frame;
    GpuVideoFrame m_gpuFrame;
    ID3D11Texture2D *m_gpuTexPtr = nullptr;
    bool m_hasFrame = false;
    std::shared_ptr<D3D11SharedDevice> m_shared;

    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_ctx;
    ComPtr<IDXGISwapChain> m_swap;
    ComPtr<ID3D11RenderTargetView> m_rtv;
    ComPtr<ID3D11VertexShader> m_vs;
    ComPtr<ID3D11PixelShader> m_ps;
    ComPtr<ID3D11InputLayout> m_layout;
    ComPtr<ID3D11Buffer> m_vb;
    ComPtr<ID3D11Buffer> m_cb;
    ComPtr<ID3D11SamplerState> m_sampler;
    ComPtr<ID3D11RasterizerState> m_rs;
    ComPtr<ID3D11Texture2D> m_tex;
    ComPtr<ID3D11ShaderResourceView> m_srv;
    HWND m_hwnd = nullptr;
    int m_texW = 0;
    int m_texH = 0;
    int m_swapW = 0;
    int m_swapH = 0;
};

D3D11VideoRenderer::D3D11VideoRenderer(QWidget *parent)
{
    m_gpu = D3D11SharedDevice::create();
    m_window = new RenderWindow(m_gpu);
    m_container = QWidget::createWindowContainer(m_window, parent);
    m_container->setMinimumSize(640, 360);
    m_container->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_container->setAttribute(Qt::WA_NativeWindow);
    m_container->setAttribute(Qt::WA_NoSystemBackground);
}

void D3D11VideoRenderer::setBackendKind(Backend kind)
{
    m_backend = kind;
}

QString D3D11VideoRenderer::name() const
{
    return m_backend == Backend::D3D11Hw ? QStringLiteral("D3D11 硬解")
                                         : QStringLiteral("D3D11");
}

D3D11VideoRenderer::~D3D11VideoRenderer()
{
    if (m_window) {
        m_window->releaseGpu();
    }
    if (m_container) {
        m_container->hide();
        m_container->setParent(nullptr);
        delete m_container;
    }
    m_window = nullptr;
}

QWidget *D3D11VideoRenderer::widget()
{
    return m_container;
}

void D3D11VideoRenderer::present(const QImage &frame)
{
    if (m_window) {
        m_window->setFrame(frame);
    }
}

void D3D11VideoRenderer::presentGpu(const GpuVideoFrame &frame)
{
    if (m_window) {
        m_window->setGpuFrame(frame);
    }
}

void D3D11VideoRenderer::clear(const QString &)
{
    if (m_window) {
        m_window->setPlaceholder();
    }
}

#endif // Q_OS_WIN
