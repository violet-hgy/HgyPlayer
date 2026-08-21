#include "CefBrowserHostImpl.h"

#include "CefClientHandler.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>
#include <QUrl>
#include <QWidget>

#include <string>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
std::string toUtf8(const QString &text)
{
    return text.toUtf8().constData();
}

CefRect makeCefRect(int x, int y, int width, int height)
{
    CefRect rect;
    rect.x = x;
    rect.y = y;
    rect.width = width;
    rect.height = height;
    return rect;
}
} // namespace

CefBrowserHostImpl::CefBrowserHostImpl()
    : m_client(new CefClientHandler())
{
    m_client->setBrowserCreatedHandler([this](CefRefPtr<CefBrowser> browser) {
        m_browser = browser;
        flushPendingNavigation();
    });
    m_client->setPageLoadedHandler([this]() {
        m_pageLoaded = true;
        if (m_pageLoadedCallback) {
            m_pageLoadedCallback();
        }
    });
}

CefBrowserHostImpl::~CefBrowserHostImpl()
{
    destroy();
}

bool CefBrowserHostImpl::create(QWidget *parentWidget)
{
    if (!parentWidget) {
        return false;
    }
    m_parent = parentWidget;
    parentWidget->setAttribute(Qt::WA_NativeWindow, true);
    parentWidget->setAttribute(Qt::WA_DontCreateNativeAncestors, true);
    parentWidget->winId();

    const int w = qMax(1, parentWidget->width());
    const int h = qMax(1, parentWidget->height());

    CefWindowInfo windowInfo;
#ifdef Q_OS_WIN
    windowInfo.SetAsChild(reinterpret_cast<CefWindowHandle>(parentWidget->winId()),
                          makeCefRect(0, 0, w, h));
#else
    Q_UNUSED(windowInfo);
    return false;
#endif

    CefBrowserSettings browserSettings;
    browserSettings.background_color = CefColorSetARGB(255, 255, 255, 255);

    std::string url = "about:blank";
    if (!m_pendingUrl.isEmpty()) {
        url = toUtf8(m_pendingUrl);
    }

    CefBrowserHost::CreateBrowser(windowInfo,
                                  m_client,
                                  url,
                                  browserSettings,
                                  nullptr,
                                  nullptr);
    m_pendingUrl.clear();
    m_created = true;
    return true;
}

void CefBrowserHostImpl::destroy()
{
    m_pageLoaded = false;
    m_pendingUrl.clear();
    m_pendingHtml.clear();
    CefRefPtr<CefBrowserHost> hostToClose;
    if (m_browser) {
        if (CefRefPtr<CefBrowserHost> host = m_browser->GetHost()) {
            hostToClose = host;
        }
    } else if (m_client && m_client->browser()) {
        if (CefRefPtr<CefBrowserHost> host = m_client->browser()->GetHost()) {
            hostToClose = host;
        }
    }
    if (hostToClose) {
        hostToClose->CloseBrowser(true);
        // Give CEF a short chance to process close tasks before shutdown.
        for (int i = 0; i < 20; ++i) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            QThread::msleep(5);
        }
    }
    m_browser = nullptr;
    m_client = nullptr;
    m_created = false;
    m_parent.clear();
}

void CefBrowserHostImpl::resize(int width, int height)
{
    ensureBrowserSize(width, height);
}

void CefBrowserHostImpl::navigateToUrl(const QString &url)
{
    m_pendingHtml.clear();
    m_pendingUrl = url;
    m_pageLoaded = false;
    flushPendingNavigation();
}

void CefBrowserHostImpl::loadHtmlPage(const QString &html)
{
    const QString encoded =
        QString::fromUtf8(QUrl::toPercentEncoding(html, QByteArray(), QByteArray("!#$&()*+,-./:;=?@[]^_`{|}~")));
    navigateToUrl(QStringLiteral("data:text/html;charset=utf-8,") + encoded);
}

void CefBrowserHostImpl::flushPendingNavigation()
{
    CefRefPtr<CefBrowser> browser = m_browser;
    if (!browser && m_client) {
        browser = m_client->browser();
        m_browser = browser;
    }
    if (!browser) {
        return;
    }

    CefRefPtr<CefFrame> frame = browser->GetMainFrame();
    if (!frame) {
        return;
    }

    if (!m_pendingUrl.isEmpty()) {
        frame->LoadURL(toUtf8(m_pendingUrl));
        m_pendingUrl.clear();
    }
}

void CefBrowserHostImpl::executeJavaScript(const QString &script)
{
    CefRefPtr<CefBrowser> browser = m_browser;
    if (!browser && m_client) {
        browser = m_client->browser();
    }
    if (!browser) {
        return;
    }
    CefRefPtr<CefFrame> frame = browser->GetMainFrame();
    if (!frame) {
        return;
    }
    frame->ExecuteJavaScript(toUtf8(script), frame->GetURL(), 0);
}

bool CefBrowserHostImpl::isCreated() const
{
    return m_created;
}

bool CefBrowserHostImpl::isPageLoaded() const
{
    return m_pageLoaded;
}

void CefBrowserHostImpl::setPageLoadedCallback(std::function<void()> callback)
{
    m_pageLoadedCallback = std::move(callback);
}

void CefBrowserHostImpl::ensureBrowserSize(int width, int height)
{
    CefRefPtr<CefBrowser> browser = m_browser;
    if (!browser && m_client) {
        browser = m_client->browser();
        m_browser = browser;
    }
    if (!browser) {
        return;
    }

    CefRefPtr<CefBrowserHost> host = browser->GetHost();
    if (!host) {
        return;
    }

#ifdef Q_OS_WIN
    host->NotifyMoveOrResizeStarted();
    if (CefWindowHandle hwnd = host->GetWindowHandle()) {
        MoveWindow(hwnd, 0, 0, qMax(1, width), qMax(1, height), TRUE);
    }
#else
    Q_UNUSED(width);
    Q_UNUSED(height);
#endif
    // For windowed (SetAsChild) mode, resizing the native child window is sufficient.
    // Calling WasResized() is only required for OSR and can trigger "Not implemented"
    // logs in some CEF builds.
}
