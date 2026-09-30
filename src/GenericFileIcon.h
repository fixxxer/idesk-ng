/* 
 * Idesk -- GenericFileIcon.h
 *
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

#ifndef GENERIC_FILE_ICON_CLASS
#define GENERIC_FILE_ICON_CLASS

#include <string>
#include "Database.h"

using namespace std;

/*
 * Turns a plain file, folder, or symlink sitting in the XDG Desktop
 * directory into an icon -- the same thing GNOME/KDE/XFCE already do
 * for anything on the desktop that isn't a launcher.
 *
 * The icon is resolved by MIME/content type via GIO
 * (g_file_query_info() + G_FILE_ATTRIBUTE_STANDARD_ICON), which already
 * knows how to tell folders, symlinks, and file types apart without
 * iDesk-NG needing its own MIME database. The resulting icon-theme
 * name(s) then go through the same best-effort path lookup already used
 * for .desktop files (resolveIconThemeName(), see Misc.h) -- not a full
 * Icon Theme Specification resolver; see DESIGN.md.
 *
 * Activating the icon runs `xdg-open <path>`, which opens a file with
 * the user's configured default application, or a folder in their
 * default file manager -- deliberately not reimplementing that
 * resolution ourselves.
 *
 * Unlike FreeDesktopIcon, there is no "is this well-formed" question
 * here -- any file that exists can be turned into an icon this way, so
 * there is no isValid()/shouldDisplay() gate to check after construction.
 */
class GenericFileIcon : public Table
{
    public:
        GenericFileIcon(const string & path);
};

#endif
