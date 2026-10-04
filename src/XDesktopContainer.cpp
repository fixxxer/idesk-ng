/* vim:tabstop=4:expandtab:shiftwidth=4
 * 
 * Idesk -- XDesktopContainer.cpp
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

#include "XDesktopContainer.h"
#include "XIconWithShadow.h"
#include "XImlib2Background.h"
#include "Database.h"
#include "DesktopIconConfig.h"
#include "IconLayout.h"
#include "ContextMenu.h"
#include "XImlib2Image.h"
#include "XImlib2Caption.h"

#include <csignal>
#include <sys/select.h>
#include <algorithm>
#include <gio/gio.h>
#include <unistd.h>
#include <climits>

#include <X11/keysym.h>
#ifdef HAVE_STARTUP_NOTIFICATION
#include <libsn/sn.h>
#endif /* HAVE_STARTUP_NOTIFICATION  */

XDesktopContainer *xcontainer;

// Defined in App.cpp; set by signalhandler() on SIGTERM/SIGINT, read by
// eventLoop() below -- see the comment next to its definition for why
// this exists instead of calling _exit() straight from the signal
// handler.
extern volatile sig_atomic_t quitRequested;

// Same self-path resolution as Install.cpp's resolveSelfPath() (kept
// duplicated rather than shared, consistent with this project's
// existing convention for small, independent pieces) -- needed here so
// the "this icon is protected" message, launched via the existing
// runCommand() fork+exec helper below, calls back into idesk-ng
// correctly whether running from a dev build directory or a real
// system install.
static string resolveSelfPathForMessage()
{
    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0)
        return "idesk";
    buf[len] = '\0';
    return string(buf);
}

XDesktopContainer::XDesktopContainer(AbstractApp * a) : DesktopContainer(a)
{
    xcontainer=this; 	
    initXWin();
    initImlib();
}

void XDesktopContainer::run()
{
    times[0] = 0; times[1] = 0; times[2] = 0; 
    numClicks[0] = 0; numClicks[1] = 0; numClicks[2] = 0; 
    configure();
    create();
    loadIcons();
    arrangeIcons();

    eventLoop();
}

XDesktopContainer::~XDesktopContainer()
{
    destroy();
    XCloseDisplay(display);
}

void XDesktopContainer::create()
{
    getRootImage();
}

void XDesktopContainer::destroy()
{
    
    vector<AbstractIcon *>::reverse_iterator rIt = iconList.rbegin();
    for(; rIt != iconList.rend(); rIt++)
        delete *rIt;

    iconList.clear();
        
    delete config;
    delete actionConfig;
    delete bg;
    
    XFlush(display);
}

void XDesktopContainer::initXWin()
{
	
    display = XOpenDisplay(NULL);
    
    if (!display){
	 cerr << "Display is null!\n";
	 _exit(1);
    }
    
     char *name =  DisplayString(display);
     int screen = DefaultScreen(display);
     rootWindow  = RootWindow(display,  screen);
     
     XTextProperty prop;
     Atom start = XInternAtom(display,"_IDESK_START", false);
     cerr << "[Idesk] Starting on display " <<  name << endl;
     prop.value = (unsigned char *)name;
     prop.encoding = XA_STRING;
     prop.format = 8;
     prop.nitems = strlen(name);
     XSetTextProperty(display, rootWindow, &prop, start);
     
     XSelectInput( display, rootWindow,  PropertyChangeMask| SubstructureNotifyMask);
     XSync(display, false);
     
}

void XDesktopContainer::initImlib()
{
    //Imlib2 stuff
    imlib_context_set_display(display);
    imlib_context_set_visual(DefaultVisual(display,DefaultScreen(display)) );
    imlib_context_set_colormap(DefaultColormap(display, 
                DefaultScreen(display)) );
}

