/* 
 * Idesk -- FreeDesktopIcon.h
 *
 * Copyright (c) 2013, neagix
 * Copyright (c) 2026, iDesk-NG contributors
 * Some rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 *      Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *      
 *      Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *      
 *      Neither the name of the <ORGANIZATION> nor the names of its
 *      contributors may be used to endorse or promote products derived from
 *      this software without specific prior written permission.
 *
 * (See the included file COPYING / BSD )
 */

#ifndef FREEDESKTOP_ICON_CLASS
#define FREEDESKTOP_ICON_CLASS

#include <string>
#include "Database.h"

using namespace std;

/*
 * Parses a freedesktop.org Desktop Entry file (.desktop) and exposes the
 * result as a Table populated with the SAME keys DesktopIconConfig already
 * reads from .lnk files (Icon, Caption, ToolTip.Caption, Command, Width,
 * Height). Because of that, DesktopIconConfig needs no separate code path
 * for .desktop-derived icons -- it stays 100% agnostic between a Table that
 * came from a .lnk and one that came from here.
 *
 * Scope of the Desktop Entry Specification implemented (see
 * https://specifications.freedesktop.org/desktop-entry-spec/latest/):
 *  - only the [Desktop Entry] group is read; other groups such as
 *    [Desktop Action ...] are ignored.
 *  - Type=Application (Exec/TryExec) and Type=Link (URL, opened via
 *    xdg-open) are supported. Type=Directory is not handled -- it isn't
 *    meaningful for a standalone .desktop icon file.
 *  - localized keys (Name[xx], Comment[xx], ...) are ignored in favour of
 *    the base key, which every spec-compliant file must still provide.
 *  - field codes in Exec (%f %F %u %U %i %c %k %d %D %n %N %v %m %%) are
 *    stripped rather than expanded -- idesk icons take no launch arguments.
 *  - Icon= resolution: an absolute path is used as-is; a bare icon-theme
 *    name gets a short, best-effort lookup under /usr/share/pixmaps and
 *    /usr/share/icons/hicolor/. Full Icon Theme Specification resolution
 *    (theme inheritance, index.theme parsing, scalable/ handling) is a
 *    separate, larger piece of work -- see DESIGN.md.
 *
 * The vendor extension keys X-Idesk-Width / X-Idesk-Height are read into
 * Width / Height directly. X-Idesk-X / X-Idesk-Y (Path B, see
 * DESIGN.md) are parsed too, but deliberately exposed under their own
 * key names rather than as "X"/"Y" -- DesktopConfig::scanIconDirectory()
 * decides whether to apply them, and only does so the first time this
 * icon is seen (not yet in the layout DB). Once a position exists there
 * (from this seed, from arrangeIcons(), or from the user dragging the
 * icon), it always wins over whatever the .desktop file says -- the
 * file is never re-read for position after that.
 */
class FreeDesktopIcon : public Table
{
    protected:
        bool visible;

        static string stripExecFieldCodes(const string & exec);
        static string resolveIconPath(const string & icon);

    public:
        FreeDesktopIcon(const string & filename);

        // false for Hidden=true / NoDisplay=true entries: a well-formed
        // .desktop file that explicitly asks not to be shown, which is
        // different from a malformed one (isValid() covers that case,
        // inherited from Table).
        bool shouldDisplay() { return visible; }
};

#endif
