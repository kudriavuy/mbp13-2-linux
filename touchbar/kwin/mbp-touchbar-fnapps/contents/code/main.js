// Touch Bar keys for the active program: reports the active window's class
// to mbp-touchbar (user unit mbp-touchbar-app@<class>), which hands it to
// tiny-dfr (/run/tiny-dfr/app; [[Apps]] in /etc/tiny-dfr/config.toml choose
// the key set) and sets F1-F12 or media keys in Touch Bar configuration 1.
// The window's title goes to mbp-touchbar-bridge (org.mbp.TouchBar
// SetTitle) for rules by web site (MatchTitle); it is kept only in /run.
// The program list lives in that config.toml, not here.

let last = "";
let watched = null;

function sendTitle() {
    const w = workspace.activeWindow;
    const title = w ? String(w.caption || "") : "";
    callDBus("org.mbp.TouchBar", "/org/mbp/TouchBar", "org.mbp.TouchBar", "SetTitle", title);
}

function update(w) {
    // follow the title of the active window only
    if (watched)
        watched.captionChanged.disconnect(sendTitle);
    watched = w || null;
    if (watched)
        watched.captionChanged.connect(sendTitle);
    sendTitle();

    // unit instance names allow letters, digits, ".", "_" and "-"
    let cls = w ? String(w.resourceClass || w.resourceName || "") : "";
    cls = cls.replace(/[^A-Za-z0-9._-]/g, "_") || "none";
    if (cls === last)
        return;
    last = cls;
    callDBus("org.freedesktop.systemd1", "/org/freedesktop/systemd1", "org.freedesktop.systemd1.Manager",
        "StartUnit", "mbp-touchbar-app@" + cls + ".service", "replace");
}

workspace.windowActivated.connect(update);
update(workspace.activeWindow);
