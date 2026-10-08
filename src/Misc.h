/* vim:tabstop=4:expandtab:shiftwidth=4
 * 
 * Idesk -- Misc.h
 *
 * Copyright (c) 2002, Chris (nikon) (nikon@sc.rr.com)
 * All rights reserved.
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

#ifndef MISC_H
#define MISC_H

#include <string>
#include <cctype>
#include <algorithm>
#include <unistd.h>
#include <sstream>

using namespace std;

// Set by "idesk --kiosk" (see Application::processArguments). In kiosk mode
// nothing is ever written to disk, the right-click menu is off, every icon is
// locked in place and the Lock gesture does nothing -- whatever ideskrc says.
extern bool kioskMode;

string getUpper(const string & str);
//void Reboot();
string itos(int i);

// Best-effort resolution of a bare icon-theme name (e.g. "firefox",
// "folder", "text-x-generic") to an actual image file on disk. Tries a
// short, fixed list of conventional locations/extensions -- NOT a full
// Icon Theme Specification resolver (no theme inheritance, no
// index.theme parsing, no size matching). Returns "" if nothing is
// found. Shared by FreeDesktopIcon (.desktop Icon= values) and
// GenericFileIcon (MIME-type icons for plain files); see DESIGN.md.
string resolveIconThemeName(const string & name);

#endif
