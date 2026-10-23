#pragma once

#include <functional>
#include <string>
#include <vector>

namespace vw {

// Toolkit-neutral menu description; each platform turns it into its native menu.
struct MenuItem {
    std::string label;
    bool separator = false;
    bool enabled = true;
    bool checkable = false;
    bool checked = false;
    std::vector<MenuItem> children;
    std::function<void()> activate;

    static MenuItem Separator()
    {
        MenuItem m;
        m.separator = true;
        return m;
    }
};

class Wallpaper;

struct MenuActions {
    std::function<void(const std::string& monitorId)> chooseVideo;   // "" = every monitor
    std::function<void()> quit;
};

// The tray menu, built from the wallpaper's current state.
std::vector<MenuItem> buildAppMenu(Wallpaper& wallpaper, const MenuActions& actions);

} // namespace vw
