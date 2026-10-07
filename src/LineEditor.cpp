/* 
 * Idesk -- LineEditor.cpp
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

#include "LineEditor.h"
#include <X11/Xutil.h>
#include <X11/keysym.h>

using namespace std;

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


void LineEditor::set(const string & s)
{
    text = s;
    cursor = text.size();
    selectAll = !text.empty();
    pendingDead = 0;
}

bool LineEditor::handleKey(XKeyEvent * kev)
{
    char buf[32];
    KeySym ks = NoSymbol;
    int n = XLookupString(kev, buf, sizeof(buf), &ks, NULL);
    bool ctrl = (kev->state & ControlMask) != 0;
    bool alt = (kev->state & Mod1Mask) != 0;

    if (ks == XK_BackSpace)
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
            return false; // Shift alone and the like: keep a pending dead key

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
        if (text.size() + add.size() <= maxBytes)
        {
            text.insert(cursor, add);
            cursor += add.size();
        }
    }
    return true;
}

static int editorTextWidth(Display * d, XftFont * f, const string & s)
{
    if (s.empty())
        return 0;
    XGlyphInfo e;
    XftTextExtentsUtf8(d, f, (const XftChar8 *)s.c_str(), s.length(), &e);
    return e.xOff;
}

int LineEditor::scrollFor(Display * d, XftFont * f, int visibleW) const
{
    int cursorPx = editorTextWidth(d, f, text.substr(0, cursor));
    return cursorPx > visibleW ? cursorPx - visibleW : 0;
}

void LineEditor::placeCursorAt(Display * d, XftFont * f, int px)
{
    selectAll = false;
    pendingDead = 0;
    size_t best = 0;
    int bestDist = px < 0 ? -px : px; // boundary 0 is at x = 0
    size_t pos = 0;
    while (pos < text.size())
    {
        pos = nextCharEnd(text, pos);
        int dist = editorTextWidth(d, f, text.substr(0, pos)) - px;
        if (dist < 0)
            dist = -dist;
        if (dist < bestDist)
        {
            bestDist = dist;
            best = pos;
        }
    }
    cursor = best;
}
