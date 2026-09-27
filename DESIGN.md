# iDesk-NG — Design Notes

This file records the architecture and migration decisions behind the
idesk -> iDesk-NG fork, so they don't only live in chat history.

## Lineage

original idesk (SourceForge, aavelar, BSD, last real source change ~0.7.5
in 2006) -> neagix/idesk fork on GitHub (BSD-3-Clause, added SVG via
librsvg, XDG config path, fixed the `fork()`/`execl()` bugs, started
but never finished `.desktop` support) -> iDesk-NG (this repo).

`fixxxer/idesk` (an earlier personal fork) predates almost all of
neagix's fixes and is not used as a base.

## Already applied

Of Debian's 5 packaging patches for idesk 0.7.5, only one was still
needed on top of neagix's tree — the rest were independently already
fixed upstream:
- NOT needed (already fixed in neagix): Xdialog->zenity example fix,
  missing `<sys/stat.h>` include, `imlib2-config`->pkg-config migration,
  missing `execl()` sentinel.
- Applied: Helmut Grohne's cross-compile fix for Xft detection
  (Debian bug #915558), ported by hand from `configure.in` to
  `configure.ac` since neagix already renamed the file.

## Target architecture

```
        iDesk-NG Core
             |
     +-------+--------+
     |                |
  X11 backend    Wayland backend
  (Xlib/XCB,      (wlr-layer-shell)
   primary,
   today's users)
```

- **X11 stays primary.** iDesk-NG's real userbase is minimalist WMs
  without built-in desktop icons: Openbox, Fluxbox, i3, dwm, IceWM,
  JWM. None of these have a serious Wayland port (Openbox's own
  maintainers have said "no plans whatsoever"; the closest thing is
  `waybox`, a from-scratch clone, still unstable). X11 will outlive
  GNOME/KDE's own X11 sessions by a long stretch here.
- **Wayland backend, when built, targets wlr-layer-shell** — this
  covers wlroots compositors (Sway, Hyprland, river, labwc) AND KDE
  Plasma Wayland (KWin implements layer-shell for third-party
  clients) AND XFCE's future Wayland session (built on wlroots via
  labwc/wayfire). One backend, three targets.
- **GNOME is explicitly out of scope.** Mutter does not implement
  wlr-layer-shell and GNOME 50 (March 2026) removed X11 sessions
  entirely, so there is no public protocol for a third-party desktop
  icon app to hook into anymore. GNOME's own desktop icons are a
  GNOME Shell extension (DING) running inside the Shell process with
  private APIs — replicating that would be a separate from-scratch
  GJS project sharing no code with this one, and isn't worth it since
  DING is itself open source and forkable/themeable by anyone who
  wants different icon behavior on GNOME.

## Desktop-environment compatibility (X11 sessions only)

GNOME, KDE, and XFCE all draw desktop icons via a component that is
separate from the actual window manager, so their native icon layer
can be turned off without breaking the WM:
- GNOME (X11 session): disable the `ding@rastersoft.com` extension;
  Mutter keeps running as the WM.
- KDE Plasma (X11 session): right-click desktop -> Configure Desktop
  -> set containment to empty; `kwin_x11` keeps running.
- XFCE: turn off xfdesktop's icon layer; `xfwm4` keeps running.

iDesk-NG then needs an XDG autostart `.desktop` entry to launch inside
these full sessions (they use XDG autostart, not `.xinitrc` like the
minimalist WMs).

## Legacy / standard icon support

`~/Desktop` (`XDG_DESKTOP_DIR`, per the freedesktop.org xdg-user-dirs
spec) is where GNOME/DING, KDE Folder View, and XFCE/xfdesktop already
store desktop icons — plain files, folders, symlinks, and `.desktop`
launchers, not a proprietary per-DE format. Plan:

- `.lnk` stays a first-class, permanently supported authoring format —
  not just "legacy". Writing a `.lnk` by hand (or via a provisioning
  script) with `X`/`Y` set remains the way to pre-seed icon positions
  for a fresh install.
- `.desktop` parsing gets finished: the `FreeDesktopIcon` class exists
  in neagix's tree but is an empty stub, and `DesktopIconConfig` is
  missing the constructor overload the (commented-out) call site in
  `DesktopConfig.cpp` expects. This is the main net-new C++ work.
- Plain files/folders in `~/Desktop` (no `.lnk`, no `.desktop`) get an
  icon resolved by MIME type via GIO (`g_content_type_guess` +
  `g_content_type_get_icon`) — already linked in for SVG support, no
  new dependency.
- Default scan location becomes `~/Desktop`, merging all three kinds
  above; `~/.config/idesktop/` (fallback `~/.ideskrc/`) keeps being
  read forever for existing installs — no silent moves, ever.

### `.lnk` -> `.desktop` field mapping (used by `--migrate-to-desktop`)

| `.lnk` field         | `.desktop` field       |
|----------------------|-------------------------|
| `Caption`            | `Name`                  |
| `CaptionTip`         | `Comment`                |
| `Command`/`Command[0]`| `Exec`                  |
| `Icon`               | `Icon`                   |
| (implicit)           | `Type=Application`       |
| `Width`, `Height`     | `X-Idesk-Width`, `X-Idesk-Height` (freedesktop `X-` vendor extension keys — ignored safely by every other reader) |
| `X`, `Y`              | NOT written into the `.desktop` (see below) |

### Position handling

Icon position never lives in the `.desktop` file. Reasons:
1. Many `.desktop` files under `~/Desktop` are copies/symlinks of a
   real system launcher (`/usr/share/applications/foo.desktop`) —
   rewriting them on every drag would mean touching files that don't
   belong to the desktop layout.
2. It keeps every `.desktop` iDesk-NG writes fully portable and clean
   for any other file manager/DE to read.

Position lives in iDesk-NG's own layout DB, keyed by icon, reusing the
existing `Database`/`Table` parser already used for `ideskrc` — no new
parsing code needed.

Two supported ways to pre-seed a position for an icon that has never
been placed by hand:
- **Path A:** author a `.lnk` with `X`/`Y` set, run
  `idesk-ng --migrate-to-desktop`. The migration reads the `.lnk`'s
  `X`/`Y` once and seeds the layout DB with it, then converts the file
  to `.desktop` as above.
- **Path B:** author a `.desktop` directly (no `.lnk` involved) with
  `X-Idesk-X` / `X-Idesk-Y` extension keys. iDesk-NG reads those keys
  only the first time it sees that icon, seeds the layout DB, and
  never touches the `.desktop` file again afterwards.

A genuinely new icon — one dragged into place by hand in a running
session, with no prior `.lnk` or seed keys — is placed where the user
drops it, same as it already works today, and same as GNOME/KDE/XFCE's
own manual icon placement.

Migration is always explicit (`--migrate-to-desktop`), never automatic
or silent — existing `~/.config/idesktop/` installs keep working
untouched unless the user opts in.
