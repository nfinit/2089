/*
 * motif.c - 2089 for the X Window System, with Motif (xm2089).
 *
 * The same game as x2089 (xlib/xlib.c), with the same board (../xboard.c),
 * as a Motif application of the CDE years, after the CDE and Motif style
 * guides and the desktop's own tools (dtcalc, dtpad):
 *
 *   - a menu bar: File (New Game, New Game from Number, Open, Save As,
 *     Save String, Exit), Edit (Undo), View (Arrow Buttons, Best Scores),
 *     Options (Board Size, Game Colours, Desktop Colours, Black and White,
 *     Extended Colours) and Help (Overview, Keys, Using Help, About);
 *   - the score, best score and moves in a row of sunken fields;
 *   - the board filling a sunken frame, its tiles raised with Motif bevels
 *     (the shadows Motif works out for each tile's colour) and empty cells
 *     sunk into it, the gaps between them tight, as Motif's controls are;
 *   - under it, New Game and Undo buttons, and arrow buttons to play with
 *     the mouse if View > Arrow Buttons is on (-arrows): clicking one in a
 *     window behind others brings it forward and moves at once;
 *   - a status line along the bottom.
 *
 * Three colour schemes: the game's tile colours (the default: the
 * original's, and past 2048 ours unless Extended Colours is off), the
 * desktop's own palette (under CDE: from the colour server, so the board
 * follows the palette chosen in Style Manager), and black and white, which
 * is all a monochrome display gets.  Widgets take the desktop's colours
 * and fonts in all three.
 *
 * The keys are the other frontends' (arrows, w a s d, h j k l, 8 4 2 6, u
 * n p c q ?), typed on the board.  Each new game has a number (its seed,
 * as 2089 -r takes), shown on the status line.  Undo is off unless -u (or
 * the resource undo: true).  Saved games are files holding a save string,
 * as 2089 -l takes.  Quitting prints the command that carries on the game, and the
 * best scores are kept in ~/.2089.
 *
 * Xt takes the usual X options (-display, -geometry, -xrm, ...); resources
 * are read the X way, for class XM2089.  Fallback resources compiled in
 * stand in for an app-defaults file.  Options > Save Options writes the
 * board size, colours and arrows to ~/.xm2089, as resources, read over the
 * user's others (~/.Xdefaults) but under the command line.
 *
 * On the CDE desktop (with make install-cde, files in motif/cde): an
 * action and icons, so 2089 is in Application Manager and saved games
 * open from File Manager; a help volume for Help > Overview; and the game
 * is saved with the session, to come back at the next login.
 *
 * Author: Claude Opus 5.5 (claude-opus-5-5), October 2026.
 * MIT License; see LICENSE.
 */

#include <Xm/Xm.h>
#include <Xm/ArrowB.h>
#include <Xm/CascadeB.h>
#include <Xm/ColorObjP.h>               /* XmeGetColorObjData: the desktop palette */
#include <Xm/DrawingA.h>
#include <Xm/FileSB.h>
#include <Xm/Form.h>
#include <Xm/Frame.h>
#include <Xm/Label.h>
#include <Xm/MainW.h>
#include <Xm/MessageB.h>
#include <Xm/Protocols.h>
#include <Xm/PushB.h>
#include <Xm/RowColumn.h>
#include <Xm/SelectioB.h>
#include <Xm/Separator.h>
#include <Xm/ToggleB.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>                 /* IsModifierKey, in older Xutil.h */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "xboard.h"

#define CLASS "XM2089"

static XtAppContext app;
static Widget top, board, message, score[3];
static Widget undoitem, undobutton, schemeitem[3], arrowsitem, pad;
static Widget menubar, scorelabel[3], work, controls, newbutton;
static Widget sizeitem[T89_MAXSIZE + 1], extitem;
static Widget asknew, showsave, opendialog, savedialog, keys, about, numberdialog;
static Widget scoresdialog, askclear;
static Widget scoretext[T89_MAXSIZE + 1][4];   /* [n][3]: its Clear button */
static Display *dpy;
static Pixmap buf;                      /* what's in the board's window */
static Dimension width, height;         /* its size */
static struct x89_board brd;
static const char *name;
static char *program;                   /* argv[0], to restart a session with */

static struct t89_game game, undo;
static char command[64];                /* how to restart: program and options */
static int canundo, over;
static int newsize;                     /* the size of the next new game */
static unsigned long number;            /* this game's number (its seed), */
static int known;                       /* if it was dealt here, not loaded */

/* Application resources, settable as options. */
struct settings {
    Boolean undo, mono, arrows, extended;
    String colors;                      /* game, desktop or mono */
    int size;                           /* of new games, unless -s */
};
static struct settings res;

static XtResource resources[] = {
    { (char *) "undo", (char *) "Undo", XmRBoolean, sizeof(Boolean),
      XtOffsetOf(struct settings, undo), XmRImmediate, (XtPointer) False },
    { (char *) "mono", (char *) "Mono", XmRBoolean, sizeof(Boolean),
      XtOffsetOf(struct settings, mono), XmRImmediate, (XtPointer) False },
    { (char *) "arrows", (char *) "Arrows", XmRBoolean, sizeof(Boolean),
      XtOffsetOf(struct settings, arrows), XmRImmediate, (XtPointer) False },
    { (char *) "extended", (char *) "Extended", XmRBoolean, sizeof(Boolean),
      XtOffsetOf(struct settings, extended), XmRImmediate, (XtPointer) True },
    { (char *) "colors", (char *) "Colors", XmRString, sizeof(String),
      XtOffsetOf(struct settings, colors), XmRImmediate, (XtPointer) "game" },
    { (char *) "size", (char *) "Size", XmRInt, sizeof(int),
      XtOffsetOf(struct settings, size), XmRImmediate, (XtPointer) T89_SIZE }
};

/* (As app.resource: the most specific a resource can be, as saved options
   are; see readoptions.) */
static XrmOptionDescRec options[] = {
    { (char *) "-u", (char *) ".undo", XrmoptionNoArg, (XtPointer) "true" },
    { (char *) "-mono", (char *) ".mono", XrmoptionNoArg, (XtPointer) "true" },
    { (char *) "-arrows", (char *) ".arrows", XrmoptionNoArg, (XtPointer) "true" },
    { (char *) "-extended", (char *) ".extended", XrmoptionNoArg, (XtPointer) "true" },
    { (char *) "+extended", (char *) ".extended", XrmoptionNoArg, (XtPointer) "false" },
    { (char *) "-colors", (char *) ".colors", XrmoptionSepArg, (XtPointer) 0 },
    { (char *) "-colours", (char *) ".colors", XrmoptionSepArg, (XtPointer) 0 }  /* as was */
};

/* The options again, with -xrm, to put the command line back on top of
   ~/.xm2089 (see readoptions). */
static XrmOptionDescRec again[XtNumber(options) + 1];

/* What an app-defaults file would say, if there is none. */
static String fallback[] = {
    (char *) "XM2089.title: 2089",
    (char *) "XM2089.iconName: 2089",
    (char *) "XM2089*board.width: 400",
    (char *) "XM2089*board.height: 400",
    (char *) "XM2089*work.marginWidth: 4",
    (char *) "XM2089*work.marginHeight: 4",
    (char *) "XM2089*value.marginWidth: 4",
    (char *) "XM2089*value.marginHeight: 1",
    (char *) "XM2089*openGame.width: 440",
    (char *) "XM2089*saveGame.width: 440",
    (char *) "XM2089*pad.width: 69",
    (char *) "XM2089*pad.height: 46",
    0
};

/* ------------------------------------------------------------ colours */

/*
 * The schemes the board can be drawn in.  Each is the board's colours, the
 * bevels Motif works out for them, and the page around the board.
 */
#define GAME    0
#define DESKTOP 1
#define MONO    2

