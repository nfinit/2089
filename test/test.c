/*
 * test.c - tests for the 2089 core.  C89, needs only <stdio.h>.
 *
 *     test           run everything; exit status 0 if all passed
 *     test -g        also print the golden numbers (to update GOLD_*)
 *
 * The golden numbers pin down the RNG, spawning and the save format: any
 * platform that builds the core correctly must reproduce them exactly.
 *
 * Copyright (c) 2026 ict.  MIT License; see LICENSE.
 */

#include <stdio.h>
#include <string.h>
#include "../2089.h"
#include "../rng89.h"

/* Expected on every platform (first computed on x86_64 Linux). */
#define GOLD_RNG   0xFDCFF979UL       /* hash of 1000 r89_next values, seed 1 */
#define GOLD_GAMES 0xF986A2F1UL       /* hash of 300 complete games */

static int failures = 0, checks = 0;

static void fail(const char *what, const char *detail)
{
    if (failures < 20)
        printf("FAIL: %s %s\n", what, detail);
    failures++;
}

#define CHECK(cond, what, detail) \
    do { checks++; if (!(cond)) fail(what, detail); } while (0)

/* A test-side LCG, so the tests' choices never touch a game's RNG. */
static unsigned long tseed = 12345UL;

static int trand(int n)
{
    tseed = (tseed * 69069UL + 1UL) & 0xFFFFFFFFUL;
    return (int) ((tseed >> 16) % (unsigned long) n);
}

/* FNV-1a, 32 bits. */
static unsigned long fnv(unsigned long h, const char *s)
{
    while (*s) {
        h ^= (unsigned long) (unsigned char) *s++;
        h = (h * 16777619UL) & 0xFFFFFFFFUL;
    }
    return h;
}

static void board(struct t89_game *g, int size)
{
    int i;

    for (i = 0; i < T89_CELLS; i++)
        g->cell[i] = 0;
    g->size = size;
    g->goal = T89_GOAL;
    g->score = 0;
    g->moves = 0;
    g->rng = 1;
}

static void randboard(struct t89_game *g, int size, int maxexp)
{
    int i;

    board(g, size);
    for (i = 0; i < size * size; i++)
        g->cell[i] = (unsigned char) (trand(3) == 0 ? 0 : trand(maxexp) + 1);
}

static int same(const struct t89_game *a, const struct t89_game *b)
{
    int i;

    for (i = 0; i < a->size * a->size; i++)
        if (a->cell[i] != b->cell[i])
            return 0;
    return 1;
}

static char *show(const struct t89_game *g)
{
    static char buf[T89_SAVELEN];

    t89_save(g, buf, sizeof buf);
    return buf;
}

/* ------------------------------------------------------------ one line */

/* Row in, row out, points; slid left on a 4x4 board.  (long: int may be
   16 bits.) */
static const long rows[][9] = {
    {0, 0, 0, 0,  0, 0, 0, 0,  0},
    {1, 0, 0, 0,  1, 0, 0, 0,  0},
    {0, 0, 0, 1,  1, 0, 0, 0,  0},
    {0, 1, 0, 2,  1, 2, 0, 0,  0},
    {1, 1, 0, 0,  2, 0, 0, 0,  4},
    {1, 0, 0, 1,  2, 0, 0, 0,  4},
    {0, 1, 0, 1,  2, 0, 0, 0,  4},
    {1, 1, 1, 0,  2, 1, 0, 0,  4},
    {1, 0, 1, 1,  2, 1, 0, 0,  4},
    {1, 1, 1, 1,  2, 2, 0, 0,  8},
    {2, 1, 1, 0,  2, 2, 0, 0,  4},
    {1, 1, 2, 0,  2, 2, 0, 0,  4},
    {2, 2, 1, 1,  3, 2, 0, 0, 12},
    {0, 1, 1, 2,  2, 2, 0, 0,  4},
    {3, 3, 3, 0,  4, 3, 0, 0, 16},
    {1, 2, 1, 2,  1, 2, 1, 2,  0},
    {2, 1, 1, 2,  2, 2, 2, 0,  4},
    {11, 11, 10, 10,  12, 11, 0, 0, 6144},
    {17, 17, 0, 0,  18, 0, 0, 0, 262144L},
    {31, 31, 0, 0,  32, 0, 0, 0,  0},       /* 2^32 wraps to 0 */
    {35, 35, 0, 0,  35, 35, 0, 0,  0},      /* the largest tile can't merge */
    {34, 34, 35, 0,  35, 35, 0, 0,  0}
};

