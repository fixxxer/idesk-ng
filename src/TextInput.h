/* 
 * Idesk -- TextInput.h
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

#ifndef TEXT_INPUT_CLASS
#define TEXT_INPUT_CLASS

#include <X11/Xlib.h>
#include <string>
using namespace std;

/*
 * A small modal single-line text field -- what the context menu's
 * Rename (and later Properties) uses. Same family as ContextMenu.h: it
 * runs inside the live idesk-ng session on its existing Display, draws
 * with plain Xlib/Xft, and grabs pointer and keyboard for its duration.
 *
 * `initial` starts out fully selected, so typing replaces it. Editing:
 * typing, Backspace, Delete, Left/Right/Home/End, Ctrl+A (select all).
 * Enter accepts; Escape, or a click anywhere outside the dialog,
 * cancels. Text is UTF-8 and cursor movement and deletion step over
 * whole characters, not bytes. Dead keys (acute, grave, circumflex,
 * tilde, diaeresis, cedilla) are composed with the following letter
 * (acute + u gives u-acute) by a small built-in table -- XLookupString()
 * alone doesn't compose them, and going through XIM would make this
 * depend on the session's locale being set up, which a minimal system
 * often isn't.
 *
 * Returns true and fills `result` if accepted, false if cancelled (or
 * if the keyboard couldn't be grabbed). Not supported: mouse cursor
 * placement and clipboard paste.
 */
bool showTextInput(Display * display, int screen, Window root,
                    Visual * visual, Colormap cmap,
                    const string & title, const string & initial,
                    string & result);

#endif
