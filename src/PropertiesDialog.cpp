/* 
 * Idesk -- PropertiesDialog.cpp
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

#include "PropertiesDialog.h"
#include "LineEditor.h"
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xft/Xft.h>
#include <sys/select.h>
#include <unistd.h>
#include <csignal>
#include <algorithm>

using namespace std;

// Defined in App.cpp, set by the SIGTERM/SIGINT handler (see TextInput.cpp).
extern volatile sig_atomic_t quitRequested;

static const int PAD = 16;
static const int WIDTH = 560;
static const int GAP = 8;

static int textWidth(Display * d, XftFont * f, const string & s)
{
    if (s.empty())
        return 0;
    XGlyphInfo e;
    XftTextExtentsUtf8(d, f, (const XftChar8 *)s.c_str(), s.length(), &e);
    return e.xOff;
}

struct Form
{
    Display * display;
    Window win;
    XftDraw * draw;
    XftFont * font;
    XftColor black, gray, white, selection, hintColor, panel, accent;
    string title, footer, hint;
    vector<PropField> * fields;
    vector<LineEditor> ed;
    int focus;
    int labelW, boxX, boxW, rowH, firstY;
};

static int rowTop(const Form & f, size_t i)
{
    return f.firstY + (int)i * (f.rowH + GAP);
}

static void drawForm(Form & f)
{
    XClearWindow(f.display, f.win);
    int ascent = f.font->ascent;
    int lineH = ascent + f.font->descent;

    XftDrawStringUtf8(f.draw, &f.black, f.font, PAD, PAD + ascent,
                       (const XftChar8 *)f.title.c_str(), f.title.length());

    for (size_t i = 0; i < f.fields->size(); i++)
    {
        const PropField & pf = (*f.fields)[i];
        const LineEditor & e = f.ed[i];
        int top = rowTop(f, i);
        bool focused = ((int)i == f.focus);

        XftDrawStringUtf8(f.draw, pf.editable ? &f.black : &f.hintColor, f.font,
                           PAD, top + (f.rowH - lineH) / 2 + ascent,
                           (const XftChar8 *)pf.label.c_str(), pf.label.length());

        // frame (accent colour when focused) around the interior
        XftDrawRect(f.draw, focused ? &f.accent : &f.gray, f.boxX, top, f.boxW, f.rowH);
        XftDrawRect(f.draw, pf.editable ? &f.white : &f.panel, f.boxX + 1, top + 1,
                     f.boxW - 2, f.rowH - 2);

        int visibleW = f.boxW - 12;
        int cursorPx = textWidth(f.display, f.font, e.text.substr(0, e.cursor));
        int scroll = (focused && cursorPx > visibleW) ? cursorPx - visibleW : 0;
        int textX = f.boxX + 6 - scroll;
        int baseline = top + (f.rowH - lineH) / 2 + ascent;

        XRectangle clip;
        clip.x = f.boxX + 3;
        clip.y = top + 1;
        clip.width = f.boxW - 6;
        clip.height = f.rowH - 2;
        XftDrawSetClipRectangles(f.draw, 0, 0, &clip, 1);

        if (focused && e.selectAll && !e.text.empty())
            XftDrawRect(f.draw, &f.selection, textX, top + 3,
                         textWidth(f.display, f.font, e.text), f.rowH - 6);
        XftDrawStringUtf8(f.draw, pf.editable ? &f.black : &f.hintColor, f.font,
                           textX, baseline, (const XftChar8 *)e.text.c_str(),
                           e.text.length());
        if (focused && !e.selectAll)
            XftDrawRect(f.draw, &f.black, textX + cursorPx, top + 4, 1, f.rowH - 8);

        XftDrawSetClip(f.draw, NULL);
    }

    int y = rowTop(f, f.fields->size());
    if (!f.footer.empty())
    {
        XRectangle clip;
        clip.x = PAD;
        clip.y = y;
        clip.width = WIDTH - PAD * 2;
        clip.height = lineH + 4;
        XftDrawSetClipRectangles(f.draw, 0, 0, &clip, 1);
        XftDrawStringUtf8(f.draw, &f.hintColor, f.font, PAD, y + ascent,
                           (const XftChar8 *)f.footer.c_str(), f.footer.length());
        XftDrawSetClip(f.draw, NULL);
        y += lineH + GAP;
    }
    XftDrawStringUtf8(f.draw, &f.hintColor, f.font, PAD, y + ascent,
                       (const XftChar8 *)f.hint.c_str(), f.hint.length());
}

// next editable field after `from` in direction `dir` (+1/-1), wrapping;
// -1 if there is none
static int stepFocus(const Form & f, int from, int dir)
{
    int n = (int)f.fields->size();
    for (int k = 1; k <= n; k++)
    {
        int i = ((from + dir * k) % n + n) % n;
        if ((*f.fields)[i].editable)
            return i;
    }
    return -1;
}

static void setFocus(Form & f, int i)
{
    if (i < 0)
        return;
    f.focus = i;
    f.ed[i].selectAll = !f.ed[i].text.empty();
    f.ed[i].cursor = f.ed[i].text.size();
}

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

bool showPropertiesDialog(Display * display, int screen, Window root,
                          Visual * visual, Colormap cmap,
                          const string & title, vector<PropField> & fields,
                          const string & footer)
{
    if (fields.empty())
        return false;

    XftFont * font = XftFontOpenName(display, screen, "Sans-10");
    if (!font)
        return false;

    Form f;
    f.display = display;
    f.font = font;
    f.fields = &fields;
    f.title = title;
    f.footer = footer;
    f.hint = "Tab: next field     Enter: accept     Esc: cancel";

    int lineH = font->ascent + font->descent;
    f.rowH = lineH + 10;
    f.labelW = 0;
    for (size_t i = 0; i < fields.size(); i++)
        f.labelW = max(f.labelW, textWidth(display, font, fields[i].label));
    f.boxX = PAD + f.labelW + 12;
    f.boxW = WIDTH - PAD - f.boxX;
    f.firstY = PAD + lineH + 10;

    int height = rowTop(f, fields.size());
    if (!footer.empty())
        height += lineH + GAP;
    height += lineH + PAD;

    int winX = (DisplayWidth(display, screen) - WIDTH) / 2;
    int winY = (DisplayHeight(display, screen) - height) / 2;

    XSetWindowAttributes attrs;
    attrs.override_redirect = True;
    attrs.background_pixel = WhitePixel(display, screen);
    attrs.border_pixel = BlackPixel(display, screen);
    attrs.event_mask = ExposureMask | KeyPressMask | ButtonPressMask;

    f.win = XCreateWindow(display, root, winX, winY, WIDTH, height, 1,
                          CopyFromParent, InputOutput, CopyFromParent,
                          CWOverrideRedirect | CWBackPixel | CWBorderPixel |
                          CWEventMask, &attrs);
    XStoreName(display, f.win, "idesk-ng properties");
    XMapRaised(display, f.win);
    XFlush(display);

    f.draw = XftDrawCreate(display, f.win, visual, cmap);
    XRenderColor c;
    c.alpha = 0xffff;
    c.red = c.green = c.blue = 0;
    XftColorAllocValue(display, visual, cmap, &c, &f.black);
    c.red = c.green = c.blue = 0x8888;
    XftColorAllocValue(display, visual, cmap, &c, &f.gray);
    c.red = c.green = c.blue = 0xffff;
    XftColorAllocValue(display, visual, cmap, &c, &f.white);
    c.red = 0xb000; c.green = 0xd000; c.blue = 0xffff;
    XftColorAllocValue(display, visual, cmap, &c, &f.selection);
    c.red = c.green = c.blue = 0x5555;
    XftColorAllocValue(display, visual, cmap, &c, &f.hintColor);
    c.red = c.green = c.blue = 0xeeee;
    XftColorAllocValue(display, visual, cmap, &c, &f.panel);
    c.red = 0x2000; c.green = 0x6000; c.blue = 0xc000;
    XftColorAllocValue(display, visual, cmap, &c, &f.accent);

    f.ed.resize(fields.size());
    for (size_t i = 0; i < fields.size(); i++)
    {
        f.ed[i].set(fields[i].value);
        f.ed[i].maxBytes = 1000;
        f.ed[i].selectAll = false;
        f.ed[i].cursor = 0;
    }
    f.focus = -1;
    setFocus(f, stepFocus(f, (int)fields.size() - 1, 1)); // first editable

    // see TextInput.cpp: the keyboard grab is what routes typing here
    int gk = GrabNotViewable;
    for (int i = 0; i < 20; i++)
    {
        gk = XGrabKeyboard(display, f.win, True, GrabModeAsync, GrabModeAsync,
                           CurrentTime);
        if (gk == GrabSuccess)
            break;
        usleep(50000);
    }
    XGrabPointer(display, f.win, False, ButtonPressMask | ButtonReleaseMask,
                 GrabModeAsync, GrabModeAsync, None, None, CurrentTime);

    bool accepted = false;
    bool done = (gk != GrabSuccess) || f.focus < 0;
    XEvent ev;

    if (!done)
        drawForm(f);

    while (!done)
    {
        if (!nextEventOrQuit(display, &ev))
            break;

        if (ev.type == Expose)
            drawForm(f);
        else if (ev.type == ButtonPress)
        {
            int x = ev.xbutton.x, y = ev.xbutton.y;
            if (x < 0 || x >= WIDTH || y < 0 || y >= height)
                done = true;
            else
            {
                for (size_t i = 0; i < fields.size(); i++)
                    if (fields[i].editable && x >= f.boxX && x < f.boxX + f.boxW &&
                        y >= rowTop(f, i) && y < rowTop(f, i) + f.rowH)
                    {
                        if ((int)i != f.focus)
                        {
                            f.ed[f.focus].selectAll = false;
                            setFocus(f, (int)i);
                            drawForm(f);
                        }
                        break;
                    }
            }
        }
        else if (ev.type == KeyPress)
        {
            KeySym ks = XLookupKeysym(&ev.xkey, 0);
            bool shift = (ev.xkey.state & ShiftMask) != 0;

            if (ks == XK_Return || ks == XK_KP_Enter)
            {
                accepted = true;
                done = true;
            }
            else if (ks == XK_Escape)
                done = true;
            else if (ks == XK_Tab || ks == XK_ISO_Left_Tab || ks == XK_Down ||
                     ks == XK_Up)
            {
                int dir = (ks == XK_ISO_Left_Tab || ks == XK_Up || shift) ? -1 : 1;
                f.ed[f.focus].selectAll = false;
                f.ed[f.focus].pendingDead = 0;
                setFocus(f, stepFocus(f, f.focus, dir));
                drawForm(f);
            }
            else if (f.ed[f.focus].handleKey(&ev.xkey))
                drawForm(f);
        }
    }

    XUngrabKeyboard(display, CurrentTime);
    XUngrabPointer(display, CurrentTime);

    XftColorFree(display, visual, cmap, &f.black);
    XftColorFree(display, visual, cmap, &f.gray);
    XftColorFree(display, visual, cmap, &f.white);
    XftColorFree(display, visual, cmap, &f.selection);
    XftColorFree(display, visual, cmap, &f.hintColor);
    XftColorFree(display, visual, cmap, &f.panel);
    XftColorFree(display, visual, cmap, &f.accent);
    XftDrawDestroy(f.draw);
    XftFontClose(display, font);
    XDestroyWindow(display, f.win);
    XFlush(display);

    if (accepted)
        for (size_t i = 0; i < fields.size(); i++)
            if (fields[i].editable)
                fields[i].value = f.ed[i].text;
    return accepted;
}