static void test_rows(void)
{
    struct t89_game g;
    struct t89_result r;
    char detail[80];
    int t, i, moved;

    for (t = 0; t < (int) (sizeof rows / sizeof rows[0]); t++) {
        board(&g, 4);
        for (i = 0; i < 4; i++)
            g.cell[i] = (unsigned char) rows[t][i];
        moved = t89_slide(&g, T89_LEFT, &r);
        sprintf(detail, "row %d: %ld %ld %ld %ld", t,
                rows[t][0], rows[t][1], rows[t][2], rows[t][3]);
        for (i = 0; i < 4; i++)
            CHECK(g.cell[i] == rows[t][i + 4], "slide", detail);
        CHECK(r.points == (unsigned long) rows[t][8], "points", detail);
        CHECK(g.score == (unsigned long) rows[t][8], "score", detail);
        CHECK(moved == (memcmp(rows[t], rows[t] + 4, 4 * sizeof(long)) != 0),
              "moved flag", detail);
    }
}

/* ------------------------------------------------------------ symmetry */

/* Mirror left-right, or transpose. */
static void mirror(struct t89_game *g)
{
    int r, c, n = g->size, t;

    for (r = 0; r < n; r++)
        for (c = 0; c < n / 2; c++) {
            t = g->cell[r * n + c];
            g->cell[r * n + c] = g->cell[r * n + n - 1 - c];
            g->cell[r * n + n - 1 - c] = (unsigned char) t;
        }
}

static void transpose(struct t89_game *g)
{
    int r, c, n = g->size, t;

    for (r = 0; r < n; r++)
        for (c = r + 1; c < n; c++) {
            t = g->cell[r * n + c];
            g->cell[r * n + c] = g->cell[c * n + r];
            g->cell[c * n + r] = (unsigned char) t;
        }
}

static void test_symmetry(void)
{
    struct t89_game a, b;
    int t, size;

    for (t = 0; t < 4000; t++) {
        size = T89_MINSIZE + t % (T89_MAXSIZE - T89_MINSIZE + 1);
        randboard(&a, size, 4);

        b = a;                  /* right = mirror, left, mirror */
        t89_slide(&b, T89_RIGHT, 0);
        mirror(&a); t89_slide(&a, T89_LEFT, 0); mirror(&a);
        CHECK(same(&a, &b) && a.score == b.score, "right vs left", show(&b));

        randboard(&a, size, 4);
        b = a;                  /* up = transpose, left, transpose */
        t89_slide(&b, T89_UP, 0);
        transpose(&a); t89_slide(&a, T89_LEFT, 0); transpose(&a);
        CHECK(same(&a, &b) && a.score == b.score, "up vs left", show(&b));

        randboard(&a, size, 4);
        b = a;                  /* down = transpose, right, transpose */
        t89_slide(&b, T89_DOWN, 0);
        transpose(&a); t89_slide(&a, T89_RIGHT, 0); transpose(&a);
        CHECK(same(&a, &b) && a.score == b.score, "down vs right", show(&b));
    }
}

/* ------------------------------------------------------------ motions */

