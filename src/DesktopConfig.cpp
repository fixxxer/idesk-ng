/* vim:tabstop=4:expandtab:shiftwidth=4
 * 
 * Idesk -- DesktopConfig.cpp
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

#include "DesktopConfig.h"
#include "Util.h"
#include "FreeDesktopIcon.h"
#include "GenericFileIcon.h"
#include <sys/stat.h>

// the initializer list just sets the program defaults for non-necessary options
DesktopConfig::DesktopConfig(Database db, string ideskrcFile) :
                             AbstractConfig(ideskrcFile)
{
    wasLoaded = db.wasLoaded;
    common = new CommonOptions();

    Table table = db.Query("Config");
    
    if (!table.isValid())
    {
        cerr << "Can't find config file or missing 'Config'"
             << " table in the config file.\n";
        _exit(1);
    }

    // handle the configure options
    setOptions(table);
    loadIcons();
}

DesktopConfig::~DesktopConfig()
{
    delete common;
}

void DesktopConfig::setOptions(Table table)
{
    setDefaults();

    //DesktopContainer only options
    setDesktopOnlyOptions(table);
    
    //Options that can be overridden in the icon config
    common->setCommonDefaults();
    common->setOptions(table);

    // TODO - add functionality for other protected members in DesktopConfig.h
}

void DesktopConfig::setDefaults()
{
    isLocked = false;
    snapOn = false;
    snapWidth = 1;
    snapHeight = 1;
    startSnapTop = startSnapLeft = true;
    
    fileBackground = "None";
    colorBackground = "None";
    sourceBackground = "None";
    delayBackground = 0; //Default Inactive
    modeBackground = "STRETCH";
    
    transparency = 0;
    
    fontNameTip = "Arial";
    fontSizeTip = 10;
    foreColorTip = "#000000";
    backColorTip = "#FFFACD";
    captionTipOnHover = false;
    captionTipPlacement = "Bottom";

    // on by default: replicates what GNOME/KDE/XFCE already do for their
    // own desktop icons. Off via Desktop.AutoIcons: false in ideskrc for
    // anyone who wants ~/Desktop to stay curated (.lnk/.desktop only),
    // same as ~/.config/idesktop/ always is.
    autoIconizeDesktop = true;
}

void DesktopConfig::setDesktopOnlyOptions(Table table)
{
    string tmpStr;
    
    //locking
    if (getUpper(table.Query("Locked")) == "TRUE")
        isLocked = true;
    else if (getUpper(table.Query("Locked")) == "FALSE")
        isLocked = false;

    //snap options
    if (getUpper(table.Query("IconSnap")) == "TRUE")
        snapOn = true;
    else if (getUpper(table.Query("IconSnap")) == "FALSE")
        snapOn = false;

    snapWidth = atoi(table.Query("SnapWidth").c_str());
    snapHeight = atoi(table.Query("SnapHeight").c_str());

    tmpStr = getUpper(table.Query("SnapOrigin"));
    
    if (tmpStr == "BOTTOMRIGHT")
    {
        startSnapLeft = false;
        startSnapTop = false;
    }
    else if (tmpStr == "BOTTOMLEFT")
        startSnapTop = false;
    else if (tmpStr == "TOPRIGHT")
        startSnapLeft = false;
    // last case automatically handled with default of TOPLEFT

    if (getUpper(table.Query("Desktop.AutoIcons")) == "TRUE")
        autoIconizeDesktop = true;
    else if (getUpper(table.Query("Desktop.AutoIcons")) == "FALSE")
        autoIconizeDesktop = false;
    
     //File Background
      if (table.Query("Background.File") != "")
	    fileBackground = table.Query("Background.File");
      
      if(fileBackground != "" && fileBackground != "None") {
	      struct stat b;
	      string directory(getenv("HOME"));
	      char * s = (char *)fileBackground.c_str();
	      directory = s[0]=='~' ? directory + &s[1]:s;
	      if( stat( directory.c_str(), &b ) < 0 ){
		      cerr << "[idesk] Background file \"" << fileBackground << "\" not found." << endl;
		      fileBackground = "None";
	      }else{
		      fileBackground = directory;  
	      }
      }
      
       //Color Background
      if (table.Query("Background.Color") != "")
	    colorBackground = table.Query("Background.Color");
      
      //Source Background
      if (table.Query("Background.Source") != "")
	      sourceBackground = table.Query("Background.Source");
      
      if(sourceBackground != "" && sourceBackground != "None"){
	      struct stat b;
	      string directory(getenv("HOME"));
	      char * s = (char *)sourceBackground.c_str();
	      directory = s[0]=='~' ? directory + &s[1]:s;
	      if( stat( directory.c_str(), &b ) < 0 ) {
		      sourceBackground ="None";
		      cerr << "[idesk] Background source not found." << endl;
	      }else{
		      sourceBackground = directory;  
	      }
      }
      
      //Delay Background
      if (table.Query("Background.Delay") != "")
	      delayBackground  = atoi(table.Query("Background.Delay").c_str());
      //Max 1 year
      if(delayBackground > 525600) 
	      delayBackground = 525600;
      //Min 0 minute with this value should inactive
      if(delayBackground < 0) 
	      delayBackground = 0;
      
      //Mode Background
      if (getUpper(table.Query("Background.Mode")) == "SCALE")
	      modeBackground = getUpper(table.Query("Background.Mode"));
      if (getUpper(table.Query("Background.Mode")) == "MIRROR")
	      modeBackground = getUpper(table.Query("Background.Mode")); 
      if (getUpper(table.Query("Background.Mode")) == "FIT")
	      modeBackground = getUpper(table.Query("Background.Mode"));   
      if (getUpper(table.Query("Background.Mode")) == "CENTER")
	      modeBackground = getUpper(table.Query("Background.Mode"));
      if (getUpper(table.Query("Background.Mode")) == "STRETCH")
	      modeBackground = getUpper(table.Query("Background.Mode"));   
      
      //transparency
      if (table.Query("Transparency") != "")
        transparency = atoi(table.Query("Transparency").c_str());
      
      //font options for tooltip
      if (table.Query("ToolTip.FontName") != "")
	    fontNameTip = table.Query("ToolTip.FontName"); 
      
      if (table.Query("ToolTip.FontSize") != "")
	    fontSizeTip = atoi(table.Query("ToolTip.FontSize").c_str());
      if (fontSizeTip>256)
              fontSizeTip = 16;
      
      if (table.Query("ToolTip.ForeColor") != "")
	    foreColorTip = table.Query("ToolTip.ForeColor");
      if (table.Query("ToolTip.BackColor") != "")
	    backColorTip = table.Query("ToolTip.BackColor");
      
      //captionTipOnHover
      if (getUpper(table.Query("ToolTip.CaptionOnHover")) == "TRUE")
        captionTipOnHover = true;
      else if (getUpper(table.Query("ToolTip.CaptionOnHover")) == "FALSE")
        captionTipOnHover = false;

     //captionTipPlacement
     if (getUpper(table.Query("ToolTip.CaptionPlacement")) == "BOTTOM")
	    captionTipPlacement = "Bottom";
     else if (getUpper(table.Query("ToolTip.CaptionPlacement")) == "TOP")
	    captionTipPlacement = "Top";
     else if (getUpper(table.Query("ToolTip.CaptionPlacement")) == "LEFT")
	    captionTipPlacement = "Left";
     else if (getUpper(table.Query("ToolTip.CaptionPlacement")) == "RIGHT")
	    captionTipPlacement = "Right";
     else if (getUpper(table.Query("ToolTip.CaptionPlacement")) == "AUTO")
            captionTipPlacement = "Auto";
}


void DesktopConfig::scanIconDirectory(const string & dir, bool warnOnUnrecognized,
                                       const string & excludeFilename,
                                       bool autoIconizePlainFiles)
{
    struct dirent **files;
    int fileCount = scandir(dir.c_str(), &files, 0, alphasort);
    if (fileCount == -1)
    {
        cerr << "No icons found in " << dir << "\n";
        return;
    }

    for(int i = 0; i < fileCount; i++)
    {
        if (!excludeFilename.empty() && string(files[i]->d_name) == excludeFilename)
        {
            free(files[i]);
            continue; // this is ideskrc itself, living alongside the icons
                      // -- not an icon, and not even worth a warning about
        }

        if (!backgroundFile(files[i]->d_name))
        {
            string filename = dir + files[i]->d_name;

            if (filename.size() > 4 && filename.substr(filename.size()-4,filename.size()) == ".lnk")
            {
				Database db = Database(filename, false);
				Table table = db.Query("Icon");

				if (table.isValid())
				{   
					DesktopIconConfig *iconPtr = new DesktopIconConfig(filename, table, common); 
					iconConfigList.push_back(iconPtr);
				} else
					cerr << "Error: \"" << files[i]->d_name << "\" is not a valid .lnk desktop icon\n";
			} else if (filename.size() > 8 && filename.substr(filename.size()-8,filename.size()) == ".desktop")
			{
				FreeDesktopIcon fdi(filename);

				if (fdi.isValid() && fdi.shouldDisplay())
				{
					DesktopIconConfig *iconPtr = new DesktopIconConfig(filename, fdi, common);
					iconConfigList.push_back(iconPtr);
				} else if (!fdi.isValid())
					cerr << "Error: \"" << files[i]->d_name << "\" is not a valid .desktop desktop icon\n";
				// else: well-formed but Hidden=true/NoDisplay=true -- silently skipped, not an error
			} else if (warnOnUnrecognized)
				cerr << "Warning: \"" << files[i]->d_name << "\" is not a recognized desktop icon (.lnk or .desktop)\n";
			else if (autoIconizePlainFiles)
			{
				// A plain file/folder/symlink in the XDG Desktop dir --
				// same behaviour GNOME/KDE/XFCE already give these.
				// Directories included: scandir doesn't distinguish here,
				// and a directory's own d_name naturally never ends in
				// .lnk or .desktop, so it reaches this branch too.
				GenericFileIcon gfi(filename);
				DesktopIconConfig *iconPtr = new DesktopIconConfig(filename, gfi, common);
				iconConfigList.push_back(iconPtr);
			}
			// else: Desktop.AutoIcons is off -- a plain file that isn't
			// (yet) turned into an icon is silently skipped rather than
			// warned about, since an ordinary ~/Desktop is expected to
			// hold plenty of files that were never meant to be icons.

            free(files[i]);
        }
    }
    free(files);
}

// Resolves the user's XDG "Desktop" folder per the freedesktop.org
// xdg-user-dirs spec (https://www.freedesktop.org/wiki/Software/xdg-user-dirs/).
// This is NOT always literally ~/Desktop -- e.g. a Spanish-locale system
// typically has it at ~/Escritorio instead. Resolution order:
//   1. $XDG_DESKTOP_DIR environment variable (rarely exported directly,
//      but some session setups do)
//   2. the XDG_DESKTOP_DIR="..." line in $XDG_CONFIG_HOME/user-dirs.dirs
//      (the file xdg-user-dirs-update actually writes on login)
//   3. $HOME/Desktop, the spec's own documented default when neither of
//      the above is present (e.g. a minimal WM setup with no XDG session
//      tooling ever run)
string DesktopConfig::getXdgDesktopDir()
{
    char * tmp;
    string homeDirectory, xdgConfigHome;

    tmp = getenv("HOME");
    if (tmp)
        homeDirectory.assign(tmp);

    tmp = getenv("XDG_DESKTOP_DIR");
    if (tmp && tmp[0] != '\0')
        return string(tmp) + "/";

    tmp = getenv("XDG_CONFIG_HOME");
    if (tmp && tmp[0] != '\0')
        xdgConfigHome.assign(tmp);
    else
        xdgConfigHome = homeDirectory + "/.config";

    string userDirsFile = xdgConfigHome + "/user-dirs.dirs";
    ifstream f(userDirsFile.c_str());
    if (f.is_open())
    {
        string line;
        while (getline(f, line))
        {
            if (line.empty() || line[0] == '#')
                continue;
            if (line.find("XDG_DESKTOP_DIR") == string::npos)
                continue;

            size_t q1 = line.find('"');
            size_t q2 = (q1 == string::npos) ? string::npos
                                              : line.find('"', q1 + 1);
            if (q1 == string::npos || q2 == string::npos)
                continue;

            string value = line.substr(q1 + 1, q2 - q1 - 1);

            // the file stores a literal "$HOME" token, not something the
            // shell has already expanded for us
            size_t homePos = value.find("$HOME");
            if (homePos != string::npos)
                value.replace(homePos, 5, homeDirectory);

            f.close();
            return value + "/";
        }
        f.close();
    }

    // spec-documented default
    return homeDirectory + "/Desktop/";
}

void DesktopConfig::loadIcons()
{
    char * tmp;
    string xdgConfigHome;
    string homeDirectory;

    // see https://specifications.freedesktop.org/basedir-spec/basedir-spec-latest.html
    tmp = getenv("XDG_CONFIG_HOME");
    if (tmp) {
        xdgConfigHome.assign(tmp);
    }

    tmp = getenv("HOME");
    if (tmp) {
        homeDirectory.assign(tmp);
    }

    if (xdgConfigHome.empty()) {
        xdgConfigHome = homeDirectory + "/.config";
    }

    string idesktopDir = xdgConfigHome + "/idesktop/";

    struct stat dirStat;
    if (stat(idesktopDir.c_str(), &dirStat) != 0 || !S_ISDIR(dirStat.st_mode))
    {
        cerr << "No icons found in " << idesktopDir << " - trying legacy location ~/.idesktop\n";
        idesktopDir = homeDirectory + "/.idesktop/";
    }

    // iDesk-NG's own directory keeps being scanned first, exactly as
    // before -- warnings on unrecognized files stay on here, since every
    // file in this curated directory is expected to be an icon. ideskrc
    // itself normally lives right here too (~/.config/idesktop/ideskrc),
    // so it's excluded by name rather than warned about on every startup.
    string ideskrcBasename = ideskrcFile;
    size_t lastSlash = ideskrcBasename.find_last_of('/');
    if (lastSlash != string::npos)
        ideskrcBasename = ideskrcBasename.substr(lastSlash + 1);

    scanIconDirectory(idesktopDir, /* warnOnUnrecognized = */ true, ideskrcBasename,
                       /* autoIconizePlainFiles = */ false);

    // Merge in the standard XDG Desktop directory, where GNOME/KDE/XFCE's
    // own icons already live (see DESIGN.md "Legacy / standard icon
    // support"). Guard against the unlikely case they resolve to the same
    // path so nothing gets scanned twice.
    string xdgDesktopDir = getXdgDesktopDir();
    if (xdgDesktopDir != idesktopDir)
        scanIconDirectory(xdgDesktopDir, /* warnOnUnrecognized = */ false, "",
                           autoIconizeDesktop);
}

