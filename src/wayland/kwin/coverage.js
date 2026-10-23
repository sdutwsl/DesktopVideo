// KWin helper for videowallpaper. KWin does not expose window states to ordinary
// Wayland clients, so this script runs inside KWin and reports, per output,
// whether a maximized or fullscreen window is covering the wallpaper.
// Loaded through org.kde.KWin /Scripting by the daemon; unloaded on exit.

const SERVICE = "org.videowallpaper.Daemon";
const PATH = "/org/videowallpaper/Daemon";
const IFACE = "org.videowallpaper.Daemon";

const reported = {};   // output name -> last covered state we sent

function onCurrentDesktop(w) {
    if (w.onAllDesktops) return true;
    const cur = workspace.currentDesktop;
    return w.desktops.some(d => d.id === cur.id);
}

// Output name if this window hides the wallpaper on it, else null.
function covers(w) {
    if (!(w.normalWindow || w.dialog) || w.minimized || !w.output || !onCurrentDesktop(w)) return null;
    if (w.fullScreen) return w.output.name;
    // A maximized window is exactly the size of its maximize area (output minus panels).
    // Comparing sizes rather than the maximized flag also catches windows resized to fill it.
    const g = w.frameGeometry, area = workspace.clientArea(KWin.MaximizeArea, w);
    if (Math.abs(g.width - area.width) <= 2 && Math.abs(g.height - area.height) <= 2) return w.output.name;
    return null;
}

function evaluate() {
    const now = {};
    workspace.screens.forEach(o => { now[o.name] = false; });
    workspace.windowList().forEach(w => { const n = covers(w); if (n !== null) now[n] = true; });
    for (const name in now) {
        if (reported[name] !== now[name]) {
            reported[name] = now[name];
            callDBus(SERVICE, PATH, IFACE, "SetCovered", name, now[name]);
        }
    }
}

function track(w) {
    ["fullScreenChanged", "minimizedChanged", "frameGeometryChanged", "outputChanged", "desktopsChanged"]
        .forEach(sig => { if (w[sig]) w[sig].connect(evaluate); });
}

workspace.windowList().forEach(track);
workspace.windowAdded.connect(w => { track(w); evaluate(); });
workspace.windowRemoved.connect(evaluate);
workspace.currentDesktopChanged.connect(evaluate);
evaluate();
