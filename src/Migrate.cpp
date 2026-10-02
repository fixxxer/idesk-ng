/* 
 * Idesk -- Migrate.cpp
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

#include "Migrate.h"
#include "IconLayout.h"
#include "Database.h"
#include <dirent.h>
#include <sys/stat.h>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <cstdio>

using namespace std;

// Same idesktop-directory resolution as DesktopConfig::loadIcons() --
// kept duplicated here rather than shared, since both are short and
// shared helpers would add a dependency between an interactive-startup
// file and a one-shot CLI utility for no real benefit.
static string resolveIdesktopDir()
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

    return idesktopDir;
}

// Desktop Entry values can't contain a literal newline; a .lnk field
// could in principle have one pasted in. Collapse to a space rather
// than silently truncating or producing a broken .desktop file.
static string sanitizeForDesktopValue(const string & s)
{
    string out = s;
    for (size_t i = 0; i < out.size(); i++)
        if (out[i] == '\n' || out[i] == '\r')
            out[i] = ' ';
    return out;
}

bool runMigration()
{
    string idesktopDir = resolveIdesktopDir();

    struct dirent ** files;
    int fileCount = scandir(idesktopDir.c_str(), &files, 0, alphasort);
    if (fileCount == -1)
    {
        cerr << "idesk-ng --migrate-to-desktop: nothing found in " << idesktopDir << "\n";
        return true; // nothing to migrate isn't an error
    }

    int migrated = 0, skipped = 0, errors = 0;

    for (int i = 0; i < fileCount; i++)
    {
        string filename = files[i]->d_name;
        free(files[i]);

        if (filename.size() <= 4 || filename.substr(filename.size() - 4) != ".lnk")
            continue;

        string lnkPath = idesktopDir + filename;
        string baseName = filename.substr(0, filename.size() - 4);
        string desktopPath = idesktopDir + baseName + ".desktop";

        struct stat st;
        if (stat(desktopPath.c_str(), &st) == 0)
        {
            cerr << "Skipping \"" << filename << "\": \"" << baseName
                 << ".desktop\" already exists\n";
            skipped++;
            continue;
        }

        Database db(lnkPath, false);
        Table & table = db.Query("Icon");
        if (!table.isValid())
        {
            cerr << "Skipping \"" << filename << "\": not a valid .lnk (no Icon table)\n";
            errors++;
            continue;
        }

        string caption = table.Query("Caption");
        string tooltip = table.Query("ToolTip.Caption");
        string icon = table.Query("Icon");
        string width = table.Query("Width");
        string height = table.Query("Height");
        int x = atoi(table.Query("X").c_str());
        int y = atoi(table.Query("Y").c_str());

        string command;
        if (table.ArrayExists("Command"))
        {
            vector<string> commandArray = table.QueryArray("Command");
            if (!commandArray.empty())
                command = commandArray[0];
        }
        else
            command = table.Query("Command");

        if (command.empty())
        {
            cerr << "Skipping \"" << filename << "\": no Command to migrate\n";
            errors++;
            continue;
        }

        ofstream out(desktopPath.c_str());
        if (!out.is_open())
        {
            cerr << "Error: could not write \"" << desktopPath << "\"\n";
            errors++;
            continue;
        }

        out << "[Desktop Entry]\n";
        out << "Type=Application\n";
        out << "Name=" << sanitizeForDesktopValue(caption) << "\n";
        if (!tooltip.empty())
            out << "Comment=" << sanitizeForDesktopValue(tooltip) << "\n";
        out << "Exec=" << sanitizeForDesktopValue(command) << "\n";
        if (!icon.empty())
            out << "Icon=" << sanitizeForDesktopValue(icon) << "\n";
        if (!width.empty())
            out << "X-Idesk-Width=" << width << "\n";
        if (!height.empty())
            out << "X-Idesk-Height=" << height << "\n";
        out.close();

        seedLayoutPosition(desktopPath, x, y);

        string backupPath = lnkPath + ".bak";
        if (rename(lnkPath.c_str(), backupPath.c_str()) != 0)
            cerr << "Warning: migrated \"" << filename
                 << "\" but could not rename the original to \"" << backupPath << "\"\n";

        cerr << "Migrated \"" << filename << "\" -> \"" << baseName
             << ".desktop\" (position " << x << "," << y << " seeded)\n";
        migrated++;
    }
    free(files);

    cerr << "\nidesk-ng --migrate-to-desktop: " << migrated << " migrated, "
         << skipped << " skipped, " << errors << " errors\n";

    return errors == 0;
}
