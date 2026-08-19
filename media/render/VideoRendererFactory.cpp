#include "VideoRendererFactory.h"

#include "OpenGLVideoRenderer.h"
#include "QImageVideoRenderer.h"

#ifdef Q_OS_WIN
#include "D3D11VideoRenderer.h"
#include "NativeOpenGLVideoRenderer.h"
#endif

std::unique_ptr<IVideoRenderer> VideoRendererFactory::create(IVideoRenderer::Backend backend,
                                                             QWidget *parent)
{
    switch (backend) {
    case IVideoRenderer::Backend::QImage:
        return std::make_unique<QImageVideoRenderer>(parent);
    case IVideoRenderer::Backend::OpenGL:
        return std::make_unique<OpenGLVideoRenderer>(parent);
    case IVideoRenderer::Backend::OpenGLNative:
#ifdef Q_OS_WIN
        return std::make_unique<NativeOpenGLVideoRenderer>(parent);
#else
        Q_UNUSED(parent);
        return nullptr;
#endif
    case IVideoRenderer::Backend::D3D11:
    case IVideoRenderer::Backend::D3D11Hw:
#ifdef Q_OS_WIN
    {
        auto r = std::make_unique<D3D11VideoRenderer>(parent);
        r->setBackendKind(backend);
        return r;
    }
#else
        Q_UNUSED(parent);
        return nullptr;
#endif
    }
    return nullptr;
}

QString VideoRendererFactory::backendName(IVideoRenderer::Backend backend)
{
    switch (backend) {
    case IVideoRenderer::Backend::QImage:
        return QStringLiteral("QImage");
    case IVideoRenderer::Backend::OpenGL:
        return QStringLiteral("OpenGL (Qt)");
    case IVideoRenderer::Backend::OpenGLNative:
        return QStringLiteral("OpenGL");
    case IVideoRenderer::Backend::D3D11:
        return QStringLiteral("D3D11");
    case IVideoRenderer::Backend::D3D11Hw:
        return QStringLiteral("D3D11 硬解");
    }
    return QStringLiteral("Unknown");
}
