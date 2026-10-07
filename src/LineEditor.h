/* 
 * Idesk -- LineEditor.h
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

#ifndef LINE_EDITOR_CLASS
#define LINE_EDITOR_CLASS

#include <X11/Xlib.h>
#include <string>
using namespace std;

/*
 * The editing state of one single-line text field, shared by TextInput
 * (Rename) and PropertiesDialog. Text is UTF-8; the cursor and deletions step
 * over whole characters. Handles typing, Backspace, Delete, Left/Right/Home/
 * End and Ctrl+A, and composes the dead keys (acute, grave, circumflex,
 * tilde, diaeresis, cedilla) with the following letter from a small built-in
 * table -- XLookupString() alone doesn't compose them, and going through XIM
 * would make this depend on the session's locale being set up.
 *
 * Enter, Escape, Tab and the like are the caller's: handleKey() is only for
 * the keys that edit text.
 */
class LineEditor
{
    public:
        string text;
        size_t cursor;       // byte offset, always on a character boundary
        bool selectAll;      // whole text selected: typing replaces it
        KeySym pendingDead;  // dead key waiting for its letter, or 0
        size_t maxBytes;

        LineEditor() : cursor(0), selectAll(false), pendingDead(0), maxBytes(240) {}

        // replaces the text; cursor at the end, fully selected if non-empty
        void set(const string & s);

        // Applies one key press. Returns true if anything visible may have
        // changed (so the caller redraws), false if the key did nothing
        // (a lone Shift, say).
        bool handleKey(XKeyEvent * ev);
};

#endif
