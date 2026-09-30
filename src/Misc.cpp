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

    static const char * candidateDirs[] = {
        "/usr/share/pixmaps/",
        "/usr/share/icons/hicolor/256x256/apps/",
        "/usr/share/icons/hicolor/128x128/apps/",
        "/usr/share/icons/hicolor/48x48/apps/",
        "/usr/share/icons/hicolor/scalable/apps/",
        "/usr/share/icons/hicolor/48x48/mimetypes/",
        "/usr/share/icons/hicolor/scalable/mimetypes/",
        "/usr/share/icons/hicolor/48x48/places/",
        "/usr/share/icons/hicolor/scalable/places/",
        // hicolor is the spec-mandated fallback theme, but on most real
        // systems it's near-empty -- Adwaita (GNOME/Ubuntu/many others'
        // actual default) is where the real files live. Confirmed by
        // testing against a real filesystem: hicolor had none of these,
        // Adwaita had all of them.
        "/usr/share/icons/Adwaita/256x256/apps/",
        "/usr/share/icons/Adwaita/128x128/apps/",
        "/usr/share/icons/Adwaita/48x48/apps/",
        "/usr/share/icons/Adwaita/scalable/apps/",
        "/usr/share/icons/Adwaita/48x48/mimetypes/",
        "/usr/share/icons/Adwaita/scalable/mimetypes/",
        "/usr/share/icons/Adwaita/48x48/places/",
        "/usr/share/icons/Adwaita/scalable/places/",
        NULL
    };
    static const char * candidateExts[] = { ".png", ".svg", ".xpm", NULL };

    for (int d = 0; candidateDirs[d]; d++)
    {
        for (int e = 0; candidateExts[e]; e++)
        {
            string path = string(candidateDirs[d]) + name + candidateExts[e];
            if (access(path.c_str(), F_OK) == 0)
                return path;
        }
    }

    return "";
}
