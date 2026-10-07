/* 
 * Idesk -- TextInput.cpp
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

#include "TextInput.h"
#include "LineEditor.h"
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xft/Xft.h>
#include <sys/select.h>
#include <unistd.h>
#include <csignal>

using namespace std;

// Defined in App.cpp, set by the SIGTERM/SIGINT handler. Checked while
// waiting for events so an open dialog can't block a requested shutdown.
extern volatile sig_atomic_t quitRequested;

static const int PAD = 16;
static const int WIDTH = 420;

static int textWidth(Display * d, XftFont * f, const string & s)
{
    if (s.empty())
        return 0;
    XGlyphInfo e;
    XftTextExtentsUtf8(d, f, (const XftChar8 *)s.c_str(), s.length(), &e);
    return e.xOff;
}

struct Ui
{
    Display * display;
    Window win;
    XftDraw * draw;
    XftFont * font;
    XftColor black, gray, white, selection, hintColor;
    int fieldX, fieldY, fieldW, fieldH;
    string title;
    string hint;
};

static void drawUi(const Ui & ui, const string & text, size_t cursor,
                    bool selectAll)
{
    XClearWindow(ui.display, ui.win);
    int ascent = ui.font->ascent;

    XftDrawStringUtf8(ui.draw, &ui.black, ui.font, PAD, PAD + ascent,
                       (const XftChar8 *)ui.title.c_str(), ui.title.length());

    // field: 1px gray frame around a white interior
    XftDrawRect(ui.draw, &ui.gray, ui.fieldX, ui.fieldY, ui.fieldW, ui.fieldH);
    XftDrawRect(ui.draw, &ui.white, ui.fieldX + 1, ui.fieldY + 1,
                 ui.fieldW - 2, ui.fieldH - 2);

    // scroll horizontally so the cursor stays visible in a long name
    int visibleW = ui.fieldW - 12;
    int cursorPx = textWidth(ui.display, ui.font, text.substr(0, cursor));
    int scroll = cursorPx > visibleW ? cursorPx - visibleW : 0;
    int textX = ui.fieldX + 6 - scroll;
    int textW = textWidth(ui.display, ui.font, text);
    int baseline = ui.fieldY +
                    (ui.fieldH - (ascent + ui.font->descent)) / 2 + ascent;

    XRectangle clip;
    clip.x = ui.fieldX + 3;
    clip.y = ui.fieldY + 1;
    clip.width = ui.fieldW - 6;
    clip.height = ui.fieldH - 2;
    XftDrawSetClipRectangles(ui.draw, 0, 0, &clip, 1);

    if (selectAll && !text.empty())
        XftDrawRect(ui.draw, &ui.selection, textX, ui.fieldY + 3, textW,
                     ui.fieldH - 6);
    XftDrawStringUtf8(ui.draw, &ui.black, ui.font, textX, baseline,
                       (const XftChar8 *)text.c_str(), text.length());
    if (!selectAll)
        XftDrawRect(ui.draw, &ui.black, textX + cursorPx, ui.fieldY + 4, 1,
                     ui.fieldH - 8);

    XftDrawSetClip(ui.draw, NULL);

    XftDrawStringUtf8(ui.draw, &ui.hintColor, ui.font, PAD,
                       ui.fieldY + ui.fieldH + 8 + ascent,
                       (const XftChar8 *)ui.hint.c_str(), ui.hint.length());
}

// Waits for the next event, but wakes once a second to check whether a
// shutdown was requested. Returns false if one was.
static bool nextEventOrQuit(Display * display, XEvent * ev)
{
    for (;;)
    {
        if (quitRequested)
            return false;
        if (XPending(display))
        {
            XNextEvent(display, ev);
            return true;
        }
        int fd = ConnectionNumber(display);
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        select(fd + 1, &fds, NULL, NULL, &tv);
    }
}

bool showTextInput(Display * display, int screen, Window root,
                    Visual * visual, Colormap cmap,
                    const string & title, const string & initial,
                    string & result)
{
    XftFont * font = XftFontOpenName(display, screen, "Sans-10");
    if (!font)
        return false;

    int lineH = font->ascent + font->descent;

    Ui ui;
    ui.display = display;
    ui.font = font;
    ui.fieldX = PAD;
    ui.fieldY = PAD + lineH + 8;
    ui.fieldW = WIDTH - PAD * 2;
    ui.fieldH = lineH + 10;
    ui.title = title;
    ui.hint = "Enter: accept     Esc: cancel";
    int height = ui.fieldY + ui.fieldH + 8 + lineH + PAD;

    int winX = (DisplayWidth(display, screen) - WIDTH) / 2;
    int winY = (DisplayHeight(display, screen) - height) / 2;

    XSetWindowAttributes attrs;
    attrs.override_redirect = True;
    attrs.background_pixel = WhitePixel(display, screen);
    attrs.border_pixel = BlackPixel(display, screen);
    attrs.event_mask = ExposureMask | KeyPressMask | ButtonPressMask;

    Window win = XCreateWindow(display, root, winX, winY, WIDTH, height, 1,
                                CopyFromParent, InputOutput, CopyFromParent,
                                CWOverrideRedirect | CWBackPixel |
                                CWBorderPixel | CWEventMask, &attrs);
    XStoreName(display, win, "idesk-ng input");
    ui.win = win;
    XMapRaised(display, win);
    XFlush(display);

    ui.draw = XftDrawCreate(display, win, visual, cmap);
    XRenderColor c;
    c.alpha = 0xffff;
    c.red = c.green = c.blue = 0;
    XftColorAllocValue(display, visual, cmap, &c, &ui.black);
    c.red = c.green = c.blue = 0x8888;
    XftColorAllocValue(display, visual, cmap, &c, &ui.gray);
    c.red = c.green = c.blue = 0xffff;
    XftColorAllocValue(display, visual, cmap, &c, &ui.white);
    c.red = 0xb000; c.green = 0xd000; c.blue = 0xffff;
    XftColorAllocValue(display, visual, cmap, &c, &ui.selection);
    c.red = c.green = c.blue = 0x5555;
    XftColorAllocValue(display, visual, cmap, &c, &ui.hintColor);

    // The keyboard grab is what routes typing to this override_redirect
    // window (nothing gives it focus). It can fail briefly if something
    // else still holds a grab, so retry a few times before giving up.
    int gk = GrabNotViewable;
    for (int i = 0; i < 20; i++)
    {
        gk = XGrabKeyboard(display, win, True, GrabModeAsync, GrabModeAsync,
                           CurrentTime);
        if (gk == GrabSuccess)
            break;
        usleep(50000);
    }
    // owner_events=False: every pointer event is reported in this
    // window's own coordinate space, even a click on another of this
    // process's windows (an icon) -- with True, such a click would be
    // delivered to that icon's window instead.
    XGrabPointer(display, win, False, ButtonPressMask | ButtonReleaseMask,
                 GrabModeAsync, GrabModeAsync, None, None, CurrentTime);

    LineEditor ed;
    ed.set(initial);
    bool accepted = false;
    bool done = (gk != GrabSuccess);
    XEvent ev;

    if (!done)
        drawUi(ui, ed.text, ed.cursor, ed.selectAll);

    while (!done)
    {
        if (!nextEventOrQuit(display, &ev))
            break; // shutdown requested: cancel

        if (ev.type == Expose)
        {
            drawUi(ui, ed.text, ed.cursor, ed.selectAll);
        }
        else if (ev.type == ButtonPress)
        {
            // reported in our own coordinates (owner_events=False): a
            // click outside the dialog cancels it
            if (ev.xbutton.x < 0 || ev.xbutton.x >= WIDTH ||
                ev.xbutton.y < 0 || ev.xbutton.y >= height)
                done = true;
            else if (ev.xbutton.x >= ui.fieldX &&
                     ev.xbutton.x < ui.fieldX + ui.fieldW &&
                     ev.xbutton.y >= ui.fieldY &&
                     ev.xbutton.y < ui.fieldY + ui.fieldH)
            {
                // click in the field: cursor goes to the nearest character
                // boundary (same scroll as drawUi uses)
                int scroll = ed.scrollFor(display, font, ui.fieldW - 12);
                ed.placeCursorAt(display, font,
                                 ev.xbutton.x - (ui.fieldX + 6 - scroll));
                drawUi(ui, ed.text, ed.cursor, ed.selectAll);
            }
        }
        else if (ev.type == KeyPress)
        {
            KeySym ks = XLookupKeysym(&ev.xkey, 0);
            if (ks == XK_Return || ks == XK_KP_Enter)
            {
                accepted = true;
                done = true;
            }
            else if (ks == XK_Escape)
                done = true;
            else if (ed.handleKey(&ev.xkey))
                drawUi(ui, ed.text, ed.cursor, ed.selectAll);
        }
    }

    XUngrabKeyboard(display, CurrentTime);
    XUngrabPointer(display, CurrentTime);

    XftColorFree(display, visual, cmap, &ui.black);
    XftColorFree(display, visual, cmap, &ui.gray);
    XftColorFree(display, visual, cmap, &ui.white);
    XftColorFree(display, visual, cmap, &ui.selection);
    XftColorFree(display, visual, cmap, &ui.hintColor);
    XftDrawDestroy(ui.draw);
    XftFontClose(display, font);
    XDestroyWindow(display, win);
    XFlush(display);

    if (accepted)
        result = ed.text;
    return accepted;
}
