#include "CefVideoRenderer.h"

#include "CefRuntimeFacade.h"
#include "ICefBrowserHost.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QLabel>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <utility>

struct CefVideoRenderer::Private
{
    class HostWidget;
    HostWidget *host = nullptr;
    QLabel *fallbackLabel = nullptr;
    std::unique_ptr<ICefBrowserHost> browserHost;
    QTimer positionTimer;
    QElapsedTimer playClock;
    QString mediaPath;
    QString pendingMediaUrl;
    qint64 anchorPositionMs = 0;
    qint64 estimatedPositionMs = 0;
    bool playing = false;
    bool pageReady = false;

    void syncBrowserSize();
    void ensureBrowser();
    void runJs(const QString &script);
    void onPageLoaded();
    void loadPendingMedia();
};

namespace {
QString cefTestPageUrl()
{
    return QStringLiteral("https://www.baidu.com");
}
QString defaultPlayerHtml()
{
    return QStringLiteral(
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\"/>"
        "<style>html,body{margin:0;padding:0;width:100%;height:100%;background:#000;overflow:hidden}"
        "video{width:100%;height:100%;object-fit:contain;background:#000}"
        "#hint{position:absolute;inset:0;display:flex;align-items:center;justify-content:center;"
        "color:#888;font:14px sans-serif;pointer-events:none}</style></head><body>"
        "<div id=\"hint\">HgyPlayer · CEF</div>"
        "<video id=\"player\" playsinline preload=\"auto\"></video>"
        "<script>(function(){var v=document.getElementById('player'),h=document.getElementById('hint');"
        "function show(t){h.textContent=t;h.style.display=t?'flex':'none';}"
        "v.addEventListener('playing',function(){show('');});"
        "v.addEventListener('loadeddata',function(){show('');});"
        "v.addEventListener('error',function(){show('无法解码此媒体');});"
        "v.addEventListener('emptied',function(){show('HgyPlayer · CEF');});"
        "window.hgyPlayer={load:function(u){v.src=u;v.load();},"
        "play:function(){return v.play();},pause:function(){v.pause();},"
        "stop:function(){v.pause();v.currentTime=0;},"
        "seek:function(s){if(!isNaN(s))v.currentTime=s;},"
        "currentTimeMs:function(){return Math.round((v.currentTime||0)*1000);}};"
        "})();</script></body></html>");
}

QString ensurePlayerPageOnDisk()
{
    QDir dir(QCoreApplication::applicationDirPath());
    dir.mkpath(QStringLiteral("cef"));
    const QString path = dir.filePath(QStringLiteral("cef/player.html"));
    QFile file(path);
    if (!file.exists()) {
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return {};
        }
        file.write(defaultPlayerHtml().toUtf8());
        file.close();
    }
    return path;
}

QString jsStringLiteral(const QString &text)
{
    return QString::fromUtf8(QJsonDocument::fromVariant(text).toJson());
}
} // namespace

class CefVideoRenderer::Private::HostWidget : public QWidget
{
public:
    explicit HostWidget(Private *owner, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_owner(owner)
    {
        setMinimumSize(320, 180);
        setAttribute(Qt::WA_NativeWindow, true);
        setStyleSheet(QStringLiteral("background-color: #101010;"));
    }

protected:
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    Private *m_owner = nullptr;
};

void CefVideoRenderer::Private::HostWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_owner) {
        m_owner->syncBrowserSize();
    }
}

void CefVideoRenderer::Private::HostWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_owner) {
        m_owner->ensureBrowser();
        m_owner->syncBrowserSize();
    }
}

void CefVideoRenderer::Private::syncBrowserSize()
{
    if (!host || !browserHost || !browserHost->isCreated()) {
        return;
    }
    browserHost->resize(host->width(), host->height());
}

void CefVideoRenderer::Private::ensureBrowser()
{
    if (browserHost || !host || !CefRuntimeFacade::isInitialized()) {
        return;
    }

    browserHost = CefRuntimeFacade::createBrowserHost();
    if (!browserHost) {
        return;
    }

    CefVideoRenderer::Private *self = this;
    browserHost->setPageLoadedCallback([self]() {
        if (!self || !self->host) {
            return;
        }
        QTimer::singleShot(0, self->host, [self]() { self->onPageLoaded(); });
    });

    browserHost->navigateToUrl(cefTestPageUrl());

    if (!browserHost->create(host)) {
        browserHost.reset();
        if (fallbackLabel) {
            fallbackLabel->show();
            fallbackLabel->setText(QStringLiteral("无法创建 CEF 浏览器窗口"));
        }
        return;
    }

    if (fallbackLabel) {
        fallbackLabel->hide();
    }
    syncBrowserSize();
}

void CefVideoRenderer::Private::onPageLoaded()
{
    pageReady = true;
    if (fallbackLabel) {
        fallbackLabel->hide();
    }
    syncBrowserSize();
}

void CefVideoRenderer::Private::loadPendingMedia()
{
    if (!pageReady || pendingMediaUrl.isEmpty() || !browserHost) {
        return;
    }
    const QString script = QStringLiteral("window.hgyPlayer && window.hgyPlayer.load(%1);")
                               .arg(jsStringLiteral(pendingMediaUrl));
    browserHost->executeJavaScript(script);
}

void CefVideoRenderer::Private::runJs(const QString &script)
{
    if (browserHost && browserHost->isCreated() && pageReady) {
        browserHost->executeJavaScript(script);
    }
}

