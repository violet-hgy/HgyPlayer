#include "CefBrowserHostStub.h"

#include <QWidget>

bool CefBrowserHostStub::create(QWidget *parentWidget)
{
    Q_UNUSED(parentWidget);
    m_created = false;
    return false;
}

void CefBrowserHostStub::destroy()
{
    m_created = false;
}

void CefBrowserHostStub::resize(int width, int height)
{
    Q_UNUSED(width);
    Q_UNUSED(height);
}

void CefBrowserHostStub::navigateToUrl(const QString &url)
{
    Q_UNUSED(url);
}

void CefBrowserHostStub::loadHtmlPage(const QString &html)
{
    Q_UNUSED(html);
}

void CefBrowserHostStub::executeJavaScript(const QString &script)
{
    Q_UNUSED(script);
}

void CefBrowserHostStub::setPageLoadedCallback(std::function<void()> callback)
{
    Q_UNUSED(callback);
}
