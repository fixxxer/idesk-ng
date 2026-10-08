/* vim:tabstop=4:expandtab:shiftwidth=4
 * 
 * Idesk -- Misc.cpp
 *
 * Copyright (c) 2002, Chris (nikon) (nikon@sc.rr.com)
 * All rights reserved.
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

#include "Misc.h"
#include <unistd.h>
#include <dirent.h>
#include <fstream>
#include <vector>
#include <sys/stat.h>
#include <cstdio>

extern char ** args;

bool kioskMode = false;

string getUpper(const string & str)
{
    string work = str;
    transform(work.begin(), work.end(), work.begin(),(int(*)(int)) toupper);
    return work;
}
    
/*void Reboot()
{
    execvp( args[0], args );
}*/

string itos(int i) // convert int to string
{
 stringstream s;
 s << i;
 return s.str();
}

// Reads the icon theme via the `gsettings` command-line tool, run as a
// separate process, rather than linking GSettings/GDBus directly into
// idesk-ng itself.
//
// Found crashing on real hardware (reproducible only outside gdb --
// the classic signature of a race gdb's own slowdown masks): calling
// g_settings_new() in-process pulls in a GDBus connection to dconf,
// which spins up GLib's own worker threads. idesk-ng's pre-existing,
// 20-year-old command-launch code (XDesktopContainer.cpp) uses raw
// fork()+execl() -- and fork() in a process that has *any* other
// threads running is a well-known source of exactly this kind of
// hard-to-reproduce crash (only the forking thread survives into the
// child; if a GDBus worker thread held a lock at that instant, the
// child can deadlock or corrupt state).
//
// Shelling out avoids the problem at its root: all of GSettings'
// GDBus/threading machinery runs inside the separate `gsettings`
// process, so idesk-ng's own process never becomes multi-threaded in
// the first place, and its existing fork()-based command execution
// stays exactly as safe as it always was.
static string getGSettingsIconThemeName()
{
    FILE * pipe = popen("gsettings get org.gnome.desktop.interface icon-theme 2>/dev/null", "r");
    if (!pipe)
        return "";

    string result;
    char buf[256];
    while (fgets(buf, sizeof(buf), pipe) != NULL)
        result += buf;

    int status = pclose(pipe);
    if (status != 0)
        return ""; // gsettings not installed, schema missing, no session bus, etc.

    // output looks like: 'Yaru-dark'\n -- strip quotes and whitespace
    size_t a = result.find_first_not_of(" \t\r\n'");
    size_t b = result.find_last_not_of(" \t\r\n'");
    if (a == string::npos)
        return "";

    return result.substr(a, b - a + 1);
}

// Reads the icon theme the user actually has configured, so icon lookup
// doesn't have to guess a theme name that happens to match whichever
// distro this was developed/tested on. Checked in order: GTK3/GTK4
// settings.ini, the older GTK2 .gtkrc-2.0, then KDE Plasma's
// kdeglobals. Returns "" if none of these exist or none set a theme --
// a perfectly normal case on a minimal install that never had a full
// GNOME/KDE/XFCE session configure one.
static string getConfiguredIconThemeName()
{
    char * home = getenv("HOME");
    if (!home)
        return "";

    struct Candidate { string file; string key; };
    Candidate candidates[] = {
        { string(home) + "/.config/gtk-4.0/settings.ini", "gtk-icon-theme-name" },
        { string(home) + "/.config/gtk-3.0/settings.ini", "gtk-icon-theme-name" },
        { string(home) + "/.gtkrc-2.0", "gtk-icon-theme-name" },
        { string(home) + "/.config/kdeglobals", "Theme" },
    };

    for (size_t c = 0; c < sizeof(candidates)/sizeof(candidates[0]); c++)
    {
        ifstream f(candidates[c].file.c_str());
        if (!f.is_open())
            continue;

        string line;
        while (getline(f, line))
        {
            size_t keyPos = line.find(candidates[c].key);
            if (keyPos == string::npos)
                continue;

            size_t eq = line.find('=', keyPos);
            if (eq == string::npos)
                continue;

            string value = line.substr(eq + 1);
            // strip whitespace and any quotes (.gtkrc-2.0 quotes its values)
            size_t a = value.find_first_not_of(" \t\"'");
            size_t b = value.find_last_not_of(" \t\"'\r\n");
            if (a == string::npos)
                continue;

            f.close();
            return value.substr(a, b - a + 1);
        }
        f.close();
    }

    return "";
}

