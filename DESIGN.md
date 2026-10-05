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
- **The configured-theme file check alone wasn't enough, even on
  Ubuntu.** Found immediately on real hardware: `~/.config/gtk-3.0/
  settings.ini` doesn't exist at all on an Ubuntu install that's only
  ever been used through Openbox. That file is just a *copy*, written
  out by `gnome-settings-daemon`'s xsettings sync when a full GNOME
  session runs -- which never happens for this project's actual
  audience. The real source of truth on any GNOME-based system
  (Ubuntu included) is GSettings/dconf
  (`org.gnome.desktop.interface icon-theme`), set from first boot
  regardless of whether GNOME Shell has ever actually run. Added
  `getGSettingsIconThemeName()`, tried first via GIO (already linked,
  no new dependency) -- checks the schema actually exists before
  querying it, so it fails quietly rather than warning on a system
  that never had GNOME's schemas installed at all. Verified: no
  warning or crash on this sandbox, which has no GNOME schemas either.
- **The in-process GSettings approach above crashed on real hardware --
  reproducible only *outside* gdb, the classic signature of a race gdb's
  own slowdown masks.** `g_settings_new()` pulls in a GDBus connection to
  dconf, which spins up GLib's own worker threads. idesk-ng's
  pre-existing, 20-year-old command-launch code
  (`XDesktopContainer.cpp`) uses raw `fork()+execl()` to run whatever
  command an icon is configured with -- and `fork()` in a process that
  has any other threads running is a well-known source of exactly this
  kind of hard-to-reproduce crash: only the forking thread survives into
  the child, so if a GDBus worker thread held a lock at that instant,
  the child can deadlock or corrupt state.
  Fixed by moving the GSettings read into a separate process entirely:
  `getGSettingsIconThemeName()` now shells out to the `gsettings`
  command-line tool via `popen()` instead of linking GSettings/GDBus
  directly into idesk-ng. All of GSettings' threading machinery now runs
  inside that separate process, so idesk-ng's own process never becomes
  multi-threaded in the first place, and its existing fork()-based
  command execution stays exactly as safe as it always was. Verified:
  completes in well under 0.1s, no hang, correct result either way
  (schema present or not).
- **`XImlib2Background::spareRoot` was never initialized, and the root-
  pixmap-race fix (several commits back) exposed it as a real,
  reproducible segfault.** Caught with `gdb -ex run -ex bt`, full
  backtrace: `imlib_free_image()` crashed inside
  `XImlib2Background::Refresh()`, called from
  `XDesktopContainer::getRootImage()`. `Refresh()` has always had a
  `if (spareRoot) { ...; imlib_free_image(); }` guard before
  overwriting it -- correct IF `spareRoot` starts as `NULL`, which nothing
  in the constructor's initializer list ever set it to. This was a
  latent landmine in the original code (never triggered historically,
  because `Refresh()` was only ever called *after* `InitSpareRoot()` had
  already assigned `spareRoot` a real value), and the earlier fix that
  made `getRootImage()` try `Refresh()` *first* on a freshly-constructed
  object stepped directly on it: the `if(spareRoot)` check read
  uninitialized stack/heap garbage, happened to be non-zero, and
  `imlib_free_image()` was handed a bogus pointer.
  This also explains why it reproduced only *outside* gdb at first --
  a garbage read being non-zero is exactly the kind of thing that can
  vary with process layout, which a debugger attaching can shift
  without changing anything in the code.
  Fixed by adding `spareRoot(NULL)` to the constructor's initializer
  list, same as every other pointer member already there.

## Point 3, Path A: `--migrate-to-desktop` -- DONE

`idesk-ng --migrate-to-desktop` (`Migrate.{h,cpp}`, wired into
`App::processArguments()`) converts every `.lnk` in
`~/.config/idesktop/` to a `.desktop` file in the same directory, per
the field mapping already designed: `Caption`->`Name`,
`ToolTip.Caption`->`Comment`, `Command` (or `Command[0]` if it's an
action array)->`Exec`, `Icon`->`Icon`, `Width`/`Height`->
`X-Idesk-Width`/`X-Idesk-Height`. `X`/`Y` are deliberately never
written into the `.desktop` -- they go straight to the new layout DB
instead (`IconLayout.{h,cpp}`: `~/.config/idesktop/layout.db`, same
`Database`/`Table` grammar as everything else, one `table <absolute
path> ... end` block per icon -- confirmed safe even for paths with
spaces, since the parser reads a table's title with `getline()`, not a
whitespace token). The original `.lnk` is renamed to `<name>.lnk.bak`
rather than deleted. One-shot CLI command, runs and exits (0 on full
success, 1 if any file had a real error) -- never run automatically.

Safety behavior, all verified with a real two-`.lnk` fixture (one
using the plain `Command` field, one using `ToolTip.Caption`; also
deliberately included a decoy `CaptionTip` key in the other file to
confirm the mapping doesn't false-match on a similarly-named field --
it correctly produced no `Comment=` line for that one):
- Re-running after a successful migration finds no `.lnk` files left
  (already renamed to `.bak`) -- 0 migrated, 0 errors, idempotent.
- If a same-named `.desktop` already exists, the `.lnk` is skipped
  entirely (not touched, not renamed) rather than overwriting
  something -- confirmed the pre-existing `.desktop` stays byte-for-
  byte unchanged and the `.lnk` stays in place for the user to resolve
  by hand.

**Still open, deliberately not part of this piece:** `loadIcons()`
doesn't read `layout.db` yet, so a migrated icon's seeded position
isn't actually applied on a normal run -- it still lands wherever
`arrangeIcons()` puts it, same as any fresh icon. Wiring that read
(plus capturing `arrangeIcons()`-assigned positions back into the
layout DB for icons that had none) is the next piece of Point 3.

## Point 3, full read/write loop -- DONE

Closes the loop the previous section left open:

- **`DesktopIconConfig` now knows its own origin.** New
  `IconOrigin` enum (`ORIGIN_LNK` / `ORIGIN_LAYOUT_DB`), defaults to
  `ORIGIN_LNK`; `DesktopConfig::scanIconDirectory()` sets it explicitly
  at all three construction sites (`.lnk`, `.desktop`, plain file).
  `saveIcon()` branches on it: `ORIGIN_LNK` keeps the existing
  behavior (rewrite the `.lnk` itself) untouched; `ORIGIN_LAYOUT_DB`
  calls `seedLayoutPosition()` instead and never touches the
  `.desktop`/file at all.
- **Read side:** before constructing a `.desktop` or plain-file icon,
  `scanIconDirectory()` now calls `getLayoutPosition(path, x, y)`; if
  found, `X`/`Y` are injected into the icon's `Table` before
  construction, so the icon comes up at its saved spot and
  `arrangeIcons()` (which only acts on `X==0 && Y==0`) leaves it alone.
