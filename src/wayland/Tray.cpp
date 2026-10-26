#include "Tray.h"

#include <unistd.h>

#include <cmath>
#include <cstdio>

namespace vw {

static const char* SNI = "org.kde.StatusNotifierItem";
static const char* WATCHER = "org.kde.StatusNotifierWatcher";
static const char* DBUSMENU = "com.canonical.dbusmenu";

Tray::Tray(sdbus::IConnection& bus, std::string id, std::string title)
    : bus_(bus), id_(std::move(id)), title_(std::move(title))
{
    busName_ = std::string(SNI) + "-" + std::to_string(getpid()) + "-1";
    bus_.requestName(sdbus::ServiceName(busName_));
    exportItem();
    exportMenu();
    registerWithWatcher();

    // Re-register when the watcher (the panel) restarts.
    watcherMatch_ = bus_.addMatch(
        "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',"
        "member='NameOwnerChanged',arg0='" + std::string(WATCHER) + "'",
        [this](sdbus::Message msg) {
            std::string name, oldOwner, newOwner;
            msg >> name >> oldOwner >> newOwner;
            registered_ = false;
            if (!newOwner.empty())
                registerWithWatcher();
        },
        sdbus::return_slot);
}

Tray::~Tray()
{
    watcherMatch_.reset();
    watcher_.reset();
    menu_.reset();
    item_.reset();
    try {
        bus_.releaseName(sdbus::ServiceName(busName_));
    } catch (const sdbus::Error&) {
    }
}

// ---- org.kde.StatusNotifierItem ---------------------------------------------------

void Tray::exportItem()
{
    item_ = sdbus::createObject(bus_, sdbus::ObjectPath("/StatusNotifierItem"));
    item_->addVTable(
        sdbus::registerProperty("Category").withGetter([] { return std::string("ApplicationStatus"); }),
        sdbus::registerProperty("Id").withGetter([this] { return id_; }),
        sdbus::registerProperty("Title").withGetter([this] { return title_; }),
        sdbus::registerProperty("Status").withGetter([] { return std::string("Active"); }),
        sdbus::registerProperty("WindowId").withGetter([] { return int32_t(0); }),
        sdbus::registerProperty("IconName").withGetter([] { return std::string("preferences-desktop-wallpaper"); }),
        sdbus::registerProperty("IconPixmap").withGetter([] { return makeIcon(); }),
        sdbus::registerProperty("OverlayIconName").withGetter([] { return std::string(); }),
        sdbus::registerProperty("AttentionIconName").withGetter([] { return std::string(); }),
        sdbus::registerProperty("AttentionMovieName").withGetter([] { return std::string(); }),
        sdbus::registerProperty("ToolTip").withGetter([this] {
            return sdbus::Struct<std::string, std::vector<Pixmap>, std::string, std::string>(
                "preferences-desktop-wallpaper", std::vector<Pixmap>{}, title_, "");
        }),
        sdbus::registerProperty("ItemIsMenu").withGetter([] { return true; }),
        sdbus::registerProperty("Menu").withGetter([] { return sdbus::ObjectPath("/MenuBar"); }),

        sdbus::registerMethod("ContextMenu").implementedAs([](int32_t, int32_t) {}),
        sdbus::registerMethod("Activate").implementedAs([this](int32_t, int32_t) {
            if (onActivate)
                onActivate();
        }),
        sdbus::registerMethod("SecondaryActivate").implementedAs([](int32_t, int32_t) {}),
        sdbus::registerMethod("Scroll").implementedAs([](int32_t, std::string) {}),

        sdbus::registerSignal("NewTitle"),
        sdbus::registerSignal("NewIcon"),
        sdbus::registerSignal("NewAttentionIcon"),
        sdbus::registerSignal("NewOverlayIcon"),
        sdbus::registerSignal("NewToolTip"),
        sdbus::registerSignal("NewStatus").withParameters<std::string>())
        .forInterface(SNI);
}

void Tray::registerWithWatcher()
{
    if (registered_)
        return;
    watcher_ = sdbus::createProxy(bus_, sdbus::ServiceName(WATCHER), sdbus::ObjectPath("/StatusNotifierWatcher"));
    watcher_->callMethodAsync("RegisterStatusNotifierItem")
        .onInterface(WATCHER)
        .withArguments(busName_)
        .uponReplyInvoke([this](std::optional<sdbus::Error> error) {
            if (error)
                fprintf(stderr, "[tray] no StatusNotifierWatcher on the bus (%s); the tray icon will appear once a panel starts\n",
                        error->getMessage().c_str());
            else
                registered_ = true;
        });
}

// 32x32 ARGB icon: a soft blue tile with a white play triangle. Used by hosts
// that cannot resolve the themed IconName.
std::vector<Tray::Pixmap> Tray::makeIcon()
{
    const int size = 32, ss = 4;   // 4x4 supersampling
    std::vector<uint8_t> argb(size_t(size) * size * 4);
    auto inside = [&](double x, double y, double& r, double& g, double& b, double& a) {
        const double radius = 6.0;
        const double cx = std::clamp(x, radius, size - radius), cy = std::clamp(y, radius, size - radius);
        const bool tile = std::hypot(x - cx, y - cy) <= radius;
        if (!tile) {
            a = 0;
            return;
        }
        // Triangle (12,9) (12,23) (24,16)
        const double x0 = 12, y0 = 9, x1 = 12, y1 = 23, x2 = 24, y2 = 16;
        const double d0 = (x1 - x0) * (y - y0) - (y1 - y0) * (x - x0);
        const double d1 = (x2 - x1) * (y - y1) - (y2 - y1) * (x - x1);
        const double d2 = (x0 - x2) * (y - y2) - (y0 - y2) * (x - x2);
        const bool tri = (d0 >= 0 && d1 >= 0 && d2 >= 0) || (d0 <= 0 && d1 <= 0 && d2 <= 0);
        a = 1;
        if (tri) { r = g = b = 1.0; }
        else { r = 0.23; g = 0.51; b = 0.85; }
    };
    for (int py = 0; py < size; ++py) {
        for (int px = 0; px < size; ++px) {
            double R = 0, G = 0, B = 0, A = 0;
            for (int sy = 0; sy < ss; ++sy)
                for (int sx = 0; sx < ss; ++sx) {
                    double r = 0, g = 0, b = 0, a = 0;
                    inside(px + (sx + 0.5) / ss, py + (sy + 0.5) / ss, r, g, b, a);
                    R += r * a; G += g * a; B += b * a; A += a;
                }
            const double n = ss * ss;
            uint8_t* p = &argb[(size_t(py) * size + px) * 4];
            p[0] = uint8_t(std::lround(A / n * 255));
            p[1] = uint8_t(A > 0 ? std::lround(R / A * 255) : 0);
            p[2] = uint8_t(A > 0 ? std::lround(G / A * 255) : 0);
            p[3] = uint8_t(A > 0 ? std::lround(B / A * 255) : 0);
        }
    }
    return {Pixmap(size, size, argb)};
}

// ---- com.canonical.dbusmenu -----------------------------------------------------

void Tray::exportMenu()
{
    menu_ = sdbus::createObject(bus_, sdbus::ObjectPath("/MenuBar"));
    menu_->addVTable(
        sdbus::registerProperty("Version").withGetter([] { return uint32_t(3); }),
        sdbus::registerProperty("Status").withGetter([] { return std::string("normal"); }),
        sdbus::registerProperty("TextDirection").withGetter([] { return std::string("ltr"); }),
        sdbus::registerProperty("IconThemePath").withGetter([] { return std::vector<std::string>{}; }),

        sdbus::registerMethod("GetLayout").implementedAs(
            [this](int32_t parent, int32_t depth, std::vector<std::string> names) {
                return std::tuple<uint32_t, Layout>(revision_, layoutFor(parent, depth, names));
            }),
        sdbus::registerMethod("GetGroupProperties").implementedAs(
            [this](std::vector<int32_t> ids, std::vector<std::string> names) {
                std::vector<sdbus::Struct<int32_t, Props>> out;
                for (int32_t id : ids)
                    if (const Node* n = find(id))
                        out.emplace_back(id, propsFor(*n, names));
                return out;
            }),
        sdbus::registerMethod("GetProperty").implementedAs([this](int32_t id, std::string name) {
            if (const Node* n = find(id)) {
                Props p = propsFor(*n, {name});
                auto it = p.find(name);
                if (it != p.end())
                    return it->second;
            }
            return sdbus::Variant(std::string());
        }),
        sdbus::registerMethod("Event").implementedAs(
            [this](int32_t id, std::string eventId, sdbus::Variant, uint32_t) {
                if (eventId != "clicked")
                    return;
                const Node* n = find(id);
                if (!n || !n->item || !n->item->activate)
                    return;
                auto fn = n->item->activate;   // the handler may rebuild the menu underneath us
                fn();
            }),
        sdbus::registerMethod("EventGroup").implementedAs(
            [this](std::vector<sdbus::Struct<int32_t, std::string, sdbus::Variant, uint32_t>> events) {
                std::vector<int32_t> errors;
                for (auto& e : events) {
                    const Node* n = find(std::get<0>(e));
                    if (!n) {
                        errors.push_back(std::get<0>(e));
                        continue;
                    }
                    if (std::get<1>(e) == "clicked" && n->item && n->item->activate) {
                        auto fn = n->item->activate;
                        fn();
                    }
                }
                return errors;
            }),
        sdbus::registerMethod("AboutToShow").implementedAs([](int32_t) { return false; }),
        sdbus::registerMethod("AboutToShowGroup").implementedAs([](std::vector<int32_t>) {
            return std::tuple<std::vector<int32_t>, std::vector<int32_t>>{};
        }),

        sdbus::registerSignal("ItemsPropertiesUpdated")
            .withParameters<std::vector<sdbus::Struct<int32_t, Props>>, std::vector<sdbus::Struct<int32_t, std::vector<std::string>>>>(),
        sdbus::registerSignal("LayoutUpdated").withParameters<uint32_t, int32_t>(),
        sdbus::registerSignal("ItemActivationRequested").withParameters<int32_t, uint32_t>())
        .forInterface(DBUSMENU);

    setMenu({});
}

void Tray::setMenu(std::vector<MenuItem> items)
{
    items_ = std::move(items);
    nodes_.clear();
    nodes_.push_back({0, nullptr, {}});
    flatten(items_, 0);
    ++revision_;
    if (menu_)
        menu_->emitSignal("LayoutUpdated").onInterface(DBUSMENU).withArguments(revision_, int32_t(0));
}

void Tray::flatten(const std::vector<MenuItem>& items, size_t parentIndex)
{
    for (const MenuItem& item : items) {
        const int32_t id = int32_t(nodes_.size());
        nodes_.push_back({id, &item, {}});   // may reallocate: index into nodes_ afterwards, never hold a reference
        nodes_[parentIndex].children.push_back(id);
        flatten(item.children, size_t(id));
    }
}

const Tray::Node* Tray::find(int32_t id) const
{
    return (id >= 0 && size_t(id) < nodes_.size()) ? &nodes_[id] : nullptr;
}

Tray::Props Tray::propsFor(const Node& node, const std::vector<std::string>& names) const
{
    Props all;
    if (!node.item) {
        all["children-display"] = sdbus::Variant(std::string("submenu"));
    } else {
        const MenuItem& m = *node.item;
        if (m.separator) {
            all["type"] = sdbus::Variant(std::string("separator"));
        } else {
            all["label"] = sdbus::Variant(m.label);
            if (!m.enabled)
                all["enabled"] = sdbus::Variant(false);
            if (m.checkable) {
                all["toggle-type"] = sdbus::Variant(std::string("checkmark"));
                all["toggle-state"] = sdbus::Variant(int32_t(m.checked ? 1 : 0));
            }
            if (!m.children.empty())
                all["children-display"] = sdbus::Variant(std::string("submenu"));
        }
    }
    if (names.empty())
        return all;
    Props filtered;
    for (const std::string& n : names)
        if (auto it = all.find(n); it != all.end())
            filtered[n] = it->second;
    return filtered;
}

Tray::Layout Tray::layoutFor(int32_t id, int32_t depth, const std::vector<std::string>& names) const
{
    const Node* node = find(id);
    if (!node)
        return Layout(id, Props{}, std::vector<sdbus::Variant>{});
    std::vector<sdbus::Variant> children;
    if (depth != 0)
        for (int32_t child : node->children)
            children.emplace_back(layoutFor(child, depth < 0 ? depth : depth - 1, names));
    return Layout(id, propsFor(*node, names), std::move(children));
}

} // namespace vw
