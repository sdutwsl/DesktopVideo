// VideoWallpaper for Windows: libvlc renders into child windows of Explorer's
// WorkerW (below the desktop icons); a Shell_NotifyIcon tray menu controls it.

#include "DesktopSurface.h"
#include "Resource.h"
#include "Session.h"
#include "core/Config.h"
#include "core/Menu.h"
#include "core/VlcPlayer.h"
#include "core/Wallpaper.h"

#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wtsapi32.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace vw;

namespace {

const wchar_t* MAIN_CLASS = L"VideoWallpaperMain";
const UINT WMAPP_TRAY = WM_APP + 1;
const UINT TRAY_ID = 1;
const UINT_PTR TIMER_COVERAGE = 1;
const UINT_PTR TIMER_REBUILD = 2;

struct App {
    HINSTANCE instance = nullptr;
    HWND hwnd = nullptr;
    UINT taskbarCreated = 0;
    std::unique_ptr<Config> config;
    std::unique_ptr<VlcInstance> vlc;
    std::unique_ptr<WinPlatform> platform;
    std::unique_ptr<Wallpaper> wallpaper;
    CoverageMonitor coverage;
    MenuActions actions;
};
App app;

std::string configPath()
{
    wchar_t dir[MAX_PATH] = {0};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, dir)))
        return "config.ini";
    return toUtf8(dir) + "\\VideoWallpaper\\config.ini";
}

void enablePerMonitorDpi()
{
    // Not on older SDKs' import libraries, so look it up at run time.
    using Fn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    if (HMODULE user32 = GetModuleHandleW(L"user32.dll"))
        if (auto fn = reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"))))
            fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
}

// ---- tray icon ----------------------------------------------------------------------

void trayIcon(bool add)
{
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = app.hwnd;
    nid.uID = TRAY_ID;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WMAPP_TRAY;
    nid.hIcon = LoadIconW(app.instance, MAKEINTRESOURCEW(IDI_VIDEOWALLPAPER));
    lstrcpynW(nid.szTip, L"Video Wallpaper", ARRAYSIZE(nid.szTip));
    Shell_NotifyIconW(add ? NIM_ADD : NIM_DELETE, &nid);
    if (add) {
        nid.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &nid);
    }
}

// Turn the shared menu model into an HMENU; command ids index into `handlers`.
HMENU buildHmenu(const std::vector<MenuItem>& items, std::vector<std::function<void()>>& handlers)
{
    HMENU menu = CreatePopupMenu();
    for (const MenuItem& item : items) {
        if (item.separator) {
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            continue;
        }
        UINT flags = MF_STRING | (item.enabled ? MF_ENABLED : MF_GRAYED) | (item.checked ? MF_CHECKED : 0);
        if (!item.children.empty()) {
            HMENU sub = buildHmenu(item.children, handlers);
            AppendMenuW(menu, flags | MF_POPUP, reinterpret_cast<UINT_PTR>(sub), toWide(item.label).c_str());
        } else {
            handlers.push_back(item.activate);
            AppendMenuW(menu, flags, handlers.size(), toWide(item.label).c_str());
        }
    }
    return menu;
}

