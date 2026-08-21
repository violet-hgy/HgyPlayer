#ifndef CEFRUNTIMEFACADE_H
#define CEFRUNTIMEFACADE_H

/**
 * @file CefRuntimeFacade.h
 * @brief CEF 运行时 Facade（对外唯一入口，零 CEF 头文件依赖）
 *
 * 设计模式：
 *   - Facade：统一 Initialize / Shutdown / SubProcess / createBrowserHost
 *   - Factory Method：createBrowserHost() 产出 ICefBrowserHost
 *   - 实现细节封装在 CefRuntimeImpl（PImpl），编译单元隔离
 *
 * 用法（main.cpp）：
 * @code
 *   const int sub = CefRuntimeFacade::executeSubProcessIfNeeded(argc, argv);
 *   if (sub >= 0) return sub;
 *   CefRuntimeFacade::initialize();
 *   ...
 *   CefRuntimeFacade::shutdown();
 * @endcode
 */

#include <memory>

class ICefBrowserHost;
class QString;

class CefRuntimeFacade
{
public:
    CefRuntimeFacade() = delete;

    /**
     * @brief 子进程入口；返回值 >= 0 表示当前进程为 CEF 子进程且应直接 exit
     * @return -1 表示主进程，应继续 Qt 启动流程
     */
    static int executeSubProcessIfNeeded(int argc, char **argv);

    /** @brief 主进程初始化 CEF（可重复调用，内部幂等） */
    static bool initialize(int argc = 0, char **argv = nullptr);

    /** @brief 释放 CEF（应在 QApplication 销毁之后调用） */
    static void shutdown();

    static bool isAvailable();
    static bool isInitialized();

    /** @brief 初始化失败时的可读原因（同时写入 exe 目录 cef_init.log） */
    static QString lastInitError();

    /** @brief 创建浏览器宿主；CEF 未启用或未初始化时返回 nullptr */
    static std::unique_ptr<ICefBrowserHost> createBrowserHost();
};

#endif // CEFRUNTIMEFACADE_H
