/*
 * motif.c - 2089 for the X Window System, with Motif (xm2089).
 *
 * The same game as x2089 (xlib/xlib.c), with the same board (../xboard.c),
 * dressed as a Motif application: a main window with a menu bar, the board
 * in a drawing area, the score above it and messages below, and dialogs for
 * the questions.  Under CDE the widgets take the session's colours and
 * fonts; the board keeps the original game's.
 *
 *   Game   New Game, Undo, Save String (to copy), Load Game (a save string
 *          pasted in), Quit
 *   View   Colour or Black and White
 *   Help   Keys, About
 *
 * The keys are the other frontends' (arrows, w a s d, h j k l, 8 4 2 6, u
 * n p c q ?), typed on the board; the menus show them.  Undo is off unless
 * -u (or the resource undo: true).  Quitting prints the command that
 * carries on the game, and the best scores are kept in ~/.2089.
 *
 * Xt takes the usual X options (-display, -geometry, -fg, -bg, -xrm, ...)
 * and resources are read the X way: an app-defaults file for class XM2089
 * if one is installed, ~/.Xdefaults or xrdb.  Fallback resources compiled
 * in cover what an app-defaults file would.
 *
 * Author: Claude Opus 5.5 (claude-opus-5-5), October 2026.
 * MIT License; see LICENSE.
 */

#include <Xm/Xm.h>
#include <Xm/CascadeB.h>
#include <Xm/DrawingA.h>
#include <Xm/Form.h>
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
static Widget top, board, score, message, undoitem, colouritem, monoitem;
static Widget asknew, askload, showsave, keys, about;   /* dialogs, made when needed */
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
};
static struct settings res;

static XtResource resources[] = {
    { (char *) "undo", (char *) "Undo", XmRBoolean, sizeof(Boolean),
      XtOffsetOf(struct settings, undo), XmRImmediate, (XtPointer) False },
    { (char *) "mono", (char *) "Mono", XmRBoolean, sizeof(Boolean),
      XtOffsetOf(struct settings, mono), XmRImmediate, (XtPointer) False }
};

static XrmOptionDescRec options[] = {
    { (char *) "-u", (char *) "*undo", XrmoptionNoArg, (XtPointer) "true" },
    { (char *) "-mono", (char *) "*mono", XrmoptionNoArg, (XtPointer) "true" }
};

/* What an app-defaults file would say, if there is none. */
static String fallback[] = {
    (char *) "XM2089.title: 2089",
    (char *) "XM2089.iconName: 2089",
    (char *) "XM2089*board.width: 400",
    (char *) "XM2089*board.height: 400",
    0
};

/* ------------------------------------------------------------ the window */

/* Set a label's text. */
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
    say(res.undo ? "No moves left: Undo, New Game or Quit."
                 : "No moves left: New Game or Quit.");
}

