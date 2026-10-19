/*
 * 2089.h - the 2089 core engine: rules, state and save format of 2048.
 *
 * The core is freestanding C89.  It includes no headers, calls no library
 * functions, keeps no static data and does no I/O, so the same 2089.c builds
 * for a Unix terminal, a CGI program, Palm OS or anything else with a C89
 * compiler.  Everything a frontend needs is in the structures below; a game
 * is plain data and may be copied with assignment (for undo, say).
 *
 * Link with rng89.c.  A frontend that wants random numbers of its own that
 * stay reproducible with the game can draw them with r89_next(&g->rng).
 *
 * Tiles are stored as exponents: 0 is an empty cell, 1 is a 2, 2 is a 4 and
 * so on (11 is 2048).  Cells are kept row-major, top row first: the cell at
 * row r, column c is cell[r * size + c].
 *
 * Author: Claude Opus 5.5 (claude-opus-5-5), October 2026.
 * MIT License; see LICENSE.
 */

#ifndef T89_H
#define T89_H

#define T89_VERSION "0.1.0"

#define T89_MINSIZE 2           /* smallest and largest board edge */
#define T89_MAXSIZE 8
#define T89_SIZE    4           /* the usual board */
#define T89_CELLS   (T89_MAXSIZE * T89_MAXSIZE)
#define T89_MAXEXP  35          /* largest exponent; tiles this big don't merge */
#define T89_GOAL    11          /* 2048 */

/* Directions, in clockwise order. */
#define T89_UP    0
#define T89_RIGHT 1
#define T89_DOWN  2
#define T89_LEFT  3

/* Event bits reported by t89_move. */
#define T89_EV_WON  1           /* this move first reached the goal */
#define T89_EV_OVER 2           /* no move is possible any more */

/* Length of the longest save string, including the terminating NUL. */
#define T89_SAVELEN 100

/*
 * A game.  Frontends may read every field.  They should change them only
 * through the functions below, except goal, which a variant may set right
 * after t89_new.  (t89_load accepts any state a player could reach, and a
 * few they couldn't.)
 */
struct t89_game {
    int size;                       /* edge length, T89_MINSIZE..T89_MAXSIZE */
    int goal;                       /* exponent that wins, 1..T89_MAXEXP */
    unsigned char cell[T89_CELLS];  /* exponents; only size*size are used */
    unsigned long score;            /* sum of merged tile values, mod 2^32 */
    unsigned long moves;            /* moves that changed the board */
    unsigned long rng;              /* rng89 state (see rng89.h) */
};

/*
 * What one tile did during a move.  Every tile on the board before the move
 * has one entry, including tiles that stayed put (from == to).  Two entries
 * share a "to" when their tiles merged there; both have merged set.
 */
struct t89_motion {
    unsigned char from, to;         /* cell indices */
    unsigned char exp;              /* exponent before the move */
    unsigned char merged;           /* 1 if the tile merged at "to" */
};

/* Everything a move changed, for frontends that animate or report it. */
struct t89_result {
    int moved;                      /* 0 if the move changed nothing */
    unsigned long points;           /* score gained by this move */
    int ntiles;                     /* entries used in tile[] */
    struct t89_motion tile[T89_CELLS];
    int spawn;                      /* cell of the new tile, or -1 */
    int spawnexp;                   /* its exponent (1 or 2) */
    int events;                     /* T89_EV_* bits */
};

/*
 * Start a new game: a size x size board (0 means T89_SIZE) with two random
 * tiles, the RNG seeded with seed (r89_seed).  The core never reads a clock; pass the
 * time, a counter or a fixed number as you like.  The same size and seed
 * always give the same game on every platform.  Returns 0 on success, -1 if
 * size is out of range (the game is then left untouched).
 */
int t89_new(struct t89_game *g, int size, unsigned long seed);

/*
 * Play a move in direction dir (T89_UP etc.).  If any tile moves, a new tile
 * appears (2 with probability 9/10, 4 otherwise) and the move counts.  If
 * nothing moves, the game is unchanged and the RNG is not advanced.  r may
 * be NULL (0).  Returns 1 if the board changed, 0 if not.
 */
int t89_move(struct t89_game *g, int dir, struct t89_result *r);

/*
 * Only the slide and merge part of a move: no new tile, no move counted.
 * For tests, solvers and variants that place tiles themselves.
 */
int t89_slide(struct t89_game *g, int dir, struct t89_result *r);

/*
 * Put a random tile in a random empty cell, as after a move.  Returns the
 * cell, or -1 if the board is full.  *exp (if exp is not NULL) receives the
 * tile's exponent.
 */
int t89_spawn(struct t89_game *g, int *exp);

/* Would a move in direction dir change the board? (1 or 0) */
int t89_can_move(const struct t89_game *g, int dir);

/* 1 when no direction can change the board. */
int t89_over(const struct t89_game *g);

/* 1 when some tile has reached the goal. */
int t89_won(const struct t89_game *g);

/* Largest exponent on the board (0 for an empty board). */
int t89_best(const struct t89_game *g);

/* Number of empty cells. */
int t89_empty(const struct t89_game *g);

/* The value of a tile with exponent exp (2^exp), or 0 if exp is 0 or over 31. */
unsigned long t89_value(int exp);

/*
 * The whole game as a short string of [0-9a-z.] (safe in URLs, file names
 * and every character set), at most T89_SAVELEN bytes with the NUL.  Returns
 * its length, or -1 if len is too small or the game holds impossible values
 * (buf then holds an empty string if len > 0).
 */
int t89_save(const struct t89_game *g, char *buf, int len);

/*
 * Read a string written by t89_save.  The input is checked strictly, so it
 * may come from anywhere (a URL, a file on a floppy).  Returns 0 and fills g
 * on success; returns -1 and leaves g untouched if the string is malformed.
 */
int t89_load(struct t89_game *g, const char *s);

#endif
