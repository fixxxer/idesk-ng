/* 
 * Idesk -- ContextMenu.h
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

#ifndef CONTEXT_MENU_CLASS
#define CONTEXT_MENU_CLASS

#include <X11/Xlib.h>
#include <string>
#include <vector>
using namespace std;

/*
 * The right-click popup menu on an icon (Rename/Delete/Properties,
 * once those exist -- this piece is just the menu itself, showing
 * placeholder items and returning which one was picked). Unlike
 * MessageBox.h (its own standalone X11 connection, launched as a
 * brand new process from an Exec= line), this runs *inside* the
 * already-running idesk-ng session -- it needs to act on the live
 * icon that was actually clicked, so it reuses that session's
 * existing Display/Visual/Colormap rather than opening its own.
 *
 * Shows `items` as a simple vertical list at (x, y) -- typically the
 * click position -- clamped so the menu never renders off-screen.
 * Temporarily grabs the pointer and keyboard for the duration (so a
 * click anywhere else, not just inside the menu, correctly dismisses
 * it, same as any other context menu) and runs its own small event
 * loop until the person either clicks an item (grabs are released,
 * returns that item's index) or cancels -- clicking outside the menu
 * or pressing Escape (returns -1).
 *
 * Caller owns display/visual/cmap and is responsible for them as
 * usual; this function only grabs and ungrabs, it never opens or
 * closes the connection itself.
 */
int showContextMenu(Display * display, int screen, Window root,
                     Visual * visual, Colormap cmap,
                     const vector<string> & items, int x, int y);

#endif
