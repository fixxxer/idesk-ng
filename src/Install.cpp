/* 
 * Idesk -- Install.cpp
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

#include "Install.h"
#include "Database.h"
#include <map>
#include <sys/stat.h>
#include <fstream>
#include <iostream>
#include <cstdlib>

using namespace std;

// Deliberately NOT the same as Migrate.cpp/IconLayout.cpp's version of
// this function, despite the near-identical name: those exist to find
// an idesktop dir that (by definition of what they do) already has
// files in it -- so preferring an *existing* directory over the
// XDG-preferred one, and never creating anything, is the right
// behavior there. installIdeskrc()/installTrashIcon() specifically
// target the opposite case -- a brand new system where neither
// directory exists yet -- where that same logic would silently prefer
// the legacy ~/.idesktop/ path and then fail outright trying to write
// into a directory that was never created. This version: still
// respects an existing ~/.idesktop/ setup if that's what the person
// already has, but defaults to creating the modern,
// XDG-preferred ~/.config/idesktop/ on a genuinely fresh system rather
// than falling back to the legacy path.
static string resolveOrCreateIdesktopDir()
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

    string preferredDir = xdgConfigHome + "/idesktop/";
    string legacyDir = homeDirectory + "/.idesktop/";

    struct stat dirStat;
    if (stat(preferredDir.c_str(), &dirStat) == 0 && S_ISDIR(dirStat.st_mode))
        return preferredDir;
    if (stat(legacyDir.c_str(), &dirStat) == 0 && S_ISDIR(dirStat.st_mode))
        return legacyDir;

    // Neither exists -- fresh system. Create the modern path,
    // including ~/.config itself if that's missing too.
    mkdir(xdgConfigHome.c_str(), 0755);
    mkdir(preferredDir.c_str(), 0755);
    return preferredDir;
}

bool installIdeskrc()
{
    string idesktopDir = resolveOrCreateIdesktopDir();
    string ideskrcPath = idesktopDir + "ideskrc";

    struct stat st;
    if (stat(ideskrcPath.c_str(), &st) == 0)
    {
        cerr << "idesk-ng --install-ideskrc: \"" << ideskrcPath
             << "\" already exists, leaving it alone\n";
        return true; // not an error -- nothing to do
    }

    // fallbackToDefaultRc=true: since the file doesn't exist (just
    // confirmed above), this synthesizes the exact same Config/Actions
    // tables normal startup already falls back to in memory -- Write()
    // just persists that to disk instead of only using it in-process.
    Database db(ideskrcPath, true);
    db.Write(ideskrcPath);

    cerr << "idesk-ng --install-ideskrc: wrote factory defaults to \""
         << ideskrcPath << "\"\n";
    return true;
}

// Bounded table of common desktop-Linux languages -> their word for
// "Trash", keyed by the 2-letter language code from LC_ALL/LANG (e.g.
// "de_DE.UTF-8" -> "de"). Not every language that exists -- English is
// the fallback for anything not listed here (see DESIGN.md for why a
// full, generic translation wasn't attempted: it would need pulling in
// GNOME/KDE's own translation catalogs, exactly the kind of dependency
// this project tests against not having).
static string trashWordForLanguage(const string & lang)
{
    static map<string, string> words;
    if (words.empty())
    {
        words["es"] = "Papelera";
        words["de"] = "Papierkorb";
        words["fr"] = "Corbeille";
        words["it"] = "Cestino";
        words["pt"] = "Lixeira";
        words["ru"] = "Корзина";
        words["ja"] = "ゴミ箱";
        words["zh"] = "回收站";
        words["ko"] = "휴지통";
        words["nl"] = "Prullenbak";
        words["pl"] = "Kosz";
        words["tr"] = "Çöp Kutusu";
        words["ar"] = "سلة المهملات";
        words["sv"] = "Papperskorg";
        words["cs"] = "Koš";
        words["el"] = "Κάδος Απορριμμάτων";
        words["he"] = "אשפה";
        words["hu"] = "Kuka";
        words["fi"] = "Roskakori";
        words["da"] = "Papirkurv";
        words["no"] = "Papirkurv";
        words["nb"] = "Papirkurv";
        words["uk"] = "Кошик";
        words["ro"] = "Coș de gunoi";
    }

    // Traditional-Chinese locales specifically (zh_TW, zh_HK) use
    // different characters than the simplified default below -- checked
    // before the generic 2-letter table so it isn't shadowed by the
    // "zh" entry above.
    if (lang.substr(0, 5) == "zh_TW" || lang.substr(0, 5) == "zh_HK")
        return "垃圾桶";

    string code = lang.substr(0, 2);
    for (size_t i = 0; i < code.size(); i++)
        code[i] = tolower(code[i]);

    map<string, string>::iterator it = words.find(code);
    return it != words.end() ? it->second : "Trash";
}

static string detectSessionLanguage()
{
    char * tmp = getenv("LC_ALL");
    if (tmp && *tmp)
        return string(tmp);
    tmp = getenv("LANG");
    if (tmp && *tmp)
        return string(tmp);
    return "en"; // no locale set at all -- English either way
}

bool installTrashIcon()
{
    string idesktopDir = resolveOrCreateIdesktopDir();
    string trashPath = idesktopDir + "trash.desktop";

    struct stat st;
    if (stat(trashPath.c_str(), &st) == 0)
    {
        cerr << "idesk-ng --install-trash-icon: \"" << trashPath
             << "\" already exists, leaving it alone\n";
        return true; // not an error -- nothing to do
    }

    string name = trashWordForLanguage(detectSessionLanguage());

    ofstream out(trashPath.c_str());
    if (!out.is_open())
    {
        cerr << "idesk-ng --install-trash-icon: could not write \""
             << trashPath << "\"\n";
        return false;
    }

    out << "[Desktop Entry]\n";
    out << "Type=Application\n";
    out << "Name=" << name << "\n";
    out << "Icon=user-trash\n";
    // xdg-open's own failure mode (no file manager registered for
    // folders, or not even installed at all) is to silently do nothing
    // -- the fallback is the only thing that tells the person anything
    // happened at all. idesk-ng's own --show-message (MessageBox.{h,cpp})
    // rather than zenity: no extra runtime dependency beyond idesk-ng
    // itself, consistent with testing this project on minimal systems
    // with nothing GNOME/KDE-adjacent installed (see DESIGN.md). Always
    // in English regardless of session language -- see DESIGN.md.
    out << "Exec=xdg-open ~/.local/share/Trash/files || idesk "
           "--show-message \"No file manager found to open the Trash "
           "folder. Install one such as Nautilus, Dolphin, or PCManFM.\"\n";
    out << "X-Idesk-Protected=true\n";
    out.close();

    cerr << "idesk-ng --install-trash-icon: wrote \"" << trashPath
         << "\" (Name=" << name << ")\n";
    return true;
}
