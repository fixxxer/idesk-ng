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
static const int FRAME = 1;

struct MenuColors
{
    XftColor text, selectedText, background, border, highlight;
};

// Openbox-like: light panel inside a thin darker frame; the row under the
// pointer (or the keyboard cursor) is filled with an accent colour and its
// text turns white, so it is always obvious which item a click will pick.
static void drawMenu(Display * display, Window win, XftDraw * draw,
                      XftFont * font, const MenuColors & colors,
                      const vector<string> & items, int width, int height,
                      int hovered)
{
    // Redraw the whole thing each time rather than tracking damage --
    // this is a handful of text rows, not worth the bookkeeping.
    XftDrawRect(draw, &colors.border, 0, 0, width, height);
    XftDrawRect(draw, &colors.background, FRAME, FRAME,
                width - FRAME * 2, height - FRAME * 2);

    for (size_t i = 0; i < items.size(); i++)
    {
        int rowTop = FRAME + (int)i * ITEM_HEIGHT;
        bool selected = ((int)i == hovered);

        if (selected)
            XftDrawRect(draw, &colors.highlight, FRAME, rowTop,
                        width - FRAME * 2, ITEM_HEIGHT);

        int baseline = rowTop + PADDING_Y + font->ascent;
        XftDrawStringUtf8(draw, selected ? &colors.selectedText : &colors.text,
                           font, FRAME + PADDING_X, baseline,
                           (const XftChar8 *)items[i].c_str(),
                           items[i].length());
    }
    XFlush(display);
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
    int width = maxTextWidth + PADDING_X * 2 + FRAME * 2;
    int height = (int)items.size() * ITEM_HEIGHT + FRAME * 2;

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
    attrs.border_pixel = BlackPixel(display, screen); // frame is drawn inside
    attrs.event_mask = ButtonPressMask | ButtonReleaseMask |
                        PointerMotionMask | KeyPressMask | ExposureMask;

    Window win = XCreateWindow(display, root, x, y, width, height, 0,
                                CopyFromParent, InputOutput, CopyFromParent,
                                CWOverrideRedirect | CWBackPixel |
                                CWBorderPixel | CWEventMask, &attrs);

    XMapRaised(display, win);
    XFlush(display);

    XftDraw * draw = XftDrawCreate(display, win, visual, cmap);
    MenuColors colors;
    XRenderColor c;
    c.alpha = 0xffff;
    c.red = c.green = c.blue = 0x2222;
    XftColorAllocValue(display, visual, cmap, &c, &colors.text);
    c.red = c.green = c.blue = 0xffff;
    XftColorAllocValue(display, visual, cmap, &c, &colors.selectedText);
    c.red = c.green = c.blue = 0xf3f3;
    XftColorAllocValue(display, visual, cmap, &c, &colors.background);
    c.red = c.green = c.blue = 0x7070;
    XftColorAllocValue(display, visual, cmap, &c, &colors.border);
    c.red = 0x2d2d; c.green = 0x6b6b; c.blue = 0xc8c8;
    XftColorAllocValue(display, visual, cmap, &c, &colors.highlight);

    // Grabbed so a click anywhere -- not just inside the menu -- is
    // still delivered to us (reported in this window's own coordinate
    // space, which is what makes the simple in-bounds check below
    // work for "clicked outside -> cancel" without any coordinate
    // translation) rather than going to whatever's actually under the
    // cursor, same as any other popup menu.
    // owner_events=False so a click on another of this process's windows
    // (another icon) is reported in the menu's coordinate space and counts
    // as outside, instead of being delivered to that icon's window with
    // coordinates that could land inside the menu's bounds by accident.
    XGrabPointer(display, win, False,
                 ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                 GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
    XGrabKeyboard(display, win, True, GrabModeAsync, GrabModeAsync,
                  CurrentTime);

    // which row a point (in the menu window's own coordinates) is on, or -1
    struct RowAt
    {
        int w, count;
        int operator()(int px, int py) const
        {
            int row = (py - FRAME) / ITEM_HEIGHT;
            return (px >= FRAME && px < w - FRAME && py >= FRAME &&
                    row < count) ? row : -1;
        }
    } rowAt = { width, (int)items.size() };

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
                drawMenu(display, win, draw, font, colors, items, width,
                         height, hovered);
                break;

            case MotionNotify:
            {
                int newHovered = rowAt(ev.xmotion.x, ev.xmotion.y);
                if (newHovered != hovered)
                {
                    hovered = newHovered;
                    drawMenu(display, win, draw, font, colors, items, width,
                             height, hovered);
                }
                break;
            }

            case ButtonRelease:
            {
                selected = rowAt(ev.xbutton.x, ev.xbutton.y);
                // else: released outside the menu entirely -- selected
                // stays -1, same as Escape
                done = true;
                break;
            }

            case KeyPress:
            {
                KeySym keysym = XLookupKeysym(&ev.xkey, 0);
                int n = (int)items.size();
                if (keysym == XK_Escape)
                    done = true;
                else if (keysym == XK_Down || keysym == XK_KP_Down)
                    hovered = (hovered < 0) ? 0 : (hovered + 1) % n;
                else if (keysym == XK_Up || keysym == XK_KP_Up)
                    hovered = (hovered < 0) ? n - 1 : (hovered + n - 1) % n;
                else if ((keysym == XK_Return || keysym == XK_KP_Enter) &&
                         hovered >= 0)
                {
                    selected = hovered;
                    done = true;
                }
                if (!done)
                    drawMenu(display, win, draw, font, colors, items, width,
                             height, hovered);
                break;
            }
        }
    }

    XUngrabKeyboard(display, CurrentTime);
    XUngrabPointer(display, CurrentTime);

    XftColorFree(display, visual, cmap, &colors.text);
    XftColorFree(display, visual, cmap, &colors.selectedText);
    XftColorFree(display, visual, cmap, &colors.background);
    XftColorFree(display, visual, cmap, &colors.border);
    XftColorFree(display, visual, cmap, &colors.highlight);
    XftDrawDestroy(draw);
    XftFontClose(display, font);
    XDestroyWindow(display, win);
    XFlush(display);

    return selected;
}
