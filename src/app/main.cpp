// potationsail entry point.
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>

#include "MainWindow.h"

#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' \
version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    EnableMouseInPointer(FALSE);  // mouse stays WM_MOUSE*; pens arrive as WM_POINTER

    app::MainWindow win;
    if (!win.create(inst, show)) return 1;

    int argc = 0;
    if (LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        if (argc > 1) win.openFile(argv[1]);
        LocalFree(argv);
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!TranslateAcceleratorW(win.hwnd(), win.accelerators(), &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return int(msg.wParam);
}
