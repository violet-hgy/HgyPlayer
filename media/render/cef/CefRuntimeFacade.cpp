#include "CefRuntimeFacade.h"

#ifdef HGY_ENABLE_CEF
#include "CefRuntimeImpl.h"
#endif

#include "CefBrowserHostStub.h"

#include <QCoreApplication>
#include <QString>

#include <memory>

#ifdef HGY_ENABLE_CEF
namespace {
std::unique_ptr<CefRuntimeImpl> g_runtime;
QString g_lastInitError;
}
#endif

int CefRuntimeFacade::executeSubProcessIfNeeded(int argc, char **argv)
{
#ifdef HGY_ENABLE_CEF
    return CefRuntimeImpl::executeSubProcessIfNeeded(argc, argv);
#else
    Q_UNUSED(argc);
    Q_UNUSED(argv);
    return -1;
#endif
}

bool CefRuntimeFacade::initialize(int argc, char **argv)
{
#ifdef HGY_ENABLE_CEF
    if (!g_runtime) {
        g_runtime = std::make_unique<CefRuntimeImpl>();
    }
    const bool ok = g_runtime->initialize(argc, argv);
    if (!ok) {
        g_lastInitError = CefRuntimeImpl::lastInitError();
    } else {
        g_lastInitError.clear();
    }
    return ok;
#else
    Q_UNUSED(argc);
    Q_UNUSED(argv);
    return false;
#endif
}

void CefRuntimeFacade::shutdown()
{
#ifdef HGY_ENABLE_CEF
    if (g_runtime) {
        g_runtime->shutdown();
        g_runtime.reset();
    }
#endif
}

bool CefRuntimeFacade::isAvailable()
{
#ifdef HGY_ENABLE_CEF
    return true;
#else
    return false;
#endif
}

bool CefRuntimeFacade::isInitialized()
{
#ifdef HGY_ENABLE_CEF
    return g_runtime && g_runtime->isInitialized();
#else
    return false;
#endif
}

QString CefRuntimeFacade::lastInitError()
{
#ifdef HGY_ENABLE_CEF
    return g_lastInitError;
#else
    return QString();
#endif
}

std::unique_ptr<ICefBrowserHost> CefRuntimeFacade::createBrowserHost()
{
#ifdef HGY_ENABLE_CEF
    if (g_runtime && g_runtime->isInitialized()) {
        return g_runtime->createBrowserHost();
    }
    return nullptr;
#else
    return std::make_unique<CefBrowserHostStub>();
#endif
}
