/* 
 * Idesk -- PropertiesDialog.h
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

#ifndef PROPERTIES_DIALOG_CLASS
#define PROPERTIES_DIALOG_CLASS

#include <X11/Xlib.h>
#include <string>
#include <vector>
using namespace std;

/*
 * The context menu's Properties: a small modal form of labelled one-line
 * fields, same family as TextInput.h (runs inside the live session on its
 * existing Display, plain Xlib/Xft, grabs pointer and keyboard while open).
 *
 * Each field is editable or shown read-only (grey). Tab / Down and
 * Shift+Tab / Up move between the editable ones, a click on one focuses it;
 * editing is the TextInput's (LineEditor: UTF-8, accents, Home/End...).
 * Enter accepts, Escape or a click outside the dialog cancels. `footer` is
 * one line of grey information under the fields.
 *
 * Returns true if accepted, with every editable field's `value` replaced by
 * what is in the form; false if cancelled (values untouched).
 */
struct PropField
{
    string label;
    string value;
    bool editable;

    PropField(const string & l, const string & v, bool e)
        : label(l), value(v), editable(e) {}
};

bool showPropertiesDialog(Display * display, int screen, Window root,
                          Visual * visual, Colormap cmap,
                          const string & title, vector<PropField> & fields,
                          const string & footer);

#endif
