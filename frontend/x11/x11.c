/*
 * x11.c - 2089 for the X Window System, with nothing but Xlib.
 *
 * The kind of X game that came with a workstation or a BSD's X packages:
 * one window, drawn by hand, needing no toolkit (Xt, Athena or Motif), just
 * -lX11, which every X11 system has.  AIX shipped Motif and no Athena, for
 * one.
 *
 * Options, as X programs take them: -display, -geometry, -fn (the font for
 * the score and messages) and -mono, plus 2089's own -u, -s, -r and -l.
 * Resources: x2089.geometry, x2089.font and x2089.mono (true) in
 * ~/.Xdefaults or xrdb, under the program's name.
 *
 * The board is drawn into a pixmap the size of the window and copied to the
 * window on Expose, so it never flickers and the server keeps nothing for
 * us.  The window can be resized: cells and tile fonts follow, picking the
 * largest bitmap Helvetica Bold the server has that fits (from the font
 * list, not by scaling, which bitmap servers do badly).
 *
 * Colour where the default visual has it and the colours can be allocated;
 * black and white otherwise (a monochrome or greyscale display, or an 8-bit
 * colormap that's full), with tiles from outline to reverse video as they
 * grow, as the curses frontend's mono style.  c switches between the two.
 *
 * The rules are the curses frontend's: every key is a move, undo is off
 * unless -u, n asks first, quitting prints the command that carries on the
 * game, and the best score for each board size is kept in ~/.2089 (shared
 * with curses).
 *
 * Author: Claude Opus 5.5 (claude-opus-5-5), October 2026.
 * MIT License; see LICENSE.
 */

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"

#define CLASS "X2089"

static Display *dpy;
static int screen;
static Window win;
static Pixmap buf;                      /* what's in the window */
static GC gc;
static Atom wmdelete;
static unsigned int width, height;      /* the window's size */
static const char *name;                /* the program, for resources */

static struct t89_game game;
static char command[64];                /* how to restart: program and options */
static char msg[T89_SAVELEN + 64];      /* the message under the board */
static int undook, canundo, over;
static int asking;                      /* 1 while "new game?" waits for y/n */
static int helping;                     /* 1 while the help is up */

/* ------------------------------------------------------------ fonts */

#define NFONTS 16
static XFontStruct *font[NFONTS];       /* tile fonts, tallest first */
static int nfonts;
static XFontStruct *uifont;             /* score, messages, help */

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

static int fontheight(XFontStruct *f)
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
static void loadfonts(void)
{
    static const char *pattern[] = {
        "-*-helvetica-bold-r-normal--*-*-*-*-p-*-iso8859-1",
        "-*-fixed-bold-r-normal--*-*-*-*-c-*-iso8859-1",
        0
    };
    char **names;
    int n, i, j, k, p, px, have[NFONTS];
    XFontStruct *f;

    for (p = 0; pattern[p] && nfonts == 0; p++) {
        names = XListFonts(dpy, (char *) pattern[p], 200, &n);
        for (i = 0; names && i < n && nfonts < NFONTS; i++) {
            px = pixels(names[i]);
            for (j = 0; j < nfonts && have[j] != px; j++)
                ;
            if (px <= 0 || j < nfonts || (f = XLoadQueryFont(dpy, names[i])) == 0)
                continue;
            for (j = 0; j < nfonts && fontheight(font[j]) >= fontheight(f); j++)
                ;
            for (k = nfonts; k > j; k--) {
                font[k] = font[k - 1];
                have[k] = have[k - 1];
            }
            font[j] = f;
            have[j] = px;
            nfonts++;
        }
        if (names)
            XFreeFontNames(names);
    }
    if (nfonts == 0 && (font[0] = XLoadQueryFont(dpy, "fixed")) != 0)
        nfonts = 1;
}

/* The font for the score and messages: asked for, else a 12 or 14 point
   Helvetica Bold, else "fixed". */
