#include "Session.h"

#include <dwmapi.h>
#include <shellapi.h>

#include <set>

namespace vw {

namespace {

bool isShellWindow(HWND hwnd)
{
    wchar_t cls[64] = {0};
    GetClassNameW(hwnd, cls, 64);
    static const wchar_t* shell[] = {L"Progman", L"WorkerW", L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd",
                                     L"Windows.UI.Core.CoreWindow", L"XamlExplorerHostIslandWindow"};
    for (const wchar_t* s : shell)
        if (!lstrcmpW(cls, s))
            return true;
    return false;
}

bool isCloaked(HWND hwnd)
{
    DWORD cloaked = 0;
    return SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof cloaked)) && cloaked != 0;
}

struct Scan {
    DWORD ownPid;
    std::set<HMONITOR> covered;
};

BOOL CALLBACK scanWindow(HWND hwnd, LPARAM param)
{
    auto* scan = reinterpret_cast<Scan*>(param);
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd) || isCloaked(hwnd) || isShellWindow(hwnd))
        return TRUE;
    if (GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)
        return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == scan->ownPid)
        return TRUE;

    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONULL);
    if (!monitor)
        return TRUE;
    if (IsZoomed(hwnd)) {
        scan->covered.insert(monitor);
        return TRUE;
    }
    // Borderless fullscreen: the window rectangle swallows the whole monitor.
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    RECT rect;
    if (GetMonitorInfoW(monitor, &mi) && GetWindowRect(hwnd, &rect) &&
        rect.left <= mi.rcMonitor.left && rect.top <= mi.rcMonitor.top &&
        rect.right >= mi.rcMonitor.right && rect.bottom >= mi.rcMonitor.bottom)
        scan->covered.insert(monitor);
    return TRUE;
}

} // namespace

std::vector<std::pair<std::string, bool>> CoverageMonitor::poll(const std::vector<MonitorInfo>& monitors)
{
    Scan scan;
    scan.ownPid = GetCurrentProcessId();
    EnumWindows(scanWindow, reinterpret_cast<LPARAM>(&scan));

    // Exclusive-mode Direct3D and presentation mode are not visible to EnumWindows.
    QUERY_USER_NOTIFICATION_STATE state;
    if (SUCCEEDED(SHQueryUserNotificationState(&state)) &&
        (state == QUNS_RUNNING_D3D_FULL_SCREEN || state == QUNS_PRESENTATION_MODE || state == QUNS_BUSY)) {
        if (HWND fg = GetForegroundWindow())
            if (HMONITOR m = MonitorFromWindow(fg, MONITOR_DEFAULTTONULL))
                scan.covered.insert(m);
    }

    std::vector<std::pair<std::string, bool>> changes;
    for (const MonitorInfo& m : monitors) {
        const POINT center = {m.x + m.width / 2, m.y + m.height / 2};
        HMONITOR handle = MonitorFromPoint(center, MONITOR_DEFAULTTONULL);
        const bool covered = handle && scan.covered.count(handle) > 0;
        auto it = last_.find(m.id);
        if (it == last_.end() || it->second != covered) {
            last_[m.id] = covered;
            changes.emplace_back(m.id, covered);
        }
    }
    return changes;
}

} // namespace vw
