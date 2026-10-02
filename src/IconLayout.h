/* 
 * Idesk -- IconLayout.h
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

#ifndef ICON_LAYOUT_CLASS
#define ICON_LAYOUT_CLASS

#include <string>
using namespace std;

/*
 * Position storage for icons that can't safely keep their own X/Y --
 * .desktop files (may be copies/symlinks of a real system launcher) and
 * plain files/folders in the XDG Desktop dir (no file of their own to
 * write into at all). See DESIGN.md "Position handling".
 *
 * Format: the same Database/Table "table <title> ... end" grammar
 * already used by .lnk files and ideskrc -- no new parser. One table
 * per icon, keyed by the icon's own absolute path (safe even with
 * spaces in the filename: Database's tokenizer reads a table's title
 * with getline(), not a whitespace-delimited token).
 */

// Resolves ~/.config/idesktop/layout.db (or the legacy ~/.idesktop/
// fallback), using the exact same directory resolution as
// DesktopConfig::loadIcons() so the layout DB always lives alongside
// ideskrc and the icons themselves.
string getLayoutDbPath();

// Records (or updates) the saved position for the icon at the given
// absolute path. Creates the layout DB file if it doesn't exist yet.
// Safe to call repeatedly for the same path -- overwrites rather than
// duplicating.
void seedLayoutPosition(const string & path, int x, int y);

#endif
