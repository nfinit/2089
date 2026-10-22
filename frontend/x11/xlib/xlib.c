/*
 * xlib.c - 2089 for the X Window System, with nothing but Xlib (x2089).
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
 * Everything is drawn into a pixmap the size of the window and copied to
 * the window on Expose, so it never flickers and the server keeps nothing
 * for us.  The window can be resized: the board (drawn by xboard.c, which a
 * toolkit frontends share) follows, with tile text in the largest bitmap
 * Helvetica Bold the server has that fits (from the font list, not by
 * scaling, which bitmap servers do badly).
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
#include <X11/keysym.h>                 /* IsModifierKey, in older Xutil.h */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "xboard.h"

#define CLASS "X2089"

static Display *dpy;
static int screen;
static Window win;
static Pixmap buf;                      /* what's in the window */
static struct x89_board brd;
static Atom wmdelete;
static unsigned int width, height;      /* the window's size */
static const char *name;                /* the program, for resources */

static struct t89_game game;
static char command[64];                /* how to restart: program and options */
static char msg[T89_SAVELEN + 64];      /* the message under the board */
static int undook, canundo, over;
static int asking;                      /* 1 while "new game?" waits for y/n */
static int helping;                     /* 1 while the help is up */

/* ------------------------------------------------------------ text */

static XFontStruct *uifont;             /* score, messages, help */

static int fontheight(XFontStruct *f)
{
    return f->ascent + f->descent;
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

/* The colour of text, and of the page around the board. */
static unsigned long ink(void)
{
    return brd.colour ? brd.ui[X89_DARK] : brd.black;
}

static unsigned long paper(void)
{
    return brd.colour ? brd.ui[X89_PAGE] : brd.white;
}

/* ------------------------------------------------------------ layout */

static int pad, below;

/* Fit the board to the window: the score above, a message and the keys
   below, all centred. */
static void layout(void)
{
    int lh = fontheight(uifont), text;

    pad = (int) (width < height ? width : height) / 40;
    if (pad < 4)
        pad = 4;
    text = lh + pad + pad + 2 * lh + 2;
    x89_place(&brd, pad, pad + lh + pad, (int) width - 2 * pad,
              (int) height - 2 * pad - text, game.size);
    below = brd.y + brd.side + pad;
}

/* The window size for a board: about 400 pixels across at any size. */
static void preferred(unsigned int *w, unsigned int *h)
{
    int lh = fontheight(uifont);

    *w = 400;
    *h = 400 + (unsigned int) (3 * lh + 2 + 2 * 10);
}

/* ------------------------------------------------------------ drawing */

static void text(int x, int y, const char *s)
{
    XSetFont(dpy, brd.gc, uifont->fid);
    XSetForeground(dpy, brd.gc, ink());
    XDrawString(dpy, buf, brd.gc, x, y + uifont->ascent, s, (int) strlen(s));
}

/* A line of text centred across the window, with its top at y. */
static void centred(int y, const char *s)
{
    text(((int) width - XTextWidth(uifont, s, (int) strlen(s))) / 2, y, s);
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
    int i, lh = fontheight(uifont) + 2, x = brd.x + brd.gap, y = brd.y + brd.gap;

    XSetForeground(dpy, brd.gc, paper());
    XFillRectangle(dpy, buf, brd.gc, brd.x, brd.y, (unsigned int) brd.side,
                   (unsigned int) brd.side);
    for (i = 0; lines[i]; i++, y += lh)
        if (lines[i][0])
            text(x, y, lines[i]);
    if (brd.hascolour) {
        text(x, y, "c  colour or black and white");
        y += lh;
    }
    text(x, y, undook ? "u  undo the last move" : "(undo is off: start with -u)");
    y += 2 * lh;
    text(x, y, "Press any key to go back.");
}

/* Draw everything into the pixmap, then copy it to the window. */
static void draw(void)
{
    char s[96];
    int lh = fontheight(uifont);

    XSetForeground(dpy, brd.gc, paper());
    XFillRectangle(dpy, buf, brd.gc, 0, 0, width, height);
    f89_bnote(&game);
    sprintf(s, "2089    score %lu    best %lu    moves %lu", game.score,
            f89_btop(game.size), game.moves);
    if (XTextWidth(uifont, s, (int) strlen(s)) > (int) width)
        sprintf(s, "score %lu  best %lu", game.score, f89_btop(game.size));
    centred(brd.y - pad - lh, s);
    if (helping)
        help();
    else
        x89_draw(&brd, buf, &game);
    centred(below, msg);
    sprintf(s, "arrows move  %sn new  ? help  q quit", undook ? "u undo  " : "");
    if (XTextWidth(uifont, s, (int) strlen(s)) > (int) width)
        strcpy(s, "? help  q quit");
    centred(below + lh + 2, s);
    XCopyArea(dpy, buf, win, brd.gc, 0, 0, width, height, 0, 0);
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
    cmd = x89_key(e);
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
        brd.colour = brd.hascolour && !brd.colour;
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

    a.background_pixel = paper();
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
            printf("2089 %s (Xlib)\n", T89_VERSION);
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
    if (!geometry)
        geometry = resource("geometry");
    if (!fontname)
        fontname = resource("font");
    if (!mono && (res = resource("mono")) != 0)
        mono = strcmp(res, "true") == 0 || strcmp(res, "on") == 0 || strcmp(res, "yes") == 0;
    if (x89_open(&brd, dpy, screen, RootWindow(dpy, screen)) < 0
        || loaduifont(fontname) < 0) {
        fprintf(stderr, "%s: the server has no fonts I can use (not even fixed)\n", name);
        return EXIT_FAILURE;
    }
    x89_extend(&brd, 1, brd.tile);     /* past 2048, as many as there's room for */
    brd.colour = brd.hascolour && !mono;
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
            XCopyArea(dpy, buf, win, brd.gc, e.xexpose.x, e.xexpose.y,
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
