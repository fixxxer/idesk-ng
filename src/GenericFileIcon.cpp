/* 
 * Idesk -- GenericFileIcon.cpp
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

#include "GenericFileIcon.h"
#include "Misc.h"
#include <gio/gio.h>
#include <iostream>

using namespace std;

// Wraps a path in single quotes for safe use inside the `/bin/sh -c`
// command idesk-ng already executes actions with, escaping any embedded
// single quote as '\'' (the standard POSIX shell trick).
static string shellQuoteSingle(const string & s)
{
    string out = "'";
    for (size_t i = 0; i < s.size(); i++)
    {
        if (s[i] == '\'')
            out += "'\\''";
        else
            out += s[i];
    }
    out += "'";
    return out;
}

GenericFileIcon::GenericFileIcon(const string & path) : Table()
{
    size_t lastSlash = path.find_last_of('/');
    string caption = (lastSlash == string::npos) ? path : path.substr(lastSlash + 1);

    string iconPath;

    GFile * file = g_file_new_for_path(path.c_str());
    GFileInfo * info = g_file_query_info(file, G_FILE_ATTRIBUTE_STANDARD_ICON,
                                          G_FILE_QUERY_INFO_NONE, NULL, NULL);
    if (info)
    {
        GIcon * icon = g_file_info_get_icon(info); // owned by info, no extra ref needed
        if (icon && G_IS_THEMED_ICON(icon))
        {
            const char * const * names = g_themed_icon_get_names(G_THEMED_ICON(icon));
            for (int i = 0; names && names[i] && iconPath.empty(); i++)
                iconPath = resolveIconThemeName(names[i]);
        }
        g_object_unref(info);
    }
    g_object_unref(file);

    if (iconPath.empty())
        cerr << "Warning: could not resolve an icon for \"" << path
             << "\" (best-effort lookup only, see DESIGN.md) -- icon will be blank\n";

    // Always "valid" -- Title makes Table::isValid() true. Any file that
    // exists can become an icon this way; there is nothing to reject.
    Title = "Icon";

    Set("Caption", caption);
    Set("Command", "xdg-open " + shellQuoteSingle(path));
    Set("Icon", iconPath);
}