void XDesktopContainer::getRootImage()
{
     DesktopConfig * dConfig = dynamic_cast<DesktopConfig *>(config);
     
     
     imlib_context_set_drawable(rootWindow);
     
     bg = new XImlib2Background(this,config);

     // Prefer whatever wallpaper is already set on the root window (via
     // the standard _XROOTPMAP_ID property that feh/hsetroot/Esetroot and
     // similar tools publish) over InitSpareRoot()'s "create a temp
     // ParentRelative window and snapshot it immediately" trick below.
     //
     // Found on real hardware: InitSpareRoot() maps its temp window and
     // reads it back with imlib_create_image_from_drawable() on the very
     // next line, with no XSync/wait for the X server to actually realize
     // the ParentRelative background first -- a race that reliably grabbed
     // a blank/black image instead of the real wallpaper, even though
     // xprop -root _XROOTPMAP_ID showed a perfectly valid pixmap the whole
     // time. This is exactly why every icon caption (which crops this
     // image to fake transparency -- see XImlib2Caption::draw()) showed a
     // solid black box behind the text instead of the desktop wallpaper.
     Pixmap existingRootPixmap = bg->GetRootPixmap(None);
     if (existingRootPixmap != None)
         bg->Refresh(existingRootPixmap);
     else
         bg->InitSpareRoot(rootWindow);
	
     if(!bg->IsOneShot()){	
        timer = new Timer(this);
	bg->Finish();
     }
}

inline bool fileExists (const std::string& name) {
  struct stat buffer;   
  return (stat (name.c_str(), &buffer) == 0); 
}

void XDesktopContainer::configure()
{
    //get the user's config file
    string homeDirectory = getenv("HOME");
    string ideskrcFile = homeDirectory + "/.config/idesktop/ideskrc";
    if (!fileExists(ideskrcFile))
        ideskrcFile = homeDirectory + "/.ideskrc";

    Database db(ideskrcFile, true);
    DesktopConfig * dConfig = new DesktopConfig(db, ideskrcFile);
    config = dConfig;
    
    locked = dConfig->getLocked();
    clickDelay = dConfig->getClickSpeed();

    snapState = dConfig->getSnapState();
    snapShadow = dConfig->getSnapShadow();
    snapWidth = dConfig->getSnapWidth();
    snapHeight = dConfig->getSnapHeight();

    actionConfig = new ActionConfig(db, ideskrcFile);
}   

void XDesktopContainer::loadIcons()
{
    AbstractIconConfig * iconPtr;

    if (config->numIcons() == 0)
    {
        return;
    }

    // iterate through all the icons created by the configure class
    for(iconPtr = config->start(); config->notFinished();
        iconPtr = config->nextIcon()) {
        DesktopIconConfig * dIconConfig = dynamic_cast<DesktopIconConfig *>(iconPtr);

        XIcon * icon;
        if (dIconConfig->getSnapShadow() && dIconConfig->getSnapShadow()) {
            icon = new XIconWithShadow(this, config, iconPtr);
	    } else {
            icon = new XIcon(this, config, iconPtr);
		}
	    if (icon->isValid()){
		    if (icon->createIcon())
	    	    addIcon(icon);
	    }
    }
}

