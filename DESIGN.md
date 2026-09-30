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
- **DONE.** `.desktop` parsing: `FreeDesktopIcon` (`src/FreeDesktopIcon.{h,cpp}`)
  now parses the `[Desktop Entry]` group (Type=Application/Link, Name,
  Comment, Exec with field-code stripping, Icon with a best-effort
  pixmap/icon-theme lookup, Hidden/NoDisplay, and the `X-Idesk-Width`/
  `X-Idesk-Height` vendor keys) and populates itself as a `Table` using
  the same keys `DesktopIconConfig` already reads from `.lnk` files.
  The fix needed was smaller than first thought: `DesktopIconConfig`'s
  existing constructor already matched the call site neagix had
  commented out in `DesktopConfig.cpp` — the only real blocker was
  `FreeDesktopIcon` inheriting `Table` *privately*, which made it
  unusable as a `Table&` from outside. No new constructor was needed,
  just `public Table` and a real parser body. Verified with a small
  standalone harness against 4 fixtures (normal app, `NoDisplay=true`,
  unsupported `Type=Directory`, missing file) — all behave as designed.
  Still open: full Icon Theme Specification resolution (today's lookup
  is a short, best-effort path list, not real theme/index.theme
  resolution) and the `~/Desktop` directory merge + plain-file MIME
  icons described below, which are separate, not-yet-started work.
- **DONE.** MIME-type icons for plain files: `GenericFileIcon`
  (`src/GenericFileIcon.{h,cpp}`) turns any plain file, folder, or
  symlink in the XDG Desktop dir into an icon — the same thing
  GNOME/KDE/XFCE already do. Uses GIO's `g_file_query_info()` with
  `G_FILE_ATTRIBUTE_STANDARD_ICON` to get the correct themed icon
  name(s) for the file's real MIME/content type (folders, symlinks —
  followed to their target — and regular files are all told apart
  correctly by GIO itself, no MIME database of our own needed). Those
  theme names go through the same best-effort path lookup as
  `.desktop` icons (`resolveIconThemeName()`, moved to `Misc.h`/`.cpp`
  so both classes share it). Activating the icon runs `xdg-open <path>`
  (shell-quoted), which opens the file with the user's default app or
  the folder in their default file manager.

  New `Desktop.AutoIcons` option in ideskrc's `Config` table (default
  `true`) toggles this off for anyone who wants `~/Desktop` to stay
  curated like `~/.config/idesktop/` always is. Only applies to the XDG
  Desktop dir scan — `~/.config/idesktop/` never auto-iconizes plain
  files, regardless of this setting.

  **Dependency change:** `GenericFileIcon` needs `gio-2.0`, which used
  to only be linked in under `--enable-svg` (transitively, via
  gdk-pixbuf). Made `gio-2.0` an unconditional `PKG_CHECK_MODULES`
  requirement in `configure.ac`, same tier as imlib2/Xft/Xt, since MIME
  icon resolution is now a core feature, not an SVG-only extra. GLib is
  near-universal on any Linux desktop, so the practical footprint added
  is small — but it's a real, honest change to the "minimal build
  deps" story from earlier in this file.

  **Found by testing against a real filesystem, not just synthetic
  fixtures:** the first version of `resolveIconThemeName()` only
  searched the `hicolor` theme (the spec-mandated fallback), which
  turned out to be nearly empty on a real Ubuntu install — the actual
  icon files live in `Adwaita` (the real default theme for GNOME/Ubuntu
  and many others). Added Adwaita as additional candidate paths
  alongside hicolor. Verified end-to-end with a headless harness
  (same technique as the `~/Desktop` merge commit) against real files
  on disk: a folder resolved to Adwaita's `inode-directory.svg`, a
  `.txt` file to `text-x-generic.svg`, and a symlink to `/bin/ls`
  correctly followed through to `application-x-executable.svg` — GIO
  resolved the *target's* type, not the symlink itself.- **DONE.** `~/Desktop` merge: `loadIcons()` now scans the standard XDG
  Desktop directory in addition to `~/.config/idesktop/` (fallback
  `~/.idesktop/`), which keeps being read forever for existing installs
  — no silent moves, ever. The XDG directory is resolved properly (not
  hardcoded to the English name): `$XDG_DESKTOP_DIR` env var, then the
  `XDG_DESKTOP_DIR=` line in `$XDG_CONFIG_HOME/user-dirs.dirs` (with its
  literal `$HOME` token substituted), then `$HOME/Desktop` as the spec's
  own default. This matters concretely: a Spanish-locale system's
  `xdg-user-dirs` typically sets this to `~/Escritorio`, not `~/Desktop`
  — verified with both cases in a standalone harness (see below).

  Files in the XDG Desktop dir that aren't `.lnk`/`.desktop` (plain
  files/folders — the MIME-icon piece, still open, see below) are
  silently skipped rather than warned about: an ordinary `~/Desktop` is
  expected to hold plenty of unrelated files, and warning about each one
  would look like a bug. `~/.config/idesktop/` keeps warning on anything
  unrecognized, since every file there is expected to be an icon.

  **Bug found and fixed along the way:** `backgroundFile()` had a second,
  older condition — `if filename doesn't end in ".lnk", treat it as a
  background file (skip silently)` — left over from before `.desktop`
  support existed. This ran *before* the `.lnk`/`.desktop` dispatch in
  the scan loop, which meant it was silently discarding every `.desktop`
  file before that dispatch logic ever got a chance to see it — in
  *both* directories, including `~/.config/idesktop/` itself. In other
  words, the `.desktop` parser built for iDesk-NG was never actually
  reachable in a real `loadIcons()` run until this was fixed, despite
  compiling and passing its own isolated test earlier. Found only by
  testing the real end-to-end path (a small headless harness
  constructing a real `DesktopConfig`, no X display needed — its
  constructor never touches Xlib), not by compiling the parser in
  isolation. Fixed by having `backgroundFile()` only filter dotfiles and
  `~`-backup files, leaving format recognition entirely to the dispatch
  loop. One side effect had to be handled explicitly: `ideskrc` normally
  lives in the same directory as the icons
  (`~/.config/idesktop/ideskrc`), so removing the old blanket filter
  would have made it start printing a spurious "not a recognized
  desktop icon" warning on every single startup — `scanIconDirectory()`
  now takes an explicit `excludeFilename` and skips `ideskrc` by name
  instead.

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
