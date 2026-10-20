/*
 * curses.c - 2089 for full-screen terminals, through curses.
 *
 * Every key is a move, as in the original game.  Curses reads terminfo for
 * the terminal's codes, so this works on a VT52 or a Wyse as well as an
 * xterm; it uses only the X/Open curses calls that SVR4 curses (AIX, Solaris,
 * HP-UX, IRIX), ncurses, NetBSD curses and PDCurses all have.
 *
 * Three display styles, chosen from what the terminal can do (c switches):
 *   colour  tiles on coloured backgrounds, where the terminal has colour
 *   mono    tiles in bold and reverse video
 *   plain   numbers only, no video attributes at all: for terminals whose
 *           attributes take up a screen cell (Wyse 50 "magic cookies"),
 *           where reverse video would shift the board; chosen for them
 *           automatically, and -m asks for mono anyway
 * Box lines use the terminal's line-drawing set when terminfo has one, and
 * + - | otherwise (or always, with -a).
 *
 * This is full-screen mode: f89_screen, called by main.c when f89_fsok
 * says the terminal can do it.
 *
 * Rules shared with line mode: undo is off unless -u; quitting
 * prints the command to carry on later.  No input is ever thrown away (keys
 * typed ahead are played in order), nothing waits on a timer, and a lone ESC
 * means nothing, since curses can't tell it from the start of an arrow key.
 *
 *
 * Author: Claude Opus 5.5 (claude-opus-5-5), October 2026.
 * MIT License; see LICENSE.
 */

/* Curses is an X/Open interface, not ISO C: ask for X/Open, or a strict C89
   compiler (xlc's c89) hides what <curses.h> needs (termios, va_list). */
#define _XOPEN_SOURCE

#include <curses.h>
#include <term.h>                       /* magic_cookie_glitch */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>                     /* isatty */
#include "common.h"

#define PLAIN  F89_PLAIN
#define MONO   F89_MONO
#define COLOUR F89_COLOUR

static struct t89_game game;            /* the game, for the signal handler */
static char command[256];               /* how to restart: program and options */
static int ascii = 0, style = PLAIN, maxstyle = PLAIN;
static int hidden = 0;                  /* 1 once the cursor has been hidden */
static int cells = F89_CELLS_AUTO;      /* cell height asked for (-k, -K) */

/* The board's place and shape on the screen. */
static int top, left, cellw, cellh;     /* cell inner width and height */
static int below;                       /* the line under the board (messages) */

/* ------------------------------------------------------------ high scores */

/*
 * The best score for each board size, as the original game shows it ("best"
 * next to the score).  Kept for the user in $HOME/.2089, or in the file
 * T89_SCORES names at compile time: one save string per line, the game as it
 * stood at its best score, so that a score is only what t89_load accepts
 * (and the game can be looked at again with -l).  Lines that don't load are
 * skipped.  Without a file, scores are kept for the session only.  Several
 * players sharing one file would need setgid and locking: not done here.
 */
static char scorefile[256];             /* "" if there is none */
static char record[T89_MAXSIZE + 1][T89_SAVELEN];
static unsigned long recscore[T89_MAXSIZE + 1];
static int dirty = 0;                   /* 1 if a record is not in the file */

/* Read the file, keeping each record that beats the one held. */
static void readscores(void)
{
    struct t89_game t;
    char buf[T89_SAVELEN + 2];
    FILE *f;
    char *nl;

    if (!scorefile[0] || (f = fopen(scorefile, "r")) == 0)
        return;
    while (fgets(buf, sizeof buf, f)) {
        if ((nl = strchr(buf, '\n')) != 0)
            *nl = '\0';
        if (t89_load(&t, buf) == 0 && t.score > recscore[t.size]) {
            recscore[t.size] = t.score;
            strcpy(record[t.size], buf);
        }
    }
    fclose(f);
}

static void openscores(void)
{
#ifdef T89_SCORES
    strcpy(scorefile, T89_SCORES);
#else
    const char *home = getenv("HOME");

    if (home && *home && strlen(home) < sizeof scorefile - 8)
        sprintf(scorefile, "%s/.2089", home);
#endif
    readscores();
}

/* Write the records, after merging what another 2089 may have written
   since.  Returns -1 if the file can't be written. */
