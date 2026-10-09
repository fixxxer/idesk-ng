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
#include <cstdio>
#include <algorithm>
#include <cctype>
#include <ctime>
#include <dirent.h>

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

// ---------------------------------------------------------------------------
// Icons sent to the Trash
// ---------------------------------------------------------------------------

static const string TRASH_TITLE = "trash:";
static const long TRASH_MEMORY_SECONDS = 30L * 24 * 3600;
static const size_t TRASH_MEMORY_MAX = 200;

static bool isTrashTitle(const string & title)
{
    return title.compare(0, TRASH_TITLE.size(), TRASH_TITLE) == 0;
}

// the home Trash's info directory: $XDG_DATA_HOME/Trash/info
static string homeTrashInfoDir()
{
    string base;
    char * tmp = getenv("XDG_DATA_HOME");
    if (tmp && tmp[0] != '\0')
        base = tmp;
    else
    {
        tmp = getenv("HOME");
        base = string(tmp ? tmp : "") + "/.local/share";
    }
    return base + "/Trash/info/";
}

static string percentDecode(const string & s)
{
    string out;
    for (size_t i = 0; i < s.size(); i++)
    {
        if (s[i] == '%' && i + 2 < s.size() && isxdigit((unsigned char)s[i + 1]) &&
            isxdigit((unsigned char)s[i + 2]))
        {
            out += (char)strtol(s.substr(i + 1, 2).c_str(), NULL, 16);
            i += 2;
        }
        else
            out += s[i];
    }
    return out;
}

bool isInTrash(const string & path)
{
    string infoDir = homeTrashInfoDir();
    DIR * dir = opendir(infoDir.c_str());
    if (!dir)
        return false;

    bool found = false;
    struct dirent * e;
    while (!found && (e = readdir(dir)) != NULL)
    {
        string name = e->d_name;
        if (name.size() < 10 || name.compare(name.size() - 10, 10, ".trashinfo") != 0)
            continue;
        ifstream in((infoDir + name).c_str());
        string line;
        for (int n = 0; n < 8 && getline(in, line); n++)
            if (line.compare(0, 5, "Path=") == 0)
            {
                found = (percentDecode(line.substr(5)) == path);
                break;
            }
    }
    closedir(dir);
    return found;
}

// layout.db is there and can be written -- the same preconditions
// seedLayoutPosition() makes, without creating the file
static bool layoutDbWritable(const string & layoutDbPath)
{
    struct stat st;
    return stat(layoutDbPath.c_str(), &st) == 0 &&
           access(layoutDbPath.c_str(), R_OK | W_OK) == 0;
}

void rememberTrashedLayout(const string & path, int x, int y, bool pinned)
{
    if (kioskMode)
        return;

    string layoutDbPath = getLayoutDbPath();
    if (!layoutDbWritable(layoutDbPath))
    {
        // an icon that never had an entry has nothing to remember; one that
        // has can't be written to a layout.db that doesn't exist
        struct stat st;
        if (stat(layoutDbPath.c_str(), &st) == 0)
            return; // exists but read-only
        ofstream touch(layoutDbPath.c_str());
        if (!touch)
            return;
        touch.close();
    }

    Database db(layoutDbPath, false);
    long now = (long)time(NULL);

    // expired entries go first
    for (size_t i = 0; i < db.Tables.size(); )
    {
        if (isTrashTitle(db.Tables[i].Title) &&
            now - atol(db.Tables[i].Query("When").c_str()) > TRASH_MEMORY_SECONDS)
            db.Tables.erase(db.Tables.begin() + i);
        else
            i++;
    }

    // a second remembering of the same path replaces the first
    for (size_t i = 0; i < db.Tables.size(); i++)
        if (db.Tables[i].Title == TRASH_TITLE + path)
        {
            db.Tables.erase(db.Tables.begin() + i);
            break;
        }

    // room for one more: the oldest go
    for (;;)
    {
        size_t count = 0, oldest = 0;
        long oldestWhen = 0;
        for (size_t i = 0; i < db.Tables.size(); i++)
            if (isTrashTitle(db.Tables[i].Title))
            {
                long w = atol(db.Tables[i].Query("When").c_str());
                if (count == 0 || w < oldestWhen)
                    oldest = i, oldestWhen = w;
                count++;
            }
        if (count < TRASH_MEMORY_MAX)
            break;
        db.Tables.erase(db.Tables.begin() + oldest);
    }

    Table & t = db.AddTable(TRASH_TITLE + path);
    t.Set("X", itos(x));
    t.Set("Y", itos(y));
    if (pinned)
        t.Set("Draggable", "false");
    char when[32];
    snprintf(when, sizeof(when), "%ld", now); // seconds since 1970: not an int after 2038
    t.Set("When", when);

    db.Write(layoutDbPath);
}

bool adoptTrashedLayout(const string & path)
{
    if (kioskMode)
        return false;

    string layoutDbPath = getLayoutDbPath();
    if (!layoutDbWritable(layoutDbPath))
        return false;

    Database db(layoutDbPath, false);

    size_t memIndex = db.Tables.size();
    for (size_t i = 0; i < db.Tables.size(); i++)
        if (db.Tables[i].Title == TRASH_TITLE + path)
        {
            memIndex = i;
            break;
        }
    if (memIndex == db.Tables.size())
        return false; // nothing remembered: by far the usual case

    bool hasOwn = db.Query(path).isValid();
    if (!hasOwn && isInTrash(path))
        return false; // the Trash still holds the file that was remembered

    string x = db.Tables[memIndex].Query("X");
    string y = db.Tables[memIndex].Query("Y");
    bool pinned = getUpper(db.Tables[memIndex].Query("Draggable")) == "FALSE";
    db.Tables.erase(db.Tables.begin() + memIndex);

    if (!hasOwn)
    {
        Table & t = db.AddTable(path);
        t.Set("X", x);
        t.Set("Y", y);
        if (pinned)
            t.Set("Draggable", "false");
    }

    db.Write(layoutDbPath);
    return !hasOwn;
}
