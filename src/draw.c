/*
 * Copyright (C) 2015 Ted Meyer
 *
 * see LICENSING for details
 *
 */

#define _DEFAULT_SOURCE

#include <locale.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xft/Xft.h>
#include "draw.h"

#define MAX(a, b)   ((a) > (b) ? (a) : (b))
#define MIN(a, b)   ((a) < (b) ? (a) : (b))
#define unint unsigned int
#define unlong unsigned long


#define MAX_TITLE_LENGTH 50

// draw a rectangle on the screen; either solid or bordered
void drawrect_modifier(DC * dc, int x, int y, unint w, unint h, Bool fill, unlong color) {
    draw_rectangle(dc, dc->x + x, dc->y +y, w, h, fill, color);
}

void draw_rectangle(DC * dc, unint x, unint y, unint w, unint h, Bool fill, unlong color) {
    XRectangle rect = {x, y, w, h};
    XSetForeground(dc -> dpy, dc -> gc, color);

    (fill ? XFillRectangles : XDrawRectangles)(dc -> dpy, dc -> canvas, dc -> gc, &rect, 1);
}

// draw text
void drawtext(DC *dc, const char * text, ColorSet *col) {
    char buf[MAX_TITLE_LENGTH];

    /* shorten text if necessary */
    size_t n = strlen(text);
    size_t mn = MIN(n, sizeof buf);
    for(; textnw(dc, text, mn) > dc->w - dc->font.height/2; mn--) {
        if(mn == 0) {
            return; // dont draw text (there isn't any)
        }
    }

    memcpy(buf, text, mn);

    /* if the text was shortened, add some elipses */
    if(mn < n)
        for(n = MAX(mn-3, 0); n < mn; buf[n++] = '.');

    drawrect_modifier(dc, 0, dc->color_border_pixels,
                          dc->w, dc->h-(2*dc->color_border_pixels),
                          True, col->BG);
    drawtextn(dc, buf, mn, col);
}

// drawtext helper that actually draws the text
void drawtextn(DC * dc, const char * text, size_t n, ColorSet * col) {
    int x = dc->x + dc->font.height/2;
    int y = dc->y + dc->font.ascent + (dc->text_offset_y + dc->color_border_pixels + 2) / 2;

    XSetForeground(dc->dpy, dc->gc, col->FG);


    if(dc->font.xft_font) {
        if (!dc->xftdraw) {
            // TODO: change to a logger
            printf("error, xft drawable does not exist");
        }
        XftDrawStringUtf8(dc->xftdraw, &col->FG_xft,
            dc->font.xft_font, x, y, (unsigned char*)text, n);

    } else if(dc->font.set) {
        XmbDrawString(dc->dpy, dc->canvas, dc->font.set, dc->gc, x, y, text, n);
    } else {
        XSetFont(dc->dpy, dc->gc, dc->font.xfont->fid);
        XDrawString(dc->dpy, dc->canvas, dc->gc, x, y, text, n);
    }
}

// get a color from a string, and save it into the context
unlong getcolor(DC * dc, const char * colstr) {
    XColor color;

    if(!XAllocNamedColor(dc->dpy, dc->wa.colormap, colstr, &color, &color)) {
        printf("cannot allocate color '%s'\n", colstr);
    }

    return color.pixel;
}

// make a color set (foreground, background)
ColorSet * initcolor(DC * dc, const char * foreground, const char * background) {
    ColorSet * col = (ColorSet *)malloc(sizeof(ColorSet));

    col->BG = getcolor(dc, background);
    col->FG = getcolor(dc, foreground);

    if(dc->font.xft_font) {
        if(!XftColorAllocName(dc->dpy, DefaultVisual(dc->dpy, DefaultScreen(dc->dpy)),
            DefaultColormap(dc->dpy, DefaultScreen(dc->dpy)), foreground, &col->FG_xft)) {
        }
    }

    return col;
}