static int writescores(void)
{
    FILE *f;
    int n, ok;

    if (!dirty || !scorefile[0])
        return 0;
    readscores();
    if ((f = fopen(scorefile, "w")) == 0)
        return -1;
    for (n = T89_MINSIZE; n <= T89_MAXSIZE; n++)
        if (recscore[n] > 0)
            fprintf(f, "%s\n", record[n]);
    ok = fclose(f) == 0;
    if (ok)
        dirty = 0;
    return ok ? 0 : -1;
}

/* Take the game as a record if it beats the one for its size. */
static void note(void)
{
    if (game.score > recscore[game.size]
        && t89_save(&game, record[game.size], T89_SAVELEN) > 0) {
        recscore[game.size] = game.score;
        dirty = 1;
    }
}

/* Leave the screen as it was and say how to carry on. */
static void finish(void)
{
    move(below + 1, 0);                 /* the board stays; the hint goes */
    clrtoeol();
    refresh();
    if (hidden)
        curs_set(1);
    endwin();
    if (writescores() < 0)
        fprintf(stderr, "2089: can't save the high score in %s\n", scorefile);
    f89_resume(&game, command);
}

/* On a signal (interrupt, hangup), restore the terminal before leaving.
   endwin isn't promised to be safe here, but it is what curses programs
   have always done, and the alternative is a terminal left in raw mode.
   The scores are saved first, and further signals ignored: after a hangup
   the terminal is gone, and SVR4's signal() (AIX) resets the handler, so a
   second signal would end the program in the middle of endwin. */
static void caught(int sig)
{
    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);
#ifdef SIGHUP
    signal(SIGHUP, SIG_IGN);
#endif
    (void) sig;
    writescores();
    finish();
    exit(EXIT_FAILURE);
}

/* ------------------------------------------------------------ layout */

/*
 * Fit the board to the screen: cells 7 wide and 3 high if there's room,
 * smaller if not.  Lines needed: the status, the board, a message line and
 * the hint (both right under the board, so a compact board leaves the rest
 * of the screen free), plus a blank line under the status if there's room
 * (so a compact 4x4 board fits 12 lines, a VT50).  Cells are one line high
 * with -k, and by default on a line of 4800 baud or less in colour or mono,
 * where each move sends about a third as much (226 bytes against 619,
 * measured).  Returns -1 if even the smallest board won't fit.
 */
static int layout(void)
{
    static const int shapes[][2] = { {7, 3}, {6, 1}, {5, 1}, {4, 1} };
    int i, gap, n = game.size, w, h, small;

    small = cells == F89_CELLS_SMALL
        || (cells == F89_CELLS_AUTO && style != PLAIN && baudrate() <= 4800);
    for (i = small ? 1 : 0; i < 4; i++) {
        cellw = shapes[i][0];
        cellh = shapes[i][1];
        w = n * (cellw + 1) + 1;
        h = n * (cellh + 1) + 1;
        for (gap = 1; gap >= 0; gap--)
            if (w <= COLS && h + 3 + gap <= LINES) {
                top = 1 + gap;
                left = (COLS - w) / 2;
                below = top + h;
                return 0;
            }
    }
    return -1;
}

/* The line-drawing character, or its ASCII stand-in. */
static chtype line(chtype acs, int plainch)
{
    return ascii ? (chtype) plainch : acs;
}

static void grid(void)
{
    int n = game.size, r, c, i, y, x;
    chtype ch1;

    for (r = 0; r <= n; r++) {
        y = top + r * (cellh + 1);
        for (c = 0; c <= n; c++) {
            x = left + c * (cellw + 1);
            if (r == 0)
                ch1 = c == 0 ? line(ACS_ULCORNER, '+') : c == n ? line(ACS_URCORNER, '+')
                    : line(ACS_TTEE, '+');
            else if (r == n)
                ch1 = c == 0 ? line(ACS_LLCORNER, '+') : c == n ? line(ACS_LRCORNER, '+')
                    : line(ACS_BTEE, '+');
            else
                ch1 = c == 0 ? line(ACS_LTEE, '+') : c == n ? line(ACS_RTEE, '+')
                    : line(ACS_PLUS, '+');
            mvaddch(y, x, ch1);
            if (c < n)
                for (i = 1; i <= cellw; i++)
                    mvaddch(y, x + i, line(ACS_HLINE, '-'));
            if (r < n)
                for (i = 1; i <= cellh; i++)
                    mvaddch(y + i, x, line(ACS_VLINE, '|'));
        }
    }
}

