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
#include "TextInput.h"
#include "IconEdit.h"
#include "XImlib2Image.h"
#include "XImlib2Caption.h"

#include <csignal>
#include <sys/select.h>
#include <algorithm>
#include <gio/gio.h>
#include <unistd.h>
#include <climits>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <dirent.h>
#include <ctime>
#include <map>
#include <set>

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

XDesktopContainer::XDesktopContainer(AbstractApp * a) : DesktopContainer(a), timer(NULL), watchFd(-1), watchWd(-1), syncPending(false), syncFirstMs(0), syncLastMs(0)
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
    startDesktopWatch();

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
    stopDesktopWatch();
    
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

// An icon's footprint on screen (the image plus the room its caption takes),
// used by arrangeIcons() to keep a newly placed icon off the ones already there.
struct IconBox { int x, y, w, h; };

static bool boxesOverlap(const IconBox & a, const IconBox & b)
{
    return a.x < b.x + b.w && b.x < a.x + a.w &&
           a.y < b.y + b.h && b.y < a.y + a.h;
}

void XDesktopContainer::arrangeIcons()
{
    arrangeIcons(NULL);
}

// With `only` set, just that icon is shown (the others are already on screen
// and have positions, so the placement loop leaves them alone) -- used when a
// single new icon appears while idesk-ng is running.
void XDesktopContainer::arrangeIcons(XIcon * only)
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

    // Boxes already taken on screen: every icon that has a saved position, plus
    // each one this loop places as it goes. An un-positioned icon -- a file
    // restored from the Trash, a .desktop dropped into ~/Desktop -- has to land
    // in a slot that is still free. Slots used to be handed out from 0 without
    // looking at what already occupied them, so such an icon landed exactly on
    // top of one that was already there.
    vector<IconBox> occupied;
    for(unsigned int i = 0; i < iconList.size(); i++ )
    {
        XIcon *p = dynamic_cast<XIcon *>(iconList[i]);
        if( p->getX() != 0 || p->getY() != 0 )
        {
            IconBox b = { p->getX(), p->getY(), p->getWidth(),
                          p->getHeight() + p->getFontHeight() + 10 };
            occupied.push_back(b);
        }
    }

    int slot = 0;
    for(unsigned int i = 0; i < iconList.size(); i++ )
    {
        XIcon *iPtr = dynamic_cast<XIcon *>(iconList[i]);

        if( iPtr->getX() == 0 && iPtr->getY() == 0 )
        {
            // First slot, from here on, whose box nothing else occupies. Only
            // the first layer is searched; once it is full the icon just takes
            // the next fan slot, exactly as before this search existed.
            IconBox box = { 0, 0, iPtr->getWidth(),
                            iPtr->getHeight() + iPtr->getFontHeight() + 10 };
            for( ; ; slot++ )
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

                box.x = finalX + ((maxW - iPtr->getWidth())/2);
                box.y = finalY;

                bool taken = false;
                for( unsigned int k = 0; k < occupied.size() && !taken; k++ )
                    taken = boxesOverlap(box, occupied[k]);
                // Past the first layer there is no free slot left to find (the
                // fanned-stack layers are shifted only a few pixels, so they
                // always overlap the layer below): keep the original fan
                // behaviour instead of searching further and further out.
                if( !taken || slot >= capacity )
                    break;
            }

            iPtr->setX(box.x);
            iPtr->setY(box.y);
            occupied.push_back(box);

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
        
        if( only && iPtr != only )
            continue;

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

        pollDesktopWatch();

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
		int maxfd = xfd;
		if (watchFd >= 0)
		{
			// the kernel wakes this select() when something happens in
			// ~/Desktop: no polling, no timer -- idle costs nothing
			FD_SET(watchFd, &fds);
			if (watchFd > maxfd)
				maxfd = watchFd;
		}
		struct timeval tv;
		int waitMs = watchTimeoutMs(); // 1000 unless a refresh is due sooner
		tv.tv_sec = waitMs / 1000;
		tv.tv_usec = (waitMs % 1000) * 1000;
		select(maxfd + 1, &fds, NULL, NULL, &tv);
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
		notify("This icon is protected and can't be deleted.");
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

	// Removed from the live session immediately -- no restart needed to see it
	// disappear. Deleting the XIcon is also what destroys its three X windows
	// (image, caption, tooltip) -- see ~XImlib2Image(): until it learned to
	// destroy its own window, the image window outlived the icon as a visible,
	// unclickable ghost. Its config goes with it, the same way a refreshed icon's
	// old config does (it used to stay in DesktopConfig's list until shutdown).
	DesktopConfig * dConfig = dynamic_cast<DesktopConfig *>(config);
	removeXIcon(icon);
	if (dConfig)
		dConfig->removeIconConfig(dIconConfig);
}