DC * initdc(void) {
    if(!setlocale(LC_CTYPE, "") || !XSupportsLocale()) {
        printf("no locale support\n");
    }

     DC * dc = malloc(sizeof(DC));
    if(! dc) {
        printf("cannot allocate memory");
    }

    dc -> dpy = XOpenDisplay(NULL);
    if(! (dc -> dpy)) {
        printf("cannot open display\n");
    }

    dc->gc = XCreateGC(dc->dpy, DefaultRootWindow(dc->dpy), 0, NULL);
    dc->font.xfont = NULL;
    dc->font.set = NULL;
    dc->font.xft_font = NULL;
    dc->canvas = None;
    dc->xftdraw = NULL;

    XSetLineAttributes(dc->dpy, dc->gc, 1, LineSolid, CapButt, JoinMiter);
    return dc;
}


void initfont(DC * dc, const char * fontstr) {
    char * def, ** missing = NULL;
    int i, n;

    if((dc->font.xfont = XLoadQueryFont(dc->dpy, fontstr))) {
        dc->font.ascent = dc->font.xfont->ascent;
        dc->font.descent = dc->font.xfont->descent;
    } else if((dc->font.set = XCreateFontSet(dc->dpy, fontstr, &missing, &n, &def))) {
        char ** names;
        XFontStruct ** xfonts;

        n = XFontsOfFontSet(dc->font.set, &xfonts, &names);
        for(i = dc->font.ascent = dc->font.descent = 0; i < n; i++) {
            dc->font.ascent = MAX(dc->font.ascent, xfonts[i]->ascent);
            dc->font.descent = MAX(dc->font.descent, xfonts[i]->descent);
        }
    } else if((dc->font.xft_font = XftFontOpenName(dc->dpy,
            DefaultScreen(dc->dpy), fontstr))) {
        dc->font.ascent = dc->font.xft_font->ascent;
        dc->font.descent = dc->font.xft_font->descent;
    } else {
        printf("cannot load font '%s'\n", fontstr);
    }
    if(missing) {
        XFreeStringList(missing);
    }
    dc->font.height = dc->font.ascent + dc->font.descent;
}


void mapdc(DC * dc, Window win, unsigned int w, unsigned int h) {
    XClearArea(dc->dpy, win, 0, 0, w, h, 0);
    XCopyArea(dc->dpy, dc->canvas, win, dc->gc, 0, 0, w, h, 0, 0);
}


void resizedc(DC * dc, unsigned int w, unsigned int h, XVisualInfo * vinfo, XSetWindowAttributes * wa) {
    if(dc->canvas)
        XFreePixmap(dc->dpy, dc->canvas);
    dc->canvas = XCreatePixmap(dc->dpy, DefaultRootWindow(dc->dpy), w, h,
                               vinfo -> depth);
    dc->empty = XCreatePixmap(dc->dpy, DefaultRootWindow(dc->dpy), w, h,
                               vinfo -> depth);
    dc->x = dc->y = 0;
    dc->w = w;
    dc->h = h;
    if(dc->font.xft_font && !(dc->xftdraw)) {
        dc->xftdraw = XftDrawCreate(dc->dpy, dc->canvas, vinfo->visual, wa -> colormap);
        if(!(dc->xftdraw))
            printf("error, cannot create xft drawable\n");
    }
}


int textnw(DC * dc, const char * text, size_t len) {
    len = MIN(MAX_TITLE_LENGTH, len);
    if(dc->font.xft_font) {
        XGlyphInfo gi;
        XftTextExtentsUtf8(dc->dpy, dc->font.xft_font, (const FcChar8*)text, len, &gi);
        return gi.width;
    } else if(dc->font.set) {
        XRectangle r;

        XmbTextExtents(dc->font.set, text, len, NULL, &r);
        return r.width;
    } else {
        return XTextWidth(dc->font.xfont, text, len);
    }
}


int textw(DC * dc, const char * text) {
    return textnw(dc, text, strlen(text)) + dc->font.height;
}