static int loaduifont(const char *want)
{
    static const char *tries[] = {
        "-*-helvetica-bold-r-normal--14-*-*-*-p-*-iso8859-1",
        "-*-helvetica-bold-r-normal--12-*-*-*-p-*-iso8859-1",
        "fixed", 0
    };
    int i;

    if (want && (uifont = XLoadQueryFont(dpy, want)) != 0)
        return 0;
    if (want)
        fprintf(stderr, "%s: no font %s; using the usual one\n", name, want);
    for (i = 0; tries[i] && uifont == 0; i++)
        uifont = XLoadQueryFont(dpy, tries[i]);
    return uifont ? 0 : -1;
}

/* ------------------------------------------------------------ colours */

/*
 * The original game's colours (Cirulli's style/main.css): the page, the
 * board, an empty cell, the two text colours, and each tile from 2 to 2048,
 * then one for all larger ("super") tiles, as the original has.
 */
#define TILES 12
static const char *tilename[TILES] = {
    "#eee4da", "#ede0c8", "#f2b179", "#f59563", "#f67c5f", "#f65e3b",
    "#edcf72", "#edcc61", "#edc850", "#edc53f", "#edc22e", "#3c3a32"
};
enum { PAGE, BOARD, EMPTY, DARK, LIGHT, NAMED };
static const char *uiname[NAMED] = {
    "#faf8ef", "#bbada0", "#cdc1b4", "#776e65", "#f9f6f2"
};

static int colour, hascolour;           /* in use; possible */
static unsigned long ui[NAMED], tilepix[TILES];
static unsigned long black, white;
static Pixmap grey;                     /* a 50% stipple, for mono */

/* Allocate every colour, or none: if one fails (a full 8-bit colormap),
   free the rest and play in black and white. */
static int allocate(void)
{
    Colormap map = DefaultColormap(dpy, screen);
    Visual *v = DefaultVisual(dpy, screen);
    unsigned long got[NAMED + TILES];
    XColor c;
    int i, n = 0;

    if (DefaultDepth(dpy, screen) < 4 || v->class == StaticGray || v->class == GrayScale)
        return 0;
    for (i = 0; i < NAMED + TILES; i++) {
        if (!XParseColor(dpy, map, i < NAMED ? uiname[i] : tilename[i - NAMED], &c)
            || !XAllocColor(dpy, map, &c)) {
            if (n > 0)
                XFreeColors(dpy, map, got, n, 0);
            return 0;
        }
        got[n++] = c.pixel;
    }
    for (i = 0; i < NAMED; i++)
        ui[i] = got[i];
    for (i = 0; i < TILES; i++)
        tilepix[i] = got[NAMED + i];
    return 1;
}

/* ------------------------------------------------------------ layout */

static int pad, top, left, cell, gap, side, below;

/* Fit the board to the window: the score above, a message and the keys
   below, all centred. */
static void layout(void)
{
    int n = game.size, lh = fontheight(uifont), w, h, text;

    pad = (int) (width < height ? width : height) / 40;
    if (pad < 4)
        pad = 4;
    text = lh + pad + pad + 2 * lh + 2;
    w = (int) width - 2 * pad;
    h = (int) height - 2 * pad - text;
    side = w < h ? w : h;
    /* n cells and n + 1 gaps, a gap an eighth of a cell */
    cell = side * 8 / (9 * n + 1);
    if (cell < 1)
        cell = 1;
    gap = cell / 8 > 0 ? cell / 8 : 1;
    side = n * cell + (n + 1) * gap;
    left = ((int) width - side) / 2;
    top = ((int) height - side - text) / 2 + lh + pad;
    below = top + side + pad;
}

/* The window size for a board: about 400 pixels across at any size. */
static void preferred(unsigned int *w, unsigned int *h)
{
    int lh = fontheight(uifont);

    *w = 400;
    *h = 400 + (unsigned int) (3 * lh + 2 + 2 * 10);
}

/* ------------------------------------------------------------ drawing */

static void text(XFontStruct *f, int x, int y, unsigned long pix, const char *s)
{
    XSetFont(dpy, gc, f->fid);
    XSetForeground(dpy, gc, pix);
    XDrawString(dpy, buf, gc, x, y, s, (int) strlen(s));
}

