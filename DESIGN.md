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

## Bugs found via real hardware testing (not in the original scope, fixed along the way)

- **Caption "pseudo-transparency" showed solid black instead of the real
  wallpaper.** Every icon caption crops the desktop background image at
  its own position and paints that behind the text, to fake
  transparency without a compositor (`XImlib2Caption::draw()`). That
  crop comes from `XImlib2Background`'s `spareRoot` image, which used to
  be populated unconditionally at startup by `InitSpareRoot()`: create a
  temp `ParentRelative` window, map it, and immediately read it back
  with `imlib_create_image_from_drawable()` -- with no `XSync`/wait for
  the X server to actually realize the background first. On real
  hardware this reliably captured a blank/black image, even though
  `xprop -root _XROOTPMAP_ID` showed a perfectly valid wallpaper pixmap
  the whole time -- `GetRootPixmap()`/`Refresh()` (the same functions
  already used to react to wallpaper changes at runtime) were never
  tried first. Fixed: `XDesktopContainer::getRootImage()` now tries the
  real root pixmap via `_XROOTPMAP_ID` first, and only falls back to the
  racy temp-window snapshot when no such pixmap is published.
- **Shipped example config (`examples/dot.ideskrc`) used `FontName:
  Arial` and `Bold: true`.** Arial isn't installed on a stock Ubuntu
  system, so Xft/fontconfig silently substitutes something else, and
  combined with forced bold this looked noticeably worse than it should.
  Changed the example to `FontName: Sans` (a fontconfig alias that
  always resolves to a real installed sans font) and `Bold: false`.
- **SVG icons rendered their transparent margin as solid opaque black.**
  `createPictureFromSvg()` carefully computed `rgb[]`/`alpha[]` arrays
  with a transparency matrix, then never used them -- it built the final
  Imlib2 image straight from gdk-pixbuf's own raw buffer
  (`origPixbufRgb`), cast directly to `unsigned int *`. gdk-pixbuf's
  byte layout (R,G,B,A per byte) is not Imlib2's expected packed 32-bit
  `0xAARRGGBB`, so Imlib2 read the wrong byte as alpha, making
  transparent regions opaque. Visible symptom on real hardware: every
  icon resolved from an SVG (most of Adwaita) showed a solid black box
  around its glyph; icons resolved from PNG/XPM (loaded by Imlib2's own
  native loader, which never goes through this function) looked
  correct. Fixed by actually packing `rgb[]`/`alpha[]` into a real
  ARGB32 buffer instead of reusing gdk-pixbuf's raw bytes.
  Known pre-existing issue, not addressed here: this function (and the
  `rgb`/`alpha`/`alpha2`/`vectorPixbuf` members in general) are never
  freed in `~XImlib2Image()` -- a real but low-impact leak, since icons
  are loaded once at startup, not per-frame. Worth a follow-up pass.
- **`arrangeIcons()` placed icons off-screen (not just overlapping) once
  there were more than fit in one pass.** Found by copying ~100 real
  `.desktop` files into `~/Desktop` as a stress test: only ~40 icons
  were visible, the rest existed (clickable if you knew where to drag
  blindly) but sat at a negative X, past the left edge of the screen --
  the old column-wrap (`iconX -= 20 + maxW`) never stopped or wrapped
  back. Rewritten to compute how many icons actually fit on screen in
  one pass (`capacity = columns * rows`, using the same spacing the
  placement loop already uses) and reuse those same slots for any
  extra icons, offsetting each full "layer" by a small `+15px` X/Y
  shift -- a fanned-stack look, always on-screen, never lost. This
  matters for the layout-DB work below: capturing an icon's
  `arrangeIcons()`-assigned position as its seed would otherwise have
  permanently saved an off-screen position for anything past the
  ~40th icon.