struct scheme {
    int ok;
    unsigned long ui[X89_NAMED], tile[X89_TILES], text[X89_TILES];
    unsigned long tilets[X89_TILES], tilebs[X89_TILES], emptyts, emptybs, page;
};
static struct scheme schemes[2];
static int scheme = GAME;

/* Motif's top and bottom shadows for a background. */
static void bevel(Pixel bg, unsigned long *ts, unsigned long *bs)
{
    Pixel fg, top_shadow, bottom_shadow, select;

    XmGetColors(XtScreen(top), DefaultColormapOfScreen(XtScreen(top)), bg, &fg,
                &top_shadow, &bottom_shadow, &select);
    *ts = top_shadow;
    *bs = bottom_shadow;
}

/* The game's colours, as xboard allocated them (extended past 2048 or
   not), with bevels, on the window's own background. */
static void gamecolours(Pixel page)
{
    struct scheme *s = &schemes[GAME];
    int i;

    if (!brd.hascolour)
        return;
    memcpy(s->ui, brd.ui, sizeof s->ui);
    memcpy(s->tile, brd.tile, sizeof s->tile);
    memcpy(s->text, brd.text, sizeof s->text);
    for (i = 0; i < X89_TILES; i++)
        bevel(s->tile[i], &s->tilets[i], &s->tilebs[i]);
    bevel(s->ui[X89_EMPTY], &s->emptyts, &s->emptybs);
    s->page = page;
    s->ok = 1;
}

static int luminance(Pixel p)
{
    XColor c;

    c.pixel = p;
    XQueryColor(dpy, DefaultColormapOfScreen(XtScreen(top)), &c);
    return (30 * (c.red >> 8) + 59 * (c.green >> 8) + 11 * (c.blue >> 8)) / 100;
}

/*
 * The desktop's palette, from CDE's colour server (none without it).  The
 * page, the board and the empty cells are the primary colour set (the one
 * windows are drawn in), the cells sunk into it in its select colour.  The
 * six sets that are neither primary nor the active window's go to 2 to 64,
 * lightest first; 128 to 2048 are the active window's colour, the
 * palette's highlight (the original makes them all gold), and larger tiles
 * its darkest set.
 */
static void desktopcolours(void)
{
    struct scheme *s = &schemes[DESKTOP];
    XmPixelSet set[XmCO_NUM_COLORS];
    int use, i, j, n = 0, order[XmCO_NUM_COLORS], lum[XmCO_NUM_COLORS];
    short active, inactive, primary, secondary, text;

    if (!brd.hascolour
        || !XmeGetColorObjData(XtScreen(top), &use, set, XmCO_NUM_COLORS, &active,
                               &inactive, &primary, &secondary, &text)
        || use == XmCO_BLACK_WHITE)
        return;
    for (i = 0; i < XmCO_NUM_COLORS; i++) {
        if (i == primary || i == active)
            continue;
        lum[i] = luminance(set[i].bg);
        for (j = n; j > 0 && lum[order[j - 1]] < lum[i]; j--)
            order[j] = order[j - 1];
        order[j] = i;
        n++;
    }
    s->page = s->ui[X89_PAGE] = s->ui[X89_BOARD] = set[primary].bg;
    s->ui[X89_EMPTY] = set[primary].sc;
    s->emptyts = set[primary].ts;
    s->emptybs = set[primary].bs;
    for (i = 0; i < X89_TILES; i++) {
        j = i < 6 && i < n ? order[i] : i < 11 || n == 0 ? active  /* to 2048 */
            : order[n - 1];
        s->tile[i] = set[j].bg;
        s->text[i] = set[j].fg;
        s->tilets[i] = set[j].ts;
        s->tilebs[i] = set[j].bs;
    }
    s->ok = 1;
}

/* The game's extended colours past 2048, or the original's one colour. */
static void extend(int on)
{
    struct scheme *s = &schemes[GAME];
    int i;

    res.extended = (Boolean) (on != 0);
    XmToggleButtonSetState(extitem, res.extended, False);
    if (!s->ok)
        return;
    x89_extend(&brd, on, s->tile);
    for (i = X89_BASE; i < X89_TILES; i++)
        bevel(s->tile[i], &s->tilets[i], &s->tilebs[i]);
}

/* Draw in a scheme from now on. */
static void usescheme(int which)
{
    struct scheme *s;
    int i;

    if (which != MONO && !schemes[which].ok)
        which = schemes[GAME].ok ? GAME : MONO;
    scheme = which;
    brd.colour = which != MONO;
    if (brd.colour) {
        s = &schemes[which];
        memcpy(brd.ui, s->ui, sizeof brd.ui);
        memcpy(brd.tile, s->tile, sizeof brd.tile);
        memcpy(brd.text, s->text, sizeof brd.text);
        memcpy(brd.tilets, s->tilets, sizeof brd.tilets);
        memcpy(brd.tilebs, s->tilebs, sizeof brd.tilebs);
        brd.emptyts = s->emptyts;
        brd.emptybs = s->emptybs;
        brd.ui[X89_PAGE] = s->page;
        brd.bevel = 1;
    }
    for (i = 0; i < 3; i++)
        XmToggleButtonSetState(schemeitem[i], (Boolean) (i == which), False);
    XtVaSetValues(board, XmNbackground, brd.colour ? brd.ui[X89_PAGE] : brd.white,
                  (char *) 0);
}

/* ------------------------------------------------------------ the window */

static void label(Widget w, const char *text)
{
    XmString s = XmStringCreateLocalized((char *) text);

    XtVaSetValues(w, XmNlabelString, s, (char *) 0);
    XmStringFree(s);
}

static void say(const char *text)
{
    label(message, text[0] ? text : " ");
}

static void nomoves(void)
{
    say(res.undo ? "No moves left: Undo, or start a New Game."
                 : "No moves left: start a New Game.");
}

static void minsize(void);

/* The board's window: the pixmap, the board on it, then copy it over; and
   the scores, and what can be undone. */
static void draw(void)
{
    char s[16];

    if (buf) {
        XSetForeground(dpy, brd.gc, brd.colour ? brd.ui[X89_PAGE] : brd.white);
        XFillRectangle(dpy, buf, brd.gc, 0, 0, width, height);
        x89_draw(&brd, buf, &game);
        if (XtIsRealized(board))
            XCopyArea(dpy, buf, XtWindow(board), brd.gc, 0, 0, width, height, 0, 0);
    }
    f89_bnote(&game);
    sprintf(s, "%lu", game.score);
    label(score[0], s);
    sprintf(s, "%lu", f89_btop(game.size));
    label(score[1], s);
    sprintf(s, "%lu", game.moves);
    label(score[2], s);
    XtSetSensitive(undoitem, (Boolean) (res.undo && canundo));
    XtSetSensitive(undobutton, (Boolean) (res.undo && canundo));
    minsize();                          /* the best score may have a digit more */
}

/* Place the board in the drawing area, filling it. */
static void place(void)
{
    x89_place(&brd, 0, 0, (int) width, (int) height, game.size);
}

/* A new pixmap for the board's size, and the board placed on it. */
static void resize(void)
{
    Dimension w, h;

    XtVaGetValues(board, XmNwidth, &w, XmNheight, &h, (char *) 0);
    if (w == 0 || h == 0 || (buf && w == width && h == height))
        return;
    if (buf)
        XFreePixmap(dpy, buf);
    width = w;
    height = h;
    buf = XCreatePixmap(dpy, RootWindowOfScreen(XtScreen(board)), width, height,
                        (unsigned int) DefaultDepthOfScreen(XtScreen(board)));
    place();
}

static void exposed(Widget w, XtPointer client, XtPointer call)
{
    XEvent *e = ((XmDrawingAreaCallbackStruct *) call)->event;

    (void) w;
    (void) client;
    if (buf && e && e->type == Expose)
        XCopyArea(dpy, buf, XtWindow(board), brd.gc, e->xexpose.x, e->xexpose.y,
                  (unsigned int) e->xexpose.width, (unsigned int) e->xexpose.height,
                  e->xexpose.x, e->xexpose.y);
}