- **Write-back side:** `XDesktopContainer::arrangeIcons()`, right
  after assigning a fresh slot to an icon that had none, now checks
  (via the new `XIcon::getIconConfig()` getter) whether that icon is
  `ORIGIN_LAYOUT_DB`, and if so seeds the layout DB with the position
  it was just given -- so the *next* run finds it there instead of
  re-arranging from scratch. `.lnk` icons are untouched here, exactly
  as before.
- `layout.db` added to the same "don't even warn about this" exclusion
  `ideskrc` already had in `scanIconDirectory()` -- it lives in the
  same directory as the icons.

Verified end-to-end with a real two-file fixture and a real previously
-written `layout.db` (not mocked): a `.desktop` with a pre-seeded
position loaded at exactly that position; a fresh `.desktop` with none
loaded at (0,0) as expected (the in-X11 `arrangeIcons()` placement
itself needs a real display, verify on real hardware); calling
`saveIcon()` on an `ORIGIN_LAYOUT_DB` icon correctly wrote to
`layout.db` and not the `.desktop` file; and, running the test a
second time as a fully separate process, the icon written by the first
run's `saveIcon()` call loaded back at that exact saved position --
confirming the full round-trip survives a real process restart, not
just in-memory state.

**What's still open, by design, not a bug:** position only survives a
file *rename* for `.lnk` (unaffected) -- a `.desktop` or plain file
that gets renamed is a new path to the layout DB and starts fresh,
same as DESIGN.md's "Legacy / standard icon support" section already
called out as an accepted trade-off matching GNOME/KDE/XFCE's own
behavior.

## Point 3, Path B: `X-Idesk-X`/`X-Idesk-Y` -- DONE

Narrow but real use case: provisioning a machine (or a dotfiles repo)
with `.desktop` files that should appear at a specific position the
very first time idesk-ng ever sees them, without needing to run
`--migrate-to-desktop` or drag anything by hand first.

`FreeDesktopIcon` now parses `X-Idesk-X`/`X-Idesk-Y` alongside the
existing `X-Idesk-Width`/`Height`, but deliberately exposes them under
their own key names rather than as `"X"`/`"Y"` directly -- if it set
those unconditionally, *every* load of the file would re-apply them,
overriding anywhere the user had since dragged the icon to.
`DesktopConfig::scanIconDirectory()` decides whether to apply them: only
when `getLayoutPosition()` finds nothing for this path yet (first
sighting). When applied, the position is written into the layout DB in
that same moment, so this branch can never fire again for this icon --
from then on it behaves exactly like any other saved position,
including being overridden by a drag.

Verified end-to-end: a `.desktop` with `X-Idesk-X=555`/`X-Idesk-Y=222`
and no prior layout DB entry loaded at exactly that position on first
sight, and seeded the layout DB with it. Then, with the `.desktop`'s
own `X-Idesk-X`/`Y` values changed to something else entirely, a second
run still came back at the original 555,222 -- confirming the layout
DB, once seeded, wins over the file every time afterward.

## `XImlib2Image` memory leak -- fixed

Every icon leaked its Imlib2/gdk-pixbuf resources: `~XImlib2Image()` was
completely empty, and -- same bug class already fixed in
`XImlib2Background::spareRoot` -- `rgb`/`alpha`/`alpha2`/`image`/
`vectorPixbuf` were never initialized in the constructor either, so a
naive "just add the frees" fix would have read garbage for the common
case (a plain raster icon never touches `rgb`/`alpha`/`alpha2` at all)
instead of correctly doing nothing.

Fixed both at once: all five zero-initialized in the constructor, then
freed correctly in the destructor, each with the right deallocator
(`delete[]` for the `rgb`/`alpha`/`alpha2` arrays,
`imlib_context_set_image()` + `imlib_free_image()` for `image` -- same
idiom already used elsewhere in the codebase -- and `g_object_unref()`
for `vectorPixbuf`, a GObject, not a raw pointer). `image = imlib_load_
image(...)` turned out to be set on the *raster* path too (not just
SVG), so this fix benefits every icon, not only SVG-sourced ones.

`argbData` (the ARGB32 buffer built for the SVG alpha fix a few
commits back) got promoted from a local variable to a real class
member for this: `imlib_create_image_using_data()` does not copy or
take ownership of the buffer it's given, so it has to outlive `image`
and can only be freed *after* `imlib_free_image()` -- never before,
and never automatically.

Not verified on real hardware in this pass (needs an actual X11
session with SVG and raster icons loaded, then idesk-ng exited
cleanly, to check for leftover memory) -- compiles clean, and the
logic was traced carefully against the exact allocation sites, but
this one is worth confirming on the VM when there's a convenient
moment (e.g. `valgrind --leak-check=full` or just watching RSS across
a long run).

## Three more real leaks, found by `valgrind --leak-check=full` on real hardware

Running valgrind against a live idesk-ng session (editing an existing
icon to add `X-Idesk-X`/`X-Idesk-Y`, confirming Path B, in the same
pass) surfaced 7,904 bytes "definitely lost" in 20 blocks, on top of
the `XImlib2Image` fix above. Most of the report (librsvg/pango/
fontconfig frames shown as `???`, no symbols) is library-internal and
out of scope -- but three stacks pointed straight into our own code:

- **`DesktopConfig::scanIconDirectory()` leaked 2 `scandir()` entries
  per directory scanned** (48 bytes x 2, twice -- once per
  `scanIconDirectory()` call). `free(files[i])` sat *inside* the
  `if (!backgroundFile(...))` block, so it only ran when an entry
  wasn't skipped -- every skipped entry (`.`, `..`, dotfiles, `~`
  backups) leaked its `scandir()` allocation. Fixed with an `else`
  branch that frees it either way.
- **`XImlib2Caption::renderFont2Imlib()` leaked a `GC` and a
  `Pixmap`** (640 bytes for the GC alone) -- `tempGc`
  (`XCreateGC(..., shapeMask, ...)`) and `shapeMask` itself
  (`XCreatePixmap(...)`) are purely local/temporary, used only to
  build the window's shape mask, but neither was ever freed. Added
  `XFreeGC()`/`XFreePixmap()` right after their last use.
- **`XftFontOpen()` without a matching `XftFontClose()`, in both
  `XImlib2Caption` and `XImlib2ToolTip`** (2,725 bytes each,
  confirming something already suspected earlier in this project but
  never actually fixed). `XImlib2Caption`'s destructor already cleaned
  up several things but not the font -- added one line. `XImlib2ToolTip`'s
  destructor was completely empty: its `Tooltip` struct holds a
  `window`, a `gc`, *and* a `font`, and none of the three were ever
  released. Fixed all three there, not just the font valgrind happened
  to flag.

