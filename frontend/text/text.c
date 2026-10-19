/*
 * text.c - 2089 for any terminal: plain ASCII, one line of input at a time.
 *
 * Nothing but the C89 library: no escape codes, no raw keyboard, no colour,
 * so it works on a teletype, a VT100, a VMS console or a DOS box.
 *
 * One line is one turn.  If a line holds several keys, the last recognised
 * one counts, so a key mashed on the way to Return corrects the one before
 * it.  n (new game) and q (quit) count only on a line of their own, so a
 * slip can't end a game.  Arrow keys work too: their escape sequences arrive
 * in the line like any other key (no raw keyboard needed).
 *
 * Undo is off unless asked for with -u: living with a bad move is part of
 * the game.  Keys, arrow sequences and options come from ../common.c, shared
 * with the other frontends.
 *
 *     2089 [-c] [-u] [-s size] [-r seed] [-l savestring] [-v] [-h]
 *
 * Author: Claude Opus 5.5 (claude-opus-5-5), October 2026.
 * MIT License; see LICENSE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"

static void usage(int status)
{
    puts("usage: 2089 [-c] [-u] [-s size] [-r seed] [-l savestring] [-v] [-h]");
    puts("  -c             compact display, for slow lines and paper terminals");
    f89_usage();
    puts("  -v             print the version");
    exit(status);
}

/* Print how to resume this game, with the same options. */
static void goodbye(const struct t89_game *g, int allowundo, int compact)
{
    char command[16];

    sprintf(command, "2089 %s%s", compact ? "-c " : "", allowundo ? "-u " : "");
    f89_resume(g, command);
}

/* What to do when no move is left. */
static void gameover(int allowundo)
{
    printf("No moves left.  %sn for a new game, q to quit.\n",
           allowundo ? "u to undo, " : "");
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

/*
 * Compact display for slow lines and paper: a status line and one line of
 * numbers per row, "." for an empty cell.  About 110 characters for 4x4,
 * against about 400 for the boxed board.
 */
static void drawcompact(const struct t89_game *g)
{
    char buf[16];
    int n = g->size, r, c, w;

    f89_tile(buf, t89_best(g));
    w = (int) strlen(buf) > 4 ? (int) strlen(buf) : 4;  /* steady columns */
    printf("score %lu  moves %lu  best %s\n", g->score, g->moves, buf);
    for (r = 0; r < n; r++) {
        for (c = 0; c < n; c++) {
            f89_tile(buf, g->cell[r * n + c]);
            printf(" %*s", w, buf[0] ? buf : ".");
        }
        putchar('\n');
    }
}

static void draw(const struct t89_game *g, int compact)
{
    char buf[16];
    int n = g->size, r, c, w = 4;

    if (compact) {
        drawcompact(g);
        return;
    }
    f89_tile(buf, t89_best(g));
    if ((int) strlen(buf) > w)
        w = (int) strlen(buf);
    printf("\nscore %lu   moves %lu   best %s\n\n", g->score, g->moves, buf);
    for (r = 0; r < n; r++) {
        rule(n, w);
        for (c = 0; c < n; c++) {
            f89_tile(buf, g->cell[r * n + c]);
            printf("| %*s ", w, buf);
        }
        puts("|");
    }
    rule(n, w);
}

static void help(int allowundo)
{
    puts("\nmoves: w a s d (or k h j l, 8 4 2 6, or the arrow keys), then Return");
    puts("one move per line: if you type several keys, the last one counts");
    if (allowundo)
        puts("u  undo the last move     n  new game     p  print the save string");
    else
        puts("n  new game     p  print the save string     (undo is off: see -u)");
    puts("c  compact display on/off  ?  this help     q  quit");
    puts("n and q work only on a line of their own; Return alone redraws the board");
}

int main(int argc, char **argv)
{
    struct t89_game g, undo, before;
    struct t89_result r;
    struct f89_options o;
    char line[256], save[T89_SAVELEN];
    int i, canundo = 0, over = 0, compact = 0;
    int cmd, keys, k, len, redraw;

    f89_defaults(&o);
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0)
            usage(EXIT_SUCCESS);
        if (strcmp(argv[i], "-c") == 0) {
            compact = 1;
            continue;
        }
        if (strcmp(argv[i], "-v") == 0) {
            printf("2089 %s\n", T89_VERSION);
            return EXIT_SUCCESS;
        }
        if (f89_option(argc, argv, &i, &o) <= 0)
            usage(EXIT_FAILURE);
    }
    if (f89_start(&g, &o, "2089") < 0)
        return EXIT_FAILURE;

    over = t89_over(&g);
    printf("2089 %s - join the tiles, get to %lu!  (? for help)\n",
           T89_VERSION, t89_value(g.goal));
    draw(&g, compact);
    for (;;) {
        printf("%s> ", over ? "game over " : "");
        fflush(stdout);
        if (!fgets(line, sizeof line, stdin)) {
            putchar('\n');         /* end of input: still say how to resume */
            goodbye(&g, o.undo, compact);
            return EXIT_SUCCESS;
        }
        /* The last recognised key on the line is the turn. */
        cmd = keys = 0;
        for (i = 0; line[i]; i++) {
            if (line[i] == '\033') {
                k = f89_escape(line + i + 1, &len);
                i += len;
            } else
                k = f89_key((unsigned char) line[i]);
            if (k) {
                cmd = k;
                keys++;
            }
        }
        redraw = 0;
        switch (cmd) {
        case 0:                         /* Return alone (or nothing known) */
            redraw = 1;
            break;
        case F89_NEW:
        case F89_QUIT:
            if (keys > 1) {
                printf("%c ignored: type it on a line of its own\n",
                       cmd == F89_NEW ? 'n' : 'q');
                break;
            }
            if (cmd == F89_QUIT) {
                goodbye(&g, o.undo, compact);
                return EXIT_SUCCESS;
            }
            t89_new(&g, g.size, f89_seed(g.rng));
            canundo = over = 0;
            redraw = 1;
            break;
        case F89_UNDO:
            if (!o.undo)
                puts("undo is off: start 2089 with -u to allow it");
            else if (canundo) {
                g = undo;
                canundo = over = 0;
                redraw = 1;
            } else
                puts("nothing to undo");
            break;
        case F89_SAVE:
            t89_save(&g, save, sizeof save);
            printf("save string: %s\n", save);
            break;
        case F89_HELP:
            help(o.undo);
            break;
        case F89_DISPLAY:
            compact = !compact;
            redraw = 1;
            break;
        default:
            /* Keep the undo copy only if the move changes something. */
            before = g;
            if (t89_move(&g, f89_dir(cmd), &r)) {
                undo = before;
                canundo = 1;
                redraw = 1;
                if (r.events & T89_EV_WON)
                    printf("\n*** %lu!  You win.  Keep going if you like. ***\n",
                           t89_value(g.goal));
                if (r.events & T89_EV_OVER)
                    over = 1;
            } else if (over)
                gameover(o.undo);
            else
                puts("can't move that way");
            break;
        }
        if (!redraw)
            continue;
        draw(&g, compact);
        if (over) {
            putchar('\n');
            gameover(o.undo);
        }
    }
}