static void resized(Widget w, XtPointer client, XtPointer call)
{
    (void) w;
    (void) client;
    (void) call;
    resize();
    draw();
}

/* A widget's width as it would like it. */
static int wants(Widget w)
{
    XtWidgetGeometry g;
    Dimension now;

    XtVaGetValues(w, XmNwidth, &now, (char *) 0);
    if (XtQueryGeometry(w, (XtWidgetGeometry *) 0, &g) != XtGeometryNo
        && (g.request_mode & CWWidth))
        return g.width;
    return now;
}

/* The width a score's sunken field needs for n digits: the digits, and
   the value's and the frame's margins and shadows as they are. */
static int field(Widget value, int n)
{
    Dimension fw, vw, mw, ml, mr, ht, st;
    XmFontList fl;
    XmString s;
    char d[24];
    int w;

    XtVaGetValues(XtParent(value), XmNwidth, &fw, (char *) 0);
    XtVaGetValues(value, XmNwidth, &vw, XmNmarginWidth, &mw, XmNmarginLeft, &ml,
                  XmNmarginRight, &mr, XmNhighlightThickness, &ht,
                  XmNshadowThickness, &st, XmNfontList, &fl, (char *) 0);
    sprintf(d, "%.*s", n < 20 ? n : 20, "00000000000000000000");
    s = XmStringCreateLocalized(d);
    w = (int) XmStringWidth(fl, s);
    XmStringFree(s);
    return (fw - vw) + 2 * (ht + st + mw) + ml + mr + w;
}

/*
 * The smallest the window may be: wide enough for the menu bar, the scores
 * and the bottom row as they'd like to be, and for a board that shows its
 * numbers; then tall enough for the board to be as tall as that is wide.
 * The score fields hold as many digits as the best score has (four at
 * least).  What's around the board (frames, margins, the other rows) is
 * measured off the window as it is, so this runs once the window is up,
 * and again when anything in it changes.
 */
static void minsize(void)
{
    static int lastw, lasth;
    Dimension sw, sh, bw, bh, rw;
    char s[24];
    int w, h, need, n, i, t;

    if (!XtIsRealized(top) || !XtIsManaged(work))  /* not up, or mid-change */
        return;
    XtVaGetValues(top, XmNwidth, &sw, XmNheight, &sh, (char *) 0);
    XtVaGetValues(board, XmNwidth, &bw, XmNheight, &bh, (char *) 0);

    w = x89_least(&brd, game.size) + (sw - bw);

    XtVaGetValues(menubar, XmNwidth, &rw, (char *) 0);
    if (wants(menubar) + (sw - rw) > w)
        w = wants(menubar) + (sw - rw);

    /* each score's label and field in a third of the row */
    sprintf(s, "%lu", f89_btop(game.size));
    n = strlen(s) > 4 ? (int) strlen(s) : 4;
    for (need = 0, i = 0; i < 3; i++) {
        t = (i > 0 ? 6 : 0) + wants(scorelabel[i]) + 2 + field(score[i], n);
        if (t > need)
            need = t;
    }
    XtVaGetValues(XtParent(scorelabel[0]), XmNwidth, &rw, (char *) 0);
    if (3 * need + (sw - rw) > w)
        w = 3 * need + (sw - rw);

    /* the buttons, and the arrows after a space */
    need = wants(newbutton) + 4 + wants(undobutton);
    if (XtIsManaged(pad))
        need += 12 + wants(pad);
    XtVaGetValues(controls, XmNwidth, &rw, (char *) 0);
    if (need + (sw - rw) > w)
        w = need + (sw - rw);

    h = (w - (sw - bw)) + (sh - bh);
    if (w == lastw && h == lasth)
        return;
    lastw = w;
    lasth = h;
    XtVaSetValues(top, XmNminWidth, w, XmNminHeight, h, (char *) 0);
    if (sw < w || sh < h)
        XtVaSetValues(top, XmNwidth, sw < w ? w : sw, XmNheight, sh < h ? h : sh,
                      (char *) 0);
}

/* ------------------------------------------------------------ options */

/* ~/.xm2089, where Options > Save Options keeps them: 0 if no home. */
static int optionsfile(char *file)
{
    const char *home = getenv("HOME");

    if (!home || !home[0])
        return 0;
    sprintf(file, "%.280s/.xm2089", home);
    return 1;
}

/*
 * The saved options, read over the user's other resources (~/.Xdefaults,
 * or what xrdb loaded: Xt has them all by now), since saving them was
 * asked for later; then the command line (argc and argv as they were
 * before Xt took its part) again over those.  Xrm goes by how specific a
 * resource is, not by which came last, so saved options are written as
 * app.resource (xm2089.size), which nothing in ~/.Xdefaults beats unless
 * it says the same, and the options on the command line are the same.
 */
static void readoptions(int argc, char **argv)
{
    XrmDatabase db = XtDatabase(dpy), cmd = 0;
    char file[300];

    if (!optionsfile(file))
        return;
    XrmCombineFileDatabase(file, &db, True);
    memcpy(again, options, sizeof options);
    again[XtNumber(options)].option = (char *) "-xrm";
    again[XtNumber(options)].specifier = 0;
    again[XtNumber(options)].argKind = XrmoptionResArg;
    again[XtNumber(options)].value = 0;
    XrmParseCommand(&cmd, again, XtNumber(again), (char *) name, &argc, argv);
    XrmCombineDatabase(cmd, &db, True);
    XrmSetDatabase(dpy, db);
}

/* Options > Save Options: the board size, colours and arrows, for the
   next time 2089 starts (undo stays the command line's). */
static void saveoptions(void)
{
    char file[300], text[340];
    String appname, appclass;
    FILE *f;
    int ok;

    XtGetApplicationNameAndClass(dpy, &appname, &appclass);
    if (!optionsfile(file)) {
        say("There's no home directory to save the options in.");
        return;
    }
    if ((f = fopen(file, "w")) == 0)
        ok = 0;
    else {
        ok = fprintf(f, "! xm2089's options, from Options > Save Options.  Resources, as in\n"
                     "! ~/.Xdefaults: these win over those, the command line over these.\n"
                     "%s.size: %d\n%s.colors: %s\n%s.mono: false\n"
                     "%s.extended: %s\n%s.arrows: %s\n", appname, game.size, appname,
                     scheme == MONO ? "mono" : scheme == DESKTOP ? "desktop" : "game", appname,
                     appname, res.extended ? "true" : "false",
                     appname, res.arrows ? "true" : "false") > 0;
        ok = fclose(f) == 0 && ok;
    }
    sprintf(text, ok ? "Options saved in %.250s." : "Can't save the options in %.250s.", file);
    say(text);
}

/* ------------------------------------------------------------ leaving */

static void finish(void)
{
    sprintf(command, "%s %s%s%s%s", name, res.undo ? "-u " : "", res.arrows ? "-arrows " : "",
            scheme == MONO ? "-mono " : scheme == DESKTOP ? "-colors desktop " : "",
            res.extended ? "" : "+extended ");
    if (f89_bsave() < 0)
        fprintf(stderr, "%s: can't save the high score in %s\n", name, f89_bfile());
    f89_resume(&game, command);
}

static void quit(void)
{
    finish();
    exit(EXIT_SUCCESS);
}

/* The server went away (or the window was killed): save what can be. */
static int lost(Display *d)
{
    (void) d;
    finish();
    exit(EXIT_FAILURE);
    return 0;
}

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

/* ------------------------------------------------------------ the game */

/* Options > Board Size shows the size played (none for 2 x 2). */
static void showsize(void)
{
    int n;

    for (n = 3; n <= T89_MAXSIZE; n++)
        XmToggleButtonSetState(sizeitem[n], (Boolean) (n == game.size), False);
}