/* A line of text centred across the window, with its top at y. */
static void centred(int y, const char *s)
{
    int w = XTextWidth(uifont, s, (int) strlen(s));

    text(uifont, ((int) width - w) / 2, y + uifont->ascent,
         colour ? ui[DARK] : black, s);
}

static void box(int x, int y, int w, int h, unsigned long pix)
{
    XSetForeground(dpy, gc, pix);
    XFillRectangle(dpy, buf, gc, x, y, (unsigned int) w, (unsigned int) h);
}

/* The tallest tile font whose text fits the cell, trying 2^n if the number
   doesn't fit any; the smallest if nothing does. */
static XFontStruct *fit(char *s, int exp)
{
    int i, pass, room = cell - cell / 6;

    for (pass = 0; pass < 2; pass++) {
        for (i = 0; i < nfonts; i++)
            if (fontheight(font[i]) <= cell * 55 / 100
                && XTextWidth(font[i], s, (int) strlen(s)) <= room)
                return font[i];
        sprintf(s, "2^%d", exp);
    }
    return font[nfonts - 1];
}

/*
 * One tile.  In colour, the original's.  In mono, as the curses frontend's
 * mono style: 2 and 4 outlined, 8 to 64 outlined heavily, 128 to 1024 in
 * reverse, 2048 and up in reverse with a white frame.
 */
static void tile(int r, int c)
{
    int exp = game.cell[r * game.size + c], t = exp > TILES ? TILES - 1 : exp - 1;
    int x = left + gap + c * (cell + gap), y = top + gap + r * (cell + gap);
    int thin = cell / 40 > 1 ? cell / 40 : 1, thick = cell / 14 > 2 ? cell / 14 : 2, w;
    unsigned long ink;
    XFontStruct *f;
    char s[16];

    if (exp == 0) {
        box(x, y, cell, cell, colour ? ui[EMPTY] : white);
        return;
    }
    if (colour) {
        box(x, y, cell, cell, tilepix[t]);
        ink = exp <= 2 ? ui[DARK] : ui[LIGHT];
    } else if (exp <= 6) {
        box(x, y, cell, cell, black);
        w = exp <= 2 ? thin : thick;
        box(x + w, y + w, cell - 2 * w, cell - 2 * w, white);
        ink = black;
    } else {
        box(x, y, cell, cell, black);
        if (exp > 10) {
            box(x + thin, y + thin, cell - 2 * thin, cell - 2 * thin, white);
            box(x + 2 * thin, y + 2 * thin, cell - 4 * thin, cell - 4 * thin, black);
        }
        ink = white;
    }
    f89_tile(s, exp);
    f = fit(s, exp);
    w = XTextWidth(f, s, (int) strlen(s));
    text(f, x + (cell - w) / 2, y + (cell - fontheight(f)) / 2 + f->ascent, ink, s);
}

static void help(void)
{
    static const char *lines[] = {
        "Slide the tiles; two equal tiles",
        "that meet join into one.  Reach",
        "2048 to win (you can keep going),",
        "until no move is left.",
        "",
        "arrows, w a s d, k h j l, 8 4 2 6:  move",
        "n  new game (asks first)",
        "p  show the save string",
        "q  quit, printing the command",
        "    that carries on this game",
        "",
        0
    };
    int i, lh = fontheight(uifont) + 2, y = top + gap;

    box(left, top, side, side, colour ? ui[PAGE] : white);
    for (i = 0; lines[i]; i++, y += lh)
        if (lines[i][0])
            text(uifont, left + gap, y + uifont->ascent, colour ? ui[DARK] : black,
                 lines[i]);
    if (hascolour) {
        text(uifont, left + gap, y + uifont->ascent, colour ? ui[DARK] : black,
             "c  colour or black and white");
        y += lh;
    }
    text(uifont, left + gap, y + uifont->ascent, colour ? ui[DARK] : black,
         undook ? "u  undo the last move" : "(undo is off: start with -u)");
    y += 2 * lh;
    text(uifont, left + gap, y + uifont->ascent, colour ? ui[DARK] : black,
         "Press any key to go back.");
}

