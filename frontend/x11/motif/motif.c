/*
 * motif.c - 2089 for the X Window System, with Motif (xm2089).
 *
 * The same game as x2089 (xlib/xlib.c), with the same board (../xboard.c),
 * as a Motif application of the CDE years, after the CDE and Motif style
 * guides and the desktop's own tools (dtcalc, dtpad):
 *
 *   - a menu bar: File (New Game, Open, Save As, Save String, Exit), Edit
 *     (Undo), Options (Game Colours, Desktop Colours, Black and White) and
 *     Help (Keys, About);
 *   - the score, best score and moves in sunken frames along the top;
 *   - the board in a sunken frame, its tiles raised with Motif bevels (the
 *     shadows Motif works out for each tile's colour) and empty cells sunk
 *     into it;
 *   - beside it, arrow buttons to play with the mouse, and New Game and
 *     Undo buttons;
 *   - a status line along the bottom.
 *
 * Three colour schemes: the original game's tile colours (the default),
 * the desktop's own palette (under CDE: from the colour server, so the
 * board follows the palette chosen in Style Manager), and black and white,
 * which is all a monochrome display gets.  Widgets take the desktop's
 * colours and fonts in all three.
 *
 * The keys are the other frontends' (arrows, w a s d, h j k l, 8 4 2 6, u
 * n p c q ?), typed on the board.  Undo is off unless -u (or the resource
 * undo: true).  Saved games are files holding a save string, as 2089 -l
 * takes.  Quitting prints the command that carries on the game, and the
 * best scores are kept in ~/.2089.
 *
 * Xt takes the usual X options (-display, -geometry, -xrm, ...); resources
 * are read the X way, for class XM2089.  Fallback resources compiled in
 * stand in for an app-defaults file.
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
static Widget undoitem, undobutton, schemeitem[3];
static Widget asknew, showsave, opendialog, savedialog, keys, about;
static Display *dpy;
static Pixmap buf;                      /* what's in the board's window */
static Dimension width, height;         /* its size */
static struct x89_board brd;
static const char *name;

static struct t89_game game, undo;
static char command[64];                /* how to restart: program and options */
static int canundo, over;

/* Application resources, settable as options. */
struct settings {
    Boolean undo, mono;
    String colours;                     /* game, desktop or mono */
};
static struct settings res;

static XtResource resources[] = {
    { (char *) "undo", (char *) "Undo", XmRBoolean, sizeof(Boolean),
      XtOffsetOf(struct settings, undo), XmRImmediate, (XtPointer) False },
    { (char *) "mono", (char *) "Mono", XmRBoolean, sizeof(Boolean),
      XtOffsetOf(struct settings, mono), XmRImmediate, (XtPointer) False },
    { (char *) "colours", (char *) "Colours", XmRString, sizeof(String),
      XtOffsetOf(struct settings, colours), XmRImmediate, (XtPointer) "game" }
};

static XrmOptionDescRec options[] = {
    { (char *) "-u", (char *) "*undo", XrmoptionNoArg, (XtPointer) "true" },
    { (char *) "-mono", (char *) "*mono", XrmoptionNoArg, (XtPointer) "true" },
    { (char *) "-colours", (char *) "*colours", XrmoptionSepArg, (XtPointer) 0 }
};

/* What an app-defaults file would say, if there is none. */
static String fallback[] = {
    (char *) "XM2089.title: 2089",
    (char *) "XM2089.iconName: 2089",
    (char *) "XM2089*board.width: 400",
    (char *) "XM2089*board.height: 400",
    (char *) "XM2089*work.marginWidth: 8",
    (char *) "XM2089*work.marginHeight: 8",
    (char *) "XM2089*value.marginWidth: 6",
    (char *) "XM2089*value.fontList: -*-helvetica-bold-r-normal--*-140-*-*-p-*-iso8859-1",
    (char *) "XM2089*openGame.width: 440",
    (char *) "XM2089*saveGame.width: 440",
    (char *) "XM2089*pad.width: 108",
    (char *) "XM2089*pad.height: 108",
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

/* The original game's colours, as xboard allocated them, with bevels, on
   the window's own background. */
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
        j = i < 6 && i < n ? order[i] : i < X89_TILES - 1 || n == 0 ? active
            : order[n - 1];
        s->tile[i] = set[j].bg;
        s->text[i] = set[j].fg;
        s->tilets[i] = set[j].ts;
        s->tilebs[i] = set[j].bs;
    }
    s->ok = 1;
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
}