/* ------------------------------------------------------------ tiles */

/* Video attributes for a tile in the current style. */
/*
 * Tile colours, after the original game's (Cirulli's style/main.css), one
 * row per tile from 2 to 2048 and then all larger ("super") tiles, which
 * the original also draws alike.  Each entry is { background, text, bold }.
 * With 256 colours, the nearest xterm-256 colour to each (by CIE76 distance;
 * the gold tiles share, as they nearly do in the original); with 16 or 8,
 * the original's families (beige, orange, red, gold, dark) in ANSI colours.
 * Empty cells and the board keep the terminal's own background.
 */
#define TILES 12
static const short pal256[TILES][3] = {
    {254, 242, 0}, {187, 242, 0}, {216, 255, 0}, {209, 255, 0}, {209, 255, 0},
    {203, 255, 0}, {222, 255, 0}, {221, 255, 0}, {221, 255, 0}, {221, 255, 0},
    {178, 255, 0}, {237, 255, 0}
};
static const short pal16[TILES][3] = {
    {15, 0, 0}, {7, 0, 0}, {3, 0, 0}, {9, 15, 0}, {1, 15, 0}, {1, 15, 1},
    {11, 0, 0}, {11, 0, 0}, {11, 0, 0}, {11, 0, 0}, {11, 0, 1}, {0, 15, 0}
};
static const short pal8[TILES][3] = {
    {COLOR_WHITE, COLOR_BLACK, 0}, {COLOR_WHITE, COLOR_BLACK, 0},
    {COLOR_YELLOW, COLOR_BLACK, 0}, {COLOR_YELLOW, COLOR_BLACK, 0},
    {COLOR_RED, COLOR_WHITE, 0}, {COLOR_RED, COLOR_WHITE, 1},
    {COLOR_YELLOW, COLOR_BLACK, 0}, {COLOR_YELLOW, COLOR_BLACK, 0},
    {COLOR_YELLOW, COLOR_BLACK, 0}, {COLOR_YELLOW, COLOR_BLACK, 0},
    {COLOR_YELLOW, COLOR_BLACK, 1}, {COLOR_BLACK, COLOR_WHITE, 0}
};
static const short (*palette)[3] = pal8;    /* the one in use */
static short tilepair[TILES];               /* its colour pair for each tile */

static chtype look(int exp)
{
    int t = exp > TILES ? TILES - 1 : exp - 1;

    if (exp == 0 || style == PLAIN)
        return A_NORMAL;
    if (style == COLOUR)
        return (chtype) COLOR_PAIR(tilepair[t]) | (palette[t][2] ? A_BOLD : A_NORMAL);
    if (exp <= 2)
        return A_NORMAL;
    if (exp <= 6)
        return A_BOLD;
    if (exp <= 10)
        return A_REVERSE;
    return A_REVERSE | A_BOLD;
}

static void tile(int r, int c)
{
    char text[16];
    int exp = game.cell[r * game.size + c], y, x, i, j, len;

    f89_tile(text, exp);
    if ((int) strlen(text) > cellw)            /* too wide: 2^n instead */
        sprintf(text, "2^%d", exp);
    len = (int) strlen(text);
    y = top + r * (cellh + 1) + 1;
    x = left + c * (cellw + 1) + 1;
    attrset((int) look(exp));  /* X/Open attrset takes an int */
    for (i = 0; i < cellh; i++)
        for (j = 0; j < cellw; j++)
            mvaddch(y + i, x + j, ' ');
    mvaddstr(y + cellh / 2, x + (cellw - len + 1) / 2, text);
    attrset(A_NORMAL);
}

/* ------------------------------------------------------------ screen */

/* Text at (y, x) on a line of its own: the whole line is cleared first, so
   that nothing of a longer text before it is left (a centred text clears
   from the margin, not from where it starts).  Cut to fit the screen; the
   last column is never used, since writing there on the bottom line
   scrolls some terminals. */
static void put(int y, int x, const char *text)
{
    int room = COLS - 1 - x;

    if (y < 0 || y >= LINES)
        return;
    move(y, 0);
    clrtoeol();
    if (room > 0)
        mvaddnstr(y, x, text, room);
}