/* The board's window: the pixmap, the board on it, then copy it over. */
static void draw(void)
{
    char s[96];

    if (!buf)
        return;
    XSetForeground(dpy, brd.gc, brd.colour ? brd.ui[X89_PAGE] : brd.white);
    XFillRectangle(dpy, buf, brd.gc, 0, 0, width, height);
    x89_draw(&brd, buf, &game);
    if (XtIsRealized(board))
        XCopyArea(dpy, buf, XtWindow(board), brd.gc, 0, 0, width, height, 0, 0);
    f89_bnote(&game);
    sprintf(s, "Score %lu      Best %lu      Moves %lu", game.score,
            f89_btop(game.size), game.moves);
    label(score, s);
    XtSetSensitive(undoitem, (Boolean) (res.undo && canundo));
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

/* ------------------------------------------------------------ dialogs */

/* A dialog with only the buttons named (XmDIALOG_OK_BUTTON ...) kept. */
static void only(Widget d, int ok, int cancel, int help)
{
    if (!ok)
        XtUnmanageChild(XmMessageBoxGetChild(d, XmDIALOG_OK_BUTTON));
    if (!cancel)
        XtUnmanageChild(XmMessageBoxGetChild(d, XmDIALOG_CANCEL_BUTTON));
    if (!help)
        XtUnmanageChild(XmMessageBoxGetChild(d, XmDIALOG_HELP_BUTTON));
}

static void message_text(Widget d, const char *text)
{
    XmString s = XmStringCreateLtoR((char *) text, XmFONTLIST_DEFAULT_TAG);

    XtVaSetValues(d, XmNmessageString, s, (char *) 0);
    XmStringFree(s);
}

static void newgame(Widget w, XtPointer client, XtPointer call)
{
    (void) w;
    (void) client;
    (void) call;
    f89_bsave();
    t89_new(&game, game.size, f89_seed(game.rng));
    canundo = over = 0;
    say("");
    draw();
}

static void asknewgame(void)
{
    if (!asknew) {
        asknew = XmCreateQuestionDialog(top, (char *) "newGame", (ArgList) 0, 0);
        XtVaSetValues(XtParent(asknew), XmNtitle, "2089: New Game", (char *) 0);
        message_text(asknew, "Start a new game?");
        only(asknew, 1, 1, 0);
        XtAddCallback(asknew, XmNokCallback, newgame, (XtPointer) 0);
    }
    XtManageChild(asknew);
}

/* The save string, in a text field it can be copied from. */
static void savestring(void)
{
    char save[T89_SAVELEN];
    XmString s;

    t89_save(&game, save, sizeof save);
    printf("save string: %s\n", save);  /* and where it can be copied */
    fflush(stdout);
    if (!showsave) {
        showsave = XmCreatePromptDialog(top, (char *) "saveString", (ArgList) 0, 0);
        XtVaSetValues(XtParent(showsave), XmNtitle, "2089: Save String", (char *) 0);
        s = XmStringCreateLocalized((char *) "This game, to carry on later with Load Game or -l:");
        XtVaSetValues(showsave, XmNselectionLabelString, s, (char *) 0);
        XmStringFree(s);
        XtUnmanageChild(XmSelectionBoxGetChild(showsave, XmDIALOG_CANCEL_BUTTON));
        XtUnmanageChild(XmSelectionBoxGetChild(showsave, XmDIALOG_HELP_BUTTON));
    }
    s = XmStringCreateLocalized(save);
    XtVaSetValues(showsave, XmNtextString, s, (char *) 0);
    XmStringFree(s);
    XtManageChild(showsave);
}

static void loaded(Widget w, XtPointer client, XtPointer call)
{
    XmSelectionBoxCallbackStruct *cb = (XmSelectionBoxCallbackStruct *) call;
    struct t89_game g;
    char *text = 0;

    (void) w;
    (void) client;
    if (!XmStringGetLtoR(cb->value, XmFONTLIST_DEFAULT_TAG, &text) || !text)
        return;
    if (t89_load(&g, text) < 0)
        say("That isn't a save string.");
    else {
        f89_bsave();
        game = g;
        canundo = 0;
        over = t89_over(&game);
        place();                        /* the size may have changed */
        if (over)
            nomoves();
        else
            say("");
        draw();
    }
    XtFree(text);
}

static void loadgame(void)
{
    XmString s;

    if (!askload) {
        askload = XmCreatePromptDialog(top, (char *) "loadGame", (ArgList) 0, 0);
        XtVaSetValues(XtParent(askload), XmNtitle, "2089: Load Game", (char *) 0);
        s = XmStringCreateLocalized((char *) "A save string (from Save String or 2089 -l):");
        XtVaSetValues(askload, XmNselectionLabelString, s, (char *) 0);
        XmStringFree(s);
        XtUnmanageChild(XmSelectionBoxGetChild(askload, XmDIALOG_HELP_BUTTON));
        XtAddCallback(askload, XmNokCallback, loaded, (XtPointer) 0);
    }
    s = XmStringCreateLocalized((char *) "");
    XtVaSetValues(askload, XmNtextString, s, (char *) 0);
    XmStringFree(s);
    XtManageChild(askload);
}

static void showkeys(void)
{
    if (!keys) {
        keys = XmCreateInformationDialog(top, (char *) "keys", (ArgList) 0, 0);
        XtVaSetValues(XtParent(keys), XmNtitle, "2089: Keys", (char *) 0);
        message_text(keys,
            "Slide the tiles; two equal tiles that meet join into one.\n"
            "Reach 2048 to win (you can keep going), until no move is left.\n"
            "\n"
            "Type on the board:\n"
            "    arrows, w a s d, k h j l, 8 4 2 6     move\n"
            "    u    undo (if started with -u)\n"
            "    n    new game\n"
            "    p    save string\n"
            "    c    colour or black and white\n"
            "    q    quit, printing the command that carries on");
        only(keys, 1, 0, 0);
    }
    XtManageChild(keys);
}

static void showabout(void)
{
    char text[256];

    if (!about) {
        about = XmCreateInformationDialog(top, (char *) "about", (ArgList) 0, 0);
        XtVaSetValues(XtParent(about), XmNtitle, "2089: About", (char *) 0);
        sprintf(text, "2089 %s, with Motif %d.%d\n\n"
                "2048 in C89, after Gabriele Cirulli's game.\n"
                "https://github.com/nfinit/2089", T89_VERSION, XmVERSION, XmREVISION);
        message_text(about, text);
        only(about, 1, 0, 0);
    }
    XtManageChild(about);
}

/* ------------------------------------------------------------ playing */

static void setcolour(int on)
{
    brd.colour = brd.hascolour && on;
    XmToggleButtonSetState(colouritem, (Boolean) brd.colour, False);
    XmToggleButtonSetState(monoitem, (Boolean) !brd.colour, False);
    XtVaSetValues(board, XmNbackground, brd.colour ? brd.ui[X89_PAGE] : brd.white,
                  (char *) 0);
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
    case F89_DISPLAY: setcolour(!brd.colour); break;
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

enum { M_NEW, M_UNDO, M_SAVE, M_LOAD, M_QUIT, M_COLOUR, M_MONO, M_KEYS, M_ABOUT };

static void menu(Widget w, XtPointer client, XtPointer call)
{
    (void) w;
    switch ((int) (long) client) {
    case M_NEW:    asknewgame(); break;
    case M_UNDO:   doundo(); break;
    case M_SAVE:   savestring(); break;
    case M_LOAD:   loadgame(); break;
    case M_QUIT:   quit(); break;
    case M_COLOUR:
    case M_MONO:
        if (((XmToggleButtonCallbackStruct *) call)->set)
            setcolour((int) (long) client == M_COLOUR);
        break;
    case M_KEYS:   showkeys(); break;
    case M_ABOUT:  showabout(); break;
    }
}

/* A pulldown menu and its cascade button in the menu bar. */
static Widget pulldown(Widget bar, const char *title, int mnemonic, Widget *cascade)
{
    Widget m = XmCreatePulldownMenu(bar, (char *) title, (ArgList) 0, 0);

    *cascade = XtVaCreateManagedWidget(title, xmCascadeButtonWidgetClass, bar,
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

static Widget menus(Widget parent)
{
    Widget bar = XmCreateMenuBar(parent, (char *) "menuBar", (ArgList) 0, 0);
    Widget m, help;

    m = pulldown(bar, "Game", 'G', &help);
    item(m, "New Game...", 'N', "n", M_NEW, 0);
    undoitem = item(m, "Undo", 'U', "u", M_UNDO, 0);
    XtVaCreateManagedWidget("sep", xmSeparatorWidgetClass, m, (char *) 0);
    item(m, "Save String...", 'S', "p", M_SAVE, 0);
    item(m, "Load Game...", 'L', 0, M_LOAD, 0);
    XtVaCreateManagedWidget("sep", xmSeparatorWidgetClass, m, (char *) 0);
    item(m, "Quit", 'Q', "q", M_QUIT, 0);

    m = pulldown(bar, "View", 'V', &help);
    XtVaSetValues(m, XmNradioBehavior, True, (char *) 0);
    colouritem = item(m, "Colour", 'C', "c", M_COLOUR, 1);
    monoitem = item(m, "Black and White", 'B', "c", M_MONO, 1);

    m = pulldown(bar, "Help", 'H', &help);
    item(m, "Keys...", 'K', "?", M_KEYS, 0);
    item(m, "About 2089...", 'A', 0, M_ABOUT, 0);
    XtVaSetValues(bar, XmNmenuHelpWidget, help, (char *) 0);

    XtManageChild(bar);
    return bar;
}

/* ------------------------------------------------------------ main */

static void usage(int status)
{
    printf("usage: %s [X options] [-mono] [-u] [-s size] [-r seed] [-l savestring]\n", name);
    puts("              [-v] [-h]");
    puts("  X options      -display, -geometry, -bg, -fg, -xrm and the rest");
    puts("  -mono          black and white, even on a colour display");
    f89_usage();
    puts("  -v             print the version");
    exit(status);
}

int main(int argc, char **argv)
{
    struct f89_options o;
    Widget mainw, form;
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
    sprintf(command, "%s %s%s", name, res.undo ? "-u " : "", res.mono ? "-mono " : "");

    if (x89_open(&brd, dpy, DefaultScreen(dpy), RootWindow(dpy, DefaultScreen(dpy))) < 0) {
        fprintf(stderr, "%s: the server has no fonts I can use (not even fixed)\n", name);
        return EXIT_FAILURE;
    }
    brd.colour = brd.hascolour && !res.mono;

    mainw = XtVaCreateManagedWidget("main", xmMainWindowWidgetClass, top, (char *) 0);
    menus(mainw);
    form = XtVaCreateManagedWidget("form", xmFormWidgetClass, mainw,
                                   XmNmarginWidth, 6, XmNmarginHeight, 4, (char *) 0);
    score = XtVaCreateManagedWidget("score", xmLabelWidgetClass, form,
                                     XmNtopAttachment, XmATTACH_FORM,
                                     XmNleftAttachment, XmATTACH_FORM,
                                     XmNrightAttachment, XmATTACH_FORM, (char *) 0);
    message = XtVaCreateManagedWidget("message", xmLabelWidgetClass, form,
                                      XmNbottomAttachment, XmATTACH_FORM,
                                      XmNleftAttachment, XmATTACH_FORM,
                                      XmNrightAttachment, XmATTACH_FORM, (char *) 0);
    board = XtVaCreateManagedWidget("board", xmDrawingAreaWidgetClass, form,
                                    XmNtopAttachment, XmATTACH_WIDGET,
                                    XmNtopWidget, score,
                                    XmNbottomAttachment, XmATTACH_WIDGET,
                                    XmNbottomWidget, message,
                                    XmNleftAttachment, XmATTACH_FORM,
                                    XmNrightAttachment, XmATTACH_FORM,
                                    XmNbackground, brd.colour ? brd.ui[X89_PAGE] : brd.white,
                                    XmNtraversalOn, True,
                                    (char *) 0);
    XtAddCallback(board, XmNexposeCallback, exposed, (XtPointer) 0);
    XtAddCallback(board, XmNresizeCallback, resized, (XtPointer) 0);
    XtAddCallback(board, XmNinputCallback, input, (XtPointer) 0);
    XtAddEventHandler(board, ButtonPressMask, False, clicked, (XtPointer) 0);
    XtVaSetValues(mainw, XmNworkWindow, form, (char *) 0);

    XmToggleButtonSetState(colouritem, (Boolean) brd.colour, False);
    XmToggleButtonSetState(monoitem, (Boolean) !brd.colour, False);
    XtSetSensitive(colouritem, (Boolean) brd.hascolour);
    XtSetSensitive(monoitem, (Boolean) brd.hascolour);

    XtVaSetValues(top, XmNdeleteResponse, XmDO_NOTHING, (char *) 0);
    wmdelete = XmInternAtom(dpy, (char *) "WM_DELETE_WINDOW", False);
    XmAddWMProtocolCallback(top, wmdelete, menu, (XtPointer) M_QUIT);
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
