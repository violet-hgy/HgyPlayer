#include "CefAppHandler.h"

void CefAppHandler::OnBeforeCommandLineProcessing(const CefString &process_type,
                                                  CefRefPtr<CefCommandLine> command_line)
{
    (void)process_type;
    if (!command_line) {
        return;
    }
    command_line->AppendSwitch("do-not-de-elevate");
    command_line->AppendSwitch("disable-gpu");
    command_line->AppendSwitch("disable-gpu-compositing");
    command_line->AppendSwitch("disable-gpu-vsync");
}

CefAppHandler::CefAppHandler() = default;