/* Draw everything into the pixmap, then copy it to the window. */
static void draw(void)
{
    char s[96];
    int r, c, lh = fontheight(uifont);

    box(0, 0, (int) width, (int) height, colour ? ui[PAGE] : white);
    f89_bnote(&game);
    sprintf(s, "2089    score %lu    best %lu    moves %lu", game.score,
            f89_btop(game.size), game.moves);
    if (XTextWidth(uifont, s, (int) strlen(s)) > (int) width)
        sprintf(s, "score %lu  best %lu", game.score, f89_btop(game.size));
    centred(top - pad - lh, s);
    if (helping)
        help();
    else {
        if (colour)
            box(left, top, side, side, ui[BOARD]);
        else {
            XSetFillStyle(dpy, gc, FillOpaqueStippled);
            XSetStipple(dpy, gc, grey);
            XSetBackground(dpy, gc, white);
            box(left, top, side, side, black);
            XSetFillStyle(dpy, gc, FillSolid);
        }
        for (r = 0; r < game.size; r++)
            for (c = 0; c < game.size; c++)
                tile(r, c);
    }
    centred(below, msg);
    sprintf(s, "arrows move  %sn new  ? help  q quit", undook ? "u undo  " : "");
    if (XTextWidth(uifont, s, (int) strlen(s)) > (int) width)
        strcpy(s, "? help  q quit");
    centred(below + lh + 2, s);
    XCopyArea(dpy, buf, win, gc, 0, 0, width, height, 0, 0);
    XFlush(dpy);
}

/* A new pixmap for a new window size. */
static void resize(unsigned int w, unsigned int h)
{
    if (buf && w == width && h == height)
        return;
    if (buf)
        XFreePixmap(dpy, buf);
    width = w;
    height = h;
    buf = XCreatePixmap(dpy, win, width, height, (unsigned int) DefaultDepth(dpy, screen));
    layout();
}

static void say(const char *s)
{
    strcpy(msg, s);
}

static void nomoves(void)
{
    say(undook ? "No moves left.  u undo, n new game, q quit."
               : "No moves left.  n new game, q quit.");
}

/* ------------------------------------------------------------ leaving */

static void finish(void)
{
    if (f89_bsave() < 0)
        fprintf(stderr, "%s: can't save the high score in %s\n", name, f89_bfile());
    f89_resume(&game, command);
}

/* The server went away (or the window was killed): save what can be. */
static int lost(Display *d)
{
    (void) d;
    finish();
    exit(EXIT_FAILURE);
    return 0;
}

/* ^C in the terminal that started it, and the like. */
static void caught(int sig)
{
    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);
#ifdef SIGHUP
    signal(SIGHUP, SIG_IGN);
#endif
    (void) sig;
    finish();
    exit(EXIT_FAILURE);
}

/* ------------------------------------------------------------ playing */

/* A key as a command. */
static int key(XKeyEvent *e)
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

/* What a key press does; returns 1 to quit. */
static int press(XKeyEvent *e)
{
    struct t89_game before;
    struct t89_result r;
    static struct t89_game undo;
    char s[8];
    KeySym ks;
    int cmd, n;

    if (IsModifierKey(XLookupKeysym(e, 0)))
        return 0;                       /* Shift on its own isn't a key */
    if (helping) {
        helping = 0;
        return 0;
    }
    if (asking) {
        n = XLookupString(e, s, (int) sizeof s, &ks, (XComposeStatus *) 0);
        asking = 0;
        say("");
        if (n == 1 && (s[0] == 'y' || s[0] == 'Y')) {
            f89_bsave();
            t89_new(&game, game.size, f89_seed(game.rng));
            canundo = over = 0;
        }
        return 0;
    }
    cmd = key(e);
    if (cmd != 0)
        say("");
    switch (cmd) {
    case F89_QUIT:
        return 1;
    case F89_NEW:
        asking = 1;
        say("Start a new game?  (y/n)");
        break;
    case F89_UNDO:
        if (!undook)
            say("Undo is off: start with -u to allow it.");
        else if (canundo) {
            game = undo;
            canundo = over = 0;
        } else
            say("Nothing to undo.");
        break;
    case F89_SAVE:
        strcpy(msg, "Save string: ");
        t89_save(&game, msg + strlen(msg), T89_SAVELEN);
        printf("%s\n", msg);            /* where it can be copied */
        fflush(stdout);
        break;
    case F89_HELP:
        helping = 1;
        break;
    case F89_DISPLAY:
        colour = hascolour && !colour;
        break;
    case 0:
        break;
    default:
        before = game;
        if (t89_move(&game, f89_dir(cmd), &r)) {
            undo = before;
            canundo = 1;
            if (r.events & T89_EV_OVER) {
                over = 1;
                f89_bsave();
            }
            if (r.events & T89_EV_WON) {
                f89_tile(msg, game.goal);
                strcat(msg, !over ? "!  You win.  Keep going if you like."
                       : "!  You win, and no moves are left.");
            } else if (over)
                nomoves();
        } else if (over)
            nomoves();
        break;
    }
    return 0;
}

