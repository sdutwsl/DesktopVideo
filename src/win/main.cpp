// Muralop for Windows: libmpv renders into child windows of Explorer's
// WorkerW (below the desktop icons); a Shell_NotifyIcon tray menu controls it.

#include "DesktopSurface.h"
#include "ControlPipe.h"
#include "Resource.h"
#include "core/AppControl.h"
#include "core/Config.h"
#include "core/Menu.h"
#include "core/Wallpaper.h"

#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wtsapi32.h>

#include <functional>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace muralop;

namespace {

const wchar_t* MAIN_CLASS = L"MuralopMain";
const UINT WMAPP_TRAY = WM_APP + 1;
const UINT TRAY_ID = 1;
const UINT_PTR TIMER_REBUILD = 1;

struct App {
    HINSTANCE instance = nullptr;
    HWND hwnd = nullptr;
    UINT taskbarCreated = 0;
    std::unique_ptr<Config> config;
    std::unique_ptr<WinPlatform> platform;
    std::unique_ptr<Wallpaper> wallpaper;
    MenuActions actions;
    std::unique_ptr<AppControl> control;
    std::unique_ptr<ControlPipe> pipe;
};
App app;

std::filesystem::path uiPath()
{
    wchar_t executable[32768] = {0};
    GetModuleFileNameW(nullptr, executable, 32768);
    return std::filesystem::path(executable).parent_path() / L"muralop-ui.exe";
}

bool uiAvailable()
{
    return std::filesystem::is_regular_file(uiPath());
}

void showMainWindow()
{
    const auto ui = uiPath();
    if (reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", ui.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
        MessageBoxW(app.hwnd, L"无法打开 Muralop 主界面。", L"Muralop", MB_OK | MB_ICONERROR);
}

// Portable mode: a config.ini next to the exe (the portable zip ships one) keeps the
// settings in that folder; otherwise they live in %APPDATA%\Muralop.
struct ConfigPaths { std::string current, legacy; };

ConfigPaths configPaths()
{
    wchar_t exe[MAX_PATH] = {0};
    const DWORD len = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        std::wstring local(exe, len);
        local.resize(local.find_last_of(L'\\') + 1);
        local += L"config.ini";
        if (GetFileAttributesW(local.c_str()) != INVALID_FILE_ATTRIBUTES)
            return {toUtf8(local.c_str()), ""};
    }

    wchar_t dir[MAX_PATH] = {0};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, dir)))
        return {"config.ini", ""};
    const std::string base = toUtf8(dir);
    return {base + "\\Muralop\\config.ini", base + "\\VideoWallpaper\\config.ini"};
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
    nid.hIcon = LoadIconW(app.instance, MAKEINTRESOURCEW(IDI_MURALOP));
    lstrcpynW(nid.szTip, L"Muralop", ARRAYSIZE(nid.szTip));
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
    case ControlPipe::Message:
        if (app.pipe) app.pipe->dispatch();
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
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
        if (wParam == TIMER_REBUILD) {
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
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Muralop.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (!wcsstr(cmdLine, L"--background") && uiAvailable()) showMainWindow();
        else if (!wcsstr(cmdLine, L"--background"))
            MessageBoxW(nullptr, L"Muralop 已经在运行，看看托盘图标。", L"Muralop", MB_OK | MB_ICONINFORMATION);
        CloseHandle(mutex);
        return 0;
    }
    enablePerMonitorDpi();

    app.instance = instance;
    const ConfigPaths paths = configPaths();
    app.config = std::make_unique<Config>(paths.current, paths.legacy);
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

    app.platform = std::make_unique<WinPlatform>();
    app.wallpaper = std::make_unique<Wallpaper>(*app.platform, *app.config);

    app.actions.quit = [] { DestroyWindow(app.hwnd); };
    if (uiAvailable()) app.actions.showMainWindow = showMainWindow;
    app.actions.chooseVideo = [](const std::string& monitor) {
        const std::string path = openVideoDialog(monitor.empty() ? L"选择壁纸视频" : L"选择 " + toWide(monitor) + L" 的壁纸视频");
        if (!path.empty())
            app.wallpaper->setVideo(path, monitor);
    };

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = MAIN_CLASS;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_MURALOP));
    RegisterClassW(&wc);
    app.taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    // A hidden top-level window (not HWND_MESSAGE) so broadcasts such as
    // WM_DISPLAYCHANGE, WM_POWERBROADCAST, WM_ENDSESSION and TaskbarCreated reach us.
    app.hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, MAIN_CLASS, L"Muralop", WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
    if (!app.hwnd)
        return 1;

    trayIcon(true);
    WTSRegisterSessionNotification(app.hwnd, NOTIFY_FOR_THIS_SESSION);

    app.control = std::make_unique<AppControl>(*app.wallpaper, *app.config, [] { PostMessageW(app.hwnd, WM_CLOSE, 0, 0); });
    try {
        app.pipe = std::make_unique<ControlPipe>(app.hwnd, *app.control);
    } catch (const std::exception& e) {
        MessageBoxW(app.hwnd, toWide(e.what()).c_str(), L"Muralop", MB_OK | MB_ICONERROR);
    }

    app.wallpaper->refreshMonitors();
    if (!wcsstr(cmdLine, L"--background") && uiAvailable())
        showMainWindow();
    else if (app.wallpaper->videoFor("").empty() && !wcsstr(cmdLine, L"--background"))
        app.actions.chooseVideo("");

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    WTSUnRegisterSessionNotification(app.hwnd);
    trayIcon(false);
    app.pipe.reset();
    app.control.reset();
    app.wallpaper.reset();     // stops players, destroys the surfaces
    app.platform.reset();      // hides the WorkerW so Explorer's wallpaper returns
    CloseHandle(mutex);
    return int(msg.wParam);
}
