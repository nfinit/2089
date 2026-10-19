/*
 * text.c - 2089 for any terminal: plain ASCII, one line of input at a time.
 *
 * Nothing but C89 <stdio.h>: no escape codes, no raw keyboard, no colour, so
 * it works on a teletype, a VT100, a VMS console or a DOS box.  Type one or
 * more moves and press Return.
 *
 *     2089 [-s size] [-r seed] [-l savestring]
 *
 * Copyright (c) 2026 ict.  MIT License; see LICENSE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../../2089.h"

static void usage(void)
{
    puts("usage: 2089 [-s size] [-r seed] [-l savestring]");
    exit(EXIT_FAILURE);
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
    struct t89_game g, undo;
    struct t89_result r;
    char line[256], save[T89_SAVELEN];
    const char *load = 0;
    unsigned long seed = (unsigned long) time((time_t *) 0);
    int size = T89_SIZE, i, dir, canundo = 0, over = 0;

    for (i = 1; i < argc; i++) {
        if (i + 1 >= argc)
            usage();
        if (strcmp(argv[i], "-s") == 0)
            size = atoi(argv[++i]);
        else if (strcmp(argv[i], "-r") == 0)
            seed = strtoul(argv[++i], (char **) 0, 10);
        else if (strcmp(argv[i], "-l") == 0)
            load = argv[++i];
        else
            usage();
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
        if (!fgets(line, sizeof line, stdin))
            break;
        for (i = 0; line[i]; i++) {
            switch (line[i]) {
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
                t89_new(&g, g.size, (unsigned long) time((time_t *) 0));
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
                t89_save(&g, save, sizeof save);
                printf("to carry on later: 2089 -l %s\n", save);
                return EXIT_SUCCESS;
            default:
                continue;
            }
            undo = g;
            if (t89_move(&g, dir, &r)) {
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
    return EXIT_SUCCESS;
}
