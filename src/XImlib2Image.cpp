/* vim:tabstop=4:expandtab:shiftwidth=4
 * 
 * Idesk -- XImlib2Image.cpp
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
#ifdef SHAPE
#include  <X11/extensions/shape.h>
#endif // SHAPE
#include "XImlib2Image.h"
#include "XIcon.h"

XImlib2Image::XImlib2Image(AbstractContainer * c, AbstractIcon * iParent,
                         AbstractConfig * con, AbstractIconConfig * iConfig)
                            : AbstractImage(c, iParent, con, iConfig),
                              hasAlpha(false), glowing(false),
                              rgb(NULL), alpha(NULL), alpha2(NULL),
                              argbData(NULL), image(NULL), vectorPixbuf(NULL),
                              tooltip(NULL), colorMod(NULL), window(0), display(NULL)
{   
}

XImlib2Image::~XImlib2Image()
{
    // The icon's own X window. Nothing ever destroyed it: this destructor
    // was completely empty until the memory-leak pass, which only taught
    // it to free Imlib2/gdk-pixbuf memory -- never this server-side
    // resource, because every earlier code path that destroyed an icon
    // (shutdown, Reload) ended the process right after. Delete is the
    // first to destroy icons mid-session, and the window outlived its
    // icon as a visible ghost that nothing handled events for. `window`
    // is zero-initialized in the constructor, so an icon whose
    // createWindow() failed before XCreateWindow() safely skips this
    // (destroying an uninitialized id would raise BadWindow, whose
    // default Xlib handler exits the whole process).
    // Both must be set: `display` is assigned only by configure(), which
    // runs for the real icon image but never for XImlib2Caption (a
    // subclass sharing this member `window`, created and destroyed by
    // ~XImlib2Caption() itself using the container's display). Without
    // this, a caption's window was destroyed twice -- the second time
    // through an uninitialized Display* -- and Delete crashed the whole
    // process (found with gdb).
    if (window && display)
    {
        XDestroyWindow(display, window);
        window = 0;
    }

    // Most icons (plain PNG/XPM, loaded by Imlib2's own native loader)
    // never touch any of these -- they stay NULL and every delete/free
    // below is a safe no-op. Only SVG-sourced icons
    // (createPictureFromSvg()) actually allocate them. This mirrors the
    // exact bug class already fixed in XImlib2Background::spareRoot:
    // these members used to be left uninitialized, which would have
    // made the frees below read garbage for the common (non-SVG) case
    // instead of correctly doing nothing -- fixed by zero-initializing
    // all of them in the constructor above.
    if (image)
    {
        imlib_context_set_image(image);
        imlib_free_image();
        image = NULL;
    }

    // argbData must be freed after imlib_free_image(), never before --
    // Imlib2 keeps using this buffer for as long as `image` is alive and
    // never frees it itself (see the member's own comment in the header).
    if (argbData)
    {
        delete[] argbData;
        argbData = NULL;
    }

    if (rgb)
    {
        delete[] rgb;
        rgb = NULL;
    }
    if (alpha)
    {
        delete[] alpha;
        alpha = NULL;
    }
    if (alpha2)
    {
        delete[] alpha2;
        alpha2 = NULL;
    }

    if (vectorPixbuf)
    {
        g_object_unref(vectorPixbuf);
        vectorPixbuf = NULL;
    }

    // Found once the process could complete a full clean shutdown for
    // the first time (valgrind had never traced this far before --
    // always crashed first): imlib_create_color_modifier() in
    // configure() was never paired with imlib_free_color_modifier().
    if (colorMod)
    {
        imlib_context_set_color_modifier(colorMod);
        imlib_free_color_modifier();
    }

    // Found via a graceful-shutdown debug pass (confirmed every other
    // destructor in the chain -- ~XIcon(), ~XImlib2Caption(), etc. --
    // runs correctly after the signal-handling fix): createToolTip()
    // (called unconditionally from XIcon::createIcon(), unless
    // createWindow() fails first and returns early -- hence the
    // NULL-initialized default and this guard) allocates this with
    // `new` and nothing ever deleted it. This is exactly what was
    // still showing up as a leak in XImlib2ToolTip::createFont() even
    // after confirming the rest of the chain works.
    if (tooltip)
    {
        delete tooltip;
        tooltip = NULL;
    }
}

void XImlib2Image::configure()
{
    x = iconParent->getX();
    y = iconParent->getY();

    DesktopIconConfig * dIconConfig =
        dynamic_cast<DesktopIconConfig *>(iconConfig);
	
    DesktopConfig * dConfig =
		    dynamic_cast<DesktopConfig *>(config);

    transparency = dConfig->getTransparency();

    width = dIconConfig->getWidth();
    height = dIconConfig->getHeight();
    
    glowing = false;
    glowChange = true;

    colorMod = imlib_create_color_modifier();
    
    fillStyle = dIconConfig->getFillStyle();
    
    XDesktopContainer * xContainer =
		    dynamic_cast<XDesktopContainer *>(container);  
	
    rootWindow = xContainer->getRootWindow();
    widthOfScreen = xContainer->widthOfScreen();
    heightOfScreen = xContainer->heightOfScreen();
    display = xContainer->getDisplay();
    visual = imlib_context_get_visual();
    cmap = imlib_context_get_colormap();
    screen = DefaultScreen(display);
    depth =  DefaultDepth(display,screen);
}

#ifdef HAVE_SVG
bool XImlib2Image::createPictureFromSvg()
{
    DesktopIconConfig * dIconConfig =
        dynamic_cast<DesktopIconConfig *>(iconConfig);

    // gdk_pixbuf_new_from_file_at_size() asserts (width > 0 || width == -1)
    // internally -- with no explicit Width/Height (e.g. a GenericFileIcon,
    // or a .desktop file with no X-Idesk-Width/Height), width/height default
    // to 0, which used to hit that assertion and then crash on a null
    // GError below, since glib's own assertion failure never populates
    // gErr in the first place. Treat 0-sized icons as "nothing to render"
    // instead of asking gdk-pixbuf to load at an invalid size.
    if (width <= 0 || height <= 0)
    {
        cerr << "Warning: \"" << dIconConfig->getPictureFilename()
             << "\" has no valid Width/Height to render an SVG at -- "
             << "skipping this icon's image\n";
        return false;
    }

    GError * gErr = nullptr;
    // load SVG file and scale it to the icon size
    vectorPixbuf = gdk_pixbuf_new_from_file_at_size(dIconConfig->getPictureFilename().c_str(),
                                                 width, height, &gErr);
    if (!vectorPixbuf)
    {   
        cerr << "librsvg error: " << (gErr ? gErr->message : "(unknown -- no GError set)") << endl;
        if (gErr)
            g_clear_error (&gErr);
        return false;
    }

    hasAlpha = gdk_pixbuf_get_has_alpha(vectorPixbuf);
    /*
    rgb = (unsigned char *)malloc( width * height * 3 );
    alpha = (unsigned char *)malloc( width * height );
    alpha2 = (unsigned char *)malloc( width * height );
    */
    rgb = new unsigned char[width * height * 3];
    alpha = new unsigned char[width * height];
    alpha2 = new unsigned char[width * height];

    unsigned char r, g, b, a;
    unsigned char * origPixbufRgb = gdk_pixbuf_get_pixels(vectorPixbuf);
    unsigned char * rgbPtr = rgb;
    unsigned char * aPtr = alpha;
    unsigned char * aPtr2 = alpha2;
    unsigned char * pixbufRgb = origPixbufRgb;
    
    for(int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            r = *pixbufRgb++;
            g = *pixbufRgb++;
            b = *pixbufRgb++;
            a = *pixbufRgb++;

            *rgbPtr++ = r;
            *rgbPtr++ = g;
            *rgbPtr++ = b;
            
            // Transparency matrix.
            if( ( a - transparency ) < 0 )
            {
                *aPtr++ = 0;
                *aPtr2++ = a;
            }
            else 
            {
                *aPtr++ = a - transparency;
                *aPtr2++ = a;
            }
        }
    }

    // imlib_create_image_using_data() expects a packed 32-bit ARGB buffer
    // (0xAARRGGBB per pixel) -- NOT gdk-pixbuf's raw R,G,B,A byte layout.
    // The previous version passed origPixbufRgb (gdk-pixbuf's own buffer)
    // straight through, completely bypassing the rgb[]/alpha[] arrays just
    // computed above: Imlib2 misread gdk-pixbuf's bytes as if they were
    // already ARGB32, misinterpreting which byte was the alpha channel.
    // The visible symptom (found on real hardware): every SVG-sourced icon
    // rendered with its transparent margin as solid opaque black instead
    // of correctly showing the desktop background through it, while
    // PNG/XPM icons (loaded by Imlib2's own native loader, never touching
    // this function) looked correct. Build the real buffer from the
    // already-computed rgb[]/alpha[] arrays instead.
    argbData = new DATA32[width * height];
    for (int i = 0; i < width * height; i++)
        argbData[i] = ((DATA32)alpha[i] << 24) | ((DATA32)rgb[i*3] << 16) |
                      ((DATA32)rgb[i*3+1] << 8) | (DATA32)rgb[i*3+2];

    image = imlib_create_image_using_data(width, height, argbData);
    if (image)
    {
        imlib_context_set_image(image);
        imlib_image_set_has_alpha(1);
    }

    if (!image) {
        cerr << "Cannot create image from SVG pixbuf data" << endl;
        return false;
    }

    return true;
}
#endif

