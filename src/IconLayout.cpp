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
#include <unistd.h>
#include <fstream>
#include <cstdlib>
#include <algorithm>

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
    // Kiosk mode never writes; and where layout.db can't be created or written
    // (a read-only configuration) the position simply isn't remembered --
    // checked before opening it, because Database exits on a file it can't open.
    if (kioskMode)
        return;

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
        if (!touch)
            return;
        touch.close();
    }
    else if (access(layoutDbPath.c_str(), R_OK | W_OK) != 0)
        return;

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

bool getLayoutPosition(const string & path, int & outX, int & outY)
{
    string layoutDbPath = getLayoutDbPath();

    struct stat st;
    if (stat(layoutDbPath.c_str(), &st) != 0 || access(layoutDbPath.c_str(), R_OK) != 0)
        return false; // no (readable) layout DB -- nothing saved for anyone

    Database db(layoutDbPath, false);
    Table & table = db.Query(path);
    if (!table.isValid())
        return false;

    outX = atoi(table.Query("X").c_str());
    outY = atoi(table.Query("Y").c_str());
    return true;
}

void removeLayoutPosition(const string & path)
{
    string layoutDbPath = getLayoutDbPath();

    struct stat st;
    if (stat(layoutDbPath.c_str(), &st) != 0)
        return; // no layout DB yet -- nothing to remove

    Database db(layoutDbPath, false);

    bool found = false;
    for (vector<Table>::iterator it = db.Tables.begin();
         it != db.Tables.end(); ++it)
    {
        if (it->Title == path)
        {
            db.Tables.erase(it);
            found = true;
            break; // Title is a unique key here (seedLayoutPosition()
                   // overwrites rather than duplicates), so the first
                   // match is the only one
        }
    }

    if (found)
        db.Write(layoutDbPath);
}

bool getLayoutPinned(const string & path)
{
    string layoutDbPath = getLayoutDbPath();

    struct stat st;
    if (stat(layoutDbPath.c_str(), &st) != 0)
        return false;

    Database db(layoutDbPath, false);
    Table & table = db.Query(path);
    // "Pinned: true" is what the first version wrote; still honoured when read
    return table.isValid() &&
           (getUpper(table.Query("Draggable")) == "FALSE" ||
            getUpper(table.Query("Pinned")) == "TRUE");
}

void setLayoutPinned(const string & path, bool pinned, int x, int y)
{
    if (kioskMode)
        return;
    if (pinned)
    {
        seedLayoutPosition(path, x, y); // makes sure the entry exists
    }
    else if (!getLayoutPinned(path))
        return; // nothing to clear

    string layoutDbPath = getLayoutDbPath();
    Database db(layoutDbPath, false);
    Table & table = db.Query(path);
    if (!table.isValid())
        return;

    for (size_t i = 0; i < table.Label.size(); )
        if (table.Label[i] == "Draggable" || table.Label[i] == "Pinned")
        {
            table.Label.erase(table.Label.begin() + i);
            table.Value.erase(table.Value.begin() + i);
        }
        else
            i++;
    if (pinned)
        table.Set("Draggable", "false");

    db.Write(layoutDbPath);
}