static void test_motions(void)
{
    struct t89_game before, g, rebuilt;
    struct t89_result r;
    int t, i, j, dir, n, tiles, pairs, seen[T89_CELLS];

    for (t = 0; t < 4000; t++) {
        randboard(&before, T89_MINSIZE + t % (T89_MAXSIZE - T89_MINSIZE + 1), 3);
        dir = t % 4;
        g = before;
        CHECK(t89_can_move(&g, dir) == t89_slide(&g, dir, &r), "can_move", show(&before));
        n = g.size * g.size;

        /* Every tile appears once, and the motions rebuild the new board. */
        board(&rebuilt, g.size);
        for (i = 0, tiles = 0; i < n; i++) {
            seen[i] = 0;
            tiles += before.cell[i] != 0;
        }
        CHECK(r.ntiles == tiles, "ntiles", show(&before));
        for (i = 0, pairs = 0; i < r.ntiles; i++) {
            CHECK(before.cell[r.tile[i].from] == r.tile[i].exp, "motion exp", show(&before));
            seen[r.tile[i].from]++;
            if (r.tile[i].merged) {
                for (j = 0; j < r.ntiles; j++)
                    if (j != i && r.tile[j].to == r.tile[i].to)
                        break;
                CHECK(j < r.ntiles && r.tile[j].merged && r.tile[j].exp == r.tile[i].exp,
                      "merge partner", show(&before));
                rebuilt.cell[r.tile[i].to] = (unsigned char) (r.tile[i].exp + 1);
                pairs++;
            } else {
                CHECK(rebuilt.cell[r.tile[i].to] == 0, "two tiles in a cell", show(&before));
                rebuilt.cell[r.tile[i].to] = r.tile[i].exp;
            }
        }
        for (i = 0; i < n; i++)
            CHECK(seen[i] == (before.cell[i] != 0), "tile seen once", show(&before));
        CHECK(pairs % 2 == 0, "merges in pairs", show(&before));
        CHECK(same(&rebuilt, &g), "motions rebuild board", show(&before));
    }
}

/* ------------------------------------------------------------ games */

static unsigned long playgames(int verbose)
{
    struct t89_game g, copy;
    struct t89_result r;
    unsigned long h = 2166136261UL, sum, spawned;
    int t, i, dir, moved, size, wins = 0, events;
    char buf[T89_SAVELEN];

    for (t = 0; t < 300; t++) {
        size = (t % 10 == 9) ? 3 + (t / 10) % 4 : 4;
        CHECK(t89_new(&g, size, (unsigned long) t) == 0, "new", "");
        CHECK(t89_empty(&g) == size * size - 2, "two starting tiles", show(&g));
        for (i = 0, spawned = 0; i < size * size; i++)
            spawned += t89_value(g.cell[i]);
        events = 0;
        while (!t89_over(&g)) {
            dir = trand(4);
            copy = g;
            moved = t89_move(&g, dir, &r);
            if (!moved) {
                /* A move that changes nothing must leave the game alone. */
                CHECK(g.rng == copy.rng && g.moves == copy.moves && same(&g, &copy),
                      "no-op move", show(&copy));
                continue;
            }
            CHECK(r.spawn >= 0 && g.cell[r.spawn] == r.spawnexp, "spawn", show(&g));
            CHECK(g.score == ((copy.score + r.points) & 0xFFFFFFFFUL), "score", show(&g));
            spawned += t89_value(r.spawnexp);
            events |= r.events;
            CHECK(!(r.events & T89_EV_OVER) == !t89_over(&g), "over event", show(&g));
        }
        /* Merging keeps the total; only spawns add to it. */
        for (i = 0, sum = 0; i < size * size; i++)
            sum += t89_value(g.cell[i]);
        CHECK(sum == spawned, "tile total", show(&g));
        CHECK(!(events & T89_EV_WON) == !t89_won(&g), "won event", show(&g));
        CHECK(events & T89_EV_OVER, "game ended without an over event", show(&g));
        wins += t89_won(&g);
        t89_save(&g, buf, sizeof buf);
        h = fnv(h, buf);
        if (verbose && t < 3)
            printf("  game %d: %s\n", t, buf);
    }
    if (verbose)
        printf("  %d of 300 random-move games reached 2048\n", wins);
    return h;
}

