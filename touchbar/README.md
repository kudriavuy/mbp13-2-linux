# Touch Bar (T1) settings

On top of `appletb-t1` (`hid-appletb-kbd`): media keys by default, F1–F12 with
Fn, Esc always (driver defaults), plus:

- `hid-appletb-kbd.conf` → `/etc/modprobe.d/`: dim after 30 s, off 30 s later.
- Keys per program: the KWin script `mbp-touchbar-fnapps` starts the user
  unit `mbp-touchbar-app@<window class>`, which runs `mbp-touchbar app …`: the
  class goes to `/run/tiny-dfr/app` (tiny-dfr, configuration 2, picks the key
  set) and the same rules choose F1–F12 or media keys for the driver's `mode`
  (configuration 1; the udev rule `90-mbp-touchbar.rules` lets `wheel` write
  it). Fn still swaps while held.
  The rules are `[[Apps]]` / `[AppSets]` at the end of
  `/etc/tiny-dfr/config.toml` (copy in `tiny-dfr/config.toml`): `Set = "fn"`,
  `"strip"` or a set name. Edit it with sudo; tiny-dfr reloads it on save (a
  mistake: the whole file is ignored, `journalctl -u tiny-dfr` says why).
  `mbp-touchbar classes` lists the classes of open windows,
  `mbp-touchbar which <class>…` shows what each gets. Without `[[Apps]]` in the
  config the old terminal/editor list gives F1–F12.
- `mbp-touchbar status`: mode, brightness, timeouts, access, lid.

Install `sudo ./install.sh touchbar`, then as the user
`./touchbar/enable-kwin-script.sh`; undo `sudo ./install.sh touchbar-remove`
(as the user first `./touchbar/enable-kwin-script.sh off`).
