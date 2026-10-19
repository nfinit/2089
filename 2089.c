/*
 * 2089.c - the 2089 core engine.  See 2089.h for the interface.
 *
 * Game logic only.  Random numbers come from rng89.c, which is independent
 * of the game.
 *
 * Portability notes, since this file is meant to build anywhere:
 * - No headers, no library calls, no static or global variables.
 * - int may be 16 bits: anything that can exceed 32767 is unsigned long,
 *   and every unsigned long result is reduced to 32 bits, so a 64-bit long
 *   gives the same numbers as a 32-bit one.
 * - Characters are looked up in a string, never computed ('a' + n), so the
 *   save format also works in EBCDIC, where the letters aren't contiguous.
 *
 * Copyright (c) 2026 ict.  MIT License; see LICENSE.
 */

#include "2089.h"
#include "rng89.h"

#define M32 0xFFFFFFFFUL

/* Digits of the save format: base 36 for cells, base 16 for numbers. */
#define DIGITS "0123456789abcdefghijklmnopqrstuvwxyz"

/*
 * Cell index of position i along line k when moving in direction dir.
 * Position 0 is the edge the tiles move towards.
 */
static int at(int n, int dir, int k, int i)
{
    switch (dir) {
    case T89_UP:    return i * n + k;
    case T89_DOWN:  return (n - 1 - i) * n + k;
    case T89_RIGHT: return k * n + (n - 1 - i);
    default:        return k * n + i;
    }
}

unsigned long t89_value(int exp)
{
    if (exp <= 0 || exp > 31)
        return 0;
    return 1UL << exp;
}

int t89_best(const struct t89_game *g)
{
    int i, n = g->size * g->size, best = 0;

    for (i = 0; i < n; i++)
        if (g->cell[i] > best)
            best = g->cell[i];
    return best;
}

int t89_won(const struct t89_game *g)
{
    return t89_best(g) >= g->goal;
}

int t89_empty(const struct t89_game *g)
{
    int i, n = g->size * g->size, count = 0;

    for (i = 0; i < n; i++)
        if (g->cell[i] == 0)
            count++;
    return count;
}

int t89_can_move(const struct t89_game *g, int dir)
{
    int n = g->size, k, i, a, b;

    if (dir < 0 || dir > 3)
        return 0;
    /* A tile can move if the cell before it (towards the edge) is empty or
       holds a tile it can merge with. */
    for (k = 0; k < n; k++)
        for (i = 1; i < n; i++) {
            a = g->cell[at(n, dir, k, i - 1)];
            b = g->cell[at(n, dir, k, i)];
            if (b != 0 && (a == 0 || (a == b && a < T89_MAXEXP)))
                return 1;
        }
    return 0;
}

int t89_over(const struct t89_game *g)
{
    int dir;

    for (dir = 0; dir < 4; dir++)
        if (t89_can_move(g, dir))
            return 0;
    return 1;
}

int t89_slide(struct t89_game *g, int dir, struct t89_result *r)
{
    int n = g->size, k, i, w, e, src, dst, open, last = 0, nt = 0, moved = 0;
    int won = t89_won(g);
    unsigned long points = 0;

    if (dir < 0 || dir > 3) {
        if (r) {
            r->moved = 0;
            r->points = 0;
            r->ntiles = 0;
            r->spawn = -1;
            r->spawnexp = 0;
            r->events = 0;
        }
        return 0;
    }
    for (k = 0; k < n; k++) {
        w = 0;          /* next free position along the line */
        open = 0;       /* the tile at position w - 1 may still merge */
        for (i = 0; i < n; i++) {
            src = at(n, dir, k, i);
            e = g->cell[src];
            if (e == 0)
                continue;
            if (open && g->cell[at(n, dir, k, w - 1)] == e && e < T89_MAXEXP) {
                dst = at(n, dir, k, w - 1);
                g->cell[dst] = (unsigned char) (e + 1);
                g->cell[src] = 0;
                points = (points + t89_value(e + 1)) & M32;
                open = 0;
                moved = 1;
                if (r) {
                    r->tile[last].merged = 1;
                    r->tile[nt].merged = 1;
                }
            } else {
                dst = at(n, dir, k, w);
                if (dst != src) {
                    g->cell[dst] = (unsigned char) e;
                    g->cell[src] = 0;
                    moved = 1;
                }
                open = 1;
                w++;
                last = nt;
                if (r)
                    r->tile[nt].merged = 0;
            }
            if (r) {
                r->tile[nt].from = (unsigned char) src;
                r->tile[nt].to = (unsigned char) dst;
                r->tile[nt].exp = (unsigned char) e;
            }
            nt++;
        }
    }
    g->score = (g->score + points) & M32;
    if (r) {
        r->moved = moved;
        r->points = points;
        r->ntiles = nt;
        r->spawn = -1;
        r->spawnexp = 0;
        r->events = (!won && t89_won(g)) ? T89_EV_WON : 0;
    }
    return moved;
}

int t89_spawn(struct t89_game *g, int *exp)
{
    int i, n = g->size * g->size, pick, e;

    if (t89_empty(g) == 0)
        return -1;
    pick = (int) (r89_next(&g->rng) % (unsigned int) t89_empty(g));
    e = (r89_next(&g->rng) % 10U == 0) ? 2 : 1;
    for (i = 0; i < n; i++)
        if (g->cell[i] == 0 && pick-- == 0)
            break;
    g->cell[i] = (unsigned char) e;
    if (exp)
        *exp = e;
    return i;
}