bool XImlib2Image::createPicture()
{
	DesktopIconConfig * dIconConfig =
			dynamic_cast<DesktopIconConfig *>(iconConfig);
#ifdef HAVE_SVG
    if (dIconConfig->isSvg()) {
        return createPictureFromSvg();
    }
#endif
	
	image = imlib_load_image(dIconConfig->getPictureFilename().c_str());

	if (image)
	{
		imlib_context_set_image(image);

		orgWidth = imlib_image_get_width();
		orgHeight = imlib_image_get_height();

		if (width == 0)
			width = imlib_image_get_width();
		if (height == 0)
			height= imlib_image_get_height();

        if ((orgWidth != width) || (orgHeight != height)) {
            Imlib_Image tempImg = imlib_create_cropped_scaled_image(0, 0,
                    orgWidth, orgHeight, width, height);

            imlib_free_image();

            image = tempImg;
            imlib_context_set_image(image);
        }
        
        if (imlib_image_has_alpha() == 1)
            hasAlpha = true;
        else
            hasAlpha = false;
        return true;
	}

	cerr << "Cannot load: " << dIconConfig->getPictureFilename() << endl
			<< "Check to see if the image and path to image are valid\n";
    return false;
}

Window * XImlib2Image::getWindow()
{
    return &window;
}

