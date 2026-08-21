#ifndef ICEFBROWSERHOST_H
#define ICEFBROWSERHOST_H

/**
 * @file ICefBrowserHost.h
 * @brief CEF 浏览器宿主 Bridge 接口（模块内部，仍不含 CEF 头文件）
 */

#include <QtGlobal>

#include <functional>

class QString;
class QWidget;

class ICefBrowserHost
{
public:
    virtual ~ICefBrowserHost() = default;

    virtual bool create(QWidget *parentWidget) = 0;
    virtual void destroy() = 0;

    virtual void resize(int width, int height) = 0;

    /** 导航到 URL（如 file:///…/player.html）；CreateBrowser 为异步，内部排队 */
    virtual void navigateToUrl(const QString &url) = 0;

    virtual void loadHtmlPage(const QString &html) = 0;
    virtual void executeJavaScript(const QString &script) = 0;

    virtual bool isCreated() const = 0;
    virtual bool isPageLoaded() const = 0;

    /** 主框架 OnLoadEnd 后回调（UI 线程安全：CEF 多线程消息循环） */
    virtual void setPageLoadedCallback(std::function<void()> callback) = 0;
};

#endif // ICEFBROWSERHOST_H