int t89_move(struct t89_game *g, int dir, struct t89_result *r)
{
    int won = t89_won(g), cell, e = 0;

    if (!t89_slide(g, dir, r))
        return 0;
    g->moves = (g->moves + 1) & M32;
    cell = t89_spawn(g, &e);
    if (r) {
        r->spawn = cell;
        r->spawnexp = cell < 0 ? 0 : e;
        r->events = 0;
        if (!won && t89_won(g))
            r->events |= T89_EV_WON;
        if (t89_over(g))
            r->events |= T89_EV_OVER;
    }
    return 1;
}

int t89_new(struct t89_game *g, int size, unsigned long seed)
{
    int i;

    if (size == 0)
        size = T89_SIZE;
    if (size < T89_MINSIZE || size > T89_MAXSIZE)
        return -1;
    g->size = size;
    g->goal = T89_GOAL;
    for (i = 0; i < T89_CELLS; i++)
        g->cell[i] = 0;
    g->score = 0;
    g->moves = 0;
    r89_seed(&g->rng, seed);
    t89_spawn(g, (int *) 0);
    t89_spawn(g, (int *) 0);
    return 0;
}

/* ------------------------------------------------------------ save format
 *
 * "1" (format version), size, goal and one base-36 digit per cell, then
 * the score, move count and RNG state in hex, each after a ".":
 *
 *     14b0000000000100100.0.0.f63f5af9    (a new 4x4 game, seed 42)
 *
 * Numbers have no leading zeros, so every game has exactly one string.
 */

static int digit(int c, int base)
{
    int i;

    for (i = 0; i < base; i++)
        if (DIGITS[i] == c)
            return i;
    return -1;
}

/* Append x in hex at buf[*pos], if it fits below len - 1. */
static int puthex(char *buf, int *pos, int len, unsigned long x)
{
    char tmp[8];
    int n = 0;

    x &= M32;
    do {
        tmp[n++] = DIGITS[(int) (x & 15)];
        x >>= 4;
    } while (x != 0);
    if (*pos + n >= len)
        return -1;
    while (n > 0)
        buf[(*pos)++] = tmp[--n];
    return 0;
}

int t89_save(const struct t89_game *g, char *buf, int len)
{
    int i, n = g->size * g->size, pos = 0;

    if (len > 0)
        buf[0] = '\0';
    if (g->size < T89_MINSIZE || g->size > T89_MAXSIZE
        || g->goal < 1 || g->goal > T89_MAXEXP || len < 4 + n)
        return -1;
    for (i = 0; i < n; i++)
        if (g->cell[i] > T89_MAXEXP)
            return -1;
    buf[pos++] = '1';
    buf[pos++] = DIGITS[g->size];
    buf[pos++] = DIGITS[g->goal];
    for (i = 0; i < n; i++)
        buf[pos++] = DIGITS[g->cell[i]];
    if (pos + 1 >= len) goto toolong;
    buf[pos++] = '.';
    if (puthex(buf, &pos, len, g->score) < 0) goto toolong;
    if (pos + 1 >= len) goto toolong;
    buf[pos++] = '.';
    if (puthex(buf, &pos, len, g->moves) < 0) goto toolong;
    if (pos + 1 >= len) goto toolong;
    buf[pos++] = '.';
    if (puthex(buf, &pos, len, g->rng) < 0) goto toolong;
    buf[pos] = '\0';
    return pos;
toolong:
    buf[0] = '\0';
    return -1;
}

/* Read ".hex" at s[*pos]: 1 to 8 digits, no leading zeros. */
static int gethex(const char *s, int *pos, unsigned long *x)
{
    int d, n = 0;

    if (s[*pos] != '.')
        return -1;
    (*pos)++;
    *x = 0;
    while ((d = digit(s[*pos], 16)) >= 0) {
        if (n == 8 || (n == 1 && *x == 0))
            return -1;
        *x = (*x << 4) | (unsigned long) d;
        (*pos)++;
        n++;
    }
    return n == 0 ? -1 : 0;
}

int t89_load(struct t89_game *g, const char *s)
{
    struct t89_game t;
    int i, n, e, pos = 3;

    if (s[0] != '1')
        return -1;
    t.size = digit(s[1], 36);
    if (t.size < T89_MINSIZE || t.size > T89_MAXSIZE)
        return -1;
    t.goal = digit(s[2], 36);
    if (t.goal < 1 || t.goal > T89_MAXEXP)
        return -1;
    n = t.size * t.size;
    for (i = 0; i < T89_CELLS; i++)
        t.cell[i] = 0;
    for (i = 0; i < n; i++) {
        e = digit(s[pos++], 36);
        if (e < 0 || e > T89_MAXEXP)
            return -1;
        t.cell[i] = (unsigned char) e;
    }
    if (gethex(s, &pos, &t.score) < 0 || gethex(s, &pos, &t.moves) < 0
        || gethex(s, &pos, &t.rng) < 0 || s[pos] != '\0')
        return -1;
    /* Field by field: some compilers (gcc 2.95) turn "*g = t" into a call to
       memcpy, which a freestanding target may not have. */
    g->size = t.size;
    g->goal = t.goal;
    for (i = 0; i < T89_CELLS; i++)
        g->cell[i] = t.cell[i];
    g->score = t.score;
    g->moves = t.moves;
    g->rng = t.rng;
    return 0;
}