// Takes an icon off the screen and out of the list. The config it was showing
// is the caller's to deal with.
void XDesktopContainer::removeXIcon(XIcon * icon)
{
	vector<AbstractIcon *>::iterator it =
	    find(iconList.begin(), iconList.end(), icon);
	if (it != iconList.end())
		iconList.erase(it);
	delete icon;
	XFlush(display);
}

// ---------------------------------------------------------------------------
// Watching ~/Desktop
//
// Files that appear in, go away from, or are touched in the XDG Desktop
// directory are picked up while idesk-ng runs. Only that directory: iDesk-NG's
// own (~/.config/idesktop) is written to by idesk-ng itself all the time, and
// changes there are rare (see DESIGN.md).
//
// Cost when nothing happens: zero. inotify makes the kernel wake the existing
// select() in eventLoop() when something changes; there is no polling and no
// timer. A burst of events (copying thirty files) is folded into one
// synchronisation, run 400 ms after the last event.
// ---------------------------------------------------------------------------

static long long monotonicMs()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static long long mtimeNs(const struct stat & st)
{
	return (long long)st.st_mtim.tv_sec * 1000000000LL + st.st_mtim.tv_nsec;
}

static bool endsWithStr(const string & s, const string & suffix)
{
	return s.size() >= suffix.size() &&
	       s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Is `path` a file directly inside `dir` (which ends with '/')?
static bool isDirectChildOf(const string & path, const string & dir)
{
	return path.size() > dir.size() && path.compare(0, dir.size(), dir) == 0 &&
	       path.find('/', dir.size()) == string::npos;
}

void XDesktopContainer::startDesktopWatch()
{
	DesktopConfig * dConfig = dynamic_cast<DesktopConfig *>(config);
	if (!dConfig || dConfig->getDesktopWatchDir().empty())
		return;
	watchDir = dConfig->getDesktopWatchDir();

	watchFd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC); // CLOEXEC: launched programs don't inherit it
	if (watchFd < 0)
	{
		cerr << "Not watching " << watchDir << ": " << strerror(errno) << "\n";
		return;
	}
	watchWd = inotify_add_watch(watchFd, watchDir.c_str(),
	                            IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO |
	                            IN_CLOSE_WRITE | IN_ATTRIB);
	if (watchWd < 0)
	{
		cerr << "Not watching " << watchDir << ": " << strerror(errno) << "\n";
		close(watchFd);
		watchFd = -1;
		return;
	}

	// what the icons on screen were built from, so a later change can be told apart
	for (unsigned int i = 0; i < iconList.size(); i++)
	{
		XIcon * icon = dynamic_cast<XIcon *>(iconList[i]);
		DesktopIconConfig * cfg =
		    icon ? dynamic_cast<DesktopIconConfig *>(icon->getIconConfig()) : NULL;
		if (cfg)
			recordMtime(cfg->getIconFilename());
	}
}

void XDesktopContainer::stopDesktopWatch()
{
	if (watchFd >= 0)
		close(watchFd); // also drops the watch
	watchFd = watchWd = -1;
}

// Remembers when a .desktop/.lnk in the Desktop directory was last read, so a
// later touch or edit can be recognised. Other files carry nothing worth
// refreshing (their icon comes from the name and type, which can't change in
// place), so they aren't tracked.
void XDesktopContainer::recordMtime(const string & path)
{
	if (!isDirectChildOf(path, watchDir))
		return;
	if (!endsWithStr(path, ".desktop") && !endsWithStr(path, ".lnk"))
		return;
	struct stat st;
	if (stat(path.c_str(), &st) == 0)
		shownMtime[path] = mtimeNs(st);
}

// Called at the top of every pass of the event loop: one non-blocking read of
// the inotify descriptor, and the synchronisation once things have been quiet
// for 400 ms (or have been busy for 3 s, so a steady stream can't starve it).
void XDesktopContainer::pollDesktopWatch()
{
	if (watchFd < 0)
		return;

	char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
	for (;;)
	{
		ssize_t len = read(watchFd, buf, sizeof(buf));
		if (len <= 0)
			break; // EAGAIN: nothing more queued
		for (char * p = buf; p < buf + len; )
		{
			struct inotify_event * ev = (struct inotify_event *)p;
			p += sizeof(struct inotify_event) + ev->len;

			bool relevant = (ev->mask & IN_Q_OVERFLOW) != 0;
			if (ev->len > 0)
			{
				string name = ev->name;
				// dotfiles and ~ backups are what file managers and editors use
				// as scratch space while saving; the final rename is what counts
				if (!name.empty() && name[0] != '.' && name[name.size() - 1] != '~')
					relevant = true;
			}
			if (relevant)
			{
				long long now = monotonicMs();
				if (!syncPending)
					syncFirstMs = now;
				syncPending = true;
				syncLastMs = now;
			}
		}
	}

	if (syncPending)
	{
		long long now = monotonicMs();
		if (now - syncLastMs >= 400 || now - syncFirstMs >= 3000)
		{
			syncPending = false;
			syncDesktop();
		}
	}
}

