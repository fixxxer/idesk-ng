/* 
 * Idesk -- ContextMenu.cpp
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

#include "ContextMenu.h"
#include <X11/Xft/Xft.h>
#include <X11/keysym.h>
#include <algorithm>

using namespace std;

static const int ITEM_HEIGHT = 26;
static const int PADDING_X = 14;
static const int PADDING_Y = 6;

static void drawMenu(Display * display, Window win, XftDraw * draw,
                      XftFont * font, const XftColor & textColor,
                      const XftColor & highlightColor,
                      const vector<string> & items, int width, int hovered)
{
    // Redraw the whole thing each time rather than tracking damage --
    // this is a handful of text rows, not worth the bookkeeping.
    XClearWindow(display, win);

    for (size_t i = 0; i < items.size(); i++)
    {
        int rowTop = (int)i * ITEM_HEIGHT;

        if ((int)i == hovered)
            XftDrawRect(draw, &highlightColor, 0, rowTop, width, ITEM_HEIGHT);

        int baseline = rowTop + PADDING_Y + font->ascent;
        XftDrawStringUtf8(draw, &textColor, font, PADDING_X, baseline,
                           (const XftChar8 *)items[i].c_str(),
                           items[i].length());
    }
}

int showContextMenu(Display * display, int screen, Window root,
                     Visual * visual, Colormap cmap,
                     const vector<string> & items, int x, int y)
{
    if (items.empty())
        return -1;

    XftFont * font = XftFontOpenName(display, screen, "Sans-10");
    if (!font)
        return -1;

    // Menu width: wide enough for the longest item, plus padding on
    // both sides.
    int maxTextWidth = 0;
    for (size_t i = 0; i < items.size(); i++)
    {
        XGlyphInfo extents;
        XftTextExtentsUtf8(display, font, (const XftChar8 *)items[i].c_str(),
                            items[i].length(), &extents);
        maxTextWidth = max(maxTextWidth, (int)extents.xOff);
    }
    int width = maxTextWidth + PADDING_X * 2;
    int height = (int)items.size() * ITEM_HEIGHT;

    // Clamp so the menu always renders fully on-screen, regardless of
    // how close to an edge the click that opened it was.
    int screenWidth = DisplayWidth(display, screen);
    int screenHeight = DisplayHeight(display, screen);
    if (x + width > screenWidth)
        x = screenWidth - width;
    if (y + height > screenHeight)
        y = screenHeight - height;
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    XSetWindowAttributes attrs;
    attrs.override_redirect = True;
    attrs.background_pixel = WhitePixel(display, screen);
    attrs.border_pixel = BlackPixel(display, screen);
    attrs.event_mask = ButtonPressMask | ButtonReleaseMask |
                        PointerMotionMask | KeyPressMask | ExposureMask;

    Window win = XCreateWindow(display, root, x, y, width, height, 1,
                                CopyFromParent, InputOutput, CopyFromParent,
                                CWOverrideRedirect | CWBackPixel |
                                CWBorderPixel | CWEventMask, &attrs);

    XMapRaised(display, win);
    XFlush(display);

    XftDraw * draw = XftDrawCreate(display, win, visual, cmap);
    XftColor textColor, highlightColor;
    XRenderColor black = {0, 0, 0, 0xffff};
    XRenderColor lightGray = {0xe8e8, 0xe8e8, 0xe8e8, 0xffff};
    XftColorAllocValue(display, visual, cmap, &black, &textColor);
    XftColorAllocValue(display, visual, cmap, &lightGray, &highlightColor);

    // Grabbed so a click anywhere -- not just inside the menu -- is
    // still delivered to us (reported in this window's own coordinate
    // space, which is what makes the simple in-bounds check below
    // work for "clicked outside -> cancel" without any coordinate
    // translation) rather than going to whatever's actually under the
    // cursor, same as any other popup menu.
    XGrabPointer(display, win, True,
                 ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                 GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
    XGrabKeyboard(display, win, True, GrabModeAsync, GrabModeAsync,
                  CurrentTime);

    int hovered = -1;
    int selected = -1;
    bool done = false;
    XEvent ev;

    while (!done)
    {
        XNextEvent(display, &ev);
        switch (ev.type)
        {
            case Expose:
                drawMenu(display, win, draw, font, textColor,
                         highlightColor, items, width, hovered);
                break;

            case MotionNotify:
            {
                int row = ev.xmotion.y / ITEM_HEIGHT;
                int newHovered =
                    (ev.xmotion.x >= 0 && ev.xmotion.x < width &&
                     ev.xmotion.y >= 0 && row < (int)items.size())
                        ? row : -1;
                if (newHovered != hovered)
                {
                    hovered = newHovered;
                    drawMenu(display, win, draw, font, textColor,
                             highlightColor, items, width, hovered);
                }
                break;
            }

            case ButtonRelease:
            {
                int row = ev.xbutton.y / ITEM_HEIGHT;
                if (ev.xbutton.x >= 0 && ev.xbutton.x < width &&
                    ev.xbutton.y >= 0 && row < (int)items.size())
                    selected = row;
                // else: released outside the menu entirely -- selected
                // stays -1, same as Escape
                done = true;
                break;
            }

            case KeyPress:
            {
                KeySym keysym = XLookupKeysym(&ev.xkey, 0);
                if (keysym == XK_Escape)
                    done = true;
                break;
            }
        }
    }

    XUngrabKeyboard(display, CurrentTime);
    XUngrabPointer(display, CurrentTime);

    XftColorFree(display, visual, cmap, &textColor);
    XftColorFree(display, visual, cmap, &highlightColor);
    XftDrawDestroy(draw);
    XftFontClose(display, font);
    XDestroyWindow(display, win);
    XFlush(display);

    return selected;
}
