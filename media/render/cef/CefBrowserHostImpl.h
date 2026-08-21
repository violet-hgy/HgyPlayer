#ifndef CEFBROWSERHOSTIMPL_H
#define CEFBROWSERHOSTIMPL_H

#include "ICefBrowserHost.h"

#include "include/cef_browser.h"

#include <QPointer>

class QWidget;
class CefClientHandler;

class CefBrowserHostImpl : public ICefBrowserHost
{
public:
    CefBrowserHostImpl();
    ~CefBrowserHostImpl() override;

    bool create(QWidget *parentWidget) override;
    void destroy() override;
    void resize(int width, int height) override;
    void navigateToUrl(const QString &url) override;
    void loadHtmlPage(const QString &html) override;
    void executeJavaScript(const QString &script) override;
    bool isCreated() const override;
    bool isPageLoaded() const override;
    void setPageLoadedCallback(std::function<void()> callback) override;

private:
    void flushPendingNavigation();
    void ensureBrowserSize(int width, int height);

    QPointer<QWidget> m_parent;
    CefRefPtr<CefClientHandler> m_client;
    CefRefPtr<CefBrowser> m_browser;
    bool m_created = false;
    bool m_pageLoaded = false;
    QString m_pendingUrl;
    QString m_pendingHtml;
    std::function<void()> m_pageLoadedCallback;
};

#endif // CEFBROWSERHOSTIMPL_H
