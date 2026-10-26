#pragma once

#include "core/Platform.h"

#include <windows.h>

#include <string>
#include <vector>

namespace muralop {

// Windows implementation of the platform layer: monitors come from
// EnumDisplayMonitors, surfaces are child windows of the WorkerW that Explorer
// creates between the wallpaper and the desktop icons.
class WinPlatform : public Platform {
public:
    WinPlatform();
    ~WinPlatform() override;

    std::vector<MonitorInfo> monitors() override;
    std::unique_ptr<Surface> createSurface(const MonitorInfo& monitor) override;

    // Drop the cached WorkerW (Explorer restarted, display changed).
    void invalidateWorkerW() { workerW_ = nullptr; }

private:
    HWND findWorkerW();

    HWND workerW_ = nullptr;
    bool classRegistered_ = false;
};

std::string toUtf8(const wchar_t* text);
std::wstring toWide(const std::string& text);

} // namespace muralop