/* Play this game from now on (new, loaded or opened). */
static void start(const struct t89_game *g)
{
    f89_bsave();
    game = *g;
    canundo = known = 0;
    showsize();
    over = t89_over(&game);
    place();                            /* the size may have changed */
    minsize();
    if (over)
        nomoves();
    else
        say("");
    draw();
}

static void doundo(void)
{
    if (!res.undo)
        say("Undo is off: start with -u to allow it.");
    else if (canundo) {
        game = undo;
        canundo = over = 0;
        say("");
    } else
        say("Nothing to undo.");
    draw();
}

static void play(int cmd)
{
    struct t89_game before = game;
    struct t89_result r;
    char text[64];

    if (t89_move(&game, f89_dir(cmd), &r)) {
        undo = before;
        canundo = 1;
        say("");
        if (r.events & T89_EV_OVER) {
            over = 1;
            f89_bsave();
        }
        if (r.events & T89_EV_WON) {
            f89_tile(text, game.goal);
            strcat(text, !over ? "!  You win.  Keep going if you like."
                   : "!  You win, and no moves are left.");
            say(text);
        } else if (over)
            nomoves();
        draw();
    } else if (over)
        nomoves();
}

/* ------------------------------------------------------------ dialogs */

/* A message box's buttons: drop the ones not wanted. */
static void only(Widget d, int ok, int cancel, int help)
{
    if (!ok)
        XtUnmanageChild(XmMessageBoxGetChild(d, XmDIALOG_OK_BUTTON));
    if (!cancel)
        XtUnmanageChild(XmMessageBoxGetChild(d, XmDIALOG_CANCEL_BUTTON));
    if (!help)
        XtUnmanageChild(XmMessageBoxGetChild(d, XmDIALOG_HELP_BUTTON));
}

static void settext(Widget w, const char *resource, const char *text)
{
    XmString s = XmStringCreateLtoR((char *) text, XmFONTLIST_DEFAULT_TAG);

    XtVaSetValues(w, resource, s, (char *) 0);
    XmStringFree(s);
}

static void title(Widget d, const char *text)
{
    XtVaSetValues(XtParent(d), XmNtitle, text, (char *) 0);
}

/* A new game: game number n, of size newsize. */
static void deal(unsigned long n)
{
    struct t89_game g;
    char text[64];

    t89_new(&g, newsize, n);
    start(&g);
    number = n;
    known = 1;
    sprintf(text, "Game number %lu.", n);
    if (!over)
        say(text);
}

static void newgame(Widget w, XtPointer client, XtPointer call)
{
    (void) w;
    (void) client;
    (void) call;
    deal(f89_seed(game.rng));
}

/* The question gone: if a new game of another size wasn't started after
   all, Board Size goes back to the size played. */
static void unasked(Widget w, XtPointer client, XtPointer call)
{
    (void) w;
    (void) client;
    (void) call;
    showsize();
}

/* A new game of this size: asked first, except for a new size when the
   game hasn't begun or is over. */
static void asknewgame(int size)
{
    char text[96];

    newsize = size;
    if (size != game.size && (game.moves == 0 || over)) {
        deal(f89_seed(game.rng));
        return;
    }
    if (!asknew) {
        asknew = XmCreateQuestionDialog(top, (char *) "newGame", (ArgList) 0, 0);
        title(asknew, "2089 - New Game");
        settext(asknew, XmNokLabelString, "New Game");
        only(asknew, 1, 1, 0);
        XtAddCallback(asknew, XmNokCallback, newgame, (XtPointer) 0);
        XtAddCallback(asknew, XmNunmapCallback, unasked, (XtPointer) 0);
    }
    if (size == game.size)
        strcpy(text, "Start a new game?\nThis one will be lost.");
    else
        sprintf(text, "Start a new %d x %d game?\nThis one will be lost.", size, size);
    settext(asknew, XmNmessageString, text);
    XtManageChild(asknew);
}

/* A game number typed in: 0 to 4294967295 (as 2089 -r takes). */
static void numbered(Widget w, XtPointer client, XtPointer call)
{
    XmSelectionBoxCallbackStruct *cb = (XmSelectionBoxCallbackStruct *) call;
    char *text = 0, *p;
    unsigned long n = 0;
    int ok;

    (void) w;
    (void) client;
    if (!XmStringGetLtoR(cb->value, XmFONTLIST_DEFAULT_TAG, &text) || !text)
        return;
    for (p = text; *p == ' '; p++)
        ;
    ok = *p != '\0';
    for (; ok && *p >= '0' && *p <= '9'; p++) {
        ok = n <= (0xFFFFFFFFUL - (unsigned long) (*p - '0')) / 10;
        n = n * 10 + (unsigned long) (*p - '0');
    }
    while (*p == ' ')
        p++;
    if (ok && *p == '\0') {
        newsize = game.size;
        deal(n);
    } else
        say("A game number is 0 to 4294967295.");
    XtFree(text);
}

static void asknumber(void)
{
    char text[16];

    if (!numberdialog) {
        numberdialog = XmCreatePromptDialog(top, (char *) "gameNumber", (ArgList) 0, 0);
        title(numberdialog, "2089 - New Game from Number");
        settext(numberdialog, XmNselectionLabelString, "Game Number (0 to 4294967295):");
        settext(numberdialog, XmNokLabelString, "New Game");
        XtUnmanageChild(XmSelectionBoxGetChild(numberdialog, XmDIALOG_HELP_BUTTON));
        XtAddCallback(numberdialog, XmNokCallback, numbered, (XtPointer) 0);
    }
    if (known)
        sprintf(text, "%lu", number);
    else
        text[0] = '\0';
    settext(numberdialog, XmNtextString, text);
    XtManageChild(numberdialog);
}

/* The Best Scores table, as the records are now. */
static void fillscores(void)
{
    struct t89_game g;
    char text[300];
    int n;

    for (n = T89_MINSIZE; n <= T89_MAXSIZE; n++) {
        sprintf(text, "%d x %d", n, n);
        label(scoretext[n][0], text);
        if (t89_load(&g, f89_brec(n)) == 0) {
            sprintf(text, "%lu", f89_btop(n));
            label(scoretext[n][1], text);
            f89_tile(text, t89_best(&g));
            label(scoretext[n][2], text);
        } else {
            label(scoretext[n][1], "-");
            label(scoretext[n][2], "-");
        }
        XtSetSensitive(scoretext[n][3], (Boolean) (f89_btop(n) > 0));
    }
}

/* Clearing asked for and agreed to: one size's record, or all (0). */
static void cleared(Widget w, XtPointer client, XtPointer call)
{
    char text[300];

    (void) w;
    (void) call;
    if (f89_bclear((int) (long) client) < 0) {
        sprintf(text, "Can't clear the best scores in %.250s.", f89_bfile());
        say(text);
    }
    draw();                             /* this game may be the best now */
    fillscores();
}

/* Clear a size's best score (all, if 0)?  Asked first. */
static void clearscores(Widget w, XtPointer client, XtPointer call)
{
    int size = (int) (long) client;
    char text[128];

    (void) w;
    (void) call;
    if (!askclear) {
        askclear = XmCreateQuestionDialog(scoresdialog, (char *) "clearScores",
                                          (ArgList) 0, 0);
        title(askclear, "2089 - Clear Best Scores");
        settext(askclear, XmNokLabelString, "Clear");
        only(askclear, 1, 1, 0);
        /* Return keeps them, as the style guide has it for what can't be undone */
        XtVaSetValues(askclear, XmNdefaultButtonType, XmDIALOG_CANCEL_BUTTON, (char *) 0);
    }
    XtRemoveAllCallbacks(askclear, XmNokCallback);
    XtAddCallback(askclear, XmNokCallback, cleared, (XtPointer) (long) size);
    if (size)
        sprintf(text, "Clear the best score for %d x %d?", size, size);
    else
        strcpy(text, "Clear the best scores for every size?");
    settext(askclear, XmNmessageString, text);
    XtManageChild(askclear);
}

/* Each board size's best score and the top tile in that game, each with a
   button to clear it, and one to clear them all. */
