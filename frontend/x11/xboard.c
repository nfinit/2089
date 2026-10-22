/*
 * xboard.c - a 2089 board drawn with Xlib.  See xboard.h.
 *
 * Author: Claude Opus 5.5 (claude-opus-5-5), October 2026.
 * MIT License; see LICENSE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include "xboard.h"

/* ------------------------------------------------------------ fonts */

/* The pixel size field of an XLFD name (-foundry-family-...--size-...),
   or 0 (scalable, or not an XLFD name). */
static int pixels(const char *xlfd)
{
    int dashes = 0;

    for (; *xlfd && dashes < 7; xlfd++)
        if (*xlfd == '-')
            dashes++;
    return dashes == 7 ? atoi(xlfd) : 0;
}

static int height(XFontStruct *f)
{
    return f->ascent + f->descent;
}

/*
 * Every size of the tile font that the server has as a bitmap, one of each:
 * Helvetica Bold, which the X distribution's 75dpi and 100dpi fonts have;
 * without those, the misc fonts' bold fixed (9x15B and the like); without
 * those, "fixed", which every server has.  Scalable entries (size 0) are
 * left out: a bitmap server scales a bitmap font, which looks poor.
 */
static void loadfonts(struct x89_board *b)
{
    static const char *pattern[] = {
        "-*-helvetica-bold-r-normal--*-*-*-*-p-*-iso8859-1",
        "-*-fixed-bold-r-normal--*-*-*-*-c-*-iso8859-1",
        0
    };
    char **names;
    int n, i, j, k, p, px, have[X89_FONTS];
    XFontStruct *f;

    b->nfonts = 0;
    for (p = 0; pattern[p] && b->nfonts == 0; p++) {
        names = XListFonts(b->dpy, (char *) pattern[p], 200, &n);
        for (i = 0; names && i < n && b->nfonts < X89_FONTS; i++) {
            px = pixels(names[i]);
            for (j = 0; j < b->nfonts && have[j] != px; j++)
                ;
            if (px <= 0 || j < b->nfonts || (f = XLoadQueryFont(b->dpy, names[i])) == 0)
                continue;
            for (j = 0; j < b->nfonts && height(b->font[j]) >= height(f); j++)
                ;
            for (k = b->nfonts; k > j; k--) {
                b->font[k] = b->font[k - 1];
                have[k] = have[k - 1];
            }
            b->font[j] = f;
            have[j] = px;
            b->nfonts++;
        }
        if (names)
            XFreeFontNames(names);
    }
    if (b->nfonts == 0 && (b->font[0] = XLoadQueryFont(b->dpy, "fixed")) != 0)
        b->nfonts = 1;
}

/* ------------------------------------------------------------ colours */

/*
 * The original game's colours (Cirulli's style/main.css): the page, the
 * board, an empty cell, the two text colours, and each tile from 2 to 2048,
 * then one for all larger ("super") tiles, as the original has.
 */
static const char *tilename[X89_TILES] = {
    "#eee4da", "#ede0c8", "#f2b179", "#f59563", "#f67c5f", "#f65e3b",
    "#edcf72", "#edcc61", "#edc850", "#edc53f", "#edc22e", "#3c3a32"
};
static const char *uiname[X89_NAMED] = {
    "#faf8ef", "#bbada0", "#cdc1b4", "#776e65", "#f9f6f2"
};

/* Allocate every colour, or none: if one fails (a full 8-bit colormap),
   free the rest and play in black and white. */
static int allocate(struct x89_board *b, int screen)
{
    Colormap map = DefaultColormap(b->dpy, screen);
    Visual *v = DefaultVisual(b->dpy, screen);
    unsigned long got[X89_NAMED + X89_TILES];
    XColor c;
    int i, n = 0;

    if (DefaultDepth(b->dpy, screen) < 4 || v->class == StaticGray || v->class == GrayScale)
        return 0;
    for (i = 0; i < X89_NAMED + X89_TILES; i++) {
        if (!XParseColor(b->dpy, map, (char *) (i < X89_NAMED ? uiname[i]
                                                : tilename[i - X89_NAMED]), &c)
            || !XAllocColor(b->dpy, map, &c)) {
            if (n > 0)
                XFreeColors(b->dpy, map, got, n, 0);
            return 0;
        }
        got[n++] = c.pixel;
    }
    for (i = 0; i < X89_NAMED; i++)
        b->ui[i] = got[i];
    for (i = 0; i < X89_TILES; i++) {
        b->tile[i] = got[X89_NAMED + i];
        b->text[i] = b->ui[i < 2 ? X89_DARK : X89_LIGHT];   /* dark on 2 and 4 */
    }
    return 1;
}

