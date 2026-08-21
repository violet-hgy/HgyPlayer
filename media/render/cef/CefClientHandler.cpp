#include "CefClientHandler.h"

CefClientHandler::CefClientHandler() = default;

void CefClientHandler::setBrowserCreatedHandler(BrowserCreatedFn handler)
{
    m_browserCreatedHandler = std::move(handler);
}

void CefClientHandler::setPageLoadedHandler(PageLoadedFn handler)
{
    m_pageLoadedHandler = std::move(handler);
}

void CefClientHandler::OnAfterCreated(CefRefPtr<CefBrowser> browser)
{
    m_browser = browser;
    if (m_browserCreatedHandler) {
        m_browserCreatedHandler(browser);
    }
}

void CefClientHandler::OnBeforeClose(CefRefPtr<CefBrowser> browser)
{
    if (m_browser && m_browser->IsSame(browser)) {
        m_browser = nullptr;
    }
}

void CefClientHandler::OnLoadEnd(CefRefPtr<CefBrowser> browser,
                                 CefRefPtr<CefFrame> frame,
                                 int httpStatusCode)
{
    (void)httpStatusCode;
    if (!frame || !frame->IsMain()) {
        return;
    }
    (void)browser;
    if (m_pageLoadedHandler) {
        m_pageLoadedHandler();
    }
}