/* ------------------------------------------------------------ main */

static void usage(int status)
{
    printf("usage: %s [-display host:n] [-geometry WxH+X+Y] [-fn font] [-mono]\n", name);
    puts("             [-u] [-s size] [-r seed] [-l savestring] [-v] [-h]");
    puts("  -display host:n  the X display to use (default $DISPLAY)");
    puts("  -geometry WxH+X+Y  the window's size and place");
    puts("  -fn font       the font for the score and messages");
    puts("  -mono          black and white, even on a colour display");
    f89_usage();
    puts("  -v             print the version");
    exit(status);
}

/* A resource, as name.resource (x2089.font: ...), or 0. */
static char *resource(const char *res)
{
    return XGetDefault(dpy, (char *) name, (char *) res);
}

static void makewindow(int argc, char **argv, const char *geometry)
{
    XSizeHints *size = XAllocSizeHints();
    XWMHints *wm = XAllocWMHints();
    XClassHint *cls = XAllocClassHint();
    XTextProperty title;
    XSetWindowAttributes a;
    char *t = (char *) "2089", def[32];
    unsigned int pw, ph;
    int x, y, w, h, gravity;
    static char bits[] = { 0x01, 0x02 };

    if (!size || !wm || !cls) {
        fprintf(stderr, "%s: out of memory\n", name);
        exit(EXIT_FAILURE);
    }
    preferred(&pw, &ph);
    sprintf(def, "%ux%u+0+0", pw, ph);
    /* Smallest: cells of 16 pixels. */
    size->flags = PMinSize | PBaseSize;    /* base 0: sizes are pixels */
    size->base_width = size->base_height = 0;
    size->min_width = (9 * game.size + 1) * 2 + 2 * 4;
    size->min_height = size->min_width + 3 * fontheight(uifont) + 4 * 4;
    XWMGeometry(dpy, screen, (char *) geometry, def, 0, size, &x, &y,
                &w, &h, &gravity);
    size->flags |= PWinGravity | PSize | PPosition;
    if (geometry)
        size->flags |= USSize | USPosition;
    size->x = x;
    size->y = y;
    size->width = w;
    size->height = h;
    size->win_gravity = gravity;

    a.background_pixel = colour ? ui[PAGE] : white;
    a.event_mask = ExposureMask | KeyPressMask | StructureNotifyMask;
    a.bit_gravity = NorthWestGravity;
    win = XCreateWindow(dpy, RootWindow(dpy, screen), x, y, (unsigned int) w,
                        (unsigned int) h, 0, CopyFromParent,
                        InputOutput, CopyFromParent,
                        CWBackPixel | CWEventMask | CWBitGravity, &a);

    wm->flags = InputHint | StateHint;
    wm->input = True;
    wm->initial_state = NormalState;
    cls->res_name = (char *) name;
    cls->res_class = (char *) CLASS;
    if (!XStringListToTextProperty(&t, 1, &title)) {
        fprintf(stderr, "%s: out of memory\n", name);
        exit(EXIT_FAILURE);
    }
    XSetWMProperties(dpy, win, &title, &title, argv, argc, size, wm, cls);
    XFree((char *) title.value);
    XFree((char *) size);
    XFree((char *) wm);
    XFree((char *) cls);
    wmdelete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, win, &wmdelete, 1);

    gc = XCreateGC(dpy, win, 0, (XGCValues *) 0);
    grey = XCreateBitmapFromData(dpy, win, bits, 2, 2);
    resize((unsigned int) w, (unsigned int) h);
}