void showMenu()
{
    std::vector<std::function<void()>> handlers;
    HMENU menu = buildHmenu(buildAppMenu(*app.wallpaper, app.actions), handlers);
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(app.hwnd);   // otherwise the menu will not close when clicking elsewhere
    const UINT flags = TPM_RETURNCMD | TPM_RIGHTBUTTON |
                       (GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN);
    const int id = TrackPopupMenuEx(menu, flags, pt.x, pt.y, app.hwnd, nullptr);
    PostMessageW(app.hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
    if (id > 0 && size_t(id) <= handlers.size() && handlers[id - 1])
        handlers[id - 1]();
}

std::string openVideoDialog(const std::wstring& title)
{
    wchar_t file[MAX_PATH] = {0};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = app.hwnd;
    ofn.lpstrFilter = L"视频文件\0*.mp4;*.mkv;*.webm;*.avi;*.mov;*.wmv;*.flv;*.m4v;*.ts\0所有文件\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = title.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (!GetOpenFileNameW(&ofn))
        return "";
    return toUtf8(file);
}

// ---- window procedure -----------------------------------------------------------------

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == app.taskbarCreated) {
        // Explorer restarted: the tray icon and the WorkerW are gone.
        trayIcon(true);
        app.platform->invalidateWorkerW();
        SetTimer(hwnd, TIMER_REBUILD, 1500, nullptr);
        return 0;
    }
    switch (msg) {
    case WMAPP_TRAY:
        switch (LOWORD(lParam)) {
        case WM_CONTEXTMENU:
        case WM_RBUTTONUP:
            showMenu();
            break;
        case NIN_SELECT:
        case NIN_KEYSELECT:
        case WM_LBUTTONUP:
            app.wallpaper->setUserPaused(!app.wallpaper->userPaused());
            break;
        }
        return 0;

    case WM_TIMER:
        if (wParam == TIMER_COVERAGE) {
            for (const auto& [id, covered] : app.coverage.poll(app.wallpaper->monitors()))
                app.wallpaper->setCovered(id, covered);
        } else if (wParam == TIMER_REBUILD) {
            KillTimer(hwnd, TIMER_REBUILD);
            app.wallpaper->rebuildSurfaces();
        }
        return 0;

    case WM_DISPLAYCHANGE:
        // Debounce: several of these arrive while the layout settles.
        app.platform->invalidateWorkerW();
        SetTimer(hwnd, TIMER_REBUILD, 1000, nullptr);
        return 0;

    case WM_WTSSESSION_CHANGE:
        if (wParam == WTS_SESSION_LOCK)
            app.wallpaper->setLocked(true);
        else if (wParam == WTS_SESSION_UNLOCK)
            app.wallpaper->setLocked(false);
        return 0;

    case WM_POWERBROADCAST:
        if (wParam == PBT_APMSUSPEND)
            app.wallpaper->setSuspended(true);
        else if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND)
            app.wallpaper->setSuspended(false);
        return TRUE;

    case WM_QUERYENDSESSION:
        return TRUE;
    case WM_ENDSESSION:
        if (wParam)
            app.wallpaper->shutdown();
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR cmdLine, int)
{
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"VideoWallpaper.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"Video Wallpaper 已经在运行，看看托盘图标。", L"Video Wallpaper", MB_OK | MB_ICONINFORMATION);
        return 0;
    }
    enablePerMonitorDpi();

    app.instance = instance;
    app.config = std::make_unique<Config>(configPath());
    app.config->load();

    // A video passed on the command line (drag & drop onto the exe) becomes the default.
    int argc = 0;
    if (LPWSTR* argv = CommandLineToArgvW(cmdLine, &argc)) {
        if (argc >= 1 && cmdLine[0] && GetFileAttributesW(argv[0]) != INVALID_FILE_ATTRIBUTES) {
            app.config->set("video", toUtf8(argv[0]));
            app.config->save();
        }
        LocalFree(argv);
    }

    app.vlc = std::make_unique<VlcInstance>(std::vector<std::string>{"--quiet", "--no-video-title-show", "--no-disable-screensaver"});
    if (!app.vlc->ok()) {
        MessageBoxW(nullptr, L"libvlc 初始化失败：请确认 libvlc.dll、libvlccore.dll 和 plugins 目录与程序在同一目录。",
                    L"Video Wallpaper", MB_OK | MB_ICONERROR);
        return 1;
    }
    app.platform = std::make_unique<WinPlatform>();
    app.wallpaper = std::make_unique<Wallpaper>(*app.platform, *app.vlc, *app.config);

    app.actions.quit = [] { DestroyWindow(app.hwnd); };
    app.actions.chooseVideo = [](const std::string& monitor) {
        const std::string path = openVideoDialog(monitor.empty() ? L"选择壁纸视频" : L"选择 " + toWide(monitor) + L" 的壁纸视频");
        if (!path.empty())
            app.wallpaper->setVideo(path, monitor);
    };

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = MAIN_CLASS;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_VIDEOWALLPAPER));
    RegisterClassW(&wc);
    app.taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    // A hidden top-level window (not HWND_MESSAGE) so broadcasts such as
    // WM_DISPLAYCHANGE, WM_POWERBROADCAST, WM_ENDSESSION and TaskbarCreated reach us.
    app.hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, MAIN_CLASS, L"Video Wallpaper", WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
    if (!app.hwnd)
        return 1;

    trayIcon(true);
    WTSRegisterSessionNotification(app.hwnd, NOTIFY_FOR_THIS_SESSION);
    SetTimer(app.hwnd, TIMER_COVERAGE, 1000, nullptr);

    app.wallpaper->refreshMonitors();
    if (app.wallpaper->videoFor("").empty())
        app.actions.chooseVideo("");

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    WTSUnRegisterSessionNotification(app.hwnd);
    trayIcon(false);
    app.wallpaper.reset();     // stops players, destroys the surfaces
    app.platform.reset();      // hides the WorkerW so Explorer's wallpaper returns
    app.vlc.reset();
    CloseHandle(mutex);
    return int(msg.wParam);
}
