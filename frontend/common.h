/*
 * common.h - what every 2089 frontend on a hosted C89 system shares: the key
 * map, arrow-key escape sequences, seeding, options and the resume line.
 *
 * Unlike the core, this needs the C library (<stdio.h>, <ctype.h>, <time.h>
 * and so on).  Frontends without one (Palm OS, Windows CE) use the core
 * directly and do these jobs their own way.
 *
 * Keys give commands; when to act on them is up to the frontend (the text
 * frontend plays the last key on a line, curses plays every key).
 *
 * Author: Claude Opus 5.5 (claude-opus-5-5), October 2026.
 * MIT License; see LICENSE.
 */

#ifndef F89_H
#define F89_H

#include "2089.h"

/* Commands, from f89_key and f89_escape (0 means none). */
#define F89_UP      1
#define F89_RIGHT   2
#define F89_DOWN    3
#define F89_LEFT    4
#define F89_UNDO    5
#define F89_NEW     6
#define F89_SAVE    7           /* print the save string */
#define F89_HELP    8
#define F89_DISPLAY 9           /* switch display style */
#define F89_QUIT    10

/* Display styles for full-screen play. */
#define F89_PLAIN  0            /* no video attributes */
#define F89_MONO   1            /* bold and reverse video */
#define F89_COLOUR 2

/* Full screen: cell height. */
#define F89_CELLS_AUTO  0       /* tall if there's room, compact on slow lines */
#define F89_CELLS_SMALL 1       /* one line high (-k) */
#define F89_CELLS_TALL  2       /* tall if there's room, whatever the line (-K) */

/*
 * Options.  f89_option reads the common ones (-u -s -r -l); main.c reads
 * the ones for each mode.
 */
struct f89_options {
    int size;
    unsigned long seed;
    const char *load;           /* save string to resume, or 0 */
    int undo;                   /* 1 if undo is allowed */
    int line;                   /* line mode asked for (-t) */
    int compact;                /* line mode: compact display (-k) */
    int style;                  /* full screen: style asked for (-m, -p) */
    int forced;                 /* ... and it was asked for by name */
    int ascii;                  /* full screen: + - | boxes (-a) */
    int cells;                  /* full screen: F89_CELLS_* (-k, -K) */
};

/*
 * The command for a key, in either case (Caps Lock, or a terminal with no
 * lower case): w a s d, k h j l and 8 4 2 6 move; u n p ? c q.  0 for any
 * other key.
 */
int f89_key(int ch);

/* The core's direction (T89_UP ...) for a move command, or -1. */
int f89_dir(int cmd);

/*
 * Does ch (as an unsigned char) start a control sequence: ESC, or the 8-bit
 * CSI (0x9B, the same as ESC [) or SS3 (0x8F, ESC O) that VT220s and later
 * send in 8-bit mode?
 */
int f89_intro(int ch);

/*
 * A control sequence starting at s (with its ESC, CSI or SS3): the move for
 * an arrow key, or 0.  Knows ESC [ A and CSI A (ANSI, VT100 in cursor key
 * mode, xterm, aixterm), ESC O A and SS3 A (VT100 in application mode) and
 * ESC A (VT52), for A to D, with or without parameters (ESC [ 1 ; 2 A is
 * shift-up on xterm).  *len gets the number of bytes the sequence used, so
 * that other sequences (function keys, ESC [ ? 25 h and so on) can be
 * skipped whole; a sequence cut short by the end of the string uses what
 * there is, and one cut short by a newline, another control character or
 * the start of another sequence ends before it.  ('\033' is ESC in ASCII;
 * EBCDIC terminals don't send these.)
 */
int f89_escape(const char *s, int *len);

/* The length of the sequence at s (as f89_escape counts it), or 0 if the
   string ends before the sequence does: for reading one byte at a time. */
int f89_cslen(const char *s);

/*
 * A seed for a new game: time() mixed with clock() and salt (pass the RNG
 * state of the game being replaced, or 0), so that games started within the
 * same second differ.
 */
unsigned long f89_seed(unsigned long salt);

/* A tile as text: its value, "2^n" if that won't fit in 32 bits, "" if empty.
   buf needs 12 characters. */
void f89_tile(char *buf, int exp);

/* Options before parsing: 4x4, a fresh seed, no undo, the best style. */
void f89_defaults(struct f89_options *o);

/*
 * If argv[*i] is one of the common options, take it (and its value, moving
 * *i on) and return 1.  Return 0 if it isn't one of ours, -1 if its value is
 * missing or bad.
 */
int f89_option(int argc, char **argv, int *i, struct f89_options *o);

/* Print the usage lines for the common options. */
void f89_usage(void);

/* Start the game the options ask for; on failure print why (as name: ...)
   and return -1. */
int f89_start(struct t89_game *g, const struct f89_options *o, const char *name);

/* Print how to carry on later: "to carry on later: <command>-l <save>".
   command is the program and its options, ending in a space. */
void f89_resume(const struct t89_game *g, const char *command);

/*
 * The two ways to play.  Each plays the game to the end and returns the
 * program's exit status.
 *
 * f89_line (frontend/text/text.c) needs only the C89 library.  f89_screen
 * (frontend/curses/curses.c) returns -1 without playing if the board won't
 * fit the screen, so that the caller can fall back to f89_line.
 * f89_screenok says whether this terminal can play full screen at all; if
 * not, and there's a reason worth telling (an unknown TERM, a hardcopy
 * terminal), it puts it in why.
 */
int f89_line(struct t89_game *g, const struct f89_options *o);
int f89_screen(struct t89_game *g, const struct f89_options *o);
int f89_screenok(char *why, int len);

#endif