// Discovers icon themes actually installed on this system by looking for
// a directory containing an index.theme file -- the one thing every real
// icon theme is required to have, regardless of what it's named. This is
// what makes icon lookup work on a distro/theme combination nobody ever
// tested this on: a hardcoded theme name list would silently find
// nothing everywhere. "hicolor" is deliberately excluded here -- the
// caller always tries it last, separately, as the universal fallback.
static void listInstalledThemes(const string & dir, vector<string> & themes)
{
    DIR * d = opendir(dir.c_str());
    if (!d)
        return;

    struct dirent * entry;
    while ((entry = readdir(d)) != NULL)
    {
        string name = entry->d_name;
        if (name == "." || name == ".." || name == "hicolor")
            continue;

        string indexFile = dir + "/" + name + "/index.theme";
        struct stat st;
        if (stat(indexFile.c_str(), &st) == 0)
            themes.push_back(name);
    }
    closedir(d);
}

string resolveIconThemeName(const string & name)
{
    if (name.empty())
        return "";

    static const char * sizes[] = { "256x256", "128x128", "48x48", "scalable", NULL };
    static const char * categories[] = { "apps", "mimetypes", "places", "status", "categories", NULL };
    static const char * candidateExts[] = { ".png", ".svg", ".xpm", NULL };

    if (access("/usr/share/pixmaps/", F_OK) == 0)
    {
        for (int e = 0; candidateExts[e]; e++)
        {
            string path = string("/usr/share/pixmaps/") + name + candidateExts[e];
            if (access(path.c_str(), F_OK) == 0)
                return path;
        }
    }

    // Build the theme search order without assuming which distro or
    // desktop this is running on:
    //   1. whatever the user actually has configured (GTK/KDE settings),
    //      if anything -- the one choice guaranteed to match what they
    //      see everywhere else on their system
    //   2. every theme actually installed under /usr/share/icons,
    //      ~/.local/share/icons, ~/.icons (anything with an index.theme),
    //      discovered rather than guessed by name -- this is what makes
    //      lookup work on a theme (Yaru, breeze, Papirus, elementary,
    //      whatever) nobody hardcoded a name for
    //   3. hicolor last, always -- the one theme the spec guarantees
    //      exists, even though it's usually near-empty in practice
    vector<string> themes;

    string configured = getGSettingsIconThemeName();
    if (configured.empty())
        configured = getConfiguredIconThemeName();
    if (!configured.empty())
        themes.push_back(configured);

    char * home = getenv("HOME");
    listInstalledThemes("/usr/share/icons", themes);
    if (home)
    {
        listInstalledThemes(string(home) + "/.local/share/icons", themes);
        listInstalledThemes(string(home) + "/.icons", themes);
    }

    themes.push_back("hicolor");

    for (size_t t = 0; t < themes.size(); t++)
    {
        for (int s = 0; sizes[s]; s++)
        {
            for (int c = 0; categories[c]; c++)
            {
                for (int e = 0; candidateExts[e]; e++)
                {
                    string path = string("/usr/share/icons/") + themes[t] + "/" +
                                  sizes[s] + "/" + categories[c] + "/" +
                                  name + candidateExts[e];
                    if (access(path.c_str(), F_OK) == 0)
                        return path;
                }
            }
        }
    }

    // Nothing matched the requested name. Falling back to "" here used
    // to silently discard the whole icon downstream -- XIcon's isRaster()
    // /isSvg() both reject an empty filename as "Unknown file format"
    // and mark the icon invalid entirely (caption and all), not merely
    // picture-less, despite what this function's own caller warns
    // ("icon will be blank"). Rather than touch that deeply-coupled,
    // null-unsafe rendering code, fall back to "image-missing" -- the
    // actual freedesktop.org Icon Naming Specification name for exactly
    // this situation ("an image that could not be loaded"), which
    // real icon themes are expected to ship -- before giving up.
    if (name != "image-missing")
    {
        string fallback = resolveIconThemeName("image-missing");
        if (!fallback.empty())
            return fallback;
    }

    return "";
}
