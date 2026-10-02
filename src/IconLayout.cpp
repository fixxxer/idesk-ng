/* 
 * Idesk -- IconLayout.cpp
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

#include "IconLayout.h"
#include "Database.h"
#include "Misc.h"
#include <sys/stat.h>
#include <fstream>
#include <cstdlib>

using namespace std;

string getLayoutDbPath()
{
    char * tmp;
    string xdgConfigHome, homeDirectory;

    tmp = getenv("XDG_CONFIG_HOME");
    if (tmp)
        xdgConfigHome.assign(tmp);

    tmp = getenv("HOME");
    if (tmp)
        homeDirectory.assign(tmp);

    if (xdgConfigHome.empty())
        xdgConfigHome = homeDirectory + "/.config";

    string idesktopDir = xdgConfigHome + "/idesktop/";

    struct stat dirStat;
    if (stat(idesktopDir.c_str(), &dirStat) != 0 || !S_ISDIR(dirStat.st_mode))
        idesktopDir = homeDirectory + "/.idesktop/";

    return idesktopDir + "layout.db";
}

void seedLayoutPosition(const string & path, int x, int y)
{
    string layoutDbPath = getLayoutDbPath();

    // Database's constructor either _exit(1)s or seeds unrelated
    // ideskrc-style defaults when the file can't be opened -- neither
    // is right for "start an empty layout DB". Touching an empty file
    // first makes the constructor's ifstream.is_open() succeed, so it
    // just finds zero tables and starts clean.
    struct stat st;
    if (stat(layoutDbPath.c_str(), &st) != 0)
    {
        ofstream touch(layoutDbPath.c_str());
        touch.close();
    }

    Database db(layoutDbPath, false);

    Table & existing = db.Query(path);
    if (existing.isValid())
    {
        existing.Set("X", itos(x));
        existing.Set("Y", itos(y));
    }
    else
    {
        Table & t = db.AddTable(path);
        t.Set("X", itos(x));
        t.Set("Y", itos(y));
    }

    db.Write(layoutDbPath);
}
