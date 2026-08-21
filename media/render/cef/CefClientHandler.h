#ifndef CEFCLIENTHANDLER_H
#define CEFCLIENTHANDLER_H

#include "include/cef_client.h"

#include <functional>

class CefClientHandler : public CefClient,
                         public CefLifeSpanHandler,
                         public CefLoadHandler,
                         public CefDisplayHandler
{
public:
    using BrowserCreatedFn = std::function<void(CefRefPtr<CefBrowser>)>;
    using PageLoadedFn = std::function<void()>;

    CefClientHandler();

    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
    CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
    CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }

    void setBrowserCreatedHandler(BrowserCreatedFn handler);
    void setPageLoadedHandler(PageLoadedFn handler);

    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;
    void OnLoadEnd(CefRefPtr<CefBrowser> browser,
                   CefRefPtr<CefFrame> frame,
                   int httpStatusCode) override;
    bool OnConsoleMessage(CefRefPtr<CefBrowser> browser,
                          cef_log_severity_t level,
                          const CefString &message,
                          const CefString &source,
                          int line) override;

    CefRefPtr<CefBrowser> browser() const { return m_browser; }

private:
    CefRefPtr<CefBrowser> m_browser;
    BrowserCreatedFn m_browserCreatedHandler;
    PageLoadedFn m_pageLoadedHandler;

    IMPLEMENT_REFCOUNTING(CefClientHandler);
};

#endif // CEFCLIENTHANDLER_H
