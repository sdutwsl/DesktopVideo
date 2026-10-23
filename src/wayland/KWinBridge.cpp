#include "KWinBridge.h"
#include "kwin_coverage_script.h"   // generated: KWIN_COVERAGE_SCRIPT

#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>

namespace vw {

static const char* KWIN = "org.kde.KWin";
static const char* PLUGIN = "videowallpaper";

KWinBridge::KWinBridge(sdbus::IConnection& bus) : bus_(bus)
{
    const char* runtime = getenv("XDG_RUNTIME_DIR");
    scriptPath_ = std::string(runtime && *runtime ? runtime : "/tmp") + "/videowallpaper-kwin-" + std::to_string(getpid()) + ".js";

    bool hasKwin = false;
    try {
        auto dbus = sdbus::createProxy(bus_, "org.freedesktop.DBus", "/org/freedesktop/DBus");
        dbus->callMethod("NameHasOwner").onInterface("org.freedesktop.DBus").withArguments(std::string(KWIN)).storeResultsTo(hasKwin);
    } catch (const sdbus::Error&) {
    }
    if (hasKwin)
        load();

    // KWin restarts drop every loaded script; put ours back.
    ownerMatch_ = bus_.addMatch(
        "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',"
        "member='NameOwnerChanged',arg0='" + std::string(KWIN) + "'",
        [this](sdbus::Message& msg) {
            std::string name, oldOwner, newOwner;
            msg >> name >> oldOwner >> newOwner;
            loaded_ = false;
            if (!newOwner.empty())
                load();
        });
}

KWinBridge::~KWinBridge()
{
    ownerMatch_.reset();
    unload();
}

void KWinBridge::load()
{
    {
        std::ofstream out(scriptPath_, std::ios::trunc);
        if (!out) {
            fprintf(stderr, "[kwin] cannot write %s\n", scriptPath_.c_str());
            return;
        }
        out << KWIN_COVERAGE_SCRIPT;
    }
    try {
        scripting_ = sdbus::createProxy(bus_, KWIN, "/Scripting");
        bool unloaded = false;   // leftover from a previous crashed instance
        scripting_->callMethod("unloadScript").onInterface("org.kde.kwin.Scripting").withArguments(std::string(PLUGIN)).storeResultsTo(unloaded);
        int32_t id = -1;
        scripting_->callMethod("loadScript").onInterface("org.kde.kwin.Scripting").withArguments(scriptPath_, std::string(PLUGIN)).storeResultsTo(id);
        if (id < 0) {
            fprintf(stderr, "[kwin] KWin refused to load the coverage script\n");
            return;
        }
        auto script = sdbus::createProxy(bus_, KWIN, "/Scripting/Script" + std::to_string(id));
        script->callMethod("run").onInterface("org.kde.kwin.Script");
        loaded_ = true;
        fprintf(stderr, "[kwin] window coverage script loaded into KWin\n");
    } catch (const sdbus::Error& e) {
        fprintf(stderr, "[kwin] scripting unavailable: %s\n", e.getMessage().c_str());
    }
}

void KWinBridge::unload()
{
    if (scripting_) {
        try {
            bool ok = false;
            scripting_->callMethod("unloadScript").onInterface("org.kde.kwin.Scripting").withArguments(std::string(PLUGIN)).storeResultsTo(ok);
        } catch (const sdbus::Error&) {
        }
    }
    loaded_ = false;
    unlink(scriptPath_.c_str());
}

} // namespace vw
