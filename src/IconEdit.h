/* 
 * Idesk -- IconEdit.h
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

#ifndef ICON_EDIT_CLASS
#define ICON_EDIT_CLASS

#include <string>
using namespace std;

/*
 * The three on-disk edits behind the context menu's Rename. Pure file
 * operations with no X11 involved, so they can be tested headlessly.
 * Each returns true on success; on failure it returns false, leaves the
 * file untouched, and sets `error` to a short English message suitable
 * for showing to the person.
 */

// Sets Caption: in a .lnk icon file (the name shown under the icon).
bool setLnkCaption(const string & path, const string & caption, string & error);

// Sets Name= in the [Desktop Entry] group of a .desktop file (adding it
// right after the group header if there is none). Everything else in the
// file is preserved exactly -- localized Name[xx]= keys, comments, and
// Name= lines inside other groups such as [Desktop Action ...]. Written
// to a temporary file and renamed into place, keeping the original
// permissions. Refuses a symbolic link: a .desktop may be a link to a
// real system launcher, and writing through it would modify that.
bool setDesktopName(const string & path, const string & name, string & error);

// The general forms of the two above, for any key (Properties edits Name,
// Exec/Command and Icon). setLnkKey() sets `key` in the [Icon] table of a
// .lnk; setDesktopKey() sets `key=` in [Desktop Entry] with the same care as
// setDesktopName(), which is now setDesktopKey(..., "Name", ...).
bool setLnkKey(const string & path, const string & key, const string & value,
               string & error);
bool setDesktopKey(const string & path, const string & key,
                   const string & value, string & error);

// Reads the raw value of a key, for showing it in the Properties dialog.
// getDesktopKey: the plain `key=` in [Desktop Entry] (not Name[es]= and the
// like); returns false when the file or key is missing (value empty).
// getLnkKey: false if the file can't be read as an icon; `isArray` is set when
// the key is a list in the file (Command[0], Command[1]...), in which case
// `value` is its first element and the caller should treat it as read-only.
bool getDesktopKey(const string & path, const string & key, string & value);
bool getLnkKey(const string & path, const string & key, string & value,
               bool & isArray);

// Renames a plain file or folder on disk (through GIO, which also
// rejects invalid names and refuses to overwrite an existing file).
// newPath receives the file's new absolute path.
bool renamePlainFile(const string & path, const string & newName,
                     string & newPath, string & error);

#endif