void XDesktopContainer::arrangeIcons()
{
    DesktopConfig * dConfig = dynamic_cast<DesktopConfig *>(config);
    int maxW = 0, maxRowStep = 0;

    if( iconList.size() == 0 )
    {
        return;
    }

    for(unsigned int i = 0; i < iconList.size(); i++ )
    {
        XIcon *iPtr = dynamic_cast<XIcon *>(iconList[i]);
        if( iPtr->getWidth() > maxW )
            maxW = iPtr->getWidth();
        int rowStep = iPtr->getHeight() + 30 + iPtr->getFontHeight();
        if( rowStep > maxRowStep )
            maxRowStep = rowStep;
    }

    // How many non-overlapping grid slots actually fit on screen, using
    // the exact same column/row spacing the loop below places icons
    // with. Past this many un-positioned icons, further ones reuse the
    // same slots again with a small X/Y shift per "layer" (like a
    // fanned stack of cards) instead of continuing to add columns past
    // the edge of the screen.
    //
    // Found on real hardware with ~100 real .desktop files copied into
    // ~/Desktop as a stress test: the old unbounded "iconX -= 20+maxW"
    // column-wrap never stopped, so once there were more icons than fit
    // in one pass, the rest landed off-screen -- completely unreachable,
    // not merely overlapping. ~40 icons were visible; the rest existed
    // but weren't.
    int columns = (widthOfScreen() - 20) / (maxW + 20);
    if (columns < 1) columns = 1;
    int rows = (heightOfScreen() - 20) / maxRowStep;
    if (rows < 1) rows = 1;
    int capacity = columns * rows;

    const int shiftStep = 15; // px of X/Y offset per extra layer

    // Which corner the grid starts in, and which way it grows, both
    // follow the SAME ideskrc SnapOrigin setting (TopLeft/TopRight/
    // BottomLeft/BottomRight) that drag-to-grid snapping already uses
    // (see XIcon.cpp) -- arrangeIcons() used to ignore it completely
    // and always start top-right, growing left, no matter what
    // SnapOrigin said.
    bool fromLeft = dConfig->getStartSnapLeft();
    bool fromTop = dConfig->getStartSnapTop();

    int dirX = fromLeft ? 1 : -1;
    int dirY = fromTop ? 1 : -1;
    int originX = fromLeft ? 20 : widthOfScreen() - maxW - 20;
    int originY = fromTop ? 20 : heightOfScreen() - maxRowStep;

    int slot = 0;
    for(unsigned int i = 0; i < iconList.size(); i++ )
    {
        XIcon *iPtr = dynamic_cast<XIcon *>(iconList[i]);

        if( iPtr->getX() == 0 && iPtr->getY() == 0 )
        {
            int layer = slot / capacity;
            int posInLayer = slot % capacity;
            int col = posInLayer / rows;
            int row = posInLayer % rows;

            int baseX = originX + dirX * col * (maxW + 20);
            int baseY = originY + dirY * row * maxRowStep;

            int finalX = baseX + dirX * layer * shiftStep;
            int finalY = baseY + dirY * layer * shiftStep;
            if (finalX < 20)
                finalX = 20; // defensive floor for pathological screen/icon sizes
            if (finalY < 20)
                finalY = 20;

            iPtr->setX(finalX + ((maxW - iPtr->getWidth())/2));
            iPtr->setY(finalY);

            // This icon had no saved position (that's what X==0 && Y==0
            // means here) -- if it's a .desktop/plain-file icon (never
            // a .lnk, which already owns its own position on disk),
            // seed the layout DB with the slot arrangeIcons() just gave
            // it, so the next run finds it here instead of re-arranging
            // from scratch. See DesktopIconConfig::ORIGIN_LAYOUT_DB.
            DesktopIconConfig * dIconConfig =
                dynamic_cast<DesktopIconConfig *>(iPtr->getIconConfig());
            if (dIconConfig && dIconConfig->getOrigin() == DesktopIconConfig::ORIGIN_LAYOUT_DB)
                seedLayoutPosition(dIconConfig->getIconFilename(), iPtr->getX(), iPtr->getY());

            slot++;
        }
        
        iPtr->moveImageWindow();
        iPtr->mapImageWindow();
        //don't initially map caption for the hover effect
        iPtr->initMapCaptionWindow();
        
        //iPtr->draw();
         
    }
}

void XDesktopContainer::addIcon(AbstractIcon * icon)
{
    iconList.push_back(icon);
}

XIcon * XDesktopContainer::findIcon(Window window)
{
    for(unsigned int i = 0; i < iconList.size(); i++)
    {
        XIcon * tmpIcon = dynamic_cast<XIcon *> (iconList[i]);
        Window * tmpCapWindow = tmpIcon->getCaptionWindow();
        if ( *tmpIcon->getImageWindow() == window ||
             (tmpCapWindow != NULL && *tmpCapWindow == window) )
            return tmpIcon;
    }
    return None;
}