static void showscores(void)
{
    static const char *head[4] = { "Board", "Best", "Top Tile", "" };
    Widget form, table, all;
    int n, i;

    if (!scoresdialog) {
        scoresdialog = XmCreateInformationDialog(top, (char *) "bestScores", (ArgList) 0, 0);
        title(scoresdialog, "2089 - Best Scores");
        only(scoresdialog, 1, 0, 0);
        form = XtVaCreateManagedWidget("work", xmFormWidgetClass, scoresdialog, (char *) 0);
        table = XtVaCreateManagedWidget("table", xmRowColumnWidgetClass, form,
                                        XmNorientation, XmHORIZONTAL,
                                        XmNpacking, XmPACK_COLUMN,
                                        XmNnumColumns, T89_MAXSIZE - T89_MINSIZE + 2,
                                        XmNentryAlignment, XmALIGNMENT_END,
                                        XmNspacing, 0,
                                        XmNtopAttachment, XmATTACH_FORM,
                                        XmNleftAttachment, XmATTACH_FORM,
                                        XmNrightAttachment, XmATTACH_FORM, (char *) 0);
        for (n = T89_MINSIZE - 1; n <= T89_MAXSIZE; n++)
            for (i = 0; i < 4; i++) {
                if (i == 3 && n >= T89_MINSIZE) {
                    scoretext[n][i] = XtVaCreateManagedWidget("clear", xmPushButtonWidgetClass,
                                                              table, XmNmarginHeight, 0,
                                                              (char *) 0);
                    label(scoretext[n][i], "Clear");
                    XtAddCallback(scoretext[n][i], XmNactivateCallback, clearscores,
                                  (XtPointer) (long) n);
                    continue;
                }
                scoretext[n][i] = XtVaCreateManagedWidget("entry", xmLabelWidgetClass, table,
                                                          XmNmarginWidth, 8,
                                                          XmNmarginHeight, 1, (char *) 0);
                if (n < T89_MINSIZE)
                    label(scoretext[n][i], head[i]);
            }
        all = XtVaCreateManagedWidget("clearAll", xmPushButtonWidgetClass, form,
                                      XmNtopAttachment, XmATTACH_WIDGET,
                                      XmNtopWidget, table, XmNtopOffset, 8,
                                      XmNrightAttachment, XmATTACH_FORM, (char *) 0);
        label(all, "Clear All...");
        XtAddCallback(all, XmNactivateCallback, clearscores, (XtPointer) 0);
    }
    fillscores();
    settext(scoresdialog, XmNmessageString, "Best scores across all boards");
    XtManageChild(scoresdialog);
}

/* The save string dialog: shows this game's, and plays one typed or pasted. */
static void stringed(Widget w, XtPointer client, XtPointer call)
{
    XmSelectionBoxCallbackStruct *cb = (XmSelectionBoxCallbackStruct *) call;
    struct t89_game g;
    char *text = 0, now[T89_SAVELEN];

    (void) w;
    (void) client;
    if (!XmStringGetLtoR(cb->value, XmFONTLIST_DEFAULT_TAG, &text) || !text)
        return;
    t89_save(&game, now, sizeof now);
    if (strcmp(text, now) == 0)
        ;                               /* unchanged: nothing to do */
    else if (t89_load(&g, text) < 0)
        say("That isn't a save string.");
    else
        start(&g);
    XtFree(text);
}

static void savestring(void)
{
    char save[T89_SAVELEN];

    t89_save(&game, save, sizeof save);
    printf("save string: %s\n", save);  /* and where it can be copied */
    fflush(stdout);
    if (!showsave) {
        showsave = XmCreatePromptDialog(top, (char *) "saveString", (ArgList) 0, 0);
        title(showsave, "2089 - Save String");
        settext(showsave, XmNselectionLabelString,
                "This game's save string, to copy.  Paste another\n"
                "here to play it (or give it to 2089 -l).");
        settext(showsave, XmNokLabelString, "Play");
        settext(showsave, XmNcancelLabelString, "Close");
        XtUnmanageChild(XmSelectionBoxGetChild(showsave, XmDIALOG_HELP_BUTTON));
        XtAddCallback(showsave, XmNokCallback, stringed, (XtPointer) 0);
    }
    settext(showsave, XmNtextString, save);
    XtManageChild(showsave);
}

/* Games in files: a file holds a save string on its first line.  Open one
   into g; 0 if it worked, else -1 with why in text. */
static int readgame(const char *file, struct t89_game *g, char *text)
{
    char line[T89_SAVELEN + 2];
    FILE *f;

    if ((f = fopen(file, "r")) == 0) {
        sprintf(text, "Can't open %.250s.", file);
        return -1;
    }
    if (!fgets(line, (int) sizeof line, f))
        line[0] = '\0';
    fclose(f);
    line[strcspn(line, "\r\n")] = '\0';
    if (t89_load(g, line) < 0) {
        sprintf(text, "%.250s isn't a saved game.", file);
        return -1;
    }
    return 0;
}

static void filed(Widget w, XtPointer client, XtPointer call)
{
    XmFileSelectionBoxCallbackStruct *cb = (XmFileSelectionBoxCallbackStruct *) call;
    char *file = 0, line[T89_SAVELEN + 2], text[T89_SAVELEN + 300];
    struct t89_game g;
    FILE *f;
    int saving = client != 0;

    if (!XmStringGetLtoR(cb->value, XmFONTLIST_DEFAULT_TAG, &file) || !file)
        return;
    XtUnmanageChild(w);
    if (saving) {
        t89_save(&game, line, T89_SAVELEN);
        if ((f = fopen(file, "w")) == 0)
            sprintf(text, "Can't save the game in %.250s.", file);
        else if (fprintf(f, "%s\n", line) < 0 || fclose(f) != 0)
            sprintf(text, "Can't save the game in %.250s.", file);
        else
            sprintf(text, "Saved in %.250s.", file);
        say(text);
    } else if (readgame(file, &g, text) < 0)
        say(text);
    else
        start(&g);
    XtFree(file);
}

static void cancelled(Widget w, XtPointer client, XtPointer call)
{
    (void) client;
    (void) call;
    XtUnmanageChild(w);
}

static void filedialog(int saving)
{
    Widget *d = saving ? &savedialog : &opendialog;

    if (!*d) {
        *d = XmCreateFileSelectionDialog(top, (char *) (saving ? "saveGame" : "openGame"),
                                         (ArgList) 0, 0);
        title(*d, saving ? "2089 - Save Game As" : "2089 - Open Game");
        settext(*d, XmNpattern, "*.2089");
        settext(*d, XmNokLabelString, saving ? "Save" : "Open");
        XtUnmanageChild(XmFileSelectionBoxGetChild(*d, XmDIALOG_HELP_BUTTON));
        XtAddCallback(*d, XmNokCallback, filed, (XtPointer) (long) saving);
        XtAddCallback(*d, XmNcancelCallback, cancelled, (XtPointer) 0);
    }
    XtManageChild(*d);
}

static void showkeys(void)
{
    if (!keys) {
        keys = XmCreateInformationDialog(top, (char *) "keys", (ArgList) 0, 0);
        title(keys, "2089 - Keys");
        settext(keys, XmNmessageString,
            "Slide the tiles; two equal tiles that meet join into one.\n"
            "Reach 2048 to win (you can keep going), until no move is left.\n"
            "\n"
            "Type on the board (or click the arrow buttons, in View):\n"
            "    arrows, w a s d, k h j l, 8 4 2 6     move\n"
            "    u    undo (if started with -u)\n"
            "    n    new game\n"
            "    p    save string\n"
            "    c    next color scheme\n"
            "    q    exit");
        only(keys, 1, 0, 0);
    }
    XtManageChild(keys);
}

/*
 * CDE's help viewer on a help volume: 2089's own (frontend/x11/motif/cde,
 * installed with make install-cde), or Help4Help, CDE's help on using help.
 * Without the viewer (no CDE), the keys instead.
 */