/* The same, centred. */
static void centre(int y, const char *text)
{
    int x = (COLS - 1 - (int) strlen(text)) / 2;

    put(y, x > 0 ? x : 0, text);
}

static void status(void)
{
    char text[80];

    note();
    sprintf(text, "2089   score %lu   best %lu   moves %lu", game.score,
            recscore[game.size], game.moves);
    if ((int) strlen(text) >= COLS)     /* narrow screen: close up */
        sprintf(text, "score %lu best %lu moves %lu", game.score,
                recscore[game.size], game.moves);
    centre(0, text);
}

/* A message under the board (empty to clear it). */
static void message(const char *text)
{
    centre(below, text);
}

static void nomoves(int undo)
{
    message(undo ? "No moves left.  u to undo, n for a new game, q to quit."
                 : "No moves left.  n for a new game, q to quit.");
}

/* The keys, at the bottom.  "c style" only where there's a style to switch to. */
static void hint(int undo)
{
    char text[80];

    sprintf(text, "arrows/wasd move  %sn new  p save  %s? help  q quit",
            undo ? "u undo  " : "", maxstyle > PLAIN ? "c style  " : "");
    centre(below + 1, text);            /* under the message line */
}

/*
 * Send what's been drawn, then put the terminal back in normal video.
 *
 * Curses leaves the terminal in whatever video attributes it used last (a
 * tile's colour, after a board update) and then erases lines (ESC [ K) or
 * deletes characters without resetting them, trusting terminfo that erased
 * cells come out blank.  Many terminals fill them with the current background
 * instead ("back colour erase"; PowerTerm does, with TERM=screen), which
 * left streaks of tile colour on the status and message lines.  vidattr
 * changes the terminal's attributes and curses' record of them together.
 *
 * The cursor is parked in the bottom left corner (blank: the hint is right
 * under the board) rather than left wherever the last tile was drawn; where the terminal can
 * hide it (civis), it is hidden as well.
 */
static void settle(void)
{
    move(LINES - 1, 0);                 /* park the cursor out of the way */
    refresh();
    vidattr(A_NORMAL);
    fflush(stdout);
}

static void draw(int undo)
{
    int r, c;

    settle();                           /* erase in normal video */
    erase();
    status();
    grid();
    for (r = 0; r < game.size; r++)
        for (c = 0; c < game.size; c++)
            tile(r, c);
    hint(undo);
    settle();
}

/* Only what a move can change: tiles and the status line. */
static void update(void)
{
    int r, c;

    status();
    for (r = 0; r < game.size; r++)
        for (c = 0; c < game.size; c++)
            tile(r, c);
    settle();
}

static void help(int undo)
{
    static const char *text[] = {
        "Slide the tiles; two equal tiles that meet join into one.",
        "Reach 2048 to win (you can keep going), until no move is left.",
        "",
        "  arrow keys, w a s d, k h j l, 8 4 2 6   move",
        "  n   new game (asks first)       p   show the save string",
        0
    };
    int i, y = 1, x = COLS > 72 ? (COLS - 70) / 2 : 1;

    erase();
    for (i = 0; text[i]; i++)
        put(y++, x, text[i]);
    put(y++, x, maxstyle > PLAIN ? "  c   next display style          ^L  redraw the screen"
                                 : "  ^L  redraw the screen");
    put(y++, x, "  q   quit, showing the command that carries on this game");
    put(y + 1, x, undo ? "  u   undo the last move" : "  (undo is off: start with -u to allow it)");
    put(y + 3, x, "Press any key to go back to the game.");
    refresh();
    getch();
    draw(undo);
}

/* The next style the terminal can show, after the current one. */
static int nextstyle(void)
{
    return style >= maxstyle ? PLAIN : style + 1;
}

/* ------------------------------------------------------------ input */

/*
 * A key as a command.  Curses decodes arrow keys from terminfo; if the
 * terminal sends a sequence terminfo doesn't know (a VT100 in the other
 * cursor key mode, say), it arrives as ESC and plain bytes, and the shared
 * parser reads it instead, so its letters aren't taken as moves.
 */
