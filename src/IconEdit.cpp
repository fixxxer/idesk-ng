/* 
 * Idesk -- IconEdit.cpp
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

#include "IconEdit.h"
#include "Database.h"
#include <gio/gio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <fstream>
#include <vector>

using namespace std;

static bool isSymlink(const string & path)
{
    struct stat st;
    return lstat(path.c_str(), &st) == 0 && S_ISLNK(st.st_mode);
}

static string trim(const string & s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == string::npos)
        return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool setLnkKey(const string & path, const string & key, const string & value,
               string & error)
{
    if (isSymlink(path))
    {
        error = "This icon is a symbolic link; changing it would modify the original file.";
        return false;
    }

    // Database's constructor _exit()s the whole process when the file
    // can't be opened, so check first.
    if (access(path.c_str(), R_OK | W_OK) != 0)
    {
        error = "Cannot read and write " + path;
        return false;
    }

    Database db(path, false);
    Table & table = db.Query("Icon");
    if (!table.isValid())
    {
        error = "Not a valid .lnk icon file: " + path;
        return false;
    }

    table.Set(key, value);
    db.Write();
    return true;
}

bool setLnkCaption(const string & path, const string & caption, string & error)
{
    return setLnkKey(path, "Caption", caption, error);
}

bool getLnkKey(const string & path, const string & key, string & value,
               bool & isArray)
{
    value.clear();
    isArray = false;
    if (access(path.c_str(), R_OK) != 0)
        return false;
    Database db(path, false);
    Table & table = db.Query("Icon");
    if (!table.isValid())
        return false;
    if (table.ArrayExists(key))
    {
        isArray = true;
        vector<string> all = table.QueryArray(key);
        if (!all.empty())
            value = all[0];
    }
    else
        value = table.Query(key);
    return true;
}

bool getDesktopKey(const string & path, const string & key, string & value)
{
    value.clear();
    ifstream in(path.c_str());
    if (!in.is_open())
        return false;
    bool inEntry = false;
    string line;
    while (getline(in, line))
    {
        string t = trim(line);
        if (!t.empty() && t[0] == '[')
        {
            inEntry = (t == "[Desktop Entry]");
            continue;
        }
        if (!inEntry)
            continue;
        size_t eq = line.find('=');
        if (eq != string::npos && trim(line.substr(0, eq)) == key)
        {
            value = trim(line.substr(eq + 1));
            return true;
        }
    }
    return false;
}

bool setDesktopName(const string & path, const string & name, string & error)
{
    return setDesktopKey(path, "Name", name, error);
}

bool setDesktopKey(const string & path, const string & key,
                   const string & value, string & error)
{
    if (isSymlink(path))
    {
        error = "This launcher is a symbolic link; changing it would modify the original file.";
        return false;
    }

    ifstream in(path.c_str());
    if (!in.is_open())
    {
        error = "Cannot read " + path;
        return false;
    }

    vector<string> lines;
    string line;
    while (getline(in, line))
        lines.push_back(line);
    in.close();

    bool inEntry = false, replaced = false;
    int headerIndex = -1;
    for (size_t i = 0; i < lines.size(); i++)
    {
        string t = trim(lines[i]);
        if (!t.empty() && t[0] == '[')
        {
            inEntry = (t == "[Desktop Entry]");
            if (inEntry && headerIndex < 0)
                headerIndex = (int)i;
            continue;
        }

        if (!inEntry || replaced)
            continue;

        // only the plain key: Name[es]= has a different key, and a
        // comment line like "# Name=x" has the key "# Name"
        size_t eq = lines[i].find('=');
        if (eq != string::npos && trim(lines[i].substr(0, eq)) == key)
        {
            lines[i] = key + "=" + value;
            replaced = true;
        }
    }

    if (!replaced)
    {
        if (headerIndex < 0)
        {
            error = "No [Desktop Entry] group in " + path;
            return false;
        }
        lines.insert(lines.begin() + headerIndex + 1, key + "=" + value);
    }

    struct stat st;
    mode_t mode = (stat(path.c_str(), &st) == 0) ? (st.st_mode & 07777) : 0644;

    string tmp = path + ".idesk-tmp";
    {
        ofstream out(tmp.c_str(), ios::out | ios::trunc | ios::binary);
        if (!out.is_open())
        {
            error = "Cannot write in the folder of " + path;
            return false;
        }
        for (size_t i = 0; i < lines.size(); i++)
            out << lines[i] << "\n";
        out.flush();
        if (out.fail())
        {
            out.close();
            remove(tmp.c_str());
            error = "Cannot write " + path;
            return false;
        }
    }

    chmod(tmp.c_str(), mode);
    if (rename(tmp.c_str(), path.c_str()) != 0)
    {
        remove(tmp.c_str());
        error = "Cannot replace " + path;
        return false;
    }
    return true;
}

bool renamePlainFile(const string & path, const string & newName,
                     string & newPath, string & error)
{
    GFile * file = g_file_new_for_path(path.c_str());
    GError * gerr = NULL;
    GFile * renamed = g_file_set_display_name(file, newName.c_str(), NULL, &gerr);
    g_object_unref(file);

    if (!renamed)
    {
        error = gerr ? gerr->message : "Could not rename the file.";
        if (gerr)
            g_error_free(gerr);
        return false;
    }

    char * p = g_file_get_path(renamed);
    newPath = p ? p : "";
    g_free(p);
    g_object_unref(renamed);
    return !newPath.empty();
}
