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

extern char ** args;

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

string resolveIconThemeName(const string & name)
{
    if (name.empty())
        return "";

    // Built as theme x size x category combinations rather than a long
    // hand-written path list. Themes are tried in priority order:
    //   - Yaru: Ubuntu's actual default icon theme since 18.04 -- found
    //     to be where real icons live on a stock Ubuntu install (e.g.
    //     preferences-system-network only existed here and in Adwaita's
    //     differently-named -symbolic variant, not under the bare name
    //     this function is asked to resolve).
    //   - Adwaita: GNOME's own theme, still common and what many
    //     third-party apps ship assuming.
    //   - hicolor: the spec-mandated universal fallback theme, but
    //     confirmed near-empty on a real system -- kept last.
    // "categories/" (control-panel/settings sections, where
    // preferences-system-network lives) is a real icon context alongside
    // the more obvious apps/mimetypes/places/status.
    static const char * themes[] = { "Yaru", "Adwaita", "hicolor", NULL };
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

    for (int t = 0; themes[t]; t++)
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