Verified: the `scanIconDirectory()` fix was re-run through the
existing headless harness afterward -- same output as before, no
crash, no double-free (each `scandir()` entry now goes through exactly
one of three mutually exclusive `free()` sites: an early `continue`,
the processed-icon path, or the new `else`). The two X11-specific
fixes (`XFreeGC`/`XFreePixmap`/`XftFontClose`) can't be exercised
without a real X session -- traced carefully against valgrind's exact
allocation sites and written using the same deallocator idioms already
used elsewhere in this codebase, but worth a second valgrind pass on
the VM to confirm the "definitely lost" count actually drops.

## Signal handling -- destructors never ran on Ctrl+C/kill (found by a second valgrind pass)

A second valgrind run confirmed the `scanIconDirectory()` and `GC`/
`Pixmap` fixes above (definitely-lost total dropped by exactly 736
bytes / 8 blocks, matching their sizes) -- but the `XftFontOpen`/
`XftFontClose` fix showed *zero* improvement, identical 2,725 bytes in
both `XImlib2Caption` and `XImlib2ToolTip`, as if it had never been
applied.

Root cause: `signalhandler()` (`App.cpp`) called `_exit(1)` directly
for SIGINT and SIGTERM -- the exact signals Ctrl+C and `kill`/`pkill`
send, i.e. literally how idesk-ng has been stopped this entire testing
session. `_exit()` skips every C++ destructor in the process. The font
fix was correct; it just never got the chance to run, and neither does
*any* destructor-based cleanup in this codebase when stopped the
normal way -- including the `XImlib2Image` and `XImlib2Background::
spareRoot` fixes from earlier in this file.

Fixed properly rather than patched around:
- `signalhandler()` now only sets `volatile sig_atomic_t
  quitRequested` for SIGINT/SIGTERM (a plain flag write is
  async-signal-safe; calling complex cleanup -- malloc/free, X11 calls
  -- directly from a signal handler is not, and risks deadlock or
  corruption). SIGSEGV/SIGFPE and other signals still `_exit(1)`
  immediately -- those represent an actual crash, where process state
  may already be corrupted and attempting more code (destructors
  included) is itself risky.
- `XDesktopContainer::eventLoop()`'s main loop now checks
  `quitRequested` at the top of every iteration and breaks out
  cleanly. The tight-loop case (a background-rotation timer active)
  already iterates fast enough that this is essentially immediate. The
  common case -- no timer (`Background.Delay: 0` in every example
  config in this repo) and nothing pending -- used to call the
  blocking `XNextEvent()` directly, which could wait indefinitely with
  no X activity at all; replaced with `select()` on the X connection's
  own fd with a 1-second timeout, so the loop wakes up and checks
  `quitRequested` regularly regardless of whether anything is actually
  happening on screen.
- `Application::startIdesk()` now explicitly `delete container;` and
  `exit(0)` once `run()` returns (which only happens after `eventLoop()`
  breaks out gracefully) -- the same explicit-delete pattern
  `restartIdesk()` already used. This is what actually runs every
  destructor in the icon/container object graph.

Not yet re-verified with a third valgrind pass (needs stopping
idesk-ng with Ctrl+C specifically, not a plain `kill -9`, which still
bypasses even this -- SIGKILL cannot be caught by any process) -- next
thing to confirm on the VM.

**Follow-up, confirmed with a temporary debug build on real hardware:**
a plain (non-valgrind) run with debug prints at every link of the
chain -- `signalhandler()`, `eventLoop()`'s `quitRequested` check,
`Application::startIdesk()` past `run()`, and `~XIcon()` -- showed the
entire graceful-shutdown mechanism working exactly as designed: all
four checkpoints fired in order, `~XIcon()` ran once per icon with
`captionOn=1` each time (so `delete caption` -> `~XImlib2Caption()`
does run, including the font-close fix). The debug build was discarded
afterward, not kept in history.

This surfaced one more real, separate bug while tracing the chain:
`XImlib2Image` holds a `tooltip` member (`XImlib2ToolTip *`, allocated
with `new` in `createToolTip()`, called unconditionally from
`XIcon::createIcon()` unless `createWindow()` fails first) that
`~XImlib2Image()` never deleted -- same uninitialized-pointer-adjacent
bug class as everything else in this section, just not caught in the
first valgrind pass since it's a separate object graph branch from
`rgb`/`alpha`/`image`/`vectorPixbuf`. This is exactly why the
`XImlib2ToolTip::createFont()` leak specifically kept showing up
unchanged across multiple valgrind runs even once the signal-handling
fix was confirmed working end-to-end for everything else. Fixed:
`tooltip` zero-initialized in the constructor and deleted (guarded) in
the destructor, same pattern as every other member here.

Not yet re-verified with valgrind -- next thing to confirm on the VM,
this time stopped with Ctrl+C on the bare binary (not valgrind) first
to keep variables isolated, since valgrind's own signal handling may
behave differently around blocking/interruptible syscalls like
`select()` -- worth a comparison if the counts still don't fully
match expectations.

**Resolution, traced with temporary debug builds across several
rounds on real hardware (all discarded, not kept in history):**
stopping under valgrind with `kill -TERM` (instead of a terminal
Ctrl+C, to rule out valgrind-specific SIGINT/TTY interaction)
confirmed the signal → `quitRequested` → `eventLoop()` break chain all
fires correctly -- but the process then segfaulted (SIGSEGV) partway
through shutdown, before `valgrind`'s leak analysis could run on a
fully torn-down heap.

Root cause: `XDesktopContainer::eventLoop()`'s post-loop cleanup
(`sn_launcher_context_unref(sn_context)` and a sibling
`sn_display_unref` call) had been *completely dead code for the
project's entire history* -- the loop used to be genuinely infinite,
only ever broken via `_exit()`, which skips everything after it,
cleanup block included. This is the first time in the project's
history any code ever reached that point. `sn_context` starts `NULL`
and is only assigned when an app is actually launched via startup
notification during the session; with none launched, the first real
execution immediately dereferenced `NULL`. The sibling `sn_display`
line right below was already correctly guarded (`if (sn_display)`);
`sn_context` was not -- classic asymmetry between two adjacent,
near-identical cleanup calls. Fixed by adding the matching guard.

With that crash gone, the process could finally complete a full clean
shutdown for the first time ever -- which let valgrind trace much
further than any previous run and surface several *more* real,
previously-unreachable leaks in the process (the crash had always cut
execution short before valgrind could even see this code):