void XDesktopContainer::updateIcons()
{
	for(unsigned int i = 0; i < iconList.size(); i++)
	{
		XIcon * tmpIcon = dynamic_cast<XIcon *> (iconList[i]);
		tmpIcon->draw();
	}
}
    
void XDesktopContainer::eventLoop()
{
    XEvent ev;
#ifdef HAVE_STARTUP_NOTIFICATION
    sn_context = NULL;
    sn_display = NULL; 
    sn_bool_t retval;
    sn_display = sn_display_new (display,
        		         error_trap_push,
				 error_trap_pop);

#endif /* HAVE_STARTUP_NOTIFICATION  */
    
    for(;;)
    {
        if (quitRequested)
            break;

        if( !XPending( display ) && timer){
		if(!bg->IsOneShot()){
			timer->Update();
		}
		// pre-existing tight loop when a background-rotation timer
		// is active -- iterates fast enough that the quitRequested
		// check above already catches a stop request essentially
		// immediately, so no extra waiting needed here
	}
	else if (!XPending(display)) {
		// No timer (the common case -- Background.Delay: 0 in every
		// example config in this repo) and nothing pending: the old
		// code called the blocking XNextEvent() directly here, which
		// could wait forever with no X activity at all, meaning
		// Ctrl+C/kill wouldn't be noticed until some event finally
		// arrived. select() with a timeout on the X connection's own
		// fd wakes the loop up periodically regardless, so
		// quitRequested above is checked promptly either way.
		int xfd = ConnectionNumber(display);
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(xfd, &fds);
		struct timeval tv;
		tv.tv_sec = 1;
		tv.tv_usec = 0;
		select(xfd + 1, &fds, NULL, NULL, &tv);
		// loop back around either way; if an event is now actually
		// pending, the next iteration's XPending() check sends it to
		// XNextEvent() below as normal
	}
	else {
	 
          XNextEvent(display, &ev);
#ifdef HAVE_STARTUP_NOTIFICATION
	  if (sn_display != NULL){
	   sn_display_process_event (sn_display, &ev);
          }
#endif /* HAVE_STARTUP_NOTIFICATION  */
          event = ev;
          parseEvent();
	}
    }
    
#ifdef HAVE_STARTUP_NOTIFICATION
    // This whole cleanup block was dead code for the project's entire
    // history until the graceful-shutdown fix a few commits back: the
    // loop above used to be truly infinite, only ever broken out of via
    // _exit() (which skips everything after it, this block included),
    // so nothing had ever actually reached here before. First real
    // execution immediately crashed: sn_context starts NULL (set at the
    // top of this function) and only gets assigned when an app is
    // actually launched via startup notification during the session --
    // with none launched, sn_launcher_context_unref(NULL) segfaults.
    // sn_display right below was already correctly guarded; sn_context
    // was not -- classic copy-paste asymmetry between two adjacent,
    // near-identical cleanup calls.
    if (sn_context)
        sn_launcher_context_unref (sn_context);
    if (sn_display)
    {
       sn_display_unref (sn_display);
    }
#endif /* HAVE_STARTUP_NOTIFICATION  */
}

void XDesktopContainer::parseEvent()
{
    currentAction.clear();

    parseNonIconEvents();
    XIcon * icon = parseIconEvents();

    exeCurrentAction(icon);
}