// How long the event loop's select() may sleep: its usual second, or less when
// a synchronisation is due sooner.
int XDesktopContainer::watchTimeoutMs()
{
	if (watchFd < 0 || !syncPending)
		return 1000;
	long long now = monotonicMs();
	long long due = syncLastMs + 400;
	if (syncFirstMs + 3000 < due)
		due = syncFirstMs + 3000;
	long long wait = due - now;
	if (wait < 1)
		wait = 1;
	return wait > 1000 ? 1000 : (int)wait;
}

XIcon * XDesktopContainer::createXIcon(DesktopIconConfig * cfg)
{
	XIcon * icon;
	if (cfg->getSnapShadow())
		icon = new XIconWithShadow(this, config, cfg);
	else
		icon = new XIcon(this, config, cfg);
	if (!icon->isValid() || !icon->createIcon())
		return NULL; // left alone, as loadIcons() leaves an icon that won't load
	return icon;
}

// Brings the screen in line with the Desktop directory: icons whose file went
// away are removed, .desktop/.lnk files that changed are re-read in place, and
// files that are new become icons in the first free slot. Idempotent: what
// idesk-ng itself just did (a Delete, a Rename) leaves nothing to change, so
// the inotify events those actions cause are harmless.
void XDesktopContainer::syncDesktop()
{
	DesktopConfig * dConfig = dynamic_cast<DesktopConfig *>(config);
	if (!dConfig || watchFd < 0)
		return;

	// what is on screen from this directory
	struct Shown { XIcon * icon; DesktopIconConfig * cfg; string path; };
	vector<Shown> shown;
	set<string> shownPaths;
	for (unsigned int i = 0; i < iconList.size(); i++)
	{
		XIcon * icon = dynamic_cast<XIcon *>(iconList[i]);
		DesktopIconConfig * cfg =
		    icon ? dynamic_cast<DesktopIconConfig *>(icon->getIconConfig()) : NULL;
		if (!cfg)
			continue;
		string path = cfg->getIconFilename();
		if (!isDirectChildOf(path, watchDir))
			continue;
		Shown s = { icon, cfg, path };
		shown.push_back(s);
		shownPaths.insert(path);
	}

	// gone, or changed
	for (unsigned int i = 0; i < shown.size(); i++)
	{
		struct stat st;
		if (lstat(shown[i].path.c_str(), &st) != 0)
		{
			// the file is gone: icon and saved position go with it, as for Delete
			removeXIcon(shown[i].icon);
			dConfig->removeIconConfig(shown[i].cfg);
			removeLayoutPosition(shown[i].path);
			shownPaths.erase(shown[i].path);
			shownMtime.erase(shown[i].path);
		}
		else if (endsWithStr(shown[i].path, ".desktop") || endsWithStr(shown[i].path, ".lnk"))
		{
			map<string, long long>::iterator m = shownMtime.find(shown[i].path);
			if (m == shownMtime.end() || m->second != mtimeNs(st))
			{
				if (!refreshIcon(shown[i].icon))
				{
					// no longer readable as an icon (hidden, or broken by an
					// edit): it would not be shown at startup either. Its saved
					// position stays, in case a following save makes it valid.
					removeXIcon(shown[i].icon);
					dConfig->removeIconConfig(shown[i].cfg);
					shownPaths.erase(shown[i].path);
					shownMtime.erase(shown[i].path);
				}
			}
		}
	}

	// new
	vector<string> names;
	DIR * dir = opendir(watchDir.c_str());
	if (dir)
	{
		struct dirent * e;
		while ((e = readdir(dir)) != NULL)
			names.push_back(e->d_name);
		closedir(dir);
	}
	sort(names.begin(), names.end());
	for (unsigned int i = 0; i < names.size(); i++)
	{
		string path = watchDir + names[i];
		if (shownPaths.count(path))
			continue;

		struct stat st;
		if (lstat(path.c_str(), &st) != 0)
			continue;
		long long ns = mtimeNs(st);
		map<string, long long>::iterator r = rejectedMtime.find(path);
		if (r != rejectedMtime.end() && r->second == ns)
			continue; // already judged and complained about, unchanged since

		DesktopIconConfig * cfg = dConfig->createDesktopIconConfig(path);
		if (!cfg)
		{
			rejectedMtime[path] = ns;
			continue;
		}
		rejectedMtime.erase(path);
		dConfig->adoptIconConfig(cfg);

		XIcon * icon = createXIcon(cfg);
		if (!icon)
		{
			rejectedMtime[path] = ns;
			continue;
		}
		addIcon(icon);
		arrangeIcons(icon); // first free slot, unless layout.db already knows this file
		recordMtime(path);
		shownPaths.insert(path);
	}

	// forget what no longer exists
	for (map<string, long long>::iterator r = rejectedMtime.begin(); r != rejectedMtime.end(); )
	{
		struct stat st;
		if (lstat(r->first.c_str(), &st) != 0)
			rejectedMtime.erase(r++);
		else
			++r;
	}
	for (map<string, long long>::iterator m = shownMtime.begin(); m != shownMtime.end(); )
	{
		if (!shownPaths.count(m->first))
			shownMtime.erase(m++);
		else
			++m;
	}
	XFlush(display);
}

