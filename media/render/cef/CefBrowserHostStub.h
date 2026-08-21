#ifndef CEFBROWSERHOSTSTUB_H
#define CEFBROWSERHOSTSTUB_H

#include "ICefBrowserHost.h"

class CefBrowserHostStub : public ICefBrowserHost
{
public:
    bool create(QWidget *parentWidget) override;
    void destroy() override;
    void resize(int width, int height) override;
    void navigateToUrl(const QString &url) override;
    void loadHtmlPage(const QString &html) override;
    void executeJavaScript(const QString &script) override;
    bool isCreated() const override { return m_created; }
    bool isPageLoaded() const override { return false; }
    void setPageLoadedCallback(std::function<void()> callback) override;

private:
    bool m_created = false;
};

#endif // CEFBROWSERHOSTSTUB_H
