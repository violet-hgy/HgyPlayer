#include "CefRuntimeImpl.h"

#include "CefAppHandler.h"
#include "CefBrowserHostImpl.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>

#include <memory>

#include "include/cef_app.h"

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
struct CefPathConfig {
    QString resourcesDir;
    QString localesDir;
    QString cacheDir;
    QString subprocessPath;
    QString libcefPath;
    bool hasIcu = false;
};

QString g_lastInitError;

QString absoluteApplicationDir()
{
    if (QCoreApplication::instance()) {
        return QDir(QCoreApplication::applicationDirPath()).absolutePath();
    }

#ifdef Q_OS_WIN
    wchar_t pathBuffer[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, pathBuffer, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        const QString exePath = QDir::fromNativeSeparators(QString::fromWCharArray(pathBuffer, length));
        return QFileInfo(exePath).absolutePath();
    }
#endif

    return QDir::current().absolutePath();
}

void setLastInitError(const QString &message)
{
    g_lastInitError = message;
    qWarning().noquote() << message;

    const QString logPath =
        QDir(absoluteApplicationDir()).filePath(QStringLiteral("cef_init.log"));
    QFile logFile(logPath);
    if (logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        logFile.write(QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8());
        logFile.write(": ");
        logFile.write(message.toUtf8());
        logFile.write("\n");
    }
}

QString nativeAbsolutePath(const QString &path)
{
    return QDir::toNativeSeparators(QDir(path).absolutePath());
}

QString absoluteExecutablePath()
{
    if (QCoreApplication::instance()) {
        return nativeAbsolutePath(QCoreApplication::applicationFilePath());
    }

#ifdef Q_OS_WIN
    wchar_t pathBuffer[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, pathBuffer, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        return nativeAbsolutePath(QDir::fromNativeSeparators(QString::fromWCharArray(pathBuffer, length)));
    }
#endif

    return {};
}

CefPathConfig buildPathConfig()
{
    CefPathConfig cfg;
    const QString appDir = absoluteApplicationDir();
    cfg.resourcesDir = nativeAbsolutePath(appDir);
    cfg.localesDir = nativeAbsolutePath(QDir(appDir).filePath(QStringLiteral("locales")));
    cfg.cacheDir = nativeAbsolutePath(QDir(appDir).filePath(QStringLiteral("cef_cache")));
    cfg.subprocessPath = absoluteExecutablePath();
    cfg.libcefPath = nativeAbsolutePath(QDir(appDir).filePath(QStringLiteral("libcef.dll")));
    cfg.hasIcu = QFileInfo::exists(QDir(appDir).filePath(QStringLiteral("icudtl.dat")))
                 || QFileInfo::exists(QDir(appDir).filePath(QStringLiteral("Resources/icudtl.dat")));
    return cfg;
}

void logPathConfig(const CefPathConfig &cfg);

bool ensureCefLibraryLoaded()
{
#ifdef Q_OS_WIN
    const CefPathConfig paths = buildPathConfig();
    if (!QFileInfo::exists(paths.libcefPath)) {
        setLastInitError(QStringLiteral("libcef.dll 不存在: %1").arg(paths.libcefPath));
        return false;
    }
#else
    // noop
#endif
    return true;
}

void assignCefString(cef_string_t *dest, const QString &text)
{
    if (!dest) {
        return;
    }
#ifdef Q_OS_WIN
    CefString cefStr(dest);
    cefStr.FromWString(text.toStdWString());
    cefStr.Detach();
#else
    CefString cefStr(dest);
    cefStr.FromString(text.toUtf8().constData());
    cefStr.Detach();
#endif
}

