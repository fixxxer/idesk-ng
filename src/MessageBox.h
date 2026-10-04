/* 
 * Idesk -- MessageBox.h
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

#ifndef MESSAGE_BOX_CLASS
#define MESSAGE_BOX_CLASS

#include <string>
using namespace std;

/*
 * idesk-ng --show-message "text": a small, centered, word-wrapped
 * popup -- click anywhere or press any key to dismiss. Exists so icons
 * (the Trash icon's no-file-manager-found fallback, to start) don't
 * need to depend on zenity or any other external dialog tool being
 * installed -- the same minimal-system testing this whole project
 * cares about (see DESIGN.md).
 *
 * Opens its own fresh X11 connection and draws with plain Xlib/Xft --
 * no dependency on a running idesk-ng session, AbstractContainer, or
 * any config file. Meant to be invoked as a brand new process (e.g.
 * from an icon's Exec= line), not called from within the main event
 * loop.
 *
 * Returns false (nothing shown) if no X display could be opened or no
 * font could be loaded; true once the dialog has been shown and
 * dismissed.
 */
bool showMessage(const string & text);

#endif