void XDesktopContainer::parseNonIconEvents()
{
	
    switch (event.type)
    {
	    
        case PropertyNotify:
		//char *name = XGetAtomName(display, event.xproperty.atom );
		//cout << " Name " << name << endl ;
		
		static Atom atom_stop = None ;
                if( atom_stop == None ) atom_stop = XInternAtom(display, "_IDESK_STOP", True);
		
		if (event.xproperty.atom == atom_stop && !stop){
			XTextProperty prop; 
			int result = XGetTextProperty(display, rootWindow, &prop, atom_stop);
			if(result && prop.encoding != None && prop.value != NULL){
				string current_display_name = DisplayString(display);
				string old_display_name = (char *)prop.value;
				string mesg = (char *)prop.value;
				if(current_display_name == old_display_name){
					XDeleteProperty (display, rootWindow, atom_stop);
					cerr << "Error ... Idesk is running in " << prop.value << endl;
					cerr << "Exit." << endl;
					_exit(1);
				}
			}
		}
		
		static Atom atom_start = None ;
                if( atom_start == None ) atom_start = XInternAtom(display, "_IDESK_START", True);
		
		if (event.xproperty.atom == atom_start){
		        XTextProperty prop; 
			int result = XGetTextProperty(display, rootWindow, &prop, atom_start);
			if(result && prop.encoding != None && prop.value != NULL){
				string current_display_name = DisplayString(display);
				string old_display_name = (char *)prop.value;
				if(current_display_name == old_display_name){
					XDeleteProperty (display, rootWindow, atom_start);
					stop = XInternAtom(display,"_IDESK_STOP", false);
					prop.value = (unsigned char *)current_display_name.c_str();
					prop.encoding = XA_STRING;
					prop.format = 8;
					prop.nitems = strlen(current_display_name.c_str());
					XSetTextProperty(display, rootWindow, &prop, stop);
				}
			}
		}
		
		static Atom atom_xroot = None ;
                if( atom_xroot == None ) atom_xroot = XInternAtom(display, "_XROOTPMAP_ID", True);
		
	        if (event.xproperty.atom == atom_xroot)
	         {
			Pixmap pmap = bg->GetRootPixmap(event.xproperty.atom);
			if(bg->pixmap == None){
				bg->Refresh(pmap);
				bg->pixmap = (Pixmap)1; //For Fix
			}else{
				bg->InitSpareRoot(event.xproperty.window);
			}
			updateIcons(); 
		 }
		 break;
    }
}

XIcon * XDesktopContainer::parseIconEvents()
{
    XIcon * icon;

    icon = findIcon(event.xmotion.window);
   
    if (icon)
    {
        switch (event.type)
        {
            case ButtonPress:
                setEventState();

                if (event.xbutton.button == Button1)
                    currentAction.setLeft(hold);                
                else if (event.xbutton.button == Button2)
                    currentAction.setMiddle(hold);
                else if (event.xbutton.button == Button3)
		             currentAction.setRight(hold);
		
                if(event.xbutton.window == *icon->getImageWindow()  || event.xbutton.window == *icon->getCaptionWindow()){
			 if(bg->spareRoot){
				 icon->pressImage();
			 }
		}
                        
                break;
			
            case MotionNotify:
                if (icon->isDragging() && !isLocked())
		     icon->dragMotionNotify(event);
                break;

            case ButtonRelease:
                setEventState();

                if (event.xbutton.button == Button1)
                    translateButtonRelease(0);
                else if (event.xbutton.button == Button2)
                    translateButtonRelease(1);
                else if (event.xbutton.button == Button3)
		            translateButtonRelease(2);

		if(event.xbutton.window == *icon->getImageWindow() || event.xbutton.window == *icon->getCaptionWindow()){
			 if(bg->spareRoot){
				 icon->unpressImage();
			 }
	        }

                break;
			    
            case Expose:
                //since we are redrawing the whole window we can ignore
                //multiple expose events and only draw text once
		 if (event.xexpose.count == 0){
			 if(bg->spareRoot){
                      		icon->draw();	
			 }  
		  }
                break;
            case EnterNotify:
	        if(event.xcrossing.window == *icon->getImageWindow() || event.xcrossing.window == *icon->getCaptionWindow()){
			if(bg->spareRoot){
				icon->mouseOverEffect();
				icon->event_enter_notify();
			}
		 }
                break;
            case LeaveNotify:  
	        if(event.xcrossing.window == *icon->getImageWindow() || event.xcrossing.window == *icon->getCaptionWindow()){
			if(bg->spareRoot){
				icon->mouseOffEffect();
				icon->event_leave_notify();
			}
		}
                break;     
        }
    }
    return icon;
}