int x89_open(struct x89_board *b, Display *dpy, int screen, Drawable d)
{
    static char bits[] = { 0x01, 0x02 };

    b->dpy = dpy;
    b->black = BlackPixel(dpy, screen);
    b->white = WhitePixel(dpy, screen);
    loadfonts(b);
    if (b->nfonts == 0)
        return -1;
    b->hascolour = b->colour = allocate(b, screen);
    b->gc = XCreateGC(dpy, d, 0, (XGCValues *) 0);
    b->grey = XCreateBitmapFromData(dpy, d, bits, 2, 2);
    b->x = b->y = b->side = 0;
    b->cell = b->gap = 1;
    b->bevel = b->tight = 0;
    return 0;
}

/* ------------------------------------------------------------ drawing */

void x89_place(struct x89_board *b, int x, int y, int w, int h, int size)
{
    int side = w < h ? w : h, k = b->tight ? 16 : 8;

    /* size cells and size + 1 gaps, a gap a kth of a cell */
    b->cell = side * k / ((k + 1) * size + 1);
    if (b->cell < 1)
        b->cell = 1;
    b->gap = b->cell / k > 0 ? b->cell / k : 1;
    b->side = size * b->cell + (size + 1) * b->gap;
    b->x = x + (w - b->side) / 2;
    b->y = y + (h - b->side) / 2;
}

static void box(struct x89_board *b, Drawable d, int x, int y, int w, int h,
                unsigned long pix)
{
    XSetForeground(b->dpy, b->gc, pix);
    XFillRectangle(b->dpy, d, b->gc, x, y, (unsigned int) w, (unsigned int) h);
}

/*
 * A Motif shadow around a rectangle, t pixels thick: light (ts) on the top
 * and left and dark (bs) on the bottom and right for a raised one, the other
 * way round for a sunken one, meeting on the diagonal at the corners.
 */
static void shadow(struct x89_board *b, Drawable d, int x, int y, int w, int h, int t,
                   unsigned long ts, unsigned long bs)
{
    XPoint p[6];

    p[0].x = (short) x;           p[0].y = (short) y;
    p[1].x = (short) (x + w);     p[1].y = (short) y;
    p[2].x = (short) (x + w - t); p[2].y = (short) (y + t);
    p[3].x = (short) (x + t);     p[3].y = (short) (y + t);
    p[4].x = (short) (x + t);     p[4].y = (short) (y + h - t);
    p[5].x = (short) x;           p[5].y = (short) (y + h);
    XSetForeground(b->dpy, b->gc, ts);
    XFillPolygon(b->dpy, d, b->gc, p, 6, Nonconvex, CoordModeOrigin);
    p[0].x = (short) (x + w);     p[0].y = (short) (y + h);
    p[1].x = (short) x;           p[1].y = (short) (y + h);
    p[2].x = (short) (x + t);     p[2].y = (short) (y + h - t);
    p[3].x = (short) (x + w - t); p[3].y = (short) (y + h - t);
    p[4].x = (short) (x + w - t); p[4].y = (short) (y + t);
    p[5].x = (short) (x + w);     p[5].y = (short) y;
    XSetForeground(b->dpy, b->gc, bs);
    XFillPolygon(b->dpy, d, b->gc, p, 6, Nonconvex, CoordModeOrigin);
}

/* The tallest tile font whose text fits the cell, trying 2^n if the number
   doesn't fit any; the smallest if nothing does. */
static XFontStruct *fit(struct x89_board *b, char *s, int exp)
{
    int i, pass, cell = b->cell, room = cell - cell / 6;

    for (pass = 0; pass < 2; pass++) {
        for (i = 0; i < b->nfonts; i++)
            if (height(b->font[i]) <= cell * 55 / 100
                && XTextWidth(b->font[i], s, (int) strlen(s)) <= room)
                return b->font[i];
        sprintf(s, "2^%d", exp);
    }
    return b->font[b->nfonts - 1];
}