/* Place the board in the drawing area, a little in from the edges. */
static void place(void)
{
    int pad = (width < height ? width : height) / 40 + 2;

    x89_place(&brd, pad, pad, (int) width - 2 * pad, (int) height - 2 * pad, game.size);
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

/* ------------------------------------------------------------ leaving */

static void finish(void)
{
    sprintf(command, "%s %s%s", name, res.undo ? "-u " : "",
            scheme == MONO ? "-mono " : scheme == DESKTOP ? "-colours desktop " : "");
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

/* Play this game from now on (new, loaded or opened). */
static void start(const struct t89_game *g)
{
    f89_bsave();
    game = *g;
    canundo = 0;
    over = t89_over(&game);
    place();                            /* the size may have changed */
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

static void newgame(Widget w, XtPointer client, XtPointer call)
{
    struct t89_game g;

    (void) w;
    (void) client;
    (void) call;
    t89_new(&g, game.size, f89_seed(game.rng));
    start(&g);
}

static void asknewgame(void)
{
    if (!asknew) {
        asknew = XmCreateQuestionDialog(top, (char *) "newGame", (ArgList) 0, 0);
        title(asknew, "2089 - New Game");
        settext(asknew, XmNmessageString, "Start a new game?\nThis one will be lost.");
        settext(asknew, XmNokLabelString, "New Game");
        only(asknew, 1, 1, 0);
        XtAddCallback(asknew, XmNokCallback, newgame, (XtPointer) 0);
    }
    XtManageChild(asknew);
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

/* Games in files: a file holds a save string on its first line. */
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
    } else if ((f = fopen(file, "r")) == 0) {
        sprintf(text, "Can't open %.250s.", file);
        say(text);
    } else {
        if (!fgets(line, (int) sizeof line, f))
            line[0] = '\0';
        fclose(f);
        line[strcspn(line, "\r\n")] = '\0';
        if (t89_load(&g, line) < 0) {
            sprintf(text, "%.250s isn't a saved game.", file);
            say(text);
        } else
            start(&g);
    }
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
            "Click the arrow buttons, or type on the board:\n"
            "    arrows, w a s d, k h j l, 8 4 2 6     move\n"
            "    u    undo (if started with -u)\n"
            "    n    new game\n"
            "    p    save string\n"
            "    c    next colour scheme\n"
            "    q    exit, printing the command that carries on");
        only(keys, 1, 0, 0);
    }
    XtManageChild(keys);
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
    case F89_NEW:     asknewgame(); break;
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

enum {
    M_NEW, M_OPEN, M_SAVEAS, M_STRING, M_EXIT, M_UNDO,
    M_GAME, M_DESKTOP, M_MONO, M_KEYS, M_ABOUT,
    M_UP, M_DOWN, M_LEFT, M_RIGHT
};

static void menu(Widget w, XtPointer client, XtPointer call)
{
    int what = (int) (long) client;

    (void) w;
    switch (what) {
    case M_NEW:    asknewgame(); break;
    case M_OPEN:   filedialog(0); break;
    case M_SAVEAS: filedialog(1); break;
    case M_STRING: savestring(); break;
    case M_EXIT:   quit(); break;
    case M_UNDO:   doundo(); break;
    case M_GAME:
    case M_DESKTOP:
    case M_MONO:
        if (((XmToggleButtonCallbackStruct *) call)->set) {
            usescheme(what - M_GAME);
            draw();
        }
        break;
    case M_KEYS:   showkeys(); break;
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

/* An item: a push button, or a toggle if radio, with its key shown. */
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
        XtVaSetValues(w, XmNindicatorType, XmONE_OF_MANY, (char *) 0);
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
    Widget m, cascade;

    m = pulldown(bar, "File", 'F', &cascade);
    item(m, "New Game...", 'N', "n", M_NEW, 0);
    item(m, "Open...", 'O', 0, M_OPEN, 0);
    separator(m);
    item(m, "Save As...", 'A', 0, M_SAVEAS, 0);
    item(m, "Save String...", 'S', "p", M_STRING, 0);
    separator(m);
    item(m, "Exit", 'x', "q", M_EXIT, 0);

    m = pulldown(bar, "Edit", 'E', &cascade);
    undoitem = item(m, "Undo", 'U', "u", M_UNDO, 0);

    m = pulldown(bar, "Options", 'O', &cascade);
    XtVaSetValues(m, XmNradioBehavior, True, (char *) 0);
    schemeitem[GAME] = item(m, "Game Colours", 'G', "c", M_GAME, 1);
    schemeitem[DESKTOP] = item(m, "Desktop Colours", 'D', "c", M_DESKTOP, 1);
    schemeitem[MONO] = item(m, "Black and White", 'B', "c", M_MONO, 1);

    m = pulldown(bar, "Help", 'H', &cascade);
    item(m, "Keys...", 'K', "?", M_KEYS, 0);
    separator(m);
    item(m, "About 2089...", 'A', 0, M_ABOUT, 0);
    XtVaSetValues(bar, XmNmenuHelpWidget, cascade, (char *) 0);

    XtManageChild(bar);
    return bar;
}

/* ------------------------------------------------------------ the layout */

/* A sunken frame with a title, holding a right-aligned number: the nth of
   three across the top. */
static Widget scorebox(Widget parent, const char *wname, const char *text, int n)
{
    Widget f, t;

    f = XtVaCreateManagedWidget(wname, xmFrameWidgetClass, parent,
                                XmNshadowType, XmSHADOW_IN,
                                XmNtopAttachment, XmATTACH_FORM,
                                XmNbottomAttachment, XmATTACH_FORM,
                                XmNleftAttachment, XmATTACH_POSITION,
                                XmNleftPosition, 3 * n,
                                XmNleftOffset, n > 0 ? 4 : 0,
                                XmNrightAttachment, XmATTACH_POSITION,
                                XmNrightPosition, 3 * n + 3,
                                XmNrightOffset, n < 2 ? 4 : 0, (char *) 0);
    t = XtVaCreateManagedWidget("title", xmLabelWidgetClass, f,
                                XmNframeChildType, XmFRAME_TITLE_CHILD, (char *) 0);
    label(t, text);
    return XtVaCreateManagedWidget("value", xmLabelWidgetClass, f,
                                   XmNframeChildType, XmFRAME_WORKAREA_CHILD,
                                   XmNalignment, XmALIGNMENT_END,
                                   XmNrecomputeSize, False, (char *) 0);
}

/* An arrow button in the pad: a 3 x 3 grid of which it takes one square. */
static void arrow(Widget pad, const char *wname, int dir, int row, int col, int what)
{
    Widget a;

    a = XtVaCreateManagedWidget(wname, xmArrowButtonWidgetClass, pad,
                                XmNarrowDirection, dir,
                                XmNtraversalOn, False,
                                XmNtopAttachment, XmATTACH_POSITION, XmNtopPosition, row,
                                XmNbottomAttachment, XmATTACH_POSITION,
                                XmNbottomPosition, row + 1,
                                XmNleftAttachment, XmATTACH_POSITION, XmNleftPosition, col,
                                XmNrightAttachment, XmATTACH_POSITION,
                                XmNrightPosition, col + 1,
                                (char *) 0);
    XtAddCallback(a, XmNactivateCallback, menu, (XtPointer) (long) what);
}

static Widget button(Widget parent, const char *wname, const char *text, Widget above,
                     int offset, int what)
{
    Widget b;

    b = XtVaCreateManagedWidget(wname, xmPushButtonWidgetClass, parent,
                                XmNtraversalOn, False,
                                XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, above,
                                XmNtopOffset, offset,
                                XmNleftAttachment, XmATTACH_FORM,
                                XmNrightAttachment, XmATTACH_FORM, (char *) 0);
    label(b, text);
    XtAddCallback(b, XmNactivateCallback, menu, (XtPointer) (long) what);
    return b;
}

/*
 *  +-----------------------------------------+
 *  | File  Edit  Options                Help |
 *  +-----------------------------------------+
 *  | [Score     ] [Best      ] [Moves     ]  |
 *  | +---------------------------+   ^       |
 *  | |                           |  < >      |
 *  | |          board            |   v       |
 *  | |                           | [New Game]|
 *  | |                           | [ Undo   ]|
 *  | +---------------------------+           |
 *  +-----------------------------------------+
 *  | status                                  |
 *  +-----------------------------------------+
 */
static void layout(Widget mainw)
{
    Widget work, scores, frame, controls, pad, newb, status;

    menus(mainw);
    work = XtVaCreateManagedWidget("work", xmFormWidgetClass, mainw, (char *) 0);

    scores = XtVaCreateManagedWidget("scores", xmFormWidgetClass, work,
                                     XmNfractionBase, 9,
                                     XmNtopAttachment, XmATTACH_FORM,
                                     XmNleftAttachment, XmATTACH_FORM,
                                     XmNrightAttachment, XmATTACH_FORM, (char *) 0);
    score[0] = scorebox(scores, "score", "Score", 0);
    score[1] = scorebox(scores, "best", "Best", 1);
    score[2] = scorebox(scores, "moves", "Moves", 2);

    controls = XtVaCreateManagedWidget("controls", xmFormWidgetClass, work,
                                       XmNtopAttachment, XmATTACH_WIDGET,
                                       XmNtopWidget, scores, XmNtopOffset, 8,
                                       XmNrightAttachment, XmATTACH_FORM, (char *) 0);
    pad = XtVaCreateManagedWidget("pad", xmFormWidgetClass, controls,
                                  XmNfractionBase, 3,
                                  XmNtopAttachment, XmATTACH_FORM,
                                  XmNleftAttachment, XmATTACH_FORM,
                                  XmNrightAttachment, XmATTACH_FORM, (char *) 0);
    arrow(pad, "up", XmARROW_UP, 0, 1, M_UP);
    arrow(pad, "left", XmARROW_LEFT, 1, 0, M_LEFT);
    arrow(pad, "right", XmARROW_RIGHT, 1, 2, M_RIGHT);
    arrow(pad, "down", XmARROW_DOWN, 2, 1, M_DOWN);
    newb = button(controls, "newGame", "New Game", pad, 12, M_NEW);
    undobutton = button(controls, "undo", "Undo", newb, 6, M_UNDO);

    frame = XtVaCreateManagedWidget("boardFrame", xmFrameWidgetClass, work,
                                    XmNshadowType, XmSHADOW_IN,
                                    XmNtopAttachment, XmATTACH_WIDGET,
                                    XmNtopWidget, scores, XmNtopOffset, 8,
                                    XmNbottomAttachment, XmATTACH_FORM,
                                    XmNleftAttachment, XmATTACH_FORM,
                                    XmNrightAttachment, XmATTACH_WIDGET,
                                    XmNrightWidget, controls, XmNrightOffset, 10,
                                    (char *) 0);
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

/* ------------------------------------------------------------ main */

static void usage(int status)
{
    printf("usage: %s [X options] [-mono] [-colours game|desktop|mono] [-u] [-s size]\n", name);
    puts("              [-r seed] [-l savestring] [-v] [-h]");
    puts("  X options      -display, -geometry, -xrm and the rest");
    puts("  -colours       the board's colours: the game's (the default), the");
    puts("                 desktop palette's (under CDE), or black and white");
    puts("  -mono          black and white, even on a colour display");
    f89_usage();
    puts("  -v             print the version");
    exit(status);
}

int main(int argc, char **argv)
{
    struct f89_options o;
    Widget mainw;
    Pixel page;
    Atom wmdelete;
    int i;

    name = strrchr(argv[0], '/') ? strrchr(argv[0], '/') + 1 : argv[0];
    top = XtVaAppInitialize(&app, CLASS, options, XtNumber(options), &argc, argv,
                            fallback, (char *) 0);
    XtGetApplicationResources(top, (XtPointer) &res, resources, XtNumber(resources),
                              (ArgList) 0, 0);
    dpy = XtDisplay(top);

    /* What Xt left: ours. */
    f89_defaults(&o);
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0)
            usage(EXIT_SUCCESS);
        if (strcmp(argv[i], "-v") == 0) {
            printf("2089 %s (Motif %d.%d)\n", T89_VERSION, XmVERSION, XmREVISION);
            return EXIT_SUCCESS;
        }
        if (f89_option(argc, argv, &i, &o) <= 0)
            usage(EXIT_FAILURE);
    }
    if (o.undo)
        res.undo = True;
    if (f89_start(&game, &o, name) < 0)
        return EXIT_FAILURE;

    if (x89_open(&brd, dpy, DefaultScreen(dpy), RootWindow(dpy, DefaultScreen(dpy))) < 0) {
        fprintf(stderr, "%s: the server has no fonts I can use (not even fixed)\n", name);
        return EXIT_FAILURE;
    }

    mainw = XtVaCreateManagedWidget("main", xmMainWindowWidgetClass, top, (char *) 0);
    layout(mainw);

    XtVaGetValues(board, XmNbackground, &page, (char *) 0);
    gamecolours(page);
    desktopcolours();
    for (i = 0; i < 2; i++)
        XtSetSensitive(schemeitem[i], (Boolean) schemes[i].ok);
    usescheme(res.mono || strcmp(res.colours, "mono") == 0 ? MONO
              : strcmp(res.colours, "desktop") == 0 ? DESKTOP : GAME);

    XtVaSetValues(top, XmNdeleteResponse, XmDO_NOTHING, (char *) 0);
    wmdelete = XmInternAtom(dpy, (char *) "WM_DELETE_WINDOW", False);
    XmAddWMProtocolCallback(top, wmdelete, menu, (XtPointer) M_EXIT);
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
    else
        say("");
    XtRealizeWidget(top);
    resize();
    draw();
    XmProcessTraversal(board, XmTRAVERSE_CURRENT);
    XtAppMainLoop(app);
    return EXIT_SUCCESS;
}
