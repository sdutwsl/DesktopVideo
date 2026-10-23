#pragma once

#include "core/Menu.h"

#include <sdbus-c++/sdbus-c++.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace vw {

// System tray icon via the StatusNotifierItem protocol, with its menu exported
// through com.canonical.dbusmenu. Works on KDE, Sway/Waybar, Hyprland bars and
// on GNOME with the AppIndicator extension.
class Tray {
public:
    Tray(sdbus::IConnection& bus, std::string id, std::string title);
    ~Tray();

    void setMenu(std::vector<MenuItem> items);

    std::function<void()> onActivate;   // left click on the icon

private:
    using Props = std::map<std::string, sdbus::Variant>;
    using Layout = sdbus::Struct<int32_t, Props, std::vector<sdbus::Variant>>;
    using Pixmap = sdbus::Struct<int32_t, int32_t, std::vector<uint8_t>>;

    struct Node {
        int32_t id;
        const MenuItem* item;         // nullptr for the root
        std::vector<int32_t> children;
    };

    void exportItem();
    void exportMenu();
    void registerWithWatcher();
    void flatten(const std::vector<MenuItem>& items, size_t parentIndex);
    Props propsFor(const Node& node, const std::vector<std::string>& names) const;
    Layout layoutFor(int32_t id, int32_t depth, const std::vector<std::string>& names) const;
    const Node* find(int32_t id) const;
    static std::vector<Pixmap> makeIcon();

    sdbus::IConnection& bus_;
    std::string id_, title_, busName_;
    std::unique_ptr<sdbus::IObject> item_;
    std::unique_ptr<sdbus::IObject> menu_;
    std::unique_ptr<sdbus::IProxy> watcher_;
    sdbus::Slot watcherMatch_;

    std::vector<MenuItem> items_;
    std::vector<Node> nodes_;
    uint32_t revision_ = 1;
    bool registered_ = false;
};

} // namespace vw