static int key(int k)
{
    char seq[16];
    int n = 0, c, len;

    switch (k) {
    case KEY_UP:    return F89_UP;
    case KEY_DOWN:  return F89_DOWN;
    case KEY_RIGHT: return F89_RIGHT;
    case KEY_LEFT:  return F89_LEFT;
    default:
        if (k < 0 || k > 255)
            return 0;
        if (k != '\033')
            return f89_key(k);
        /* An ESC sequence that terminfo didn't know: read the rest.  (8-bit
           CSI and SS3 aren't read here: a terminal in 8-bit mode needs an
           8-bit TERM, such as vt320-8, and then curses decodes them.) */
        seq[n++] = (char) k;
        seq[n] = '\0';
        timeout(500);                   /* the rest is already on its way */
        while (n < (int) sizeof seq - 1 && f89_cslen(seq) == 0
               && (c = getch()) != ERR) {
            if (c > 255 || c == 0) {    /* a key curses decoded, or NUL */
                ungetch(c);
                break;
            }
            seq[n++] = (char) c;
            seq[n] = '\0';
        }
        timeout(-1);
        k = f89_escape(seq, &len);
        if (len < n)                    /* it ended before the last byte */
            ungetch((unsigned char) seq[n - 1]);
        return k;
    }
}

/* Ask a yes/no question under the board. */
static int ask(const char *question)
{
    int k;

    message(question);
    refresh();
    k = getch();
    message("");
    return k == 'y' || k == 'Y';
}

/* ------------------------------------------------------------ main */

/*
 * Does the terminal store attributes in screen cells?  terminfo says so with
 * xmc, but some entries leave it out: AIX 4.3's wy50 does, though a real
 * Wyse 50 uses "magic cookies" (ncurses' wy50 has xmc#1).  So the Wyse 50
 * family counts too, by name.
 */
static int cookies(void)
{
    const char *t = getenv("TERM");

    if (magic_cookie_glitch > 0)
        return 1;
    return t && (strncmp(t, "wy50", 4) == 0 || strncmp(t, "wyse50", 6) == 0);
}

/* The styles this terminal can show; want is the one asked for (COLOUR
   unless -m or -p), forced if it was asked for explicitly. */
/*
 * Colour pairs for the palette in use, one for each different pair of
 * colours (tiles with the same colours share one: 4 pairs for 8 colours,
 * 7 for 16, 9 for 256), so that terminals with few pairs (dtterm has 8)
 * still get colour.  Returns 0 if the terminal hasn't enough pairs.
 */
static int pairs(void)
{
    int i, j, n = 0;

    for (i = 0; i < TILES; i++) {
        for (j = 0; j < i; j++)
            if (palette[j][0] == palette[i][0] && palette[j][1] == palette[i][1])
                break;
        if (j < i) {
            tilepair[i] = tilepair[j];
            continue;
        }
        if (++n >= COLOR_PAIRS)             /* pair 0 is the terminal's own */
            return 0;
        tilepair[i] = (short) n;
        init_pair((short) n, palette[i][1], palette[i][0]);
    }
    return 1;
}

static void styles(int want, int forced)
{
    maxstyle = (termattrs() & (A_REVERSE | A_BOLD)) ? MONO : PLAIN;
    if (maxstyle == MONO && has_colors() && start_color() != ERR) {
        /* As many colours as terminfo says the terminal has, and no more:
           an 8-colour terminal only ever gets the ANSI codes. */
        palette = COLORS >= 256 ? pal256 : COLORS >= 16 ? pal16 : pal8;
        if (pairs())
            maxstyle = COLOUR;
    }
    /* Attributes that take up a cell would shift the board: plain, unless
       mono was asked for by name. */
    if (cookies() && !(forced && want == MONO))
        maxstyle = PLAIN;
    else if (cookies())
        maxstyle = MONO;
    style = want < maxstyle ? want : maxstyle;
}

int f89_fsok(char *why, int len)
{
    const char *t = getenv("TERM");
    char *cup;
    int err = 0, ok;

    why[0] = '\0';
    if (!isatty(0) || !isatty(1))
        return 0;                       /* a pipe or a file: no need to say */
    if (!t || !*t) {
        if (len > 16)
            strcpy(why, "TERM is not set");
        return 0;
    }
    if (setupterm((char *) 0, 1, &err) != OK) {
        if (len > 64)
            sprintf(why, "TERM=%.32s is not a terminal I know", t);
        return 0;
    }
    cup = tigetstr((char *) "cup");
    ok = !hard_copy && !generic_type && cup != 0 && cup != (char *) -1;
    if (!ok && len > 64)
        sprintf(why, "TERM=%.32s %s", t, hard_copy ? "is a hardcopy terminal"
                : "can't move the cursor");
    del_curterm(cur_term);
    return ok;
}

