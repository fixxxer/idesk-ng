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

#ifndef XK_dead_grave
#define XK_dead_grave 0xfe50
#endif
#ifndef XK_dead_acute
#define XK_dead_acute 0xfe51
#endif
#ifndef XK_dead_circumflex
#define XK_dead_circumflex 0xfe52
#endif
#ifndef XK_dead_tilde
#define XK_dead_tilde 0xfe53
#endif
#ifndef XK_dead_diaeresis
#define XK_dead_diaeresis 0xfe57
#endif
#ifndef XK_dead_cedilla
#define XK_dead_cedilla 0xfe5b
#endif
#ifndef XK_EuroSign
#define XK_EuroSign 0x20ac
#endif

static const size_t MAX_BYTES = 240;
static const int PAD = 16;
static const int WIDTH = 420;

static void appendUtf8(string & s, unsigned int cp)
{
    if (cp < 0x80)
        s += (char)cp;
    else if (cp < 0x800)
    {
        s += (char)(0xC0 | (cp >> 6));
        s += (char)(0x80 | (cp & 0x3F));
    }
    else if (cp < 0x10000)
    {
        s += (char)(0xE0 | (cp >> 12));
        s += (char)(0x80 | ((cp >> 6) & 0x3F));
        s += (char)(0x80 | (cp & 0x3F));
    }
    else
    {
        s += (char)(0xF0 | (cp >> 18));
        s += (char)(0x80 | ((cp >> 12) & 0x3F));
        s += (char)(0x80 | ((cp >> 6) & 0x3F));
        s += (char)(0x80 | (cp & 0x3F));
    }
}

// Start of the character that ends just before `pos`: step back over
// UTF-8 continuation bytes (10xxxxxx).
static size_t prevCharStart(const string & s, size_t pos)
{
    if (pos == 0)
        return 0;
    pos--;
    while (pos > 0 && (((unsigned char)s[pos]) & 0xC0) == 0x80)
        pos--;
    return pos;
}

static size_t nextCharEnd(const string & s, size_t pos)
{
    if (pos >= s.size())
        return s.size();
    pos++;
    while (pos < s.size() && (((unsigned char)s[pos]) & 0xC0) == 0x80)
        pos++;
    return pos;
}

static bool isDeadKey(KeySym ks)
{
    return ks == XK_dead_grave || ks == XK_dead_acute ||
           ks == XK_dead_circumflex || ks == XK_dead_tilde ||
           ks == XK_dead_diaeresis || ks == XK_dead_cedilla;
}

// The diacritic character itself, for dead key + space (or + a letter
// it can't combine with).
static unsigned int standaloneDead(KeySym ks)
{
    if (ks == XK_dead_grave) return 0x60;
    if (ks == XK_dead_acute) return 0xB4;
    if (ks == XK_dead_circumflex) return 0x5E;
    if (ks == XK_dead_tilde) return 0x7E;
    if (ks == XK_dead_diaeresis) return 0xA8;
    return 0xB8; // cedilla
}

static unsigned int lookupIn(const char * bases, const unsigned int * outs,
                              unsigned int base)
{
    for (int i = 0; bases[i]; i++)
        if ((unsigned int)(unsigned char)bases[i] == base)
            return outs[i];
    return 0;
}

