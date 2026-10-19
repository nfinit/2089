/*
 * text.c - 2089 for any terminal: plain ASCII, one line of input at a time.
 *
 * Nothing but C89 <stdio.h>: no escape codes, no raw keyboard, no colour, so
 * it works on a teletype, a VT100, a VMS console or a DOS box.  Type one or
 * more moves and press Return.
 *
 *     2089 [-s size] [-r seed] [-l savestring] [-v] [-h]
 *
 * Copyright (c) 2026 ict.  MIT License; see LICENSE.
 */

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "2089.h"

static void usage(int status)
{
    puts("usage: 2089 [-s size] [-r seed] [-l savestring] [-v] [-h]");
    puts("  -s size        board size, 2 to 8 (default 4)");
    puts("  -r seed        start the game from this number (same seed, same game)");
    puts("  -l savestring  carry on a saved game");
    puts("  -v             print the version");
    exit(status);
}

/*
 * A whole decimal number from min to max, or exit with the usage message.
 * (errno catches values too big for a 32-bit long, which strtoul clamps.)
 */
static unsigned long number(const char *s, unsigned long min, unsigned long max)
{
    char *end;
    unsigned long x;

    if (*s < '0' || *s > '9')
        usage(EXIT_FAILURE);
    errno = 0;
    x = strtoul(s, &end, 10);
    if (*end != '\0' || errno == ERANGE || x < min || x > max)
        usage(EXIT_FAILURE);
    return x;
}

/* The tile as text: its value, or 2^n if that doesn't fit in 32 bits. */
static void tiletext(char *buf, int exp)
{
    if (exp == 0)
        buf[0] = '\0';
    else if (t89_value(exp) != 0)
        sprintf(buf, "%lu", t89_value(exp));
    else
        sprintf(buf, "2^%d", exp);
}

/*
 * A seed for a new game.  time() alone repeats within a second, so mix in
 * clock() and salt: the RNG state of the game being replaced, which differs
 * after every new game.  (Only C89 sources: no getpid().)
 */
static unsigned long newseed(unsigned long salt)
{
    unsigned long s = (unsigned long) time((time_t *) 0);

    s ^= ((unsigned long) clock() * 0x9E3779B9UL) & 0xFFFFFFFFUL;
    s ^= (salt * 0x85EBCA6BUL) & 0xFFFFFFFFUL;
    return s;
}

/* Print how to resume this game. */
static void goodbye(const struct t89_game *g)
{
    char save[T89_SAVELEN];

    t89_save(g, save, sizeof save);
    printf("to carry on later: 2089 -l %s\n", save);
}

static void rule(int n, int w)
{
    int c, i;

    for (c = 0; c < n; c++) {
        putchar('+');
        for (i = 0; i < w + 2; i++)
            putchar('-');
    }
    puts("+");
}

static void draw(const struct t89_game *g)
{
    char buf[16];
    int n = g->size, r, c, w = 4;

    tiletext(buf, t89_best(g));
    if ((int) strlen(buf) > w)
        w = (int) strlen(buf);
    printf("\nscore %lu   moves %lu   best %s\n\n", g->score, g->moves, buf);
    for (r = 0; r < n; r++) {
        rule(n, w);
        for (c = 0; c < n; c++) {
            tiletext(buf, g->cell[r * n + c]);
            printf("| %*s ", w, buf);
        }
        puts("|");
    }
    rule(n, w);
}

static void help(void)
{
    puts("\nmoves: w a s d (or k h j l, or 8 4 2 6); several per line are fine");
    puts("u  undo the last move     n  new game     p  print the save string");
    puts("?  this help               q  quit");
}

int main(int argc, char **argv)
{
    struct t89_game g, undo, before;
    struct t89_result r;
    char line[256], save[T89_SAVELEN];
    const char *load = 0;
    unsigned long seed = newseed(0);
    int size = T89_SIZE, i, dir, canundo = 0, over = 0;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0)
            usage(EXIT_SUCCESS);
        if (strcmp(argv[i], "-v") == 0) {
            printf("2089 %s\n", T89_VERSION);
            return EXIT_SUCCESS;
        }
        if (i + 1 >= argc)
            usage(EXIT_FAILURE);
        if (strcmp(argv[i], "-s") == 0)
            size = (int) number(argv[++i], T89_MINSIZE, T89_MAXSIZE);
        else if (strcmp(argv[i], "-r") == 0)
            seed = number(argv[++i], 0, 0xFFFFFFFFUL);
        else if (strcmp(argv[i], "-l") == 0)
            load = argv[++i];
        else
            usage(EXIT_FAILURE);
    }
    if (load) {
        if (t89_load(&g, load) < 0) {
            puts("2089: not a valid save string");
            return EXIT_FAILURE;
        }
    } else if (t89_new(&g, size, seed) < 0) {
        printf("2089: the size must be %d to %d\n", T89_MINSIZE, T89_MAXSIZE);
        return EXIT_FAILURE;
    }

    over = t89_over(&g);
    printf("2089 %s - join the tiles, get to %lu!  (? for help)\n",
           T89_VERSION, t89_value(g.goal));
    draw(&g);
    for (;;) {
        printf("%s> ", over ? "game over " : "");
        fflush(stdout);
        if (!fgets(line, sizeof line, stdin)) {
            putchar('\n');         /* end of input: still say how to resume */
            goodbye(&g);
            return EXIT_SUCCESS;
        }
        for (i = 0; line[i]; i++) {
            /* Either case: Caps Lock, or a terminal with no lower case. */
            switch (tolower((unsigned char) line[i])) {
            case 'w': case 'k': case '8': dir = T89_UP; break;
            case 'd': case 'l': case '6': dir = T89_RIGHT; break;
            case 's': case 'j': case '2': dir = T89_DOWN; break;
            case 'a': case 'h': case '4': dir = T89_LEFT; break;
            case 'u':
                if (canundo) {
                    g = undo;
                    canundo = over = 0;
                } else
                    puts("nothing to undo");
                continue;
            case 'n':
                t89_new(&g, g.size, newseed(g.rng));
                canundo = over = 0;
                continue;
            case 'p':
                t89_save(&g, save, sizeof save);
                printf("save string: %s\n", save);
                continue;
            case '?':
                help();
                continue;
            case 'q':
                goodbye(&g);
                return EXIT_SUCCESS;
            default:
                continue;
            }
            /* Keep the undo copy only if the move changes something. */
            before = g;
            if (t89_move(&g, dir, &r)) {
                undo = before;
                canundo = 1;
                if (r.events & T89_EV_WON)
                    printf("\n*** %lu!  You win.  Keep going if you like. ***\n",
                           t89_value(g.goal));
                if (r.events & T89_EV_OVER)
                    over = 1;
            }
        }
        draw(&g);
        if (over)
            puts("\nNo moves left.  u to undo, n for a new game, q to quit.");
    }
}