static void helpvolume(const char *volume)
{
    static const char viewer[] = "/usr/dt/bin/dthelpview";
    char cmd[128];
    FILE *f;

    if ((f = fopen(viewer, "r")) == 0) {
        showkeys();
        return;
    }
    fclose(f);
    /* From /: the viewer takes a file named as the volume in the directory
       it runs in for the volume itself (/usr/local/bin/2089, say). */
    sprintf(cmd, "cd / && %s -helpVolume %s &", viewer, volume);
    if (system(cmd) != 0)
        showkeys();
}

static void showabout(void)
{
    char text[256];

    if (!about) {
        about = XmCreateInformationDialog(top, (char *) "about", (ArgList) 0, 0);
        title(about, "2089 - About");
        sprintf(text, "2089  Version %s\n\n"
                "2048 in C89, after Gabriele Cirulli's game.\n"
                "Motif %d.%d.  MIT License.\n\n"
                "https://github.com/nfinit/2089", T89_VERSION, XmVERSION, XmREVISION);
        settext(about, XmNmessageString, text);
        only(about, 1, 0, 0);
    }
    XtManageChild(about);
}

/* ------------------------------------------------------------ input */

/* The next scheme the display has, after the one in use. */
static void nextscheme(void)
{
    int s = scheme;

    do
        s = (s + 1) % 3;
    while (s != MONO && !schemes[s].ok);
    usescheme(s);
    draw();
}

/* Keys typed on the board. */
static void input(Widget w, XtPointer client, XtPointer call)
{
    XEvent *e = ((XmDrawingAreaCallbackStruct *) call)->event;
    int cmd;

    (void) w;
    (void) client;
    if (!e || e->type != KeyPress || IsModifierKey(XLookupKeysym(&e->xkey, 0)))
        return;
    switch (cmd = x89_key(&e->xkey)) {
    case F89_QUIT:    quit(); break;
    case F89_NEW:     asknewgame(game.size); break;
    case F89_UNDO:    doundo(); break;
    case F89_SAVE:    savestring(); break;
    case F89_HELP:    showkeys(); break;
    case F89_DISPLAY: nextscheme(); break;
    case 0:           break;
    default:          play(cmd); break;
    }
}

/* Clicking the board gives it the keyboard. */
static void clicked(Widget w, XtPointer client, XEvent *e, Boolean *go)
{
    (void) client;
    (void) e;
    (void) go;
    XmProcessTraversal(w, XmTRAVERSE_CURRENT);
}

/* ------------------------------------------------------------ menus */

/* The arrow buttons, shown or not. */
static void showarrows(int on)
{
    int up = XtIsRealized(top);

    /*
     * Once the window is up, the bottom row can't just grow: the shell
     * won't, and the form won't take the room from the board.  So the
     * work area is out of the window while the pad comes or goes (its
     * rows get what they ask for), then back in the space it had, where
     * the board gives or takes the difference.  minsize makes the window
     * bigger if it has to be.
     */
    res.arrows = (Boolean) (on != 0);
    if (up)
        XtUnmanageChild(work);
    if (on)
        XtManageChild(pad);
    else
        XtUnmanageChild(pad);
    if (up)
        XtManageChild(work);
    XmToggleButtonSetState(arrowsitem, res.arrows, False);
    minsize();
}

enum {
    M_NEW, M_NUMBER, M_OPEN, M_SAVEAS, M_STRING, M_EXIT, M_UNDO,
    M_ARROWS, M_SCORES, M_GAME, M_DESKTOP, M_MONO, M_EXTENDED, M_SAVEOPTS,
    M_OVERVIEW, M_KEYS, M_USING, M_ABOUT,
    M_UP, M_DOWN, M_LEFT, M_RIGHT,
    M_SIZE                              /* M_SIZE + n: an n x n board */
};

static void menu(Widget w, XtPointer client, XtPointer call)
{
    int what = (int) (long) client;

    if (what >= M_SIZE) {
        if (((XmToggleButtonCallbackStruct *) call)->set && what - M_SIZE != game.size)
            asknewgame(what - M_SIZE);
        return;
    }
    switch (what) {
    case M_NEW:    asknewgame(game.size); break;
    case M_NUMBER: asknumber(); break;
    case M_OPEN:   filedialog(0); break;
    case M_SAVEAS: filedialog(1); break;
    case M_STRING: savestring(); break;
    case M_EXIT:   quit(); break;
    case M_UNDO:   doundo(); break;
    case M_ARROWS:
        showarrows(((XmToggleButtonCallbackStruct *) call)->set);
        break;
    case M_SCORES: showscores(); break;
    case M_GAME:
    case M_DESKTOP:
    case M_MONO:                        /* one of three, the menu being mixed */
        if (((XmToggleButtonCallbackStruct *) call)->set) {
            usescheme(what - M_GAME);
            draw();
        } else
            XmToggleButtonSetState(w, True, False);
        break;
    case M_EXTENDED:
        extend(((XmToggleButtonCallbackStruct *) call)->set);
        if (scheme == GAME)
            usescheme(GAME);
        draw();
        break;
    case M_SAVEOPTS: saveoptions(); break;
    case M_OVERVIEW: helpvolume("2089"); break;
    case M_KEYS:   showkeys(); break;
    case M_USING:  helpvolume("Help4Help"); break;
    case M_ABOUT:  showabout(); break;
    case M_UP:     play(F89_UP); break;
    case M_DOWN:   play(F89_DOWN); break;
    case M_LEFT:   play(F89_LEFT); break;
    case M_RIGHT:  play(F89_RIGHT); break;
    }
}

/* A pulldown menu and its cascade button in the menu bar. */
static Widget pulldown(Widget bar, const char *text, int mnemonic, Widget *cascade)
{
    Widget m = XmCreatePulldownMenu(bar, (char *) text, (ArgList) 0, 0);

    *cascade = XtVaCreateManagedWidget(text, xmCascadeButtonWidgetClass, bar,
                                       XmNsubMenuId, m, XmNmnemonic, (KeySym) mnemonic,
                                       (char *) 0);
    return m;
}

/* An item, with its key shown: a push button, or a toggle (one of many if
   radio, a check box if CHECK). */
#define CHECK 2

static Widget item(Widget m, const char *text, int mnemonic, const char *key,
                   int what, int radio)
{
    XmString k = XmStringCreateLocalized((char *) (key ? key : ""));
    Widget w;

    w = XtVaCreateManagedWidget(text, radio ? xmToggleButtonWidgetClass
                                            : xmPushButtonWidgetClass, m,
                                XmNmnemonic, (KeySym) mnemonic,
                                XmNacceleratorText, k, (char *) 0);
    XmStringFree(k);
    if (radio) {
        XtVaSetValues(w, XmNindicatorType, radio == CHECK ? XmN_OF_MANY : XmONE_OF_MANY,
                      (char *) 0);
        XtAddCallback(w, XmNvalueChangedCallback, menu, (XtPointer) (long) what);
    } else
        XtAddCallback(w, XmNactivateCallback, menu, (XtPointer) (long) what);
    return w;
}

static void separator(Widget m)
{
    XtVaCreateManagedWidget("separator", xmSeparatorWidgetClass, m, (char *) 0);
}

