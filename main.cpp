#include "mainwindow.h"

#include "render/cef/CefRuntimeFacade.h"

#include <QApplication>
#include <QDebug>

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

    CefRuntimeFacade::shutdown();
    return code;
}
