/* 
 * Idesk -- FreeDesktopIcon.cpp
 *
 * Copyright (c) 2013, neagix
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

#include "FreeDesktopIcon.h"
#include "Misc.h"
#include <fstream>
#include <iostream>
#include <unistd.h>

using namespace std;

static inline string fdiTrim(const string & s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == string::npos)
        return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Strips Desktop Entry Exec field codes (%f %F %u %U %i %c %k %d %D %n %N
// %v %m) and unescapes a literal %% to %, since idesk launches icons with
// no file/URL arguments to substitute.
string FreeDesktopIcon::stripExecFieldCodes(const string & exec)
{
    string out;
    out.reserve(exec.size());

    for (size_t i = 0; i < exec.size(); i++)
    {
        if (exec[i] == '%' && i + 1 < exec.size())
        {
            char code = exec[i + 1];
            if (code == '%')
            {
                out += '%';
                i++;
                continue;
            }
            if (string("fFuUickdDnNvm").find(code) != string::npos)
            {
                i++; // swallow the field code, drop it entirely
                continue;
            }
        }
        out += exec[i];
    }

    return fdiTrim(out);
}

// Best-effort Icon= resolution. An absolute path is trusted as-is (same
// convention .lnk already uses). A bare icon-theme name is resolved via
// the shared resolveIconThemeName() helper (see Misc.h) -- NOT a full
// Icon Theme Specification resolver; see the header comment and
// DESIGN.md.
string FreeDesktopIcon::resolveIconPath(const string & icon)
{
    if (icon.empty())
        return "";

    if (icon[0] == '/')
        return icon;

    string resolved = resolveIconThemeName(icon);
    if (resolved.empty())
        cerr << "Warning: could not resolve icon theme name \"" << icon
             << "\" to a file (only a best-effort lookup is implemented, "
             << "not the full Icon Theme Specification) -- icon will be blank\n";
    return resolved;
}

FreeDesktopIcon::FreeDesktopIcon(const string & filename) : Table()
{
    visible = true; // default; overridden below if Hidden/NoDisplay is set

    ifstream file(filename.c_str());
    if (!file.is_open())
    {
        cerr << "Error: cannot open \"" << filename << "\"\n";
        return; // Title stays "" -> isValid() stays false
    }

    string currentGroup;
    bool sawDesktopEntryGroup = false;

    string type, name, comment, exec, tryExec, url, icon;
    string hidden, noDisplay, xIdeskWidth, xIdeskHeight;

    string line;
    while (getline(file, line))
    {
        line = fdiTrim(line);

        if (line.empty() || line[0] == '#')
            continue;

        if (line[0] == '[' && line[line.size() - 1] == ']')
        {
            currentGroup = line.substr(1, line.size() - 2);
            if (currentGroup == "Desktop Entry")
                sawDesktopEntryGroup = true;
            continue;
        }

        if (currentGroup != "Desktop Entry")
            continue; // ignore [Desktop Action ...] and any other group

        size_t eq = line.find('=');
        if (eq == string::npos)
            continue;

        string key = fdiTrim(line.substr(0, eq));
        string value = fdiTrim(line.substr(eq + 1));

        // Skip localized variants (Name[es], Comment[fr_FR], ...) -- the
        // base key above is guaranteed by spec and is what we fall back to.
        if (key.find('[') != string::npos)
            continue;

        if (key == "Type")
            type = value;
        else if (key == "Name")
            name = value;
        else if (key == "Comment")
            comment = value;
        else if (key == "Exec")
            exec = value;
        else if (key == "TryExec")
            tryExec = value;
        else if (key == "Icon")
            icon = value;
        else if (key == "URL")
            url = value;
        else if (key == "Hidden")
            hidden = value;
        else if (key == "NoDisplay")
            noDisplay = value;
        else if (key == "X-Idesk-Width")
            xIdeskWidth = value;
        else if (key == "X-Idesk-Height")
            xIdeskHeight = value;
    }

    file.close();

    if (!sawDesktopEntryGroup)
    {
        cerr << "Error: \"" << filename << "\" has no [Desktop Entry] group\n";
        return;
    }

    if (type.empty())
        type = "Application"; // spec technically requires Type=, but a
                               // missing key is common enough to tolerate

    if (type != "Application" && type != "Link")
    {
        cerr << "Error: \"" << filename << "\" has unsupported Type="
             << type << " (only Application and Link are handled)\n";
        return;
    }

    string command;
    if (type == "Application")
    {
        if (exec.empty())
        {
            cerr << "Error: \"" << filename
                 << "\" is Type=Application with no Exec=\n";
            return;
        }
        command = stripExecFieldCodes(exec);
    }
    else // Type=Link
    {
        if (url.empty())
        {
            cerr << "Error: \"" << filename
                 << "\" is Type=Link with no URL=\n";
            return;
        }
        command = "xdg-open " + url;
    }

    visible = !(getUpper(hidden) == "TRUE" || getUpper(noDisplay) == "TRUE");

    // Valid from here on -- mark the Table valid the same way a .lnk
    // "table Icon ... end" block is (Table::isValid() just checks Title).
    Title = "Icon";

    Set("Caption", name);
    Set("ToolTip.Caption", comment);
    Set("Command", command);
    Set("Icon", resolveIconPath(icon));

    // Width/Height default to 48 when not set via X-Idesk-Width/Height --
    // real-world .desktop files never carry those vendor keys, and with no
    // fallback here they'd default to 0 (DesktopIconConfig does
    // atoi(Query("Width")), and atoi("") is 0), which used to crash SVG
    // icon loading outright (see XImlib2Image.cpp). 48 matches a common
    // desktop icon size and gives something reasonable to look at.
    Set("Width", xIdeskWidth.empty() ? "48" : xIdeskWidth);
    Set("Height", xIdeskHeight.empty() ? "48" : xIdeskHeight);
}