void populateCefSettings(CefSettings &settings, const CefPathConfig &cfg)
{
    settings.no_sandbox = true;
    settings.command_line_args_disabled = true;
    settings.multi_threaded_message_loop = true;
    settings.windowless_rendering_enabled = false;

    if (!cfg.resourcesDir.isEmpty()) {
        assignCefString(&settings.resources_dir_path, cfg.resourcesDir);
    }
    if (!cfg.localesDir.isEmpty()) {
        assignCefString(&settings.locales_dir_path, cfg.localesDir);
    }
    if (!cfg.cacheDir.isEmpty()) {
        QDir().mkpath(cfg.cacheDir);
        assignCefString(&settings.root_cache_path, cfg.cacheDir);
        assignCefString(&settings.cache_path, cfg.cacheDir);
    }
}

void logPathConfig(const CefPathConfig &cfg)
{
    setLastInitError(QStringLiteral("CEF paths:\n"
                                    "  resources: %1\n"
                                    "  locales: %2\n"
                                    "  libcef: %3\n"
                                    "  icudtl.dat present: %4")
                         .arg(cfg.resourcesDir,
                              cfg.localesDir,
                              cfg.libcefPath,
                              cfg.hasIcu ? QStringLiteral("yes") : QStringLiteral("no")));
}
} // namespace

int CefRuntimeImpl::executeSubProcessIfNeeded(int argc, char **argv)
{
    if (!ensureCefLibraryLoaded()) {
        qWarning("CEF library load failed before subprocess dispatch");
        return -1;
    }

#ifdef Q_OS_WIN
    HINSTANCE instance = GetModuleHandle(nullptr);
    CefMainArgs mainArgs(instance);
#else
    CefMainArgs mainArgs(argc, argv);
#endif
    Q_UNUSED(argv);

    CefRefPtr<CefAppHandler> app(new CefAppHandler());
    const int exitCode = CefExecuteProcess(mainArgs, app, nullptr);
    return exitCode;
}

bool CefRuntimeImpl::initialize(int argc, char **argv)
{
    Q_UNUSED(argc);
    Q_UNUSED(argv);

    if (m_initialized) {
        return true;
    }

    if (!ensureCefLibraryLoaded()) {
        setLastInitError(QStringLiteral("CEF 库加载失败：未找到 libcef.dll 或加载出错"));
        return false;
    }

#ifdef Q_OS_WIN
    HINSTANCE instance = GetModuleHandle(nullptr);
    CefMainArgs mainArgs(instance);
#else
    CefMainArgs mainArgs(argc, argv);
#endif

    const CefPathConfig paths = buildPathConfig();
    if (!paths.hasIcu) {
        logPathConfig(paths);
        setLastInitError(QStringLiteral("CEF 初始化失败：缺少 icudtl.dat\n"
                                        "请将 CEF Resources 复制到 exe 同目录"));
        return false;
    }

    // Clear stale Chromium singleton locks that may cause early-exit codes.
    const QDir cacheDir(paths.cacheDir);
    QFile::remove(cacheDir.filePath(QStringLiteral("SingletonLock")));
    QFile::remove(cacheDir.filePath(QStringLiteral("SingletonCookie")));
    QFile::remove(cacheDir.filePath(QStringLiteral("SingletonSocket")));

    CefSettings settings;
    populateCefSettings(settings, paths);

    CefRefPtr<CefAppHandler> app(new CefAppHandler());
    if (!CefInitialize(mainArgs, settings, app, nullptr)) {
        const int exitCode = CefGetExitCode();
        logPathConfig(paths);
        setLastInitError(QStringLiteral("CefInitialize 失败，exitCode=%1\n"
                                        "7=缺少资源文件，详情见 cef_debug.log")
                             .arg(exitCode));
        return false;
    }

    g_lastInitError.clear();

    m_initialized = true;
    return true;
}

void CefRuntimeImpl::shutdown()
{
    if (!m_initialized) {
        return;
    }
    CefShutdown();
    m_initialized = false;
}

std::unique_ptr<ICefBrowserHost> CefRuntimeImpl::createBrowserHost()
{
    if (!m_initialized) {
        return nullptr;
    }
    return std::make_unique<CefBrowserHostImpl>();
}

QString CefRuntimeImpl::lastInitError()
{
    return g_lastInitError;
}