// Always moves to the desktop trash (g_file_trash(), the standard
// freedesktop.org mechanism -- works without any GNOME/KDE/XFCE
// installed, see DESIGN.md's --install-trash-icon entry) rather than
// permanently deleting, and treats every icon origin the same way
// (.lnk, .desktop, or a plain file/folder) -- deliberately not a
// "pretty trash here, gone forever there" split by origin or
// directory; see the Point 3 design discussion in DESIGN.md for why.
// Refuses outright for a protected icon (X-Idesk-Protected=true, so
// far only the Trash icon itself) rather than silently doing nothing,
// so the person gets feedback either way.
void XDesktopContainer::deleteIcon(XIcon * icon)
{
	DesktopIconConfig * dIconConfig =
	    dynamic_cast<DesktopIconConfig *>(icon->getIconConfig());
	if (!dIconConfig)
		return;

	if (dIconConfig->isProtected())
	{
		string cmd = resolveSelfPathForMessage() +
		    " --show-message \"This icon is protected and can't be deleted.\"";
		runCommand(cmd);
		return;
	}

	string path = dIconConfig->getIconFilename();

	GFile * file = g_file_new_for_path(path.c_str());
	GError * error = NULL;
	bool trashed = g_file_trash(file, NULL, &error);
	if (error)
		g_error_free(error);
	g_object_unref(file);

	if (!trashed)
	{
		cerr << "Could not move \"" << path << "\" to trash\n";
		return;
	}

	// Without this, a future icon that happens to reuse this exact
	// path (a package reinstall, recreating an icon with the same
	// name) would silently inherit this deleted icon's old position.
	// Harmless to call even for a .lnk-origin icon that was never in
	// the layout DB to begin with -- removeLayoutPosition() is a
	// no-op when there's no matching entry.
	removeLayoutPosition(path);

	// Icon windows use background_pixmap = ParentRelative (see
	// XImlib2Image.cpp) -- the lightweight standard X11 way to look
	// "transparent" against the desktop wallpaper without copying any
	// pixels themselves. Destroying such a window does NOT
	// automatically repaint the parent underneath it (a well-known
	// X11 gotcha, not a bug in that rendering choice): the window's
	// last-rendered pixels simply stay on screen until something
	// explicitly asks for that area to be repainted. Every other
	// place this codebase destroys icon windows (normal shutdown, a
	// full Reload) either exits entirely or rebuilds the whole
	// background, so none of them ever needed to handle this -- this
	// is the first time a single icon is removed while the session
	// and its background stay exactly as they were. Query the real
	// on-screen geometry of this icon's windows (image + caption)
	// directly from X before destroying them, union their bounds, and
	// XClearArea(..., exposures=True) that rectangle afterward so the
	// wallpaper underneath reappears immediately -- found and fixed
	// after confirming on real hardware (Fluxbox) that the icon
	// stopped responding to clicks (the window really was destroyed)
	// but its image lingered as a visual ghost.
	int clearX = 0, clearY = 0, clearRight = 0, clearBottom = 0;
	bool haveRect = false;

	XImlib2Image * xImg = dynamic_cast<XImlib2Image *>(icon->getImage());
	if (xImg)
	{
		Window * w = xImg->getWindow();
		if (w)
		{
			XWindowAttributes attrs;
			if (XGetWindowAttributes(display, *w, &attrs))
			{
				clearX = attrs.x;
				clearY = attrs.y;
				clearRight = attrs.x + attrs.width;
				clearBottom = attrs.y + attrs.height;
				haveRect = true;
			}
		}
	}

	XImlib2Caption * xCap = dynamic_cast<XImlib2Caption *>(icon->getCaption());
	if (xCap)
	{
		Window * cw = xCap->getWindow();
		if (cw)
		{
			XWindowAttributes attrs;
			if (XGetWindowAttributes(display, *cw, &attrs))
			{
				if (!haveRect)
				{
					clearX = attrs.x;
					clearY = attrs.y;
					clearRight = attrs.x + attrs.width;
					clearBottom = attrs.y + attrs.height;
					haveRect = true;
				}
				else
				{
					clearX = min(clearX, attrs.x);
					clearY = min(clearY, attrs.y);
					clearRight = max(clearRight, attrs.x + attrs.width);
					clearBottom = max(clearBottom, attrs.y + attrs.height);
				}
			}
		}
	}

	// Removed from the live session immediately -- no restart needed
	// to see it disappear. Only the XIcon (the visual/window side)
	// is deleted here; the underlying DesktopIconConfig stays in
	// DesktopConfig::iconConfigList and is cleaned up with everything
	// else at normal shutdown -- harmless, and not worth the extra
	// bookkeeping of also removing it from that list mid-session.
	vector<AbstractIcon *>::iterator it =
	    find(iconList.begin(), iconList.end(), icon);
	if (it != iconList.end())
		iconList.erase(it);
	delete icon;

	if (haveRect)
		XClearArea(display, rootWindow, clearX, clearY,
		           clearRight - clearX, clearBottom - clearY, True);
}