- `XImlib2ToolTip`: `fontDrawHandle` (`XftDrawCreate()` in
  `createWindow()`) was missed by the earlier tooltip destructor fix --
  same pattern, just a member not yet known about at the time. Also
  zero-initialized `tooltip.window`/`gc`/`font` in the constructor
  (previously only guarded in the destructor; these are actually
  assigned later, by the owner's separate `createFont()`/`createWindow()`
  calls -- see `XImlib2Image::createToolTip()` -- not by this
  constructor itself).
- `DesktopConfig::~DesktopConfig()` only ever freed `common` -- never
  any of the `DesktopIconConfig` objects `loadIcons()`/
  `scanIconDirectory()` built up in `iconConfigList` (inherited from
  `AbstractConfig`). `XIcon` only *references* its config via a member
  pointer, it doesn't own it; `DesktopConfig`, which actually created
  each one with `new`, is the right owner to delete them -- same
  pattern as `XDesktopContainer::destroy()`'s existing cleanup of its
  own `iconList`.
- `XImlib2Image::configure()`: `imlib_create_color_modifier()` was
  never paired with `imlib_free_color_modifier()` (an Imlib2 context
  call, like `imlib_free_image()` -- no argument, operates on whatever
  was last passed to `imlib_context_set_color_modifier()`).

Verified end to end on real hardware: `[1]+ Exit 1` / SIGSEGV in the
job-control output is gone, replaced by a clean `[1]+ Done`. Both
`XftFontOpen`/`createFont()` leaks that had persisted unchanged across
every earlier valgrind run (the ones that motivated this entire
investigation) are completely absent from the leak report now that the
process can actually reach its own cleanup code. `still reachable`
dropped from ~5.3MB to ~830KB in the same comparison, consistent with
far more of the object graph actually tearing down correctly than ever
before. The four fixes in this entry haven't had their own dedicated
before/after valgrind comparison yet (found in the same pass that
fixed the crash) -- worth one more run to confirm the `definitely
lost` count drops further, but the headline problem (a crash that had
silently made every destructor-based fix this session look
ineffective to valgrind specifically) is resolved.

**Confirmed with a clean before/after comparison on real hardware:**
same `kill -TERM` method, same clean `Done` (no crash). `definitely
lost` dropped from 10,040 to 7,008 bytes (21 to 14 blocks),
`indirectly lost` from 43,919 to 41,936. One more real leak turned up
in the detailed trace, one level deeper than the `DesktopConfig` fix
above: `DesktopIconConfig::~DesktopIconConfig()` was itself completely
empty, so fixing `DesktopConfig` to actually delete each
`DesktopIconConfig` just meant this destructor started running without
ever freeing what it owns -- `common` (a `CommonOptions*`, allocated
with `new` in the constructor, exclusively owned by this object).
Fixed the same way as everything else in this investigation. At this
point the remaining `definitely lost`/`indirectly lost` entries are
all librsvg/pango/fontconfig/libexpat internals with no symbols and no
frame anywhere in this codebase's own stack -- out of scope, the
accepted cost of the libraries themselves rather than anything
idesk-ng can fix.

## `--install-ideskrc` and `--install-trash-icon` -- DONE

Two more one-shot setup commands, same family as
`--migrate-to-desktop`: explicit, never run automatically, each skips
(doesn't overwrite) when its target file already exists.

**`--install-ideskrc`** writes `~/.config/idesktop/ideskrc` with
idesk-ng's own factory defaults -- by reusing the exact same
`Database(path, true)` mechanism normal startup already falls back to
in memory when no `ideskrc` exists, then `Write()`-ing that to disk
instead of only using it in-process. Zero duplicated content between
the embedded defaults and this command.

**`--install-trash-icon`** adds `trash.desktop`: `Icon=user-trash`
(the standard freedesktop.org name, resolved the same dynamic
theme-discovery way as every other icon), `Exec=xdg-open
~/.local/share/Trash/files || zenity --error --text "..."` (xdg-open's
own failure mode with no file manager registered is to silently do
nothing -- the zenity fallback, confirmed to work since `Exec=` values
run through a real `/bin/sh -c`, is the only thing that tells the
person anything happened at all; the fallback message is always in
English regardless of session language -- deliberately not localized,
unlike the icon's own name, since translating a full sentence into
~20 languages for a message almost nobody will ever see wasn't worth
the scope), and `X-Idesk-Protected=true` (parsed by `FreeDesktopIcon`
now; not yet enforced anywhere -- the future Delete context-menu
action is expected to check it and refuse).

The icon's `Name` is localized via a bounded table (~20 common desktop
languages -- es, de, fr, it, pt, ru, ja, zh including a separate
Traditional-Chinese case for zh_TW/zh_HK, ko, nl, pl, tr, ar, sv, cs,
el, he, hu, fi, da, no/nb, uk, ro) keyed by the 2-letter code from
`LC_ALL`, falling back to `LANG`, falling back to English for any
language not in the table or with no locale set at all. Deliberately
not a claim to cover every language that exists -- a full, generic
translation would need pulling in GNOME/KDE's own translation
catalogs, exactly the kind of dependency this project tests against
*not* having (this whole feature was motivated by testing on a Debian
+ Fluxbox box with none of that installed).

**Made the one-shot flags properly composable in the same pass.**
`--migrate-to-desktop` previously called `_exit()` the instant it was
matched in `processArguments()`'s argv loop, meaning only the first
recognized one-shot flag in a given invocation would ever actually
run. Restructured so each one-shot action (now three) runs in argv
order within the loop, and only once the whole loop has finished does
the process decide its exit code -- 0 if everything that ran
succeeded, 1 if any of them failed. This is what lets
`--install-ideskrc --install-trash-icon --migrate-to-desktop` (or any
subset, any order) all run together in one invocation, rather than
needing one run per flag. Considered adding a single `--install-all`
convenience flag on top of this instead/as well; decided against it --
once the flags compose freely, it would be pure sugar for "pass the
other three", adding a flag whose only job is staying in sync with
whatever one-shot commands exist later.

`--help` stays exclusive: checked in its own pass before the one-shot
actions' loop even runs, so combining it with any action flag shows
the help text and does nothing else, rather than ambiguously also
running whatever else was on the command line.