static Widget menus(Widget parent)
{
    Widget bar = XmCreateMenuBar(parent, (char *) "menuBar", (ArgList) 0, 0);
    Widget m, cascade, sizes;
    char text[8];
    int n;

    m = pulldown(bar, "File", 'F', &cascade);
    item(m, "New Game...", 'N', "n", M_NEW, 0);
    item(m, "New Game from Number...", 'u', 0, M_NUMBER, 0);
    item(m, "Open...", 'O', 0, M_OPEN, 0);
    separator(m);
    item(m, "Save As...", 'A', 0, M_SAVEAS, 0);
    item(m, "Save String...", 'S', "p", M_STRING, 0);
    separator(m);
    item(m, "Exit", 'x', "q", M_EXIT, 0);

    m = pulldown(bar, "Edit", 'E', &cascade);
    undoitem = item(m, "Undo", 'U', "u", M_UNDO, 0);

    m = pulldown(bar, "View", 'V', &cascade);
    arrowsitem = item(m, "Arrow Buttons", 'A', 0, M_ARROWS, CHECK);
    separator(m);
    item(m, "Best Scores...", 'B', 0, M_SCORES, 0);

    m = pulldown(bar, "Options", 'O', &cascade);
    sizes = XmCreatePulldownMenu(m, (char *) "boardSize", (ArgList) 0, 0);
    XtVaSetValues(sizes, XmNradioBehavior, True, (char *) 0);
    XtVaCreateManagedWidget("Board Size", xmCascadeButtonWidgetClass, m,
                            XmNsubMenuId, sizes, XmNmnemonic, (KeySym) 'S', (char *) 0);
    for (n = 3; n <= T89_MAXSIZE; n++) {
        sprintf(text, "%d x %d", n, n);
        sizeitem[n] = item(sizes, text, '0' + n, 0, M_SIZE + n, 1);
    }
    separator(m);
    schemeitem[GAME] = item(m, "Game Colors", 'G', "c", M_GAME, 1);
    schemeitem[DESKTOP] = item(m, "Desktop Colors", 'D', "c", M_DESKTOP, 1);
    schemeitem[MONO] = item(m, "Black and White", 'B', "c", M_MONO, 1);
    separator(m);
    extitem = item(m, "Extended Colors", 'E', 0, M_EXTENDED, CHECK);
    separator(m);
    item(m, "Save Options", 'O', 0, M_SAVEOPTS, 0);

    m = pulldown(bar, "Help", 'H', &cascade);
    item(m, "Overview...", 'v', 0, M_OVERVIEW, 0);
    item(m, "Keys...", 'K', "?", M_KEYS, 0);
    separator(m);
    item(m, "Using Help...", 'U', 0, M_USING, 0);
    separator(m);
    item(m, "About 2089...", 'A', 0, M_ABOUT, 0);
    XtVaSetValues(bar, XmNmenuHelpWidget, cascade, (char *) 0);

    XtManageChild(bar);
    return bar;
}

/* ------------------------------------------------------------ the layout */

/* A label and a sunken field holding a right-aligned number: the nth of
   three in a row. */
static Widget scorebox(Widget parent, const char *wname, const char *text, int n)
{
    Widget t, f;

    t = XtVaCreateManagedWidget(wname, xmLabelWidgetClass, parent,
                                XmNtopAttachment, XmATTACH_FORM,
                                XmNbottomAttachment, XmATTACH_FORM,
                                XmNleftAttachment, XmATTACH_POSITION,
                                XmNleftPosition, n,
                                XmNleftOffset, n > 0 ? 6 : 0, (char *) 0);
    label(t, text);
    scorelabel[n] = t;
    f = XtVaCreateManagedWidget("field", xmFrameWidgetClass, parent,
                                XmNshadowType, XmSHADOW_IN,
                                XmNtopAttachment, XmATTACH_FORM,
                                XmNbottomAttachment, XmATTACH_FORM,
                                XmNleftAttachment, XmATTACH_WIDGET,
                                XmNleftWidget, t, XmNleftOffset, 2,
                                XmNrightAttachment, XmATTACH_POSITION,
                                XmNrightPosition, n + 1, (char *) 0);
    return XtVaCreateManagedWidget("value", xmLabelWidgetClass, f,
                                   XmNalignment, XmALIGNMENT_END,
                                   XmNrecomputeSize, False, (char *) 0);
}

/* An arrow button in the pad, an inverted T: one square of a 3 x 2 grid
   (in sixths of the pad, both ways). */
static void arrow(const char *wname, int dir, int row, int col, int what)
{
    Widget a;

    a = XtVaCreateManagedWidget(wname, xmArrowButtonWidgetClass, pad,
                                XmNarrowDirection, dir,
                                XmNtraversalOn, False,
                                XmNtopAttachment, XmATTACH_POSITION,
                                XmNtopPosition, 3 * row,
                                XmNbottomAttachment, XmATTACH_POSITION,
                                XmNbottomPosition, 3 * row + 3,
                                XmNleftAttachment, XmATTACH_POSITION,
                                XmNleftPosition, 2 * col,
                                XmNrightAttachment, XmATTACH_POSITION,
                                XmNrightPosition, 2 * col + 2,
                                (char *) 0);
    XtAddCallback(a, XmNactivateCallback, menu, (XtPointer) (long) what);
}

/* A button in the bottom row, after the one to its left (if any). */
static Widget button(Widget parent, const char *wname, const char *text, Widget left,
                     int what)
{
    Widget b;

    b = XtVaCreateManagedWidget(wname, xmPushButtonWidgetClass, parent,
                                XmNtraversalOn, False,
                                XmNbottomAttachment, XmATTACH_FORM,
                                XmNleftAttachment, left ? XmATTACH_WIDGET : XmATTACH_FORM,
                                XmNleftWidget, left, XmNleftOffset, left ? 4 : 0,
                                (char *) 0);
    label(b, text);
    XtAddCallback(b, XmNactivateCallback, menu, (XtPointer) (long) what);
    return b;
}

/*
 *  +-----------------------------------------+
 *  | File  Edit  View  Options          Help |
 *  +-----------------------------------------+
 *  | Score [   64] Best [ 9184] Moves [   14]|
 *  | +-------------------------------------+ |
 *  | |                                     | |
 *  | |                board                | |
 *  | |                                     | |
 *  | +-------------------------------------+ |
 *  | [New Game] [Undo]                ^    |
 *  |                                <  v  >  |  (View > Arrow Buttons)
 *  +-----------------------------------------+
 *  | status                                  |
 *  +-----------------------------------------+
 */
static void layout(Widget mainw)
{
    Widget scores, frame, status;

    menubar = menus(mainw);
    work = XtVaCreateManagedWidget("work", xmFormWidgetClass, mainw, (char *) 0);

    scores = XtVaCreateManagedWidget("scores", xmFormWidgetClass, work,
                                     XmNfractionBase, 3,
                                     XmNtopAttachment, XmATTACH_FORM,
                                     XmNleftAttachment, XmATTACH_FORM,
                                     XmNrightAttachment, XmATTACH_FORM, (char *) 0);
    score[0] = scorebox(scores, "score", "Score", 0);
    score[1] = scorebox(scores, "best", "Best", 1);
    score[2] = scorebox(scores, "moves", "Moves", 2);

    controls = XtVaCreateManagedWidget("controls", xmFormWidgetClass, work,
                                       XmNbottomAttachment, XmATTACH_FORM,
                                       XmNleftAttachment, XmATTACH_FORM,
                                       XmNrightAttachment, XmATTACH_FORM, (char *) 0);
    newbutton = button(controls, "newGame", "New Game", (Widget) 0, M_NEW);
    undobutton = button(controls, "undo", "Undo", newbutton, M_UNDO);
    pad = XtVaCreateWidget("pad", xmFormWidgetClass, controls,
                           XmNfractionBase, 6,
                           XmNbottomAttachment, XmATTACH_FORM,
                           XmNrightAttachment, XmATTACH_FORM, (char *) 0);
    arrow("up", XmARROW_UP, 0, 1, M_UP);
    arrow("left", XmARROW_LEFT, 1, 0, M_LEFT);
    arrow("down", XmARROW_DOWN, 1, 1, M_DOWN);
    arrow("right", XmARROW_RIGHT, 1, 2, M_RIGHT);

    frame = XtVaCreateManagedWidget("boardFrame", xmFrameWidgetClass, work,
                                    XmNshadowType, XmSHADOW_IN,
                                    XmNtopAttachment, XmATTACH_WIDGET,
                                    XmNtopWidget, scores, XmNtopOffset, 4,
                                    XmNbottomAttachment, XmATTACH_WIDGET,
                                    XmNbottomWidget, controls, XmNbottomOffset, 4,
                                    XmNleftAttachment, XmATTACH_FORM,
                                    XmNrightAttachment, XmATTACH_FORM, (char *) 0);
    board = XtVaCreateManagedWidget("board", xmDrawingAreaWidgetClass, frame,
                                    XmNtraversalOn, True, XmNmarginWidth, 0,
                                    XmNmarginHeight, 0, (char *) 0);
    XtAddCallback(board, XmNexposeCallback, exposed, (XtPointer) 0);
    XtAddCallback(board, XmNresizeCallback, resized, (XtPointer) 0);
    XtAddCallback(board, XmNinputCallback, input, (XtPointer) 0);
    XtAddEventHandler(board, ButtonPressMask, False, clicked, (XtPointer) 0);

    status = XtVaCreateManagedWidget("status", xmFrameWidgetClass, mainw,
                                     XmNshadowType, XmSHADOW_IN, (char *) 0);
    message = XtVaCreateManagedWidget("message", xmLabelWidgetClass, status,
                                      XmNalignment, XmALIGNMENT_BEGINNING, (char *) 0);
    XtVaSetValues(mainw, XmNworkWindow, work, XmNmessageWindow, status, (char *) 0);
}

