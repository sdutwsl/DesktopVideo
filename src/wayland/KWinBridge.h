#pragma once

#include <sdbus-c++/sdbus-c++.h>

#include <memory>
#include <string>

namespace vw {

// KWin hides window states from ordinary Wayland clients, so on Plasma we load a
// small script into KWin itself (org.kde.KWin /Scripting) that calls back our
// Control interface with per-output coverage. No-op on other compositors.
class KWinBridge {
public:
    explicit KWinBridge(sdbus::IConnection& bus);
    ~KWinBridge();

    bool active() const { return loaded_; }

private:
    void load();
    void unload();

    sdbus::IConnection& bus_;
    std::unique_ptr<sdbus::IProxy> scripting_;
    sdbus::Slot ownerMatch_;
    std::string scriptPath_;
    bool loaded_ = false;
};

} // namespace vw