Verified end-to-end, all headless (pure filesystem + env var logic,
no X11 needed):
- `--install-ideskrc` on a genuinely fresh `$HOME` (neither
  `~/.config/idesktop/` nor the legacy `~/.idesktop/` existing yet)
  correctly created `~/.config/idesktop/` and wrote real, valid
  `Config`/`Actions` content -- this caught a real bug first: the
  directory-resolution helper copied from `Migrate.cpp` is written for
  the opposite case (a directory that already has files in it), so on
  a truly fresh system it silently preferred the legacy path *and*
  never created it, failing outright. Fixed with a dedicated resolver
  for the install commands specifically
  (`resolveOrCreateIdesktopDir()`) that still respects an existing
  legacy setup if that's what's there, but creates the modern,
  XDG-preferred path (including `~/.config` itself if missing) on a
  genuinely fresh system instead of falling back to the deprecated one.
- Re-running `--install-ideskrc` against the file it just created
  correctly skipped rather than overwriting.
- `--install-trash-icon` tested across `es_MX`, `de_DE`, `zh_TW`
  (confirmed the separate Traditional-Chinese branch fires, not the
  generic `zh` entry), `ja_JP`, an unrecognized language code, and no
  `LANG` set at all -- each produced the exact right `Name=`, with the
  last two both correctly falling back to `Trash`.
- All three flags combined in one invocation, in both orders, each
  produced the same three files either way (`ideskrc`, `trash.desktop`,
  the migrated `.desktop` + seeded `layout.db`), single `exit 0`.
- `--help` combined with `--install-ideskrc` showed only the help text
  and left `$HOME` completely untouched -- confirmed the directory was
  never even created, not just that the file wasn't written.

## `--show-message` -- DONE (replaces zenity for the Trash icon's fallback)

A fourth one-shot command, `idesk --show-message "TEXT"`: a small,
centered, word-wrapped popup dismissed by any click or key. Built so
the Trash icon's no-file-manager-found fallback (and anything else
later that wants to tell the person something) doesn't need `zenity`
or any other external dialog tool installed -- one less runtime
dependency, consistent with testing this project on minimal systems
with nothing GNOME/KDE-adjacent present at all.

Not a new approach -- reuses the exact Xlib/Xft patterns already used
throughout this codebase for captions and tooltips (font loading,
`override_redirect` windows, `XftDraw`), just as a standalone X11
connection of its own rather than something tied to a running
idesk-ng session's `AbstractContainer`/config -- it's meant to be
launched as a brand new process from an icon's `Exec=` line, same as
any other command. The one genuinely new piece is the word-wrap itself
(`wrapText()` in `MessageBox.cpp`): a simple greedy line-break,
measuring each candidate line against the real loaded font via
`XftTextExtentsUtf8()` rather than estimating; a single word wider
than the box on its own is left to overflow rather than being split
mid-word, an acceptable rare edge case.

