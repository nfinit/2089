/*
 * main.c - the 2089 program: options, the game, and how to play it.
 *
 * Built two ways.  Plain, it is the text frontend: line mode only, nothing
 * but the C89 library (frontend/text/2089).  With T89_CURSES defined and
 * linked with frontend/curses/curses.c and a curses library, it plays full
 * screen when the terminal can, and falls back to line mode when it can't:
 * output or input not a terminal, an unknown TERM, a hardcopy terminal, a
 * screen too small for the board, or -t (frontend/curses/2089).  -k means
 * compact in either mode: one-line cells, or the compact line display.
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
#ifdef T89_CURSES
    puts("usage: 2089 [-t] [-k] [-K] [-m] [-p] [-a] [-u] [-s size] [-r seed]");
    puts("            [-l savestring] [-v] [-h]");
    puts("  -t             line mode: one move per line, for any terminal");
    puts("  -k             compact: cells one line high (the default at 4800 baud or");
    puts("                 less in colour or mono); in line mode, the compact display");
    puts("  -K             not compact: tall cells even on a slow line, boxed display");
    puts("  -m             mono: no colour, tiles in bold and reverse video");
    puts("  -p             plain: no video attributes at all (Wyse 50 and the like)");
    puts("  -a             draw boxes with + - | instead of line-drawing characters");
#else
    puts("usage: 2089 [-k] [-K] [-u] [-s size] [-r seed] [-l savestring] [-v] [-h]");
    puts("  -k             compact display, for slow lines and paper terminals");
    puts("  -K             boxed display (the default)");
#endif
    f89_usage();
    puts("  -v             print the version");
    exit(status);
}

int main(int argc, char **argv)
{
    struct t89_game g;
    struct f89_options o;
    int i;
#ifdef T89_CURSES
    char why[80];
    int status;
#endif

    f89_defaults(&o);
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0)
            usage(EXIT_SUCCESS);
        if (strcmp(argv[i], "-k") == 0 || strcmp(argv[i], "-K") == 0) {
            o.compact = argv[i][1] == 'k';
            o.cells = o.compact ? F89_CELLS_SMALL : F89_CELLS_TALL;
            continue;
        }
        if (strcmp(argv[i], "-v") == 0) {
#ifdef T89_CURSES
            printf("2089 %s (curses)\n", T89_VERSION);
#else
            printf("2089 %s\n", T89_VERSION);
#endif
            return EXIT_SUCCESS;
        }
#ifdef T89_CURSES
        if (strcmp(argv[i], "-t") == 0) {
            o.line = 1;
            continue;
        }
        if (strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "-p") == 0) {
            o.style = argv[i][1] == 'm' ? F89_MONO : F89_PLAIN;
            o.forced = 1;
            continue;
        }
        if (strcmp(argv[i], "-a") == 0) {
            o.ascii = 1;
            continue;
        }
#endif
        if (f89_option(argc, argv, &i, &o) <= 0)
            usage(EXIT_FAILURE);
    }
    if (f89_start(&g, &o, "2089") < 0)
        return EXIT_FAILURE;

#ifdef T89_CURSES
    if (!o.line) {
        if (f89_fsok(why, sizeof why)) {
            status = f89_screen(&g, &o);
            if (status >= 0)
                return status;
        } else if (why[0])
            printf("2089: %s: playing in line mode\n", why);
    }
#endif
    return f89_line(&g, &o);
}