void XDesktopContainer::exeCurrentAction(XIcon * icon)
{
	// Right-click context menu: a plain single right-click has no
	// default action bound to it anywhere in this project's example
	// ideskrc files (only "right doubleClk" maps to Execute[1]), so
	// this doesn't conflict with anything -- and matches the
	// near-universal convention (right-click, not double-right-click,
	// opens a context menu) users already expect. Placeholder items
	// for now -- this piece is just the menu itself; Rename/Delete/
	// Properties land as their own pieces on top of it.
	if (icon && currentAction.getRight() == singleClk)
	{
		vector<string> items;
		items.push_back("Rename");
		items.push_back("Delete");
		items.push_back("Properties");

		int chosen = showContextMenu(display, DefaultScreen(display),
		                              rootWindow, imlib_context_get_visual(),
		                              imlib_context_get_colormap(), items,
		                              event.xbutton.x_root, event.xbutton.y_root);

		if (chosen >= 0 && items[chosen] == "Delete")
			deleteIcon(icon); // may free `icon` -- nothing below may touch it
		else if (chosen >= 0)
			cerr << "Context menu: \"" << items[chosen] << "\" chosen for \""
			     << icon->getIconConfig()->getCaption() << "\"\n";

		return;
	}

			
	if (actionConfig->getReload()->isOccuring(currentAction)){
		app->restartIdesk();
	}
    
    if (actionConfig->getLock()->isOccuring(currentAction))
    {
        toggleLock();
        DesktopConfig * dConfig = dynamic_cast<DesktopConfig *>(config);
        dConfig->saveLockState(locked); 
    }
    
    if (icon) //make sure icon is not NULL
    {
        if (actionConfig->getDrag()->isOccuring(currentAction)
            && !isLocked()
            && !icon->isDragging() ) //only start drag if not already occuring
            icon->dragButtonPress(event);
        else if (actionConfig->getEndDrag()->isOccuring(currentAction))
            icon->dragButtonRelease(event);

        for (int i = 0; i < icon->getCommandArray().size() &&
                        i < actionConfig->getExecuteActions().size();
                        i++)
		if (actionConfig->getExecuteAction(i)->isOccuring(currentAction)){
#ifdef HAVE_STARTUP_NOTIFICATION
			if (sn_display != NULL)
			{
			  sn_context = sn_launcher_context_new (sn_display, DefaultScreen (display));
			  if ((sn_context != NULL) && !sn_launcher_context_get_initiated (sn_context))
			  {
			   sn_launcher_context_set_name (sn_context, icon->getCommand(i).c_str());
			   sn_launcher_context_set_description (sn_context, icon->getCommand(i).c_str());
		           sn_launcher_context_set_binary_name (sn_context, icon->getCommand(i).c_str());
                           sn_launcher_context_set_icon_name(sn_context, icon->getCommand(i).c_str());
			   
			   sn_launcher_context_initiate (sn_context,
					   icon->getCommand(i).c_str(),
					   icon->getCommand(i).c_str(),	      
					   event.xproperty.time);
			  }
			}
#endif  /*HAVE_STARTUP_NOTIFICATION */
			runCommand(icon->getCommand(i));
		}
    }

}

