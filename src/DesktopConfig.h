/* vim:tabstop=4:expandtab:shiftwidth=4
 * 
 * Idesk -- DesktopConfig.h
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

#ifndef DESKTOP_CONFIG_CLASS
#define DESKTOP_CONFIG_CLASS

#include "AbstractClasses.h"
#include "Database.h"
#include "FreeDesktopIcon.h"
#include "DesktopIconConfig.h"
#include "Misc.h"
#include <dirent.h>

class DesktopConfig : public AbstractConfig
{
    protected:
    bool isLocked, wasLoaded;
    bool contextMenuEnabled;
    int snapWidth, snapHeight;
    bool snapOn;
    bool startSnapLeft, startSnapTop;
    bool autoIconizeDesktop;
	
	string fileBackground, colorBackground;
	string sourceBackground, modeBackground;
	int delayBackground;
    
	int transparency;
	
	string fontNameTip, foreColorTip, backColorTip;
	string captionTipPlacement;
	int fontSizeTip;
	bool captionTipOnHover;
	
    CommonOptions * common;

    // The standard XDG Desktop directory when it is scanned as a directory of
    // its own (empty when it is the same as iDesk-NG's directory, or absent).
    // This is the one the container watches for files appearing and going away.
    string desktopWatchDir;
    // The same directory by name, whether or not it exists yet (empty only when
    // it is iDesk-NG's own directory): what the container waits for if the
    // directory is created after idesk-ng has started.
    string desktopDir;

    virtual void loadIcons();
    public:
        const string & getDesktopWatchDir() { return desktopWatchDir; }
        const string & getDesktopDir() { return desktopDir; }
        // Config for one file of the Desktop directory, by the same rules the
        // startup scan applies to it; NULL if the file isn't (or isn't any
        // longer) shown as an icon. The caller hands the result back through
        // adoptIconConfig() once it is going to use it.
        DesktopIconConfig * createDesktopIconConfig(const string & path);
        void adoptIconConfig(DesktopIconConfig * c) { iconConfigList.push_back(c); }
        // Takes `c` out of the list (if it is in it) and deletes it.
        void removeIconConfig(DesktopIconConfig * c);
        // Re-reads one icon from its file and swaps it into the list; the
        // caller deletes the returned-from `old` config. NULL if unreadable.
        DesktopIconConfig * rebuildIconConfig(DesktopIconConfig * old);
    protected:
        DesktopIconConfig * createIconConfig(const string & filename,
                                              const string & displayName,
                                              bool warnOnUnrecognized,
                                              bool autoIconizePlainFiles);
    virtual void scanIconDirectory(const string & dir, bool warnOnUnrecognized,
                                    const string & excludeFilename,
                                    bool autoIconizePlainFiles);
    virtual string getXdgDesktopDir();
    virtual bool backgroundFile(const string & filename);

    public:
	DesktopConfig(Database db, string ideskrcFile);
    virtual ~DesktopConfig();

    virtual void setDefaults();
    virtual void setOptions(Table);
    virtual void setDesktopOnlyOptions(Table);

    virtual bool getLocked() { return isLocked || kioskMode; }
    virtual bool getContextMenuEnabled() { return contextMenuEnabled && !kioskMode; }
    virtual bool getSnapState() { return snapOn; }
    virtual bool getStartSnapTop() { return startSnapTop; }
    virtual bool getStartSnapLeft() { return startSnapLeft; }
    virtual int getSnapWidth() { return snapWidth; }
    virtual int getSnapHeight() { return snapHeight; }
    virtual bool getAutoIconizeDesktop() { return autoIconizeDesktop; }
	
	virtual string getFileBackground(){ return fileBackground;}
	virtual string getColorBackground(){ return colorBackground;}
	virtual string getSourceBackground(){ return sourceBackground;}
	virtual int getDelayBackground(){ return delayBackground;}
	virtual string getModeBackground(){ return modeBackground;}
	
	virtual int getTransparency() { return transparency; }
	
	virtual string getFontTip() { return fontNameTip; }
	virtual int getFontSizeTip() { return fontSizeTip; }
	virtual string getForeColorTip() { return foreColorTip; }
	virtual string getBackColorTip() { return backColorTip; }
	virtual string getCaptionTipPlacement() { return captionTipPlacement; }
	virtual bool getCaptionTipOnHover() { return captionTipOnHover; }


    virtual bool getBoldness() { return common->getBoldness(); }

    virtual bool isShadow() { return common->isShadow(); }
    virtual int getShadowX() { return common->getShadowX(); }
    virtual int getShadowY() { return common->getShadowY(); }
    virtual string getShadowColor() { return common->getShadowColor(); }

    virtual bool getSnapShadow() { return common->getSnapShadow(); }
    virtual int getSnapShadowTrans() {return common->getSnapShadowTrans();}
	
    virtual int getCursorOver() { return common->getCursorOver(); }
		
    virtual string getFont() { return common->getFont(); }
    virtual int getFontSize() { return common->getFontSize(); }
    virtual string getFontColor() { return common->getFontColor(); }

};

#endif