/*
 * One tile.  In colour, the original's (or whatever the frontend set),
 * raised with a bevel if asked.  In mono, as the curses frontend's mono
 * style: 2 and 4 outlined, 8 to 64 outlined heavily, 128 to 1024 in
 * reverse, 2048 and up in reverse with a white frame.
 */
static void tile(struct x89_board *b, Drawable d, int r, int c, int exp)
{
    int t = exp > X89_TILES ? X89_TILES - 1 : exp - 1, cell = b->cell;
    int x = b->x + b->gap + c * (cell + b->gap), y = b->y + b->gap + r * (cell + b->gap);
    int thin = cell / 40 > 1 ? cell / 40 : 1, thick = cell / 14 > 2 ? cell / 14 : 2, w;
    unsigned long ink;
    XFontStruct *f;
    char s[16];
    int bevel = b->colour && b->bevel ? (cell >= 60 ? 3 : cell >= 20 ? 2 : 1) : 0;

    if (exp == 0) {
        box(b, d, x, y, cell, cell, b->colour ? b->ui[X89_EMPTY] : b->white);
        if (bevel)
            shadow(b, d, x, y, cell, cell, bevel, b->emptybs, b->emptyts);
        return;
    }
    if (b->colour) {
        box(b, d, x, y, cell, cell, b->tile[t]);
        if (bevel)
            shadow(b, d, x, y, cell, cell, bevel, b->tilets[t], b->tilebs[t]);
        ink = b->text[t];
    } else if (exp <= 6) {
        box(b, d, x, y, cell, cell, b->black);
        w = exp <= 2 ? thin : thick;
        box(b, d, x + w, y + w, cell - 2 * w, cell - 2 * w, b->white);
        ink = b->black;
    } else {
        box(b, d, x, y, cell, cell, b->black);
        if (exp > 10) {
            box(b, d, x + thin, y + thin, cell - 2 * thin, cell - 2 * thin, b->white);
            box(b, d, x + 2 * thin, y + 2 * thin, cell - 4 * thin, cell - 4 * thin, b->black);
        }
        ink = b->white;
    }
    f89_tile(s, exp);
    f = fit(b, s, exp);
    w = XTextWidth(f, s, (int) strlen(s));
    XSetFont(b->dpy, b->gc, f->fid);
    XSetForeground(b->dpy, b->gc, ink);
    XDrawString(b->dpy, d, b->gc, x + (cell - w) / 2,
                y + (cell - height(f)) / 2 + f->ascent, s, (int) strlen(s));
}

void x89_draw(struct x89_board *b, Drawable d, const struct t89_game *g)
{
    int r, c;

    if (b->colour)
        box(b, d, b->x, b->y, b->side, b->side, b->ui[X89_BOARD]);
    else {
        XSetFillStyle(b->dpy, b->gc, FillOpaqueStippled);
        XSetStipple(b->dpy, b->gc, b->grey);
        XSetBackground(b->dpy, b->gc, b->white);
        box(b, d, b->x, b->y, b->side, b->side, b->black);
        XSetFillStyle(b->dpy, b->gc, FillSolid);
    }
    for (r = 0; r < g->size; r++)
        for (c = 0; c < g->size; c++)
            tile(b, d, r, c, g->cell[r * g->size + c]);
}

/* ------------------------------------------------------------ keys */

int x89_key(XKeyEvent *e)
{
    char s[8];
    KeySym ks;
    int n = XLookupString(e, s, (int) sizeof s, &ks, (XComposeStatus *) 0);

    switch (ks) {
    case XK_Up:    return F89_UP;
    case XK_Down:  return F89_DOWN;
    case XK_Left:  return F89_LEFT;
    case XK_Right: return F89_RIGHT;
#ifdef XK_KP_Up
    case XK_KP_Up:    return F89_UP;
    case XK_KP_Down:  return F89_DOWN;
    case XK_KP_Left:  return F89_LEFT;
    case XK_KP_Right: return F89_RIGHT;
#endif
    default:
        return n == 1 ? f89_key((unsigned char) s[0]) : 0;
    }
}