/* ------------------------------------------------------------ the desktop */

/*
 * The session manager is saving the session (logging out of CDE, say):
 * set WM_COMMAND to the command that brings this game back, which is what
 * it will run at the next login.  (CDE's dtsession, like the X11R5-era
 * managers, uses the ICCCM's WM_SAVE_YOURSELF.)
 */
static void saveyourself(Widget w, XtPointer client, XtPointer call)
{
    static char save[T89_SAVELEN];
    char *argv[10];
    int argc = 0;

    (void) w;
    (void) client;
    (void) call;
    t89_save(&game, save, sizeof save);
    argv[argc++] = program;
    if (res.undo)
        argv[argc++] = (char *) "-u";
    if (res.arrows)
        argv[argc++] = (char *) "-arrows";
    if (!res.extended)
        argv[argc++] = (char *) "+extended";
    if (scheme != GAME) {
        argv[argc++] = (char *) "-colors";
        argv[argc++] = (char *) (scheme == MONO ? "mono" : "desktop");
    }
    argv[argc++] = (char *) "-l";
    argv[argc++] = save;
    XSetCommand(dpy, XtWindow(top), argv, argc);
}

/* The window's icon: 2089's, from the desktop's icon path (XMICONSEARCHPATH,
   where make install-cde puts it), if it's there. */
static void icon(void)
{
    Pixel fg, bg;
    Pixmap p;

    XtVaGetValues(top, XmNforeground, &fg, XmNbackground, &bg, (char *) 0);
    p = XmGetPixmap(XtScreen(top), (char *) "2089.l", fg, bg);
    if (p != XmUNSPECIFIED_PIXMAP)
        XtVaSetValues(top, XmNiconPixmap, p, (char *) 0);
}

/* ------------------------------------------------------------ main */

static void usage(int status)
{
    printf("usage: %s [X options] [-mono] [-colors game|desktop|mono] [+extended]\n", name);
    puts("              [-arrows] [-u] [-s size] [-r seed] [-l savestring] [-v] [-h] [file]");
    puts("  X options      -display, -geometry, -xrm and the rest");
    puts("  -colors        the board's colors: the game's (the default), the");
    puts("                 desktop palette's (under CDE), or black and white");
    puts("  -mono          black and white, even on a color display");
    puts("  +extended      tiles past 2048 in the original's one color, not ours");
    puts("                 (Options > Extended Colors; -extended for ours)");
    puts("  -arrows        show the arrow buttons (View > Arrow Buttons)");
    f89_usage();
    puts("  -v             print the version");
    puts("  file           a saved game to open (File > Save As writes them)");
    exit(status);
}

int main(int argc, char **argv)
{
    struct f89_options o;
    struct t89_game g;
    Widget mainw;
    Pixel page;
    Atom wmdelete, wmsave;
    const char *file = 0;
    char why[T89_SAVELEN + 300], **args;
    int i, nargs;

    program = argv[0];
    name = strrchr(argv[0], '/') ? strrchr(argv[0], '/') + 1 : argv[0];
    if ((args = (char **) malloc((size_t) (argc + 1) * sizeof *args)) == 0)
        return EXIT_FAILURE;
    memcpy(args, argv, (size_t) (argc + 1) * sizeof *args);
    nargs = argc;
    top = XtVaAppInitialize(&app, CLASS, options, XtNumber(options), &argc, argv,
                            fallback, (char *) 0);
    dpy = XtDisplay(top);
    readoptions(nargs, args);
    free(args);
    XtGetApplicationResources(top, (XtPointer) &res, resources, XtNumber(resources),
                              (ArgList) 0, 0);

    /* What Xt left: ours. */
    f89_defaults(&o);
    if (res.size >= T89_MINSIZE && res.size <= T89_MAXSIZE)
        o.size = res.size;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0)
            usage(EXIT_SUCCESS);
        if (strcmp(argv[i], "-v") == 0) {
            printf("2089 %s (Motif %d.%d)\n", T89_VERSION, XmVERSION, XmREVISION);
            return EXIT_SUCCESS;
        }
        if (argv[i][0] != '-' && !file) {
            file = argv[i];
            continue;
        }
        if (f89_option(argc, argv, &i, &o) <= 0)
            usage(EXIT_FAILURE);
    }
    if (o.undo)
        res.undo = True;
    if (file) {
        if (readgame(file, &g, why) < 0) {
            fprintf(stderr, "%s: %s\n", name, why);
            return EXIT_FAILURE;
        }
        game = g;
    } else if (f89_start(&game, &o, name) < 0)
        return EXIT_FAILURE;
    else if (!o.load) {
        number = o.seed;
        known = 1;
    }

    if (x89_open(&brd, dpy, DefaultScreen(dpy), RootWindow(dpy, DefaultScreen(dpy))) < 0) {
        fprintf(stderr, "%s: the server has no fonts I can use (not even fixed)\n", name);
        return EXIT_FAILURE;
    }
    brd.tight = 1;
    x89_extend(&brd, res.extended, brd.tile);

    mainw = XtVaCreateManagedWidget("main", xmMainWindowWidgetClass, top, (char *) 0);
    layout(mainw);

    XtVaGetValues(board, XmNbackground, &page, (char *) 0);
    gamecolours(page);
    desktopcolours();
    for (i = 0; i < 2; i++)
        XtSetSensitive(schemeitem[i], (Boolean) schemes[i].ok);
    XtSetSensitive(extitem, (Boolean) schemes[GAME].ok);
    XmToggleButtonSetState(extitem, res.extended, False);
    showsize();
    showarrows(res.arrows);
    usescheme(res.mono || strcmp(res.colors, "mono") == 0 ? MONO
              : strcmp(res.colors, "desktop") == 0 ? DESKTOP : GAME);

    XtVaSetValues(top, XmNdeleteResponse, XmDO_NOTHING, (char *) 0);
    wmdelete = XmInternAtom(dpy, (char *) "WM_DELETE_WINDOW", False);
    XmAddWMProtocolCallback(top, wmdelete, menu, (XtPointer) M_EXIT);
    wmsave = XmInternAtom(dpy, (char *) "WM_SAVE_YOURSELF", False);
    XmAddWMProtocolCallback(top, wmsave, saveyourself, (XtPointer) 0);
    icon();
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
    else if (known) {
        sprintf(why, "Game number %lu.", number);
        say(why);
    } else
        say("");
    XtRealizeWidget(top);
    resize();
    minsize();
    draw();
    XmProcessTraversal(board, XmTRAVERSE_CURRENT);
    XtAppMainLoop(app);
    return EXIT_SUCCESS;
}
