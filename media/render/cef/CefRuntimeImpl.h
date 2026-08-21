#ifndef CEFRUNTIMEIMPL_H
#define CEFRUNTIMEIMPL_H

/**
 * @file CefRuntimeImpl.h
 * @brief CEF 运行时实现（仅 HGY_ENABLE_CEF 编译单元引用 libcef）
 */

#include "ICefBrowserHost.h"

#include <memory>

class CefRuntimeImpl
{
public:
    static int executeSubProcessIfNeeded(int argc, char **argv);

    bool initialize(int argc, char **argv);
    void shutdown();

    bool isInitialized() const { return m_initialized; }

    static QString lastInitError();

    std::unique_ptr<ICefBrowserHost> createBrowserHost();

private:
    bool m_initialized = false;
};

#endif // CEFRUNTIMEIMPL_H