// Latin-1 letters only (which is what the common layouts' dead keys
// produce); every result is a code point below U+0100.
static unsigned int composeDead(KeySym dead, unsigned int base)
{
    static const char acuteB[] = "aeiouyAEIOUY";
    static const unsigned int acute[] =
        {0xE1,0xE9,0xED,0xF3,0xFA,0xFD,0xC1,0xC9,0xCD,0xD3,0xDA,0xDD};
    static const char graveB[] = "aeiouAEIOU";
    static const unsigned int grave[] =
        {0xE0,0xE8,0xEC,0xF2,0xF9,0xC0,0xC8,0xCC,0xD2,0xD9};
    static const char circB[] = "aeiouAEIOU";
    static const unsigned int circ[] =
        {0xE2,0xEA,0xEE,0xF4,0xFB,0xC2,0xCA,0xCE,0xD4,0xDB};
    static const char diaeB[] = "aeiouyAEIOU";
    static const unsigned int diae[] =
        {0xE4,0xEB,0xEF,0xF6,0xFC,0xFF,0xC4,0xCB,0xCF,0xD6,0xDC};
    static const char tildeB[] = "anoANO";
    static const unsigned int tilde[] = {0xE3,0xF1,0xF5,0xC3,0xD1,0xD5};
    static const char cedB[] = "cC";
    static const unsigned int ced[] = {0xE7,0xC7};

    if (dead == XK_dead_acute) return lookupIn(acuteB, acute, base);
    if (dead == XK_dead_grave) return lookupIn(graveB, grave, base);
    if (dead == XK_dead_circumflex) return lookupIn(circB, circ, base);
    if (dead == XK_dead_diaeresis) return lookupIn(diaeB, diae, base);
    if (dead == XK_dead_tilde) return lookupIn(tildeB, tilde, base);
    if (dead == XK_dead_cedilla) return lookupIn(cedB, ced, base);
    return 0;
}

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

    string text = initial;
    size_t cursor = text.size();
    bool selectAll = !text.empty();
    KeySym pendingDead = 0;
    bool accepted = false;
    bool done = (gk != GrabSuccess);
    XEvent ev;

    if (!done)
        drawUi(ui, text, cursor, selectAll);

    while (!done)
    {
        if (!nextEventOrQuit(display, &ev))
            break; // shutdown requested: cancel

        if (ev.type == Expose)
        {
            drawUi(ui, text, cursor, selectAll);
        }
        else if (ev.type == ButtonPress)
        {
            // reported in our own coordinates (owner_events=False): a
            // click outside the dialog cancels it
            if (ev.xbutton.x < 0 || ev.xbutton.x >= WIDTH ||
                ev.xbutton.y < 0 || ev.xbutton.y >= height)
                done = true;
        }
        else if (ev.type == KeyPress)
        {
            char buf[32];
            KeySym ks = NoSymbol;
            int n = XLookupString(&ev.xkey, buf, sizeof(buf), &ks, NULL);
            bool ctrl = (ev.xkey.state & ControlMask) != 0;
            bool alt = (ev.xkey.state & Mod1Mask) != 0;
            bool changed = true;

            if (ks == XK_Return || ks == XK_KP_Enter)
            {
                accepted = true;
                done = true;
            }
            else if (ks == XK_Escape)
                done = true;
            else if (ks == XK_BackSpace)
            {
                if (selectAll)
                {
                    text.clear();
                    cursor = 0;
                    selectAll = false;
                }
                else if (cursor > 0)
                {
                    size_t p = prevCharStart(text, cursor);
                    text.erase(p, cursor - p);
                    cursor = p;
                }
            }
            else if (ks == XK_Delete || ks == XK_KP_Delete)
            {
                if (selectAll)
                {
                    text.clear();
                    cursor = 0;
                    selectAll = false;
                }
                else if (cursor < text.size())
                    text.erase(cursor, nextCharEnd(text, cursor) - cursor);
            }
            else if (ks == XK_Left || ks == XK_KP_Left)
            {
                if (selectAll)
                {
                    cursor = 0;
                    selectAll = false;
                }
                else
                    cursor = prevCharStart(text, cursor);
            }
            else if (ks == XK_Right || ks == XK_KP_Right)
            {
                if (selectAll)
                {
                    cursor = text.size();
                    selectAll = false;
                }
                else
                    cursor = nextCharEnd(text, cursor);
            }
            else if (ks == XK_Home || ks == XK_KP_Home)
            {
                cursor = 0;
                selectAll = false;
            }
            else if (ks == XK_End || ks == XK_KP_End)
            {
                cursor = text.size();
                selectAll = false;
            }
            else if (ctrl && (ks == XK_a || ks == XK_A))
                selectAll = !text.empty();
            else if (isDeadKey(ks))
                pendingDead = ks;
            else
            {
                unsigned int cp = 0;
                if (!ctrl && !alt)
                {
                    if (ks >= 0x20 && ks <= 0x7E)
                        cp = (unsigned int)ks;
                    else if (ks >= 0xA0 && ks <= 0xFF)
                        cp = (unsigned int)ks; // Latin-1 keysym == code point
                    else if (ks >= 0x01000000)
                        cp = (unsigned int)(ks & 0x00FFFFFF);
                    else if (ks == XK_EuroSign)
                        cp = 0x20AC;
                    else if (n == 1 && (unsigned char)buf[0] >= 0x20 &&
                             (unsigned char)buf[0] < 0x7F)
                        cp = (unsigned char)buf[0]; // keypad digits etc.
                }

                if (cp == 0)
                    changed = false; // Shift alone and the like: keep a
                                     // pending dead key waiting
                else
                {
                    string add;
                    if (pendingDead)
                    {
                        unsigned int composed = composeDead(pendingDead, cp);
                        if (composed)
                            appendUtf8(add, composed);
                        else
                        {
                            appendUtf8(add, standaloneDead(pendingDead));
                            if (cp != ' ')
                                appendUtf8(add, cp);
                        }
                        pendingDead = 0;
                    }
                    else
                        appendUtf8(add, cp);

                    if (selectAll)
                    {
                        text.clear();
                        cursor = 0;
                        selectAll = false;
                    }
                    if (text.size() + add.size() <= MAX_BYTES)
                    {
                        text.insert(cursor, add);
                        cursor += add.size();
                    }
                }
            }

            if (!done && changed)
                drawUi(ui, text, cursor, selectAll);
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
        result = text;
    return accepted;
}