void XDesktopContainer::setEventState()
{
    currentAction.setControl(false);
    currentAction.setShift(false);
    currentAction.setAlt(false);

    if (event.xbutton.state & ControlMask) currentAction.setControl(true);
    if (event.xbutton.state & ShiftMask) currentAction.setShift(true);
    if (event.xbutton.state & Mod1Mask) currentAction.setAlt(true);

    if (event.xbutton.state & Button1Mask && currentAction.getLeft() == none)
        currentAction.setLeft(hold);

    if (event.xbutton.state & Button2Mask && currentAction.getMiddle() == none)
        currentAction.setMiddle(hold);

    if (event.xbutton.state & Button3Mask && currentAction.getRight() == none)
        currentAction.setRight(hold);
}
    
void XDesktopContainer::translateButtonRelease(int button)
{
    if (event.xbutton.time - times[button] <= clickDelay)
        numClicks[button]++;
    else {
        numClicks[button] = 1;
        times[button] = event.xbutton.time;
    }

    if (numClicks[button] == 1)
        currentAction.setButton(button, singleClk);
    else if (numClicks[button] == 2)
        currentAction.setButton(button, doubleClk);
    else if (numClicks[button] >= 3)
        currentAction.setButton(button, tripleClk);
    else
        currentAction.setButton(button, none);
}

void XDesktopContainer::saveState()
{
    //save each of the icons
    for(unsigned int i = 0; i < iconList.size(); i++)
        saveIcon(iconList[i]);

    //general config saves
    
    DesktopConfig * dConfig = dynamic_cast<DesktopConfig *>(config);

    dConfig->saveLockState(locked);
}

void XDesktopContainer::saveIcon(AbstractIcon * xIcon)
{
    xIcon->save();
}

void XDesktopContainer::reloadState()
{
    //TODO -- Reload all of the icons internally instead of rebooting whole
    //        program. Not way too important though.
}

void XDesktopContainer::runCommand(const string & command)
{
    pid_t pid;
    // fork and execute program by replacing child's process
    pid = fork();
    if (pid == 0) {
#ifdef HAVE_STARTUP_NOTIFICATION
	    if (sn_context != NULL)
		     sn_launcher_context_setup_child_process (sn_context);
#endif /* HAVE_STARTUP_NOTIFICATION  */
                setsid();
		if (execl("/bin/sh", "/bin/sh", "-c", command.c_str(), (char *)0) == -1) {
			fprintf(stderr, "Error to execute command '%s': %s\n", command.c_str(), strerror(errno));
			exit(1);
		}
		// this line is never reached
    } else if (pid < 0) {
        fprintf(stderr, "Failed to fork process to run command '%s': %s\n", command.c_str(), strerror(errno));
    } else {
        waitpid(pid, NULL, 0);
    }
}
int XDesktopContainer::widthOfScreen()
{
    return WidthOfScreen(DefaultScreenOfDisplay(display));
}

int XDesktopContainer::heightOfScreen()
{
    return HeightOfScreen(DefaultScreenOfDisplay(display));
}

