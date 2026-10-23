#pragma once

#include "core/Platform.h"

#include <windows.h>

#include <map>
#include <string>
#include <vector>

namespace vw {

// Per-monitor "is a maximized or fullscreen window hiding the wallpaper?" check.
// Polled from a timer; Windows has no cheap push notification for this.
class CoverageMonitor {
public:
    // Returns the monitors whose state changed since the last call.
    std::vector<std::pair<std::string, bool>> poll(const std::vector<MonitorInfo>& monitors);

private:
    std::map<std::string, bool> last_;
};

} // namespace vw