/* ------------------------------------------------------------ save/load */

static const char *bad[] = {
    "", "1", "14", "14b", "24b0000000000000000.0.0.0",
    "11b0000.0.0.0",                            /* size 1 */
    "19b" "0000000000000000000000000000000000000000000000000000000000000000000000000000000.0.0.0",
    "140" "0000000000000000.0.0.0",             /* goal 0 */
    "14b" "000000000000000.0.0.0",              /* 15 cells */
    "14b" "00000000000000000.0.0.0",            /* 17 cells */
    "14b" "000000000000000z.0.0.0",             /* exponent 35 is fine... */
    "14b" "0000000000000000.0.0",               /* two numbers */
    "14b" "0000000000000000.0.0.0.0",           /* four numbers */
    "14b" "0000000000000000.00.0.0",            /* leading zero */
    "14b" "0000000000000000.01.0.0",
    "14b" "0000000000000000.123456789.0.0",     /* 9 hex digits */
    "14b" "0000000000000000..0.0",
    "14b" "0000000000000000.0.0.0 ",
    "14b" "0000000000000000.A.0.0",             /* upper case */
    "14B" "0000000000000000.0.0.0",
    "14b" "000000000000000A.0.0.0",
    "14b" "0000000000000000.g.0.0",
    0
};

static void test_save(int verbose)
{
    struct t89_game g, h;
    char buf[T89_SAVELEN], mut[T89_SAVELEN], again[T89_SAVELEN], small[10];
    int t, i, len, pos, ok, accepted = 0;
    static const char alphabet[] = "0123456789abcdefghijklmnopqrstuvwxyz.A\377";

    for (i = 0; bad[i]; i++)
        if (i != 10)
            CHECK(t89_load(&g, bad[i]) < 0, "bad save accepted", bad[i]);
    CHECK(t89_load(&g, bad[10]) == 0, "good save rejected", bad[10]);

    /* The longest string fits T89_SAVELEN. */
    board(&g, T89_MAXSIZE);
    g.goal = T89_MAXEXP;
    for (i = 0; i < T89_CELLS; i++)
        g.cell[i] = T89_MAXEXP;
    g.score = g.moves = g.rng = 0xFFFFFFFFUL;
    len = t89_save(&g, buf, sizeof buf);
    CHECK(len > 0 && len < T89_SAVELEN, "longest save", buf);
    if (verbose)
        printf("  longest save string: %d characters\n", len);
    CHECK(t89_save(&g, small, sizeof small) < 0 && small[0] == '\0', "short buffer", "");
    CHECK(t89_save(&g, buf, len) < 0, "buffer one short", "");
    CHECK(t89_save(&g, buf, len + 1) == len, "exact buffer", "");

    for (t = 0; t < 3000; t++) {
        t89_new(&g, T89_MINSIZE + t % (T89_MAXSIZE - T89_MINSIZE + 1), (unsigned long) t);
        for (i = 0; i < t % 50; i++)
            t89_move(&g, trand(4), 0);
        len = t89_save(&g, buf, sizeof buf);
        CHECK(len > 0 && t89_load(&h, buf) == 0, "round trip", buf);
        CHECK(same(&g, &h) && g.score == h.score && g.moves == h.moves
              && g.rng == h.rng && g.goal == h.goal && g.size == h.size, "round trip", buf);

        /* A mutated string is either rejected or is exactly the save of
           what was loaded: one game, one string. */
        strcpy(mut, buf);
        pos = trand(len + 1);
        switch (trand(3)) {
        case 0: mut[pos] = alphabet[trand((int) sizeof alphabet - 1)]; if (pos == len) mut[pos + 1] = '\0'; break;
        case 1: memmove(mut + pos, mut + pos + 1, (size_t) (len - pos)); break;
        default: memmove(mut + pos + 1, mut + pos, (size_t) (len - pos + 1));
            mut[pos] = alphabet[trand((int) sizeof alphabet - 1)]; break;
        }
        h = g;
        ok = t89_load(&h, mut) == 0;
        if (ok) {
            accepted++;
            t89_save(&h, again, sizeof again);
            CHECK(strcmp(again, mut) == 0, "load not canonical", mut);
        } else
            CHECK(same(&g, &h) && g.rng == h.rng, "failed load changed game", mut);
    }
    if (verbose)
        printf("  %d of 3000 mutated save strings were still valid\n", accepted);
}

