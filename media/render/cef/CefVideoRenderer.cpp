#include "CefVideoRenderer.h"

#include "CefRuntimeFacade.h"
#include "ICefBrowserHost.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
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
    bool autoPlayWhenReady = false;

    void syncBrowserSize();
    void ensureBrowser();
    void runJs(const QString &script);
    void onPageLoaded();
    void loadPendingMedia();
};

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
    const QString out = QStringLiteral("%1 %2\n")
                            .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs), line);
    f.write(out.toUtf8());
}

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
        "function diag(tag){var err=v.error?(' code='+v.error.code):'';"
        "console.log('[HGY] '+tag+' readyState='+v.readyState+' networkState='+v.networkState+err+' src='+v.currentSrc);}"
        "v.addEventListener('loadstart',function(){show('正在加载媒体...');diag('loadstart');});"
        "v.addEventListener('loadedmetadata',function(){show('');diag('loadedmetadata');});"
        "v.addEventListener('loadeddata',function(){show('');diag('loadeddata');});"
        "v.addEventListener('canplay',function(){diag('canplay');});"
        "v.addEventListener('playing',function(){show('');diag('playing');});"
        "v.addEventListener('stalled',function(){show('网络/解码阻塞');diag('stalled');});"
        "v.addEventListener('waiting',function(){show('缓冲中...');diag('waiting');});"
        "v.addEventListener('error',function(){show('无法解码此媒体');diag('error');});"
        "v.addEventListener('emptied',function(){show('HgyPlayer · CEF');diag('emptied');});"
        "window.hgyPlayer={load:function(u){v.src=u;v.load();diag('load-call');},"
        "play:function(){diag('play-call');var p=v.play();"
        "if(p&&p.then){p.then(function(){diag('play-ok');}).catch(function(e){console.log('[HGY] play-fail '+e);show('播放失败: '+e);diag('play-fail');});}"
        "return p;},"
        "pause:function(){v.pause();diag('pause-call');},"
        "stop:function(){v.pause();v.currentTime=0;diag('stop-call');},"
        "seek:function(s){if(!isNaN(s))v.currentTime=s;diag('seek-call');},"
        "currentTimeMs:function(){return Math.round((v.currentTime||0)*1000);}};"
        "})();</script></body></html>");
}

