/*
 * xboard.h - a 2089 board drawn with Xlib, for the X frontends: the plain
 * Xlib one (x11.c) and toolkit ones, which draw the board the same way in a
 * widget's window.
 *
 * The board goes in any rectangle of any drawable (a window or a pixmap of
 * the screen's depth), as large as fits.  Tiles are in the original game's
 * colours where the display has them, and black and white otherwise; tile
 * text in the largest bitmap Helvetica Bold that fits.
 *
 * Author: Claude Opus 5.5 (claude-opus-5-5), October 2026.
 * MIT License; see LICENSE.
 */

#ifndef X89_H
#define X89_H

#include <X11/Xlib.h>
#include "2089.h"

#define X89_FONTS 16
#define X89_TILES 12            /* 2 to 2048, then one for all larger */

/* Named colours, in ui[]. */
#define X89_PAGE  0             /* around the board */
#define X89_BOARD 1
#define X89_EMPTY 2             /* an empty cell */
#define X89_DARK  3             /* text: on 2 and 4, and around the board */
#define X89_LIGHT 4             /* text on larger tiles */
#define X89_NAMED 5

struct x89_board {
    Display *dpy;
    GC gc;                      /* for anyone drawing on the same screen */
    XFontStruct *font[X89_FONTS];   /* tile fonts, tallest first */
    int nfonts;
    int colour;                 /* 1 to draw in colour (only if hascolour) */
    int hascolour;              /* 1 if the colours could be allocated */
    unsigned long ui[X89_NAMED], tile[X89_TILES], black, white;
    Pixmap grey;                /* a 50% stipple, for the mono board */
    int x, y, side;             /* where the board is, after x89_place */
    int cell, gap;
};

/*
 * Get ready to draw on screen of dpy: tile fonts, colours (all or none, in
 * the default colormap), a GC and the stipple, made for drawable d (any
 * window on that screen).  Colour is on if the display has it.  Returns -1
 * if the server has no fonts at all.
 */
int x89_open(struct x89_board *b, Display *dpy, int screen, Drawable d);

/* Place a size x size board, as large as fits, centred in the rectangle. */
void x89_place(struct x89_board *b, int x, int y, int w, int h, int size);

/* Draw the board and its tiles where x89_place put it. */
void x89_draw(struct x89_board *b, Drawable d, const struct t89_game *g);

#endif