bool XImlib2Image::createWindow()
{	
    if (!createPicture())
        return false;

    DesktopConfig * dConfig =
		    dynamic_cast<DesktopConfig *>(config);
    

    XSetWindowAttributes attr;
    attr.background_pixmap = ParentRelative;
    attr.backing_store = Always;
    attr.override_redirect = True;
    attr.save_under = True;
    attr.cursor = XCreateFontCursor(display, dConfig->getCursorOver());
  
    attr.event_mask = SubstructureRedirectMask |
                      SubstructureNotifyMask   |
                      ButtonPressMask          |
                      ButtonReleaseMask        |
                      PointerMotionMask        |
                      EnterWindowMask          |
                      PropertyChangeMask       |
                      LeaveWindowMask           |
                      ExposureMask;
    
    window = XCreateWindow( display,
                            rootWindow,
                            0,
                            0,
                            width,
                            height,
                            0,
                            depth,
                            CopyFromParent,
                            CopyFromParent,
			    CWSaveUnder|CWBackPixmap|CWBackingStore|CWOverrideRedirect|CWEventMask| CWCursor,
                            &attr );  
    shapeWindow();

    return true;
}

void XImlib2Image::createToolTip()
{
  tooltip = new XImlib2ToolTip(container,iconParent,config,iconConfig);
  tooltip->createFont();
  tooltip->createWindow();
}

Window XImlib2Image::getWindowToolTip()
{
  return tooltip->getWindow();
}

