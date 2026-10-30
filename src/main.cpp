#include "common.h"

#include <objbase.h>

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int) {
    using SetDpiContextFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        auto setContext = reinterpret_cast<SetDpiContextFn>(
            GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (setContext) {
            setContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        }
    }

    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES;
    InitCommonControlsEx(&controls);

    HRESULT ole = OleInitialize(nullptr);
    if (FAILED(ole)) {
        MessageBoxW(nullptr, L"No se pudo iniciar OLE.", L"Nébula Media",
                    MB_OK | MB_ICONERROR);
        return 1;
    }

    Gdiplus::GdiplusStartupInput gdiplusInput;
    ULONG_PTR gdiplusToken = 0;
    if (Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusInput, nullptr) !=
        Gdiplus::Ok) {
        MessageBoxW(nullptr, L"No se pudo iniciar GDI+.", L"Nébula Media",
                    MB_OK | MB_ICONERROR);
        OleUninitialize();
        return 1;
    }

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring snapshotPath;
    std::wstring snapshotMode;
    std::wstring snapshotArgument;
    bool snapshotDeep = false;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            std::wstring arg = argv[i];
            if (arg == L"--snapshot" && i + 1 < argc) {
                snapshotPath = argv[++i];
            } else if (arg == L"--drive") {
                snapshotMode = L"drive";
            } else if (arg == L"--settings") {
                snapshotMode = L"settings";
            } else if (arg == L"--audio" && i + 1 < argc) {
                snapshotMode = L"audio";
                snapshotArgument = argv[++i];
            } else if (arg == L"--search" && i + 1 < argc) {
                snapshotMode = L"search";
                snapshotArgument = argv[++i];
            } else if (arg == L"--deep") {
                snapshotDeep = true;
            } else if (arg == L"--scroll") {
                snapshotMode = L"scroll";
            } else if (arg == L"--update") {
                snapshotMode = L"update";
            }
        }
        LocalFree(argv);
    }
    if (snapshotDeep && snapshotMode == L"search") snapshotMode = L"deep";

    const bool headless = !snapshotPath.empty();
    int result = 1;
    {
        nebula::App app;
        if (app.Create(instance, !headless)) {
            if (!headless) {
                result = app.Run();
            } else {
                app.PrepareSnapshot(snapshotMode, snapshotArgument);
                const DWORD wait = snapshotMode == L"audio" ? 700
                                   : snapshotMode == L"drive" || snapshotMode == L"deep"
                                       ? 800
                                       : 2200;
                const DWORD end = GetTickCount() + wait;
                while (GetTickCount() < end) {
                    MSG msg{};
                    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                        TranslateMessage(&msg);
                        DispatchMessageW(&msg);
                    }
                    Sleep(15);
                }
                result = app.SaveSnapshot(snapshotPath) ? 0 : 2;
                DestroyWindow(app.Hwnd());
            }
        } else if (!headless) {
            MessageBoxW(nullptr, L"No se pudo crear la ventana principal.", L"Nébula Media",
                        MB_OK | MB_ICONERROR);
        }
    }

    nebula::ShutdownPaintResources();
    Gdiplus::GdiplusShutdown(gdiplusToken);
    OleUninitialize();
    return result;
}
