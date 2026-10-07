/* vim:tabstop=4:expandtab:shiftwidth=4
 * 
 * Idesk -- DesktopIconConfig.h
 *
 * Copyright (c) 2013, neagix
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

#ifndef DESKTOP_ICON_CLASS
#define DESKTOP_ICON_CLASS

#include <fcntl.h>
#include "AbstractClasses.h"
#include "Database.h"
#include "cursors.h"    /* defines XC_watch, etc. */ 

class CommonOptions
{
    protected:
        Table table;

        string fontName, fontColor;
	
        int fontSize;
	
        bool isBold;

        string shadowColor;
        bool shadowOn;
        int shadowX, shadowY;

        int snapShadowTrans;
        bool snapShadow;

        int clickDelay;

        bool captionOnHover;
	
        string captionPlacement;
	string fillStyle;
	
	int cursorOver;

        /* TODO
         * High Contrast - I'm not that interested in this old option. I may
         * look into it if someone asks. */
 
    public:
	 CommonOptions();
        ~CommonOptions();
        virtual void setOptions(Table);
        virtual void setCommonDefaults();
        virtual void setDefaultsFromParent(CommonOptions &/***/ other);

        virtual bool getBoldness() { return isBold; }

        virtual bool isShadow() { return shadowOn; }
        virtual int getShadowX() { return shadowX; }
        virtual int getShadowY() { return shadowY; }
        virtual string getShadowColor() { return shadowColor; }

        virtual bool getSnapShadow() { return snapShadow; }
        virtual int getSnapShadowTrans() { return snapShadowTrans; }
               
        virtual string getFont() { return fontName; }
        virtual int getFontSize() { return fontSize; }
        virtual string getFontColor() { return fontColor; }
	
        virtual bool getCaptionOnHover() { return captionOnHover; }

        virtual string getCaptionPlacement() { return captionPlacement; }
	
	virtual int getCursorOver(){ return cursorOver;}
	virtual string getFillStyle(){ return fillStyle;}
};


class DesktopIconConfig : public AbstractIconConfig
{
    public:
        // Where this icon's position should be persisted when dragged.
        // ORIGIN_LNK: the .lnk file itself (existing behavior, safe --
        // every real .lnk is an icon definition idesk-ng owns outright).
        // ORIGIN_LAYOUT_DB: a .desktop (may be a copy/symlink of a real
        // system launcher) or a plain file/folder with no file of its
        // own to write into -- see IconLayout.h / DESIGN.md "Position
        // handling". Defaults to ORIGIN_LNK; every non-.lnk construction
        // site in DesktopConfig::scanIconDirectory() sets this
        // explicitly, so the default only matters as a safe fallback.
        enum IconOrigin { ORIGIN_LNK, ORIGIN_LAYOUT_DB };

    protected:
        CommonOptions * common;

        int x, y;
        int width, height;
        IconOrigin origin;
        bool protectedFromDelete;
        bool tipEnabled;       // per icon: false suppresses its tooltip

    public:
	DesktopIconConfig(const string & fName, Table &table, CommonOptions * parentData);
        virtual ~DesktopIconConfig();

        virtual void setIconOptions(Table);

        virtual string getExtension(const string & file);

        virtual void setOrigin(IconOrigin o) { origin = o; }
        virtual IconOrigin getOrigin() { return origin; }

        // X-Idesk-Protected=true in a .desktop (see Install.cpp's
        // --install-trash-icon, the first icon to set this) -- the
        // context menu's Delete action checks this and refuses.
        virtual bool isProtected() { return protectedFromDelete; }
        virtual bool isTipEnabled() { return tipEnabled; }

        // Used after a plain file is renamed from the context menu, so
        // the restart's saveState() records its position under the new
        // path instead of resurrecting a layout entry for the old one.
        virtual void setIconFilename(const string & f) { iconFilename = f; }

        // Sets where this icon is without writing anything anywhere (unlike
        // saveIcon()). Used when an icon is rebuilt in place, to keep it
        // exactly where it currently is.
        virtual void setPosition(int xc, int yc) { x = xc; y = yc; }

        virtual int getX() { return x; }
        virtual int getY() { return y; }
        virtual int getWidth() { return width; }
        virtual int getHeight() { return height; }
        virtual bool isSvg() { return picExtension == "SVG"; }
        virtual bool isRaster() { return (picExtension == "JPEG" ||
                                          picExtension == "GIF"  ||
                                          picExtension == "PPM"  ||
                                          picExtension == "PGM"  ||
                                          picExtension == "XPM"  ||
	                                      picExtension == "JPG"  ||
                                          picExtension == "TIFF" ||
                                          picExtension == "PNG"); }

        virtual void saveIcon(int xCord, int yCord);

        virtual bool getBoldness() { return common->getBoldness(); }

        virtual bool isShadow() { return common->isShadow(); }
        virtual int getShadowX() { return common->getShadowX(); }
        virtual int getShadowY() { return common->getShadowY(); }
        virtual string getShadowColor() { return common->getShadowColor(); }

        virtual bool getSnapShadow() { return common->getSnapShadow(); }
        virtual int getSnapShadowTrans() {return common->getSnapShadowTrans();}
        
        virtual string getFont() { return common->getFont(); }
        virtual int getFontSize() { return common->getFontSize(); }
        virtual string getFontColor() { return common->getFontColor(); }
        
        virtual bool getCaptionOnHover() { return common->getCaptionOnHover(); }
		
        virtual string getCaptionPlacement() { return common->getCaptionPlacement(); }
	virtual string getFillStyle() { return common->getFillStyle();}
};

#endif
