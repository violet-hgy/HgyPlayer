#include "mainwindow.h"

#include "render/cef/CefRuntimeFacade.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QEventLoop>
#include <QThread>

int main(int argc, char *argv[])
{
    const int cefSubProcessExit = CefRuntimeFacade::executeSubProcessIfNeeded(argc, argv);
    if (cefSubProcessExit >= 0) {
        return cefSubProcessExit;
    }

    QApplication a(argc, argv);

    if (!CefRuntimeFacade::initialize(argc, argv)) {
        qWarning("CEF initialize failed; browser renderer unavailable.");
    }

    MainWindow w;
    w.show();
    const int code = a.exec();

    // Drain pending close work from CEF-owned windows/contexts before shutdown.
    for (int i = 0; i < 20; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(5);
    }

    CefRuntimeFacade::shutdown();
    return code;
}