The Trash icon's `Exec=` (`Install.cpp`) now reads `xdg-open
~/.local/share/Trash/files || idesk --show-message "..."` instead of
`zenity --error --text "..."`.

Verified headlessly with a real (virtual) X server -- `Xvfb` plus
`xdotool`/`x11-utils` for synthetic input and window inspection, since
this piece genuinely needs X11 unlike most of this project's other
one-shot commands:
- The long fallback message correctly wrapped into 2 lines; the
  resulting window measured exactly 392x70 -- maxTextWidth (360) +
  padding (16) * 2, confirming the sizing math and the wrap both
  landed exactly as computed, not just plausibly close.
- On a 1024x768 virtual screen, the window's absolute position came
  back as exactly (316, 349) -- `(1024-392)/2, (768-70)/2` to the
  pixel, confirming the centering math.
- The process stayed alive and blocked while waiting, confirmed by
  checking it was still running a second after mapping the window.
- A synthetic `xdotool click` correctly dismissed it -- the process
  exited cleanly immediately after, confirmed by checking it was gone
  a second later.

**Follow-up fix, found immediately on real hardware:** the Trash
icon's `Exec=` called a bare `idesk --show-message ...`, which only
works if the binary happens to be on `$PATH` -- true after a real
`make install`, but not when running straight from the build directory
(`./src/idesk`), exactly how this project gets tested throughout this
whole file. `resolveSelfPath()` (`Install.cpp`) now reads
`/proc/self/exe` to embed whatever path is actually running at the
moment `--install-trash-icon` is invoked, falling back to the bare
`"idesk"` only if that can't be read. Linux-specific, but this project
already depends on X11/Xlib/Imlib2 and isn't targeting anything else.
Verified: the generated `Exec=` line now reads the real, confirmed-
executable absolute path of the running binary, correct whether run
from a dev build directory or a real system install, no configuration
needed either way.

## Context menu, base piece -- DONE (Rename/Delete/Properties themselves still pending)

Right-click on an icon now opens a small popup menu -- this piece is
just the menu itself (`ContextMenu.{h,cpp}`): shows placeholder items,
tracks hover, and returns which one was picked (or -1 if cancelled).
Rename/Delete/Properties land as their own pieces on top of this.

Hooked onto a plain single right-click (`currentAction.getRight() ==
singleClk`) in `XDesktopContainer::exeCurrentAction()` -- checked
first, before any of the configured-action matching below it. Doesn't
conflict with anything: no example `ideskrc` in this project binds a
default action to a bare right single-click, only `right doubleClk`
(mapped to `Execute[1]`), and right-click-for-context-menu (not
double-right-click) is the near-universal convention users already
expect.

Unlike `MessageBox.{h,cpp}` (its own standalone X11 connection,
launched as a brand new process), this runs *inside* the already-live
idesk-ng session and reuses its existing `Display`/`Visual`/`Colormap`
-- it needs to know which real icon was clicked and (once Delete etc.
exist) act on it, not just show text and exit. `XGrabPointer`/
`XGrabKeyboard` for the menu's duration is what makes a click anywhere
else on screen -- not just inside the menu -- correctly dismiss it:
with `owner_events=False`, X11 reports every pointer event in the
*grab window's own coordinate space* regardless of where it actually
happened, so "was this click inside the menu" is a single in-bounds
check against the menu's own width/height, no coordinate translation
needed.

Verified with a real (virtual) X server end to end -- `Xvfb` plus
`xdotool` for synthetic clicks and `xwininfo` for window inspection,
same tools as `--show-message`'s own verification. This one needs a
full live idesk-ng session (not just the one popup in isolation), so
the test setup is heavier: a real `.lnk` with an actual `Icon=` field
pointing at a real image (missing this the first time around produced
zero icon windows at all and cost a wasted round -- `Cannot determine
file extension of:` in the log was the tell).
- A right-click on the icon's image window produced a fourth
  top-level window sized exactly `100x78` -- matching 3 placeholder
  items at `ITEM_HEIGHT` (26px) each, 78px total -- at precisely the
  clicked coordinates.
- Moving the pointer onto the second row and left-clicking closed the
  menu (back to 3 windows) and produced exactly `Context menu:
  "Delete" chosen for "TestIcon"` in the log -- confirming hover
  tracking, click-to-select, the index-to-item mapping, the menu
  window's own teardown, *and* that the right live icon (found via
  the real click coordinates, not guessed) was the one identified.
- Right-clicking again and then clicking far outside the menu's
  bounds closed it (back to 3 windows) with no `Context menu:` line
  at all in the log -- confirming click-outside correctly cancels
  without selecting anything. Escape shares the exact same cancel
  path in the code (`selected` stays `-1`) but wasn't separately
  exercised in this pass.

**Multiple isolated sandbox crashes while building this test setup,
worth noting for next time:** backgrounding a long-lived process (the
virtual X server) with `&` and leaving it running *across* separate
tool calls repeatedly caused a full environment reset (all live
processes wiped, though the git repository and working tree on disk
were unaffected both times). Root cause: a background process that
keeps the invoking shell's stdout/stderr inherited keeps that output
stream open indefinitely, which hangs whatever is waiting for it to
close. Fixed by always explicitly redirecting such a process's output
to a file (`> /tmp/xvfb.log 2>&1`) and, more importantly, doing the
entire launch-interact-teardown sequence for a given test -- including
explicitly killing the X server and idesk-ng -- within a single,
self-contained command rather than spreading it across several.

## Context menu: Delete -- DONE

The first real action on top of the context menu base piece.
`XDesktopContainer::deleteIcon()` always moves the icon's underlying
file to the desktop trash via `g_file_trash()` (GIO; the standard
freedesktop.org mechanism, works without any GNOME/KDE/XFCE installed
at all, same as `--install-trash-icon`) -- deliberately never a
permanent delete, and deliberately the exact same path for every icon
origin (`.lnk`, `.desktop`, or a plain file/folder in `~/Desktop`),
not a "pretty trash here, permanent there" split by origin or
directory. See the Point 3 design discussion earlier in this file for
why that split was rejected.

**`X-Idesk-Protected` is enforced for the first time.**
`DesktopIconConfig` didn't retain this field anywhere after
construction (`setIconOptions()` only ever extracted specific fields
like caption/command/x/y into its own members) -- added
`protectedFromDelete` (set from `table.Query("X-Idesk-Protected") ==
"true"`, zero-initialized in the constructor same as everything else
in this class) plus a public `isProtected()`. A protected icon's
Delete is refused outright -- not silently ignored -- via a
`--show-message` popup explaining why, launched through the existing
`runCommand()` fork+exec helper (the same one every icon's own `Exec=`
already goes through) rather than blocking the main process on it.

**Live removal, no restart needed.** On a successful trash, the
`XIcon` (not the underlying `DesktopIconConfig`, which stays in
`DesktopConfig::iconConfigList` and is cleaned up at normal shutdown
same as always -- not worth the extra bookkeeping of also removing it
mid-session) is erased from `iconList` and deleted, which cascades
through the whole destructor chain fixed earlier in this file --
caption, image, tooltip, everything -- so the icon disappears from
the screen immediately.

Verified end-to-end with a real virtual X server, in two passes:
- **Normal icon:** right-clicked, selected Delete. The underlying file
  was confirmed gone from its original location and present in
  `~/.local/share/Trash/files/` immediately after. The live window
  count dropped by exactly the icon's own windows (confirmed via a
  clean before/after delta), with no restart -- the icon visibly
  disappeared mid-session.
- **Protected icon** (a `.desktop` with `X-Idesk-Protected=true`,
  same as `--install-trash-icon` generates): right-clicked, selected
  Delete. The file was confirmed still present on disk afterward, the
  icon's own windows were confirmed still present and unchanged, and
  a new window titled "idesk-ng" appeared, sized and centered exactly
  as `--show-message` always does -- confirmed via the same
  pixel-exact centering math already verified for that piece
  (`(1024-392)/2, (768-49)/2` matched the reported window position to
  the pixel) -- confirming the refusal path fires correctly and gives
  real feedback rather than silently doing nothing.

**Follow-up fix, found immediately on real hardware (Fluxbox) -- diagnosed
wrong the first time, kept here because the wrong turn is instructive:**
after a successful delete, the icon stopped responding to clicks but its
image stayed on screen, a ghost. (As first reported, only the caption had
disappeared.)

*First diagnosis (wrong):* that `ParentRelative` windows need an explicit
`XClearArea` to repaint the parent. A debug build on real hardware showed
`XClearArea` firing with a sane rectangle and a real `_XROOTPMAP_ID`
wallpaper pixmap present -- and the ghost remained. In hindsight the first
report already held the real clue: the caption's area *did* repaint on its
own, which is exactly what an X server does when any window over a root
with a background is destroyed. The `XClearArea` code was removed.

*Actual root cause:* every icon owns three X windows -- image, caption,
tooltip. `~XImlib2Caption` and `~XImlib2ToolTip` destroyed theirs;
`~XImlib2Image` never destroyed the icon image window. That destructor was
completely empty until the memory-leak pass, and that pass only taught it
to free Imlib2/gdk-pixbuf memory, never this server-side resource. It never
mattered before because every earlier path that destroys icons (shutdown,
Reload) ends the process right away, and the X server reclaims a dead
client's windows. Delete is the first to destroy icons mid-session: the
orphaned window stayed alive on the server, still painted, but no longer
matched to any icon in `iconList`, so idesk-ng dropped its events -- a
visible, unclickable ghost. An earlier verification of this feature also
misread its own numbers: the window count fell 6 -> 4 and was read as
"image and caption destroyed"; it was caption and tooltip, with the image
window still orphaned.

*Fix:* `window` (now zero-initialized) is destroyed in `~XImlib2Image`.

*Second bug, found by the fix itself (with gdb):* `XImlib2Caption` privately
inherits `XImlib2Image` and shares the member `window`. `~XImlib2Caption`
destroys it using the container's display, then the base destructor
destroyed it again through the base `display` member -- which `configure()`
sets only for the real icon image, never for a caption -- i.e. an
uninitialized `Display *`: SIGSEGV inside `XDestroyWindow`, and the signal
handler's silent `_exit(1)` made the whole process simply vanish. Fixed
twice over: `display` is zero-initialized and the base destructor requires
both `window` and `display`; `~XImlib2Caption` also zeroes `window` after
destroying it (single owner).

*Verification (virtual X server).* Checking that the process is still alive
at every step matters here: an earlier "0 windows after delete" reading was
meaningless, because a crashed client's windows vanish with it. Two icons:
6 windows; delete one -> 3 and process alive; delete the other -> 0 and
process alive; both files in the Trash (before the fix a single delete left
4). Clean shutdown (SIGTERM) re-tested after the destructor change with icon
shadows on, including `SnapShadow`'s extra windows (8 windows for 2 icons
instead of 6): exit code 0 both times. Not verified: Delete itself with
`SnapShadow` enabled (only shutdown was tested there, through the same
destructor chain), and, most importantly, that the ghost is visually
gone on a real Fluxbox session -- since confirmed: two icons deleted on
real hardware, nothing left on screen, both files in the Trash.

**Follow-up: Delete also removes the icon's entry from the layout
DB.** Not just tidiness -- a real (if narrow) correctness gap:
without this, a future icon that happens to land on the exact same
absolute path as one that was just deleted (a package reinstalling a
`.desktop` at a standard location, or recreating an icon with the same
name) would silently inherit the deleted icon's old position from the
still-present `layout.db` entry. Unlike a rename, where the same kind
of staleness is an accepted, already-documented trade-off (see
"Legacy / standard icon support" earlier in this file) because there's
no realistic scenario for the *old* path to get reused by something
else, a deleted path absolutely can be reused.

New `removeLayoutPosition(path)` in `IconLayout.{h,cpp}`: opens the
layout DB (a no-op if it doesn't exist -- nothing to remove), finds
the matching table by `Title` (the absolute path, same key
`seedLayoutPosition()`/`getLayoutPosition()` already use) and erases
it, writing back only if something was actually found. Called from
`deleteIcon()` right after a successful trash, unconditionally --
harmless even for a `.lnk`-origin icon that was never in the layout DB
to begin with.

Verified end-to-end on a virtual X server: seeded `layout.db` with a
real entry for a `.desktop` icon's path, confirmed the icon loaded at
exactly that seeded position (300,200 in the test), deleted it through
the context menu, and confirmed `layout.db` came back completely empty
afterward.

## Context menu: Rename -- DONE

**What "Rename" changes** (agreed rule): for a `.lnk` or a `.desktop` it
changes the *shown* name -- `Caption:` / `Name=` -- and never the file; for a
plain file or folder in `~/Desktop`, which has no name other than its file
name, it renames the file itself. A `.desktop` is edited in place (`Name=`
inside `[Desktop Entry]` only), so `Name[es]=` keys, comments, other groups'
`Name=` lines, and the file's mode all survive. A symbolic link is refused for
both `.lnk` and `.desktop`: a launcher can be a link to a real system file, and
writing through it would modify that.

**Pieces.** `IconEdit.{h,cpp}`: the three on-disk edits, no X11, covered by
`tests/IconEditTest.cpp` (25 checks, including the traps above, symlinks,
collisions, invalid names, folders and UTF-8). `TextInput.{h,cpp}`: a modal
single-line field built on the same Xlib/Xft pattern as `ContextMenu`.
`XDesktopContainer::renameIcon()` and `notify()` tie them to the menu.

**`TextInput`.** The initial text starts fully selected, so typing replaces
it. Editing: typing, Backspace, Delete, Left/Right/Home/End, Ctrl+A. Enter
accepts; Escape or a click outside cancels. Text is UTF-8 and the cursor and
deletion step over whole characters (Backspace after `n`-tilde removes the
character, not one byte of it). Dead keys -- acute, grave, circumflex, tilde,
diaeresis, cedilla -- are composed with the next letter from a small built-in
table covering the Latin-1 vowels, `n`, `y` and `c`; a dead key followed by
space, or by a letter it can't combine with, yields the plain diacritic. This
deliberately avoids XIM: `XLookupString()` alone does not compose dead keys,
and XIM would tie the dialog to the session having a UTF-8 locale configured,
which a minimal system often does not. Not supported: placing the cursor with
the mouse and clipboard paste. While waiting for events it wakes once a second
to honour `quitRequested`, so an open dialog cannot block a shutdown
(`ContextMenu`'s own modal loop still lacks this).

**Refresh replaces just that icon, in place.** (The first version restarted
the whole desktop with `Application::restartIdesk()`, which blinks every icon;
on current Ubuntu it is much worse than a blink, because gdk-pixbuf loads each
image through a glycin helper process -- over 100 `vfork`s at startup were seen
under gdb -- and a restart relaunches all of them.) Now the per-file icon
construction that used to live inside `DesktopConfig::scanIconDirectory()` is
`createIconConfig()`, so startup and refresh run exactly the same code;
`rebuildIconConfig()` re-reads one icon from its file and swaps the result into
the same slot of the config list; and `XDesktopContainer::refreshIcon()` builds
a new `XIcon` from it at the old icon's current position
(`DesktopIconConfig::setPosition()`, which writes nothing to disk), shows it,
and only *then* tears down the old `XIcon` and its config. A plain file's
`layout.db` entry is carried to the new path (old entry removed, new one seeded
at the same position) and its in-memory path updated.

*A trap worth recording:* `XIcon::createIcon()` only creates the windows.
Positioning and mapping them is done per icon at the end of `arrangeIcons()`
(`moveImageWindow()`, `mapImageWindow()`, `initMapCaptionWindow()`), which one
replaced icon never goes through. The first version of the refresh skipped
those three calls and the renamed icon simply vanished -- unmapped, unplaced --
while the window count stayed correct, so counting windows could not see it;
comparing window geometry did.

*Fallback:* if the file can no longer be read as an icon, or the new icon cannot
be created, `refreshIcon()` returns false with the old icon untouched and the
caller does the full restart, which is always safe because whatever changed is
already on disk. Forced by renaming a plain file to `x.desktop` (no longer a
valid icon): the restart fired, the other icons reloaded and the process stayed
up. (That leaves an orphan `layout.db` entry for the unreadable file; harmless.)

*Verification* (virtual X server): for a `.desktop`, a plain file and a `.lnk`
the rename causes 0 restarts, the window count stays constant, the icon's image
region is pixel-identical before and after (0 differing pixels, with the pointer
parked in a neutral spot -- with it hovering, hover rendering alone differs),
the window is `IsViewable`, the caption is re-measured and re-centered, a second
rename of the already-rebuilt icon works, and a rebuilt icon can still be
deleted. Under valgrind, four renames (every type, plus a repeat) give 0 invalid
reads/writes/frees and no leak with a frame in this project's code. The test
harness itself had two traps: a geometry regex that rejected negative
coordinates (a wide caption centered under a narrow icon has a negative X), and
screenshots taken with the pointer over one icon but not the other. Failures (name already exists, invalid name,
symlink) are reported through the same `--show-message` popup; messages go
through a `shellQuote()` so text containing quotes or shell metacharacters
(a file name) cannot become part of the command.

**Bug found while writing it, in `ContextMenu`:** it grabbed the pointer with
`owner_events=True` while its own comment documented `False`. With `True`, a
click on *another* icon while the menu is open is delivered to that icon's
window with coordinates relative to it, which can land inside the menu's
bounds by accident and be read as choosing an option. Now `False`, as
documented (`TextInput` uses `False` too).

Verified on a virtual X server (`Xvfb`, `xdotool`, `latam` keymap, and
`xwininfo`/screenshots for window state and appearance): Escape and
click-outside cancel with no restart and the file unchanged; typing
`M`, dead acute, `u`, `s`, `i`, `c`, `a` stores `M` + `0xC3 0xBA` + `sica`;
`N`, `i`, n-tilde, `o`, Left, Backspace leaves exactly `Nio` with no stray
bytes; a `.desktop` renamed to a name with n-tilde keeps `Name[es]=` and its
`X-Idesk-X/Y`; a plain file renamed `notas.txt` -> `ideas.txt` ends with the
file renamed and `layout.db` holding the new path at the same position and no
entry for the old; renaming onto an existing name pops the error message and
neither restarts nor touches either file; Escape closes the context menu; and
a click on a different icon with the menu open closes it without opening the
dialog (checked after the fix; not run against the old code, so this shows it
passes, not that it would have caught the bug). A screenshot confirmed the
dialog's layout: title, framed field, selection highlight, caret, hint line.

Not verified: any of this on a real keyboard and session (dead-key behavior in
particular depends on the person's real layout), Rename of a `.desktop` that
is a symlink in the live UI (refused in the unit tests only), and Rename with
`SnapShadow` enabled.

## A frozen desktop while a launched program runs -- and an X connection loss that is NOT explained

**Report (Ubuntu VM, real Openbox session).** After recovering two files from
the Trash and copying them back into `~/Desktop`, idesk-ng died with
`XIO: fatal IO error 11 (Resource temporarily unavailable) on X server ":0"
after 6882 requests (6882 known processed) with 138 events remaining.` Earlier
in the same session the Trash icon had opened Files (Nautilus) several times.
The desktop server itself stayed up. Also reported: after a Rename the icons
visibly "cough" while the desktop reloads (at the time that was the full
restart; Rename now replaces only the affected icon -- see the Rename section).

**Found and fixed -- `runCommand()` blocked the whole event loop.** After
`fork()` the parent did a blocking `waitpid(pid, NULL, 0)`, in the code since
the original 0.7.5 sources (checked with `git log -S`). Every icon, tooltip and
menu froze for as long as the launched program stayed open; with
`Exec=firefox` that means a dead desktop until Firefox quits. In an Openbox
session `xdg-open` ran Nautilus in the foreground (the three different
Nautilus PIDs in the log), so idesk-ng was frozen for as long as Files was
open while X events piled up unread -- which fits the "138 events remaining".
Reproduced with an icon whose `Exec=` is `sleep 20`: a right-click on another
icon did nothing until the command ended, and then the queued click opened its
menu. Fixed by not waiting. The first version of the fix left reaping to a SIGCHLD
handler that looped over `waitpid(-1)`; that was the wrong design. A thread dump
from the real Ubuntu 26.04 VM showed `libglycin` threads blocked in `wait4()`
(gdk-pixbuf loads each image through a glycin helper process there), and a
handler that reaps *every* child can steal the exit status of processes other
libraries spawn and wait for themselves. That is an inference from the stack,
not a failure that was seen -- but idesk-ng has no business touching children it
did not create. So `runCommand()` now double-forks: the intermediate child exits
at once and the parent waits only for it; the grandchild that runs the program
is adopted by init (or the session's subreaper), so nobody in idesk-ng has to
reap it, and **no SIGCHLD handler is installed at all**. The child's exec-failure
path uses `_exit(127)` rather than `exit(1)`, since it is a forked copy of a
multithreaded process. Verified (virtual X server): with `sleep 20` running the
context menu opens (7 windows) and closes; the launched command really runs
(marker file); 0 zombies, checked against a control showing that the sandbox's
init does reap orphans; the process's caught-signal mask no longer includes
SIGCHLD. Not testable there: the glycin interplay itself (that sandbox has the
classic gdk-pixbuf loaders). Confirmed on the real VM: Nautilus open from the
Trash icon no longer blocks idesk-ng.

**Found and fixed with valgrind -- two uninitialized values in our own code.**
`XDesktopContainer::timer` was only assigned when the background rotates, so
with the default config the loop condition `!XPending(display) && timer` read
whatever was in that memory. If non-zero, the loop spins at 100% CPU and never
reaches its `select()`; if zero it behaves. Same family as `spareRoot`: it
depended on heap garbage, which is exactly the kind of thing that differs from
one machine to the next. Now initialized to `NULL`. Separately,
`XImlib2ToolTip` passed `GCBackground` to `XCreateGC` without ever setting
`gcv.background` (nothing there draws with the GC's background; the flag is
dropped). After both fixes valgrind reports no uninitialized-value error with
a frame in this project's code.

**NOT found: the cause of the connection loss.** With an xcb-based Xlib the
`errno` in that message is stale and the error generally means the connection
to the server was lost, so this may well not be idesk-ng's doing. What was
tried and did *not* reproduce it: blocking idesk-ng for 20 s under a flood of
pointer motion, in a virtual X server; valgrind across hover, the Trash
double-click (`fork`+`exec`), two Deletes, hovering over the gaps, and
shutdown -- 0 invalid reads, writes or frees; file descriptors holding at 4
across three Rename-triggered restarts (so nothing leaks across `exec`). Not
tested: a real Nautilus/GNOME stack, a real Xorg on the VM's virtual GPU, or
drag-and-drop from Files. The freeze above removes the one concrete mechanism
found (a long block with a growing backlog) but nothing shows it was the
trigger. If it recurs: run `gdb -q -batch -ex "set pagination off" -ex "set
breakpoint pending on" -ex "break exit" -ex run -ex bt -ex "thread apply all
bt" --args ./src/idesk` (Xlib's fatal handler ends in `exit()`, so this stops
with the stack of whatever was running), and look at the Xorg log for lines
around the same time.

**Seen but left alone** (real, unrelated to the above): a new
`sn_launcher_context` is created on every launch without releasing the previous
one (a small leak per click); `event.xproperty.time` is passed to
`sn_launcher_context_initiate()` even when the event is a button press, so the
timestamp is the wrong field of the union; and `ContextMenu`'s modal loop still
does not honour `quitRequested` (`TextInput`'s does).

**Update after the real-hardware retest.** A gdb session on the VM ran idle in
`select()` inside `eventLoop` until it was ended with SIGTERM -- no crash was
captured -- and the Xorg log has nothing from the server side around the
incident. The connection loss did not recur after the fixes above, which is not
the same as being fixed.
