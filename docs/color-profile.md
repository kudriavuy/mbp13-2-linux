# Colour profile of the built-in panel

The MacBookPro13,2 panel is a wide-gamut (Display P3) panel. macOS ships a
calibrated ICC profile for it ("Color LCD"); it is Apple's file and is not
included here. Use the one from your own macOS:

1. In macOS: System Preferences → Displays → Colour → "Color LCD" → Open
   Profile, then File → Save as... onto a USB stick, or copy it from
   `/Library/ColorSync/Profiles/Displays/` (the file name contains the
   display's ID).
2. In Linux: `install -Dm644 <file>.icc ~/.local/share/icc/color-lcd.icc`.
3. KDE Plasma 6: System Settings → Display → Colour profile → ICC profile →
   choose the file, or
   `kscreen-doctor output.eDP-1.iccprofile.$HOME/.local/share/icc/color-lcd.icc`
   (back: `kscreen-doctor output.eDP-1.colorProfileSource.EDID` or `.sRGB`).

Keep the profile on your machine: it is per panel and Apple's.
HDR / wide colour gamut output is reported as `incapable` for this panel.