void DesktopConfig::saveLockState(bool lockState)
{
    if (!wasLoaded) {
        return;
    }

    Database db = Database(ideskrcFile, true);
    Table & table = db.Query("Config");
    
    if (db.wasLoaded) {
        if(table.isValid())
        {
            if (lockState)
                table.Set("Locked", "true");
            else
                table.Set("Locked", "false");

            db.Write();
        }
        else
        {
            cerr << "Incorrect config file\n";
            return;
        }
    }
}

bool DesktopConfig::backgroundFile(const string & filename)
{
    // Only filters out hidden dotfiles (this also naturally covers "."
    // and "..") and editor backup files ending in '~'. Recognizing which
    // *remaining* files are actual icons (.lnk, .desktop, or -- not yet
    // implemented -- a plain file resolved by MIME type) is entirely the
    // job of scanIconDirectory()'s own dispatch below.
    //
    // NOTE: this used to also reject anything not ending in ".lnk",
    // which meant a .desktop file was discarded right here and never
    // even reached the .desktop branch in scanIconDirectory() -- silently
    // defeating both the original (dead) FreeDesktopIcon stub and the
    // real parser implemented for iDesk-NG. Found via end-to-end testing
    // with a real DesktopConfig/loadIcons() run, not just compiling.
    if (filename.size() > 0 && (
            filename[0] == '.' ||
	filename[filename.size() - 1] == '~' ))
        return true;

    return false;
}

