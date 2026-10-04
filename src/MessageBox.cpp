/* 
 * Idesk -- MessageBox.cpp
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

#include "MessageBox.h"
#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>
#include <vector>
#include <sstream>
#include <iostream>

using namespace std;

// Simple greedy word-wrap: adds words to the current line as long as
// they fit within maxWidth (measured with the real font, not an
// estimate), starting a new line when the next word wouldn't fit. A
// single word wider than maxWidth on its own (a long URL with no
// spaces, say) is left to overflow its line rather than being split
// mid-word -- an acceptable, rare degenerate case, not worth the
// extra complexity of hyphenation.
static vector<string> wrapText(Display * display, XftFont * font,
                                const string & text, int maxWidth)
{
    vector<string> lines;
    istringstream iss(text);
    string word, currentLine;

    while (iss >> word)
    {
        string candidate = currentLine.empty() ? word : currentLine + " " + word;
        XGlyphInfo extents;
        XftTextExtentsUtf8(display, font,
                            (const XftChar8 *)candidate.c_str(),
                            candidate.length(), &extents);

        if (extents.xOff > maxWidth && !currentLine.empty())
        {
            lines.push_back(currentLine);
            currentLine = word;
        }
        else
            currentLine = candidate;
    }

    if (!currentLine.empty())
        lines.push_back(currentLine);
    if (lines.empty())
        lines.push_back(""); // degenerate case: empty message text

    return lines;
}

bool showMessage(const string & text)
{
    Display * display = XOpenDisplay(NULL);
    if (!display)
    {
        cerr << "idesk-ng --show-message: cannot open X display\n";
        return false;
    }

    int screen = DefaultScreen(display);
    Window root = RootWindow(display, screen);
    Visual * visual = DefaultVisual(display, screen);
    Colormap cmap = DefaultColormap(display, screen);

    // Same font-name convention XImlib2Caption/XImlib2ToolTip already
    // use elsewhere in this codebase -- fontconfig resolves "Sans" to
    // whatever sane default is actually installed, same as those.
    XftFont * font = XftFontOpenName(display, screen, "Sans-10");
    if (!font)
    {
        cerr << "idesk-ng --show-message: could not load a font\n";
        XCloseDisplay(display);
        return false;
    }

    const int maxTextWidth = 360;
    const int padding = 16;
    const int lineSpacing = 4;

    vector<string> lines = wrapText(display, font, text, maxTextWidth);

    int lineHeight = font->ascent + font->descent;
    int textHeight = (int)lines.size() * lineHeight +
                      ((int)lines.size() - 1) * lineSpacing;
    int windowWidth = maxTextWidth + padding * 2;
    int windowHeight = textHeight + padding * 2;

    int screenWidth = DisplayWidth(display, screen);
    int screenHeight = DisplayHeight(display, screen);
    int winX = (screenWidth - windowWidth) / 2;
    int winY = (screenHeight - windowHeight) / 2;

    XSetWindowAttributes attrs;
    attrs.override_redirect = True;
    attrs.background_pixel = WhitePixel(display, screen);
    attrs.border_pixel = BlackPixel(display, screen);
    attrs.event_mask = ButtonPressMask | KeyPressMask | ExposureMask;

    Window win = XCreateWindow(display, root, winX, winY,
                                windowWidth, windowHeight, 1,
                                CopyFromParent, InputOutput, CopyFromParent,
                                CWOverrideRedirect | CWBackPixel |
                                CWBorderPixel | CWEventMask, &attrs);

    XStoreName(display, win, "idesk-ng");
    XMapRaised(display, win);
    XFlush(display);

    XftDraw * draw = XftDrawCreate(display, win, visual, cmap);
    XftColor textColor;
    XRenderColor black;
    black.red = 0; black.green = 0; black.blue = 0; black.alpha = 0xffff;
    XftColorAllocValue(display, visual, cmap, &black, &textColor);

    bool dismissed = false;
    XEvent ev;
    while (!dismissed)
    {
        XNextEvent(display, &ev);
        switch (ev.type)
        {
            case Expose:
            {
                int y = padding + font->ascent;
                for (size_t i = 0; i < lines.size(); i++)
                {
                    XftDrawStringUtf8(draw, &textColor, font, padding, y,
                                       (const XftChar8 *)lines[i].c_str(),
                                       lines[i].length());
                    y += lineHeight + lineSpacing;
                }
                break;
            }
            case ButtonPress:
            case KeyPress:
                dismissed = true;
                break;
        }
    }

    XftColorFree(display, visual, cmap, &textColor);
    XftDrawDestroy(draw);
    XftFontClose(display, font);
    XDestroyWindow(display, win);
    XCloseDisplay(display);

    return true;
}