// Single-quotes a string for /bin/sh (every Exec-style command in this
// project runs through "sh -c"), so text that contains quotes, spaces or
// shell metacharacters -- a file name in an error message, say -- can
// never be interpreted as part of the command.
static string shellQuote(const string & s)
{
	string out = "'";
	for (size_t i = 0; i < s.size(); i++)
	{
		if (s[i] == '\'')
			out += "'\\''";
		else
			out += s[i];
	}
	out += "'";
	return out;
}

static bool endsWith(const string & s, const string & suffix)
{
	return s.size() >= suffix.size() &&
	       s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static string baseNameOf(const string & path)
{
	size_t p = path.find_last_of('/');
	return p == string::npos ? path : path.substr(p + 1);
}

// Trim, and turn any newline into a space (a .lnk/.desktop value is one line).
static string cleanName(const string & in)
{
	string s = in;
	for (size_t i = 0; i < s.size(); i++)
		if (s[i] == '\n' || s[i] == '\r' || s[i] == '\t')
			s[i] = ' ';
	size_t a = s.find_first_not_of(' ');
	if (a == string::npos)
		return "";
	size_t b = s.find_last_not_of(' ');
	return s.substr(a, b - a + 1);
}

// Tell the person something, via the same --show-message popup the Trash
// icon uses, launched through the existing runCommand() fork+exec helper
// so the main loop isn't blocked on it.
void XDesktopContainer::notify(const string & text)
{
	runCommand(shellQuote(resolveSelfPathForMessage()) +
	           " --show-message " + shellQuote(text));
}

// Re-reads one icon from disk and swaps it in on screen, in place -- no
// restart. Builds a fresh DesktopIconConfig from the icon's file (the code
// startup uses, via DesktopConfig::rebuildIconConfig, which also swaps it into
// the config list), builds and shows a new XIcon for it at the old icon's
// current position, and only then tears down the old XIcon and its config.
// Returns false, leaving the old icon on screen, if a step fails -- the caller
// then falls back to a full restart, which is always safe because whatever
// changed is already on disk.
//
// On success the old `oldIcon` is destroyed: callers must not touch it again.
bool XDesktopContainer::refreshIcon(XIcon * oldIcon)
{
	DesktopIconConfig * oldConfig =
	    dynamic_cast<DesktopIconConfig *>(oldIcon->getIconConfig());
	DesktopConfig * dConfig = dynamic_cast<DesktopConfig *>(config);
	if (!oldConfig || !dConfig)
		return false;

	int x = oldIcon->getX();
	int y = oldIcon->getY();

	DesktopIconConfig * fresh = dConfig->rebuildIconConfig(oldConfig);
	if (!fresh)
		return false;
	// keep the icon exactly where it is on screen right now, whether or not
	// anything was ever written to disk for its position
	fresh->setPosition(x, y);

	XIcon * newIcon;
	if (fresh->getSnapShadow() && fresh->getSnapShadow())
		newIcon = new XIconWithShadow(this, config, fresh);
	else
		newIcon = new XIcon(this, config, fresh);

	if (!newIcon->isValid() || !newIcon->createIcon())
		return false;

	vector<AbstractIcon *>::iterator it =
	    find(iconList.begin(), iconList.end(), oldIcon);
	if (it != iconList.end())
		iconList.erase(it);
	delete oldIcon;
	delete oldConfig; // only now: the XIcon that referenced it is gone

	addIcon(newIcon);

	// createIcon() only creates the windows. Positioning and mapping them is
	// done per icon at the end of arrangeIcons() on startup, which a single
	// replaced icon never goes through: without these three calls the new icon
	// sat unmapped and unplaced, i.e. the renamed icon simply vanished.
	newIcon->moveImageWindow();
	newIcon->mapImageWindow();
	newIcon->initMapCaptionWindow();

	XFlush(display);
	return true;
}

// Rename, per the agreed rule: for a .lnk or a .desktop it changes the
// *shown* name (Caption: / Name=) and never the file; for a plain
// file/folder in ~/Desktop -- which has no name other than its file name
// -- it renames the file itself. The screen is then refreshed by replacing
// just that icon in place (refreshIcon); if that fails it falls back to the
// full restart (Application::restartIdesk, the existing Reload).
void XDesktopContainer::renameIcon(XIcon * icon)
{
	DesktopIconConfig * dIconConfig =
	    dynamic_cast<DesktopIconConfig *>(icon->getIconConfig());
	if (!dIconConfig)
		return;

	string path = dIconConfig->getIconFilename();
	bool isLnk = endsWith(path, ".lnk");
	bool isDesktop = endsWith(path, ".desktop");

	string initial = (isLnk || isDesktop) ? dIconConfig->getCaption()
	                                      : baseNameOf(path);

	string typed;
	if (!showTextInput(display, DefaultScreen(display), rootWindow,
	                   imlib_context_get_visual(), imlib_context_get_colormap(),
	                   "Rename", initial, typed))
		return;

	string newName = cleanName(typed);
	if (newName.empty() || newName == initial)
		return;

	string error;
	bool ok;
	if (isLnk)
		ok = setLnkCaption(path, newName, error);
	else if (isDesktop)
		ok = setDesktopName(path, newName, error);
	else
	{
		string newPath;
		ok = renamePlainFile(path, newName, newPath, error);
		if (ok)
		{
			// Position is keyed by file path in layout.db: carry it over to
			// the new path and drop the old entry. The in-memory path is
			// updated as well so that, should the in-place refresh below fail
			// and fall back to a full restart, saveState() records this icon
			// under its new path instead of resurrecting the old entry.
			removeLayoutPosition(path);
			seedLayoutPosition(newPath, icon->getX(), icon->getY());
			dIconConfig->setIconFilename(newPath);
		}
	}

	if (!ok)
	{
		notify(error);
		return;
	}

	// `icon` is destroyed by a successful refresh -- not touched after this
	if (!refreshIcon(icon))
		app->restartIdesk();
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
		else if (chosen >= 0 && items[chosen] == "Rename")
			renameIcon(icon); // may restart idesk-ng and never return
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

// Launches `command` through /bin/sh and returns without waiting for it.
//
// Double fork, the standard way to start something you don't want to be
// responsible for: the intermediate child exits at once, so the grandchild
// that actually runs the program is adopted by init (or the session's
// subreaper) and nobody in idesk-ng ever has to reap it. This replaces two
// earlier designs: the original blocking waitpid() (every icon, tooltip and
// menu froze while the launched program was open), and a SIGCHLD handler that
// reaped *every* child -- which also steals the exit status of children other
// libraries spawn and wait for themselves. On current Ubuntu gdk-pixbuf loads
// each image through a glycin helper process, with a thread blocked in wait4()
// for it; idesk-ng must never touch children it did not create.
void XDesktopContainer::runCommand(const string & command)
{
    pid_t pid = fork();
    if (pid == 0) {
        // intermediate child: only forks again and leaves (async-signal-safe
        // calls only -- this is a copy of a multithreaded process)
        pid_t pid2 = fork();
        if (pid2 == 0) {
#ifdef HAVE_STARTUP_NOTIFICATION
            if (sn_context != NULL)
                sn_launcher_context_setup_child_process (sn_context);
#endif /* HAVE_STARTUP_NOTIFICATION  */
            setsid();
            if (execl("/bin/sh", "/bin/sh", "-c", command.c_str(), (char *)0) == -1) {
                fprintf(stderr, "Error to execute command '%s': %s\n", command.c_str(), strerror(errno));
                _exit(127);
            }
        }
        _exit(pid2 < 0 ? 1 : 0);
    } else if (pid < 0) {
        fprintf(stderr, "Failed to fork process to run command '%s': %s\n", command.c_str(), strerror(errno));
    } else {
        // wait only for the intermediate child, which exits immediately
        int status;
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) { }
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