/* ------------------------------------------------------------ the rest */

static unsigned long test_rng(void)
{
    unsigned long h = 2166136261UL, state;
    unsigned int x;
    char buf[8];
    int i;

    r89_seed(&state, 1UL);
    for (i = 0; i < 1000; i++) {
        x = r89_next(&state);
        CHECK(x <= 0xFFFFU, "random range", "");
        sprintf(buf, "%u,", x);
        h = fnv(h, buf);
    }
    return h;
}

static void test_misc(void)
{
    struct t89_game g, h;
    struct t89_result r;

    CHECK(t89_new(&g, 1, 0) < 0 && t89_new(&g, 9, 0) < 0, "bad size accepted", "");
    CHECK(t89_new(&g, 0, 7) == 0 && g.size == 4, "default size", "");
    t89_new(&h, 4, 7);
    CHECK(same(&g, &h) && g.rng == h.rng, "same seed, same game", show(&g));
    t89_new(&h, 4, 8);
    CHECK(!same(&g, &h) || g.rng != h.rng, "different seeds", show(&h));
    h = g;
    CHECK(t89_move(&g, 4, &r) == 0 && t89_move(&g, -1, 0) == 0 && r.moved == 0
          && same(&g, &h) && g.rng == h.rng, "bad direction", show(&g));
    CHECK(t89_value(0) == 0 && t89_value(1) == 2 && t89_value(11) == 2048
          && t89_value(31) == 0x80000000UL && t89_value(32) == 0, "t89_value", "");

    /* A full board with no pairs is over; one pair and it isn't. */
    t89_load(&g, "14b1212212112122121.0.0.0");
    CHECK(t89_over(&g) && t89_empty(&g) == 0 && t89_spawn(&g, 0) == -1, "over", show(&g));
    t89_load(&g, "14b1212212112122111.0.0.0");
    CHECK(!t89_over(&g) && t89_can_move(&g, T89_LEFT) && t89_can_move(&g, T89_RIGHT)
          && t89_can_move(&g, T89_UP) && t89_can_move(&g, T89_DOWN), "not over", show(&g));

    /* Reaching the goal is reported once. */
    t89_load(&g, "14baa00000000000000.0.0.1");
    CHECK(t89_move(&g, T89_LEFT, &r) && (r.events & T89_EV_WON) && t89_won(&g), "win", show(&g));
    CHECK(!t89_move(&g, T89_LEFT, &r) || !(r.events & T89_EV_WON), "win again", show(&g));
}

int main(int argc, char **argv)
{
    int verbose = argc > 1 && strcmp(argv[1], "-g") == 0;
    unsigned long rng, games;

    printf("2089 core %s tests\n", T89_VERSION);
    test_rows();
    test_symmetry();
    test_motions();
    test_misc();
    test_save(verbose);
    rng = test_rng();
    games = playgames(verbose);
    if (verbose)
        printf("  GOLD_RNG 0x%08lXUL  GOLD_GAMES 0x%08lXUL\n", rng, games);
    CHECK(rng == GOLD_RNG, "golden RNG hash", "");
    CHECK(games == GOLD_GAMES, "golden games hash", "");
    printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