QString ensurePlayerPageOnDisk()
{
    QDir dir(QCoreApplication::applicationDirPath());
    dir.mkpath(QStringLiteral("cef"));
    const QString path = dir.filePath(QStringLiteral("cef/player.html"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return {};
    }
    file.write(defaultPlayerHtml().toUtf8());
    file.close();
    return path;
}

QString jsStringLiteral(const QString &text)
{
    return QString::fromUtf8(QJsonDocument::fromVariant(text).toJson(QJsonDocument::Compact));
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
    appendCefPlayerLog(QStringLiteral("[HGY] ensureBrowser begin"));

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

    if (!browserHost->create(host)) {
        appendCefPlayerLog(QStringLiteral("[HGY] ensureBrowser create failed"));
        browserHost.reset();
        if (fallbackLabel) {
            fallbackLabel->show();
            fallbackLabel->setText(QStringLiteral("无法创建 CEF 浏览器窗口"));
        }
        return;
    }

    const QString playerPath = ensurePlayerPageOnDisk();
    if (!playerPath.isEmpty()) {
        appendCefPlayerLog(QStringLiteral("[HGY] navigate player page %1").arg(playerPath));
        browserHost->navigateToUrl(QUrl::fromLocalFile(playerPath).toString());
    } else {
        appendCefPlayerLog(QStringLiteral("[HGY] load inline player html"));
        browserHost->loadHtmlPage(defaultPlayerHtml());
    }

    if (fallbackLabel) {
        fallbackLabel->hide();
    }
    syncBrowserSize();
}

void CefVideoRenderer::Private::onPageLoaded()
{
    appendCefPlayerLog(QStringLiteral("[HGY] onPageLoaded"));
    pageReady = true;
    loadPendingMedia();
    if (autoPlayWhenReady) {
        runJs(QStringLiteral("window.hgyPlayer && window.hgyPlayer.play();"));
        autoPlayWhenReady = false;
        playing = true;
        playClock.restart();
        positionTimer.start();
    }
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
    appendCefPlayerLog(QStringLiteral("[HGY] loadPendingMedia url=%1").arg(pendingMediaUrl));
    const QString script = QStringLiteral("window.hgyPlayer && window.hgyPlayer.load(%1);")
                               .arg(jsStringLiteral(pendingMediaUrl));
    browserHost->executeJavaScript(script);
    anchorPositionMs = 0;
    estimatedPositionMs = 0;
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
    appendCefPlayerLog(QStringLiteral("[HGY] CefVideoRenderer ctor"));
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
            QStringLiteral("CEF 就绪，等待加载媒体文件"));
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
    appendCefPlayerLog(QStringLiteral("[HGY] openMedia path=%1").arg(filePath));
    if (!browserAvailable()) {
        if (d->fallbackLabel) {
            d->fallbackLabel->show();
            d->fallbackLabel->setText(
                QStringLiteral("CEF 不可用\n\n"
                               "需启用 HGY_ENABLE_CEF 并将 libcef.dll / Resources 部署到 exe 目录"));
        }
        return false;
    }

    if (!QFileInfo::exists(filePath)) {
        if (d->fallbackLabel) {
            d->fallbackLabel->show();
            d->fallbackLabel->setText(QStringLiteral("媒体文件不存在：\n%1").arg(filePath));
        }
        return false;
    }

    d->mediaPath = filePath;
    d->pendingMediaUrl = QUrl::fromLocalFile(filePath).toString();
    d->pageReady = false;
    d->autoPlayWhenReady = false;
    d->playing = false;
    d->anchorPositionMs = 0;
    d->estimatedPositionMs = 0;

    d->ensureBrowser();
    if (!d->browserHost) {
        if (d->fallbackLabel) {
            d->fallbackLabel->show();
            d->fallbackLabel->setText(QStringLiteral("CEF 浏览器创建失败"));
        }
        return false;
    }

    if (d->browserHost->isCreated()) {
        // Reload the player page per media open so JS state starts clean.
        const QString playerPath = ensurePlayerPageOnDisk();
        if (!playerPath.isEmpty()) {
            d->browserHost->navigateToUrl(QUrl::fromLocalFile(playerPath).toString());
        } else {
            d->browserHost->loadHtmlPage(defaultPlayerHtml());
            d->pageReady = true;
            d->loadPendingMedia();
        }
    }

    if (d->fallbackLabel) {
        d->fallbackLabel->show();
        d->fallbackLabel->setText(QStringLiteral("媒体已加载，点击播放"));
    }

    return true;
}

void CefVideoRenderer::playMedia()
{
    appendCefPlayerLog(QStringLiteral("[HGY] playMedia pageReady=%1 autoPlayWhenReady=%2")
                           .arg(d->pageReady ? QStringLiteral("1") : QStringLiteral("0"),
                                d->autoPlayWhenReady ? QStringLiteral("1") : QStringLiteral("0")));
    if (!browserAvailable()) {
        return;
    }
    d->ensureBrowser();
    if (!d->pageReady) {
        d->autoPlayWhenReady = true;
        return;
    }
    d->runJs(QStringLiteral("window.hgyPlayer && window.hgyPlayer.play();"));
    d->autoPlayWhenReady = false;
    d->playing = true;
    d->playClock.restart();
    d->positionTimer.start();
}

void CefVideoRenderer::pauseMedia()
{
    appendCefPlayerLog(QStringLiteral("[HGY] pauseMedia"));
    if (!browserAvailable()) {
        return;
    }
    d->autoPlayWhenReady = false;
    pollBrowserPosition();
    d->runJs(QStringLiteral("window.hgyPlayer && window.hgyPlayer.pause();"));
    d->playing = false;
    d->positionTimer.stop();
}

void CefVideoRenderer::stopMedia()
{
    appendCefPlayerLog(QStringLiteral("[HGY] stopMedia"));
    if (!browserAvailable()) {
        d->playing = false;
        d->autoPlayWhenReady = false;
        d->anchorPositionMs = 0;
        d->estimatedPositionMs = 0;
        d->positionTimer.stop();
        return;
    }
    d->runJs(QStringLiteral("window.hgyPlayer && window.hgyPlayer.stop();"));
    d->playing = false;
    d->autoPlayWhenReady = false;
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