CefVideoRenderer::CefVideoRenderer(QWidget *parent)
    : d(std::make_unique<Private>())
{
    d->host = new Private::HostWidget(d.get(), parent);

    auto *layout = new QVBoxLayout(d->host);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    d->fallbackLabel = new QLabel(d->host);
    d->fallbackLabel->setAlignment(Qt::AlignCenter);
    d->fallbackLabel->setWordWrap(true);
    d->fallbackLabel->setStyleSheet(QStringLiteral("color: #888; padding: 16px;"));
    layout->addWidget(d->fallbackLabel);

    if (!CefRuntimeFacade::isAvailable()) {
        d->fallbackLabel->setText(
            QStringLiteral("CEF 未编译进工程\n\n"
                           "请使用 -DHGY_ENABLE_CEF=ON -DCEF_ROOT=<cef_binary 路径> 重新配置 CMake"));
    } else if (!CefRuntimeFacade::isInitialized()) {
        const QString err = CefRuntimeFacade::lastInitError();
        d->fallbackLabel->setText(
            err.isEmpty()
                ? QStringLiteral("CEF 初始化失败\n\n请查看 exe 目录 cef_init.log")
                : err);
    } else {
        d->fallbackLabel->setText(
            QStringLiteral("正在打开测试网页…\n%1").arg(cefTestPageUrl()));
    }

    d->positionTimer.setInterval(250);
    QObject::connect(&d->positionTimer, &QTimer::timeout, [this]() { pollBrowserPosition(); });

    if (CefRuntimeFacade::isInitialized()) {
        QTimer::singleShot(0, d->host, [this]() { d->ensureBrowser(); });
    }
}

CefVideoRenderer::~CefVideoRenderer()
{
    d->positionTimer.stop();
    if (d->browserHost) {
        d->browserHost->destroy();
        d->browserHost.reset();
    }
}

QWidget *CefVideoRenderer::widget()
{
    return d->host;
}

void CefVideoRenderer::present(const QImage &frame)
{
    Q_UNUSED(frame);
}

void CefVideoRenderer::presentGpu(const GpuVideoFrame &frame)
{
    Q_UNUSED(frame);
}

void CefVideoRenderer::clear(const QString &placeholder)
{
    stopMedia();
    d->mediaPath.clear();
    d->pendingMediaUrl.clear();
    d->runJs(QStringLiteral("window.hgyPlayer && window.hgyPlayer.stop();"));
    if (d->fallbackLabel && (!d->pageReady || !browserAvailable())) {
        d->fallbackLabel->show();
        d->fallbackLabel->setText(placeholder.isEmpty() ? QStringLiteral("HgyPlayer · CEF")
                                                        : placeholder);
    }
}

bool CefVideoRenderer::browserAvailable() const
{
    return CefRuntimeFacade::isAvailable() && CefRuntimeFacade::isInitialized();
}

bool CefVideoRenderer::openMedia(const QString &filePath)
{
    Q_UNUSED(filePath);

    if (!browserAvailable()) {
        if (d->fallbackLabel) {
            d->fallbackLabel->show();
            d->fallbackLabel->setText(
                QStringLiteral("CEF 不可用\n\n"
                               "需启用 HGY_ENABLE_CEF 并将 libcef.dll / Resources 部署到 exe 目录"));
        }
        return false;
    }

    d->ensureBrowser();
    if (!d->browserHost) {
        if (d->fallbackLabel) {
            d->fallbackLabel->show();
            d->fallbackLabel->setText(QStringLiteral("CEF 浏览器创建失败"));
        }
        return false;
    }

    return true;
}

void CefVideoRenderer::playMedia()
{
    if (!browserAvailable()) {
        return;
    }
    d->ensureBrowser();
    if (!d->pageReady) {
        return;
    }
    d->runJs(QStringLiteral("window.hgyPlayer && window.hgyPlayer.play();"));
    d->playing = true;
    d->playClock.restart();
    d->positionTimer.start();
}

void CefVideoRenderer::pauseMedia()
{
    if (!browserAvailable()) {
        return;
    }
    pollBrowserPosition();
    d->runJs(QStringLiteral("window.hgyPlayer && window.hgyPlayer.pause();"));
    d->playing = false;
    d->positionTimer.stop();
}

void CefVideoRenderer::stopMedia()
{
    if (!browserAvailable()) {
        d->playing = false;
        d->anchorPositionMs = 0;
        d->estimatedPositionMs = 0;
        d->positionTimer.stop();
        return;
    }
    d->runJs(QStringLiteral("window.hgyPlayer && window.hgyPlayer.stop();"));
    d->playing = false;
    d->anchorPositionMs = 0;
    d->estimatedPositionMs = 0;
    d->positionTimer.stop();
}

void CefVideoRenderer::seekMedia(qint64 positionMs)
{
    if (!browserAvailable() || !d->pageReady) {
        return;
    }
    const double sec = qMax<qint64>(0, positionMs) / 1000.0;
    d->runJs(QStringLiteral("window.hgyPlayer && window.hgyPlayer.seek(%1);").arg(sec, 0, 'f', 3));
    d->anchorPositionMs = qMax<qint64>(0, positionMs);
    d->estimatedPositionMs = d->anchorPositionMs;
    if (d->playing) {
        d->playClock.restart();
    }
}

qint64 CefVideoRenderer::browserPositionMs() const
{
    return d->estimatedPositionMs;
}

void CefVideoRenderer::pollBrowserPosition()
{
    if (d->playing) {
        d->estimatedPositionMs = d->anchorPositionMs + d->playClock.elapsed();
    }
    d->runJs(QStringLiteral(
        "window.hgyPlayer && (window.__hgyPosMs = window.hgyPlayer.currentTimeMs());"));
}