void XImlib2Image::setupLayer() {
  
    XDesktopContainer * xContainer =
            dynamic_cast<XDesktopContainer *>(container);
	    
    Display *disp = xContainer->getDisplay();

    Atom type = XInternAtom(disp, "_NET_WM_WINDOW_TYPE", false);
    Atom state = XInternAtom(disp, "_NET_WM_STATE", false);
    
    Atom state_atoms[] = {
        XInternAtom(disp, "_NET_WM_STATE_BELOW", false),
        XInternAtom(disp, "_NET_WM_STATE_HIDDEN", false),
        XInternAtom(disp, "_NET_WM_STATE_SKIP_PAGER", false),
        XInternAtom(disp, "_NET_WM_STATE_SKIP_TASKBAR", false),
        XInternAtom(disp, "_NET_WM_STATE_STICKY", false)
    };

    Atom type_atoms =  XInternAtom(disp, "_NET_WM_WINDOW_TYPE_DESKTOP", false);

    XChangeProperty(disp, window, state, XA_ATOM,
                                 32, PropModeReplace,
                                 (unsigned char *)(state_atoms), 5);
    XChangeProperty(disp, window, type, XA_ATOM,
                                 32, PropModeReplace,
                                 (unsigned char *)(&type_atoms), 1);

    XChangeProperty(disp, window, state, XA_ATOM,
                                  32, PropModeReplace,
                                  (unsigned char *)(state_atoms), 5);
    
    XChangeProperty(disp, window, type, XA_ATOM,
                                  32, PropModeReplace,
                                  (unsigned char *)(&type_atoms), 1);

}


void XImlib2Image::shapeWindow()
{
     XDesktopContainer * xContainer = dynamic_cast<XDesktopContainer *>(container);
     
    //Shape the window to the icon
    imlib_context_set_image(image);

    imlib_context_set_color_modifier(colorMod);
    imlib_set_color_modifier_tables(mapNone, mapNone, mapNone, mapZeroAlpha);
    Pixmap pixmap, pixMask;
    imlib_render_pixmaps_for_whole_image(&pixmap, &pixMask);
    XSetWindowBackgroundPixmap (display, window, pixmap);
    
#ifdef SHAPE
    XShapeCombineMask(display, window, ShapeBounding, 0, 0, pixMask, ShapeSet);
#endif // SHAPE
    
    imlib_reset_color_modifier();
    imlib_free_pixmap_and_mask(pixmap);
}

void XImlib2Image::lowerWindow()
{
    XLowerWindow( display , window  );
}

void XImlib2Image::moveWindow(int xCord, int yCord)
{
    XIcon * xIcon = dynamic_cast<XIcon *>(iconParent);

    if (xCord >= 0 &&
        yCord >= 0 &&
        xCord < widthOfScreen &&
        yCord < heightOfScreen )
    {
        x = xCord;
        y = yCord;
        xIcon->setX(x);
        xIcon->setY(y);
         
	 XMoveWindow( display , window, x, y );
    }
    else
    {
        x = widthOfScreen/2;
        y = heightOfScreen/2;
        xIcon->setX(x);
        xIcon->setY(y);
	XMoveWindow( display , window, x, y );
    }
}

void XImlib2Image::redraw()
{
}

void XImlib2Image::draw()
{
    refreshIcon();
}

void XImlib2Image::unmapWindow()
{
    XUnmapWindow( display, window );
}

void XImlib2Image::mapWindow()
{
    XMapWindow( display , window );
}

void XImlib2Image::refreshIcon()
{
    imlib_context_set_image(image);
    x = iconParent->getX();
    y = iconParent->getY();
	
    applyMouseOverEffects();
    repaint();
}

void XImlib2Image::repaint()
{
    XDesktopContainer * xContainer =
	dynamic_cast<XDesktopContainer *>(container);
    
    Imlib_Image cropImage = xContainer->bg->createCropImage(x, y, width, height, width, height);
    
    imlib_context_set_dither(1);

    imlib_context_set_blend(1);       //automatically blend image and background
    imlib_context_set_dither_mask(0);
    imlib_context_set_image(cropImage);
    imlib_blend_image_onto_image(image, 1, 0, 0, width, height, 0, 0, width, height);
    
    imlib_image_set_has_alpha(1);
    
    imlib_context_set_anti_alias(1);  //smoother scaling
    imlib_context_set_blend(0);    
    
    imlib_context_set_drawable(window);
    imlib_render_image_on_drawable_at_size(0, 0, width, height);
    
    imlib_free_image();
    imlib_context_set_drawable(xContainer->getRootWindow());
}

void XImlib2Image::applyMouseOverEffects()
{
    if (glowChange)
    {
	        imlib_context_set_color_modifier(colorMod);
		imlib_get_color_modifier_tables(mapNone, mapNone, mapNone, NULL);
		imlib_reset_color_modifier();

	if (glowing == true)  // reset to standard alpha mapping
           imlib_set_color_modifier_tables(mapNone, mapNone, mapNone, mapNone);
	else  // remap alpha values lower to increase transparency
	       imlib_set_color_modifier_tables(mapNone, mapNone, mapNone, defaultTransTable);
	
        glowChange = false;
    }
}

