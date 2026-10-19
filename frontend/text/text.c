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
 * the game.
 *
 *     2089 [-c] [-u] [-s size] [-r seed] [-l savestring] [-v] [-h]
 *
 * Author: Claude Opus 5.5 (claude-opus-5-5), October 2026.
 * MIT License; see LICENSE.
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
    puts("usage: 2089 [-c] [-u] [-s size] [-r seed] [-l savestring] [-v] [-h]");
    puts("  -c             compact display, for slow lines and paper terminals");
    puts("  -u             allow undo (u takes back the last move)");
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

/* Print how to resume this game, with the same options. */
static void goodbye(const struct t89_game *g, int allowundo, int compact)
{
    char save[T89_SAVELEN];

    t89_save(g, save, sizeof save);
    printf("to carry on later: 2089 %s%s-l %s\n",
           compact ? "-c " : "", allowundo ? "-u " : "", save);
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

    tiletext(buf, t89_best(g));
    w = (int) strlen(buf) > 4 ? (int) strlen(buf) : 4;  /* steady columns */
    printf("score %lu  moves %lu  best %s\n", g->score, g->moves, buf);
    for (r = 0; r < n; r++) {
        for (c = 0; c < n; c++) {
            tiletext(buf, g->cell[r * n + c]);
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

/*
 * The escape sequence starting at line[*i]: the move for an arrow key, or 0.
 * Knows ESC [ A (ANSI, VT100 in cursor key mode, xterm, aixterm), ESC O A
 * (VT100 in application mode) and ESC A (VT52), for A to D, with or without
 * parameters (ESC [ 1 ; 2 A is shift-up on xterm).  Leaves *i on the last
 * byte of the sequence, so that other sequences (function keys and so on)
 * are skipped whole and their letters aren't read as keys.  ('\033' is ESC
 * in ASCII; EBCDIC terminals don't send these sequences.)
 */
static int escape(const char *line, int *i)
{
    int j = *i + 1;

    if (line[j] == '[' || line[j] == 'O')
        j++;
    while ((line[j] >= '0' && line[j] <= '9') || line[j] == ';')
        j++;
    if (line[j] == '\0' || line[j] == '\n') {
        *i = j - 1;                     /* cut short: skip what there is */
        return 0;
    }
    *i = j;
    switch (line[j]) {
    case 'A': return 'w';
    case 'B': return 's';
    case 'C': return 'd';
    case 'D': return 'a';
    default:  return 0;
    }
}

/* The command a key stands for (w, d, s, a for moves), or 0 for none. */
static int command(int key)
{
    /* Either case: Caps Lock, or a terminal with no lower case. */
    switch (tolower(key)) {
    case 'w': case 'k': case '8': return 'w';
    case 'd': case 'l': case '6': return 'd';
    case 's': case 'j': case '2': return 's';
    case 'a': case 'h': case '4': return 'a';
    case 'u': return 'u';
    case 'n': return 'n';
    case 'p': return 'p';
    case '?': return '?';
    case 'c': return 'c';
    case 'q': return 'q';
    default:  return 0;
    }
}

int main(int argc, char **argv)
{
    struct t89_game g, undo, before;
    struct t89_result r;
    char line[256], save[T89_SAVELEN];
    const char *load = 0;
    unsigned long seed = newseed(0);
    int size = T89_SIZE, i, dir, canundo = 0, over = 0, compact = 0, allowundo = 0;
    int cmd, keys, k, redraw;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0)
            usage(EXIT_SUCCESS);
        if (strcmp(argv[i], "-c") == 0) {
            compact = 1;
            continue;
        }
        if (strcmp(argv[i], "-u") == 0) {
            allowundo = 1;
            continue;
        }
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
    draw(&g, compact);
    for (;;) {
        printf("%s> ", over ? "game over " : "");
        fflush(stdout);
        if (!fgets(line, sizeof line, stdin)) {
            putchar('\n');         /* end of input: still say how to resume */
            goodbye(&g, allowundo, compact);
            return EXIT_SUCCESS;
        }
        /* The last recognised key on the line is the turn. */
        cmd = keys = 0;
        for (i = 0; line[i]; i++) {
            k = line[i] == '\033' ? escape(line, &i) : command((unsigned char) line[i]);
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
        case 'n':
        case 'q':
            if (keys > 1) {
                printf("%c ignored: type it on a line of its own\n", cmd);
                break;
            }
            if (cmd == 'q') {
                goodbye(&g, allowundo, compact);
                return EXIT_SUCCESS;
            }
            t89_new(&g, g.size, newseed(g.rng));
            canundo = over = 0;
            redraw = 1;
            break;
        case 'u':
            if (!allowundo)
                puts("undo is off: start 2089 with -u to allow it");
            else if (canundo) {
                g = undo;
                canundo = over = 0;
                redraw = 1;
            } else
                puts("nothing to undo");
            break;
        case 'p':
            t89_save(&g, save, sizeof save);
            printf("save string: %s\n", save);
            break;
        case '?':
            help(allowundo);
            break;
        case 'c':
            compact = !compact;
            redraw = 1;
            break;
        default:
            dir = cmd == 'w' ? T89_UP : cmd == 'd' ? T89_RIGHT
                : cmd == 's' ? T89_DOWN : T89_LEFT;
            /* Keep the undo copy only if the move changes something. */
            before = g;
            if (t89_move(&g, dir, &r)) {
                undo = before;
                canundo = 1;
                redraw = 1;
                if (r.events & T89_EV_WON)
                    printf("\n*** %lu!  You win.  Keep going if you like. ***\n",
                           t89_value(g.goal));
                if (r.events & T89_EV_OVER)
                    over = 1;
            } else if (over)
                gameover(allowundo);
            else
                puts("can't move that way");
            break;
        }
        if (!redraw)
            continue;
        draw(&g, compact);
        if (over) {
            putchar('\n');
            gameover(allowundo);
        }
    }
}