int f89_screen(struct t89_game *g, const struct f89_options *op)
{
    struct t89_game undo, before;
    struct t89_result r;
    struct f89_options o;
    char save[T89_SAVELEN + 32];
    int k, cmd, canundo = 0, over = 0;
    int cramped = 0;                    /* 1 while the board doesn't fit */

    game = *g;
    o = *op;
    ascii = o.ascii;
    cells = o.cells;
    sprintf(command, "2089 %s%s%s%s%s", o.undo ? "-u " : "",
            o.forced && o.style == MONO ? "-m " : "",
            o.forced && o.style == PLAIN ? "-p " : "", ascii ? "-a " : "",
            cells == F89_CELLS_SMALL ? "-k " : cells == F89_CELLS_TALL ? "-K " : "");

    initscr();
    cbreak();
    noecho();
    nonl();
    intrflush(stdscr, FALSE);
    keypad(stdscr, TRUE);
    signal(SIGINT, caught);
    signal(SIGTERM, caught);
#ifdef SIGHUP
    signal(SIGHUP, caught);
#endif
    styles(o.style, o.forced);          /* before layout: slow lines depend on it */
    if (layout() < 0) {
        endwin();
        signal(SIGINT, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
#ifdef SIGHUP
        signal(SIGHUP, SIG_DFL);
#endif
        printf("2089: the screen is too small for a %dx%d board (%d columns, %d lines):"
               " playing in line mode\n", game.size, game.size, COLS, LINES);
        return -1;
    }
    hidden = curs_set(0) != ERR;        /* where the terminal can (civis) */
    idcok(stdscr, FALSE);               /* no character delete: see settle */
    openscores();
    over = t89_over(&game);
    draw(o.undo);
    if (over)
        nomoves(o.undo);

    for (;;) {
        settle();
        k = getch();
        if (k == ERR)
            continue;
        if (k == '\f') {                /* ^L: lay out again and redraw */
            clearok(curscr, TRUE);
            cramped = layout() < 0;
            if (cramped) {              /* short lines: the screen is small */
                erase();
                put(0, 0, "Too small.");
                put(1, 0, "Make it bigger");
                put(2, 0, "and press ^L.");
            } else
                draw(o.undo);
            continue;
        }
        cmd = key(k);
        /* A failed layout leaves no board to draw on (and half of a new
           shape), so only quitting works until ^L finds room. */
        if (cramped && cmd != F89_QUIT)
            continue;
        if (cmd != 0) {
            message("");
            settle();                   /* see settle: no stale colour */
        }
        switch (cmd) {
        case F89_QUIT:
            finish();
            return EXIT_SUCCESS;
        case F89_NEW:
            if (ask("Start a new game?  (y/n)")) {
                writescores();          /* if it fails, finish says so */
                t89_new(&game, game.size, f89_seed(game.rng));
                canundo = over = 0;
                update();
            }
            break;
        case F89_UNDO:
            if (!o.undo)
                message("Undo is off: start with -u to allow it.");
            else if (canundo) {
                game = undo;
                canundo = over = 0;
                update();
            } else
                message("Nothing to undo.");
            break;
        case F89_SAVE:
            strcpy(save, "Save string: ");
            t89_save(&game, save + strlen(save), T89_SAVELEN);
            message(save);
            break;
        case F89_HELP:
            help(o.undo);
            break;
        case F89_DISPLAY:
            if (maxstyle > PLAIN) {         /* nothing to switch to otherwise */
                style = nextstyle();
                draw(o.undo);
            }
            break;
        case 0:
            break;
        default:
            before = game;
            if (t89_move(&game, f89_dir(cmd), &r)) {
                undo = before;
                canundo = 1;
                update();
                if (r.events & T89_EV_WON) {
                    sprintf(save, "%lu!  You win.  Keep going if you like.",
                            t89_value(game.goal));
                    message(save);
                }
                if (r.events & T89_EV_OVER) {
                    over = 1;
                    writescores();
                    nomoves(o.undo);
                }
            } else if (over)
                nomoves(o.undo);                /* keep saying so */
            break;
        }
    }
}