void XImlib2Image::mouseOverEffect()
{
    glowing = true;
    glowChange = true;
    refreshIcon();
}

void XImlib2Image::mouseOffEffect()
{
    glowing = false;
    glowChange = true;
    refreshIcon();
}


void XImlib2Image::event_enter_notify ()
{
   if(tooltip)
     tooltip->event_enter_notify();
}

void XImlib2Image::event_leave_notify ()
{
  if(tooltip)
    tooltip->event_leave_notify(); 
}

void XImlib2Image::unpressImage(){
     if(fillStyle != "None"){
         mouseOffEffect();
         event_leave_notify(); 
      }  
}

void XImlib2Image::pressImage(){
	    XGCValues values;
	    values.function = GXinvert;
	    values.fill_style =  FillSolid;
	    values.background =  WhitePixel(display, depth);
	    values.foreground =  BlackPixel(display, depth);

            Pixmap pixmap = XCreatePixmap(display, window, width, height, depth);
	    GC gcPress = XCreateGC(display, pixmap, GCFillStyle | GCFunction| GCForeground | GCBackground, &values);
	 
	    if(fillStyle == "FillInvert"){
            mouseOverEffect();
	        XFillRectangle(display, window, gcPress, 0, 0, width, height);
	    }
	    else if(fillStyle == "FillHLine"){  
            mouseOverEffect();
            for (unsigned int i = 0; i < height; i += 3)
                XDrawLine(display, window, gcPress, 0, i, width, i);
	    }	    
	    else if(fillStyle == "FillVLine"){
            mouseOverEffect();
	        for (unsigned int i = 0; i < height; i += 3)
		        XDrawLine(display, window, gcPress, i, 0, i, height);	    
	    }  
        XFreePixmap(display, pixmap);
	XFreeGC(display, gcPress);
}


void XImlib2Image::initalize()
{
    createTransTables();
}

void XImlib2Image::createTransTables()
{
    for (int i = 0; i < 256; i++)
    {
        if (i - transparency > 0)
            defaultTransTable[i] = i - transparency;
        else
            defaultTransTable[i] = 0;
    }
}

// array used to map the first few values of alpha to totally transparent and
// all other pixels to opaque. Used to create the window shape mask
unsigned char XImlib2Image::mapZeroAlpha[256] =
{
    0, 0, 0, 0, 0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255
}
;

// a generic mapping of 0 to 0, 1 to 1, etc
// used to preserve colors while remapping alpha channel
unsigned char XImlib2Image::mapNone[256] =
{
    0  , 1  , 2  , 3  , 4  , 5  , 6  , 7  , 8  , 9  , 10 , 11 , 12 ,
    13 , 14 , 15 , 16 , 17 , 18 , 19 , 20 , 21 , 22 , 23 , 24 , 25 ,
    26 , 27 , 28 , 29 , 30 , 31 , 32 , 33 , 34 , 35 , 36 , 37 , 38 ,
    39 , 40 , 41 , 42 , 43 , 44 , 45 , 46 , 47 , 48 , 49 , 50 , 51 ,
    52 , 53 , 54 , 55 , 56 , 57 , 58 , 59 , 60 , 61 , 62 , 63 , 64 ,
    65 , 66 , 67 , 68 , 69 , 70 , 71 , 72 , 73 , 74 , 75 , 76 , 77 ,
    78 , 79 , 80 , 81 , 82 , 83 , 84 , 85 , 86 , 87 , 88 , 89 , 90 ,
    91 , 92 , 93 , 94 , 95 , 96 , 97 , 98 , 99 , 100, 101, 102, 103,
    104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116,
    117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127, 128, 129,
    130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142,
    143, 144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155,
    156, 157, 158, 159, 160, 161, 162, 163, 164, 165, 166, 167, 168,
    169, 170, 171, 172, 173, 174, 175, 176, 177, 178, 179, 180, 181,
    182, 183, 184, 185, 186, 187, 188, 189, 190, 191, 192, 193, 194,
    195, 196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207,
    208, 209, 210, 211, 212, 213, 214, 215, 216, 217, 218, 219, 220,
    221, 222, 223, 224, 225, 226, 227, 228, 229, 230, 231, 232, 233,
    234, 235, 236, 237, 238, 239, 240, 241, 242, 243, 244, 245, 246,
    247, 248, 249, 250, 251, 252, 253, 254, 255 
};
