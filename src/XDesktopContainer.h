/* vim:tabstop=4:expandtab:shiftwidth=4
 * 
 * Idesk -- XDesktopContainer.h
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

#ifndef XDESKTOP_CONTAINER_CLASS
#define XDESKTOP_CONTAINER_CLASS

#include <sys/wait.h>
#include <map>
#include <errno.h>
#ifdef HAVE_STARTUP_NOTIFICATION
#include <libsn/sn.h>
#include <assert.h>
#define G_USEC_PER_SEC 1000000
#endif /* HAVE_STARTUP_NOTIFICATION  */

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#include <X11/Xlib.h>
#include <Imlib2.h>

#include "DesktopContainer.h"
#include "DesktopConfig.h"
#include "ActionConfig.h"


static int error_trap_depth = 0;

class XIcon;
class Timer;
class  XImlib2Background;



class XDesktopContainer : public DesktopContainer
{
    private:
        Timer *timer;	
        // Watching ~/Desktop for files that appear, go away or are touched.
        int watchFd, watchWd;
        string watchDir;          // the Desktop directory, whether or not it exists yet
        // Until the directory exists, the nearest directory above it that does is
        // watched instead (waitWd), for the creation of waitName inside it.
        int waitWd;
        string waitName;
        bool targetWatched;       // watchWd is a watch on watchDir itself
        // A rename made outside idesk-ng arrives as two events that share a cookie
        map<unsigned int, string> pendingMoveFrom;       // cookie -> name that left
        vector< pair<string, string> > renamedFiles;     // (old, new) full paths
        void armDesktopWatch();
        void noteRename(const string & from, const string & to);
        bool syncPending;
        long long syncFirstMs, syncLastMs;
        map<string, long long> shownMtime;    // .desktop/.lnk path -> mtime (ns) when shown
        map<string, long long> rejectedMtime; // path -> mtime (ns) of a file that isn't an icon
        void startDesktopWatch();
        void stopDesktopWatch();
        void pollDesktopWatch();
        int watchTimeoutMs();
        void syncDesktop();
        void recordMtime(const string & path);
        XIcon * createXIcon(DesktopIconConfig * cfg);
        void removeXIcon(XIcon * icon);
    public:    
        Window rootWindow;
	XImlib2Background * bg;
	Atom stop;
	
    protected:
        Display* display;
	
#ifdef HAVE_STARTUP_NOTIFICATION
	SnDisplay *sn_display;
	SnLauncherContext *sn_context;
#endif /* HAVE_STARTUP_NOTIFICATION  */
	
        XEvent event;
	
        unsigned int times[3];
        unsigned int numClicks[3];
    public:
        XDesktopContainer(AbstractApp * a);
        ~XDesktopContainer();

        void destroy();
        void create();
        
        void run();
        void parseEvent();

        void initXWin();
        void initImlib();
        void getRootImage();
        void configure();

        void addIcon(AbstractIcon *);
        void addXIcon(const string & file, const string & pictureFile,
                      const string & captionText, const string & command,
                      int xCord, int yCord,
                      const string & fontName, int fontSize, 
                      const string & color,
                      Imlib_Image * spareRoot);

        void eventLoop();
        void parseNonIconEvents();
        XIcon * parseIconEvents();
        void exeCurrentAction(XIcon * icon);
        bool canDrag(XIcon * icon);
        void deleteIcon(XIcon * icon);
        void renameIcon(XIcon * icon);
        void propertiesIcon(XIcon * icon);
        bool renamePlainOnDisk(XIcon * icon, DesktopIconConfig * cfg,
                               const string & newName, string & error);
        bool refreshIcon(XIcon * oldIcon);
        void notify(const string & text);

        virtual void setEventState();
        virtual void translateButtonRelease(int button);
        
        void saveState();
        void saveIcon(AbstractIcon *);
        void reloadState();

        Display * getDisplay() { return display; }
        Window getRootWindow() { return rootWindow; }
	
        void loadIcons();
        void arrangeIcons();               // the base class's contract: all icons
        void arrangeIcons(XIcon * only);   // just one (NULL: all)
	void updateIcons();
        XIcon * findIcon(Window window);
        void addIcon( const string & file, const string & pictureFile,
                      const string & captionText, const string & command, 
                      int xCord, int yCord ) ;

        void runCommand(const string & command);

        virtual int widthOfScreen();
        virtual int heightOfScreen();
	
#ifdef HAVE_STARTUP_NOTIFICATION
	void startup_timeout(SnLauncherContext * sn_context);
	
	static void error_trap_push (SnDisplay *display, Display   *xdisplay){ ++error_trap_depth;}

	static void error_trap_pop (SnDisplay *display, Display   *xdisplay){
		if (error_trap_depth == 0)
		{
			fprintf (stderr, "Error trap underflow!\n");
			exit (1);
		}
  
		XSync (xdisplay, False); /* get all errors out of the queue */
		--error_trap_depth;
	}
#endif /* HAVE_STARTUP_NOTIFICATION  */
	
};

#endif
