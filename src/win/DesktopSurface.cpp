#include "DesktopSurface.h"
#include "core/MpvPlayer.h"

#include <cstdio>

namespace vw {

static const wchar_t* SURFACE_CLASS = L"VideoWallpaperSurface";
static const UINT WMAPP_MPV_EVENTS = WM_APP + 1;   // posted by mpv's wakeup callback

std::string toUtf8(const wchar_t* text)
{
    const int len = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    std::string out(len > 0 ? len - 1 : 0, '\0');
    if (len > 1)
        WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), len, nullptr, nullptr);
    return out;
}

std::wstring toWide(const std::string& text)
{
    const int len = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    std::wstring out(len > 0 ? len - 1 : 0, L'\0');
    if (len > 1)
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), len);
    return out;
}

// ---- WinSurface -------------------------------------------------------------------

class WinSurface : public Surface {
public:
    WinSurface(HWND hwnd, MonitorInfo monitor) : hwnd_(hwnd), monitor_(std::move(monitor))
    {
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    }
    ~WinSurface() override
    {
        detach();
        if (hwnd_) {
            SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
            DestroyWindow(hwnd_);
        }
    }
    const MonitorInfo& monitor() const override { return monitor_; }

    // mpv draws into our window itself (D3D11 by default).
    void attach(MpvPlayer& player) override
    {
        player_ = &player;
        player.setWindow(static_cast<int64_t>(reinterpret_cast<intptr_t>(hwnd_)));
        HWND hwnd = hwnd_;
        player.setWakeup([hwnd] { PostMessageW(hwnd, WMAPP_MPV_EVENTS, 0, 0); });
        player.initialize();
    }
    void detach() override
    {
        if (player_)
            player_->setWakeup(nullptr);
        player_ = nullptr;
    }

    static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WMAPP_MPV_EVENTS) {
            auto* self = reinterpret_cast<WinSurface*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (self && self->player_)
                self->player_->processEvents();
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

private:
    HWND hwnd_;
    MonitorInfo monitor_;
    MpvPlayer* player_ = nullptr;
};

// ---- WinPlatform ------------------------------------------------------------------

WinPlatform::WinPlatform()
{
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WinSurface::windowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = SURFACE_CLASS;
    classRegistered_ = RegisterClassW(&wc) != 0;
}

WinPlatform::~WinPlatform()
{
    // Hiding the WorkerW brings Explorer's own wallpaper back once our windows are gone.
    if (workerW_)
        ShowWindow(workerW_, SW_HIDE);
    if (classRegistered_)
        UnregisterClassW(SURFACE_CLASS, GetModuleHandleW(nullptr));
}

std::vector<MonitorInfo> WinPlatform::monitors()
{
    std::vector<MonitorInfo> result;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM param) -> BOOL {
        auto* out = reinterpret_cast<std::vector<MonitorInfo>*>(param);
        MONITORINFOEXW info = {};
        info.cbSize = sizeof(info);
        if (!GetMonitorInfoW(monitor, &info))
            return TRUE;
        MonitorInfo m;
        m.id = toUtf8(info.szDevice);   // "\\.\DISPLAY1"
        m.x = info.rcMonitor.left;
        m.y = info.rcMonitor.top;
        m.width = info.rcMonitor.right - info.rcMonitor.left;
        m.height = info.rcMonitor.bottom - info.rcMonitor.top;
        m.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
        out->push_back(m);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    return result;
}

// Ask Progman to spawn a WorkerW behind the desktop icons and return it.
HWND WinPlatform::findWorkerW()
{
    if (workerW_ && IsWindow(workerW_))
        return workerW_;

    HWND progman = FindWindowW(L"Progman", nullptr);
    if (!progman)
        return nullptr;
    // Undocumented: 0x052C makes Progman split the wallpaper off into a WorkerW.
    SendMessageTimeoutW(progman, 0x052C, 0xD, 0x0, SMTO_NORMAL, 1000, nullptr);
    SendMessageTimeoutW(progman, 0x052C, 0xD, 0x1, SMTO_NORMAL, 1000, nullptr);

    // Classic layout (Windows 7 .. 11 23H2): the WorkerW we want is the top-level
    // sibling right after the one hosting SHELLDLL_DefView.
    struct Search { HWND result = nullptr; };
    Search search;
    EnumWindows([](HWND hwnd, LPARAM param) -> BOOL {
        if (!FindWindowExW(hwnd, nullptr, L"SHELLDLL_DefView", nullptr))
            return TRUE;
        auto* s = reinterpret_cast<Search*>(param);
        s->result = FindWindowExW(nullptr, hwnd, L"WorkerW", nullptr);
        return FALSE;
    }, reinterpret_cast<LPARAM>(&search));
    workerW_ = search.result;

    // Windows 11 24H2 and later: SHELLDLL_DefView and the WorkerW both live inside Progman.
    if (!workerW_)
        workerW_ = FindWindowExW(progman, nullptr, L"WorkerW", nullptr);

    if (workerW_)
        ShowWindow(workerW_, SW_SHOW);
    else
        fprintf(stderr, "[win] could not find the desktop WorkerW window\n");
    return workerW_;
}

std::unique_ptr<Surface> WinPlatform::createSurface(const MonitorInfo& monitor)
{
    HWND parent = findWorkerW();
    if (!parent)
        return nullptr;

    // Monitor rectangle in the WorkerW's client coordinates.
    RECT rect = {monitor.x, monitor.y, monitor.x + monitor.width, monitor.y + monitor.height};
    MapWindowPoints(HWND_DESKTOP, parent, reinterpret_cast<POINT*>(&rect), 2);

    HWND hwnd = CreateWindowExW(WS_EX_NOACTIVATE, SURFACE_CLASS, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                                rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
                                parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd)
        return nullptr;
    return std::make_unique<WinSurface>(hwnd, monitor);
}

} // namespace vw
