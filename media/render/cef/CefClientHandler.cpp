#include "CefClientHandler.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDebug>
#include <QFile>
#include <QString>

namespace {
void appendCefPlayerLog(const QString &line)
{
    QString dir = QDir::currentPath();
    if (QCoreApplication::instance()) {
        dir = QCoreApplication::applicationDirPath();
    }
    const QString logPath = QDir(dir).filePath(QStringLiteral("cef_player.log"));
    QFile f(logPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return;
    }
    const QString ts = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    const QString out = QStringLiteral("%1 %2\n").arg(ts, line);
    f.write(out.toUtf8());
}
} // namespace

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
    appendCefPlayerLog(QStringLiteral("[HGY] OnLoadEnd status=%1 url=%2")
                           .arg(httpStatusCode)
                           .arg(QString::fromStdWString(frame->GetURL().ToWString())));
    if (m_pageLoadedHandler) {
        m_pageLoadedHandler();
    }
}

bool CefClientHandler::OnConsoleMessage(CefRefPtr<CefBrowser> browser,
                                        cef_log_severity_t level,
                                        const CefString &message,
                                        const CefString &source,
                                        int line)
{
    Q_UNUSED(browser);
    Q_UNUSED(level);
    const QString msg = QString::fromStdWString(message.ToWString());
    if (!msg.startsWith(QStringLiteral("[HGY]"))) {
        return false;
    }
    const QString src = QString::fromStdWString(source.ToWString());
    const QString lineText = QStringLiteral("CEF console %1:%2 %3").arg(src).arg(line).arg(msg);
    appendCefPlayerLog(lineText);
    qWarning().noquote() << lineText;
    return false;
}