- **An icon whose Icon= name resolved to nothing (empty string) was
  silently discarded entirely, not just shown without a picture.**
  Traced the "Unknown file format: " warnings (empty filename after the
  colon) that had shown up in every real test this session straight to
  `XIcon`'s constructor: `isRaster()`/`isSvg()` both reject an empty
  filename, hit the `else` branch, and set `valid = false` -- which
  discards the whole icon (caption included), not merely its picture,
  despite the resolver's own warning claiming "icon will be blank."
  There's even a pre-existing `// TODO: implement way to skip icon and
  not segfault` comment right there acknowledging the gap. Accounted
  for exactly the small gap (~5 icons) between "not Hidden/NoDisplay"
  and "actually visible" in the ~100-icon stress test.
  Fixing `XIcon` itself was ruled out as too risky for how it was
  found: `image->` is dereferenced unconditionally in 13+ places in
  that file with no null checks, and there's no real X session in this
  environment to exercise every one of those paths after a change.
  Fixed upstream instead, in `resolveIconThemeName()` (now shared by
  both `FreeDesktopIcon` and `GenericFileIcon`): when nothing matches
  the requested name, fall back to `image-missing` -- the actual
  freedesktop.org Icon Naming Specification name for exactly this
  situation, which real icon themes ship (found under `.../status/`,
  a candidate directory this lookup didn't search before). This keeps
  `getPictureFilename()` non-empty for any well-formed icon, so the
  `XIcon` discard path this was feeding simply never triggers.
- **`arrangeIcons()` always started top-right and grew left, ignoring
  `SnapOrigin`.** `SnapOrigin` (`TopLeft`/`TopRight`/`BottomLeft`/
  `BottomRight`) already existed and was already wired into drag-to-grid
  snapping (`XIcon`/`XIconWithShadow`'s use of `getStartSnapLeft()`/
  `getStartSnapTop()`), but `arrangeIcons()` never consulted it for the
  *initial* auto-placement -- it hardcoded starting at the top-right
  corner and growing leftward no matter what the config said. Rewrote
  the grid math (origin point + growth direction, both derived from
  `getStartSnapLeft()`/`getStartSnapTop()`) so initial placement and
  drag-snapping now agree with each other and with `SnapOrigin`. A
  `Center` origin was considered and deliberately not added here --
  centering needs a fundamentally different fill pattern (rings/spiral
  outward) rather than a corner + growth direction, so it would be a
  separate algorithm, not a small extension of this one.
- **Icon lookup never searched Yaru, Ubuntu's own default icon theme
  since 18.04, or the `categories/` subdirectory.** Found via `sudo
  find / -name "preferences-system-network*"` on real hardware: the
  bare name only existed under Yaru's `categories/` dir and as a
  differently-named `-symbolic` variant under Adwaita -- neither
  reachable by the lookup as it stood, so a real, present icon still
  fell back to `image-missing`. Restructured `resolveIconThemeName()`
  from a hand-written path list into theme x size x category
  combinations (themes tried in order: Yaru, Adwaita, hicolor;
  categories: apps, mimetypes, places, status, categories) -- easier to
  extend than the old flat list, and immediately covers this case.
  Still a best-effort search, not full Icon Theme Specification
  resolution (no theme inheritance, no index.theme parsing, no
  `-symbolic` suffix variants tried) -- see the function's own header
  comment for the current scope.
- **Icon theme lookup was hardcoded to Ubuntu-specific theme names
  (Yaru, Adwaita), which would find nothing at all on a different
  distro/DE.** Raised directly: all real testing this session happened
  on Ubuntu, the one distro at hand -- but idesk-ng's actual target
  users run arbitrary WMs (Openbox, Fluxbox, i3...) on arbitrary
  distros, often after removing GNOME/KDE/XFCE entirely, where neither
  Yaru nor even Adwaita may exist, and where even the `image-missing`
  fallback could itself fail to resolve -- silently regressing back to
  the "whole icon discarded" bug just fixed, just on a different
  distro. Rewrote `resolveIconThemeName()` to never hardcode a theme
  name again:
  1. reads the user's actually-configured theme first, if any
     (`~/.config/gtk-4.0|gtk-3.0/settings.ini`, `~/.gtkrc-2.0`,
     `~/.config/kdeglobals` -- covers GNOME, XFCE's GTK-based config,
     and KDE Plasma);
  2. discovers every theme genuinely installed under
     `/usr/share/icons`, `~/.local/share/icons`, `~/.icons` by looking
     for an `index.theme` file -- the one thing every real icon theme
     is required to have, regardless of its name (Yaru, breeze,
     Papirus, elementary, anything);
  3. tries `hicolor` last, always, as the one theme the spec
     guarantees exists.
  Verified on a sandbox with no Yaru installed: dynamic discovery found
  8 themes (Adwaita, Humanity, Humanity-Dark, LoginIcons, default,
  hicolor, ubuntu-mono-dark, ubuntu-mono-light) with zero names
  hardcoded, and `text-x-generic` still resolved correctly -- no
  regression.
