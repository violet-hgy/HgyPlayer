#ifndef CEFAPPHANDLER_H
#define CEFAPPHANDLER_H

#include "include/cef_app.h"

class CefAppHandler : public CefApp, public CefBrowserProcessHandler
{
public:
    CefAppHandler();

    CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override
    {
        return this;
    }

    void OnBeforeCommandLineProcessing(const CefString &process_type,
                                       CefRefPtr<CefCommandLine> command_line) override;

private:
    IMPLEMENT_REFCOUNTING(CefAppHandler);
};

#endif // CEFAPPHANDLER_H