int main(int argc, char **argv)
{
    struct f89_options o;
    const char *display = 0, *geometry = 0, *fontname = 0;
    char *res;
    int i, mono = 0, quit = 0;
    XEvent e;

    name = strrchr(argv[0], '/') ? strrchr(argv[0], '/') + 1 : argv[0];
    f89_defaults(&o);
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0)
            usage(EXIT_SUCCESS);
        if (strcmp(argv[i], "-v") == 0) {
            printf("2089 %s (X11)\n", T89_VERSION);
            return EXIT_SUCCESS;
        }
        if (strcmp(argv[i], "-mono") == 0) {
            mono = 1;
            continue;
        }
        if (i + 1 < argc && strcmp(argv[i], "-display") == 0) {
            display = argv[++i];
            continue;
        }
        if (i + 1 < argc && strcmp(argv[i], "-geometry") == 0) {
            geometry = argv[++i];
            continue;
        }
        if (i + 1 < argc && (strcmp(argv[i], "-fn") == 0 || strcmp(argv[i], "-font") == 0)) {
            fontname = argv[++i];
            continue;
        }
        if (f89_option(argc, argv, &i, &o) <= 0)
            usage(EXIT_FAILURE);
    }
    if (f89_start(&game, &o, name) < 0)
        return EXIT_FAILURE;
    undook = o.undo;

    if ((dpy = XOpenDisplay((char *) display)) == 0) {
        fprintf(stderr, "%s: can't open display %s\n", name, XDisplayName((char *) display));
        return EXIT_FAILURE;
    }
    screen = DefaultScreen(dpy);
    black = BlackPixel(dpy, screen);
    white = WhitePixel(dpy, screen);
    if (!geometry)
        geometry = resource("geometry");
    if (!fontname)
        fontname = resource("font");
    if (!mono && (res = resource("mono")) != 0)
        mono = strcmp(res, "true") == 0 || strcmp(res, "on") == 0 || strcmp(res, "yes") == 0;
    loadfonts();
    if (nfonts == 0 || loaduifont(fontname) < 0) {
        fprintf(stderr, "%s: the server has no fonts I can use (not even fixed)\n", name);
        return EXIT_FAILURE;
    }
    hascolour = allocate();
    colour = hascolour && !mono;
    sprintf(command, "%s %s%s", name, undook ? "-u " : "", mono ? "-mono " : "");

    makewindow(argc, argv, geometry);
    XSetIOErrorHandler(lost);
    signal(SIGINT, caught);
    signal(SIGTERM, caught);
#ifdef SIGHUP
    signal(SIGHUP, caught);
#endif
    f89_bopen();
    over = t89_over(&game);
    if (over)
        nomoves();
    XMapWindow(dpy, win);

    while (!quit) {
        XNextEvent(dpy, &e);
        switch (e.type) {
        case Expose:
            XCopyArea(dpy, buf, win, gc, e.xexpose.x, e.xexpose.y,
                      (unsigned int) e.xexpose.width, (unsigned int) e.xexpose.height,
                      e.xexpose.x, e.xexpose.y);
            break;
        case ConfigureNotify:
            if ((unsigned int) e.xconfigure.width != width
                || (unsigned int) e.xconfigure.height != height) {
                resize((unsigned int) e.xconfigure.width, (unsigned int) e.xconfigure.height);
                draw();
            }
            break;
        case MapNotify:
            draw();
            break;
        case KeyPress:
            quit = press(&e.xkey);
            if (!quit)
                draw();
            break;
        case MappingNotify:
            XRefreshKeyboardMapping(&e.xmapping);
            break;
        case ClientMessage:
            if ((Atom) e.xclient.data.l[0] == wmdelete)
                quit = 1;
            break;
        }
    }
    finish();
    XCloseDisplay(dpy);
    return EXIT_SUCCESS;
}
