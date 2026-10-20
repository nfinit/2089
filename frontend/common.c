/*
 * common.c - what every 2089 frontend on a hosted C89 system shares.  See
 * common.h.
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
#include "common.h"

int f89_key(int ch)
{
    switch (tolower(ch)) {
    case 'w': case 'k': case '8': return F89_UP;
    case 'd': case 'l': case '6': return F89_RIGHT;
    case 's': case 'j': case '2': return F89_DOWN;
    case 'a': case 'h': case '4': return F89_LEFT;
    case 'u': return F89_UNDO;
    case 'n': return F89_NEW;
    case 'p': return F89_SAVE;
    case '?': return F89_HELP;
    case 'c': return F89_DISPLAY;
    case 'q': return F89_QUIT;
    default:  return 0;
    }
}

int f89_dir(int cmd)
{
    switch (cmd) {
    case F89_UP:    return T89_UP;
    case F89_RIGHT: return T89_RIGHT;
    case F89_DOWN:  return T89_DOWN;
    case F89_LEFT:  return T89_LEFT;
    default:        return -1;
    }
}

int f89_intro(int ch)
{
    return ch == '\033' || ch == 0x9B || ch == 0x8F;
}

int f89_escape(const char *s, int *len)
{
    int j = 1;

    if ((unsigned char) s[0] == '\033' && (s[1] == '[' || s[1] == 'O'))
        j++;
    while ((s[j] >= '0' && s[j] <= '9') || s[j] == ';')
        j++;
    if (s[j] == '\0' || s[j] == '\n') {
        *len = j;                       /* cut short: skip what there is */
        return 0;
    }
    *len = j + 1;
    switch (s[j]) {
    case 'A': return F89_UP;
    case 'B': return F89_DOWN;
    case 'C': return F89_RIGHT;
    case 'D': return F89_LEFT;
    default:  return 0;
    }
}

unsigned long f89_seed(unsigned long salt)
{
    unsigned long s = (unsigned long) time((time_t *) 0);

    s ^= ((unsigned long) clock() * 0x9E3779B9UL) & 0xFFFFFFFFUL;
    s ^= (salt * 0x85EBCA6BUL) & 0xFFFFFFFFUL;
    return s;
}

void f89_tile(char *buf, int exp)
{
    if (exp == 0)
        buf[0] = '\0';
    else if (t89_value(exp) != 0)
        sprintf(buf, "%lu", t89_value(exp));
    else
        sprintf(buf, "2^%d", exp);
}

void f89_defaults(struct f89_options *o)
{
    o->size = T89_SIZE;
    o->seed = f89_seed(0);
    o->load = 0;
    o->undo = 0;
    o->line = 0;
    o->compact = 0;
    o->style = F89_COLOUR;
    o->forced = 0;
    o->ascii = 0;
    o->cells = F89_CELLS_AUTO;
}

/*
 * A whole decimal number from min to max into *x; 0 on success, -1 if not.
 * (errno catches values too big for a 32-bit long, which strtoul clamps.)
 */
static int number(const char *s, unsigned long min, unsigned long max,
                  unsigned long *x)
{
    char *end;

    if (*s < '0' || *s > '9')
        return -1;
    errno = 0;
    *x = strtoul(s, &end, 10);
    if (*end != '\0' || errno == ERANGE || *x < min || *x > max)
        return -1;
    return 0;
}

int f89_option(int argc, char **argv, int *i, struct f89_options *o)
{
    const char *a = argv[*i];
    unsigned long x;

    if (strcmp(a, "-u") == 0) {
        o->undo = 1;
        return 1;
    }
    if (strcmp(a, "-s") != 0 && strcmp(a, "-r") != 0 && strcmp(a, "-l") != 0)
        return 0;
    if (*i + 1 >= argc)
        return -1;
    (*i)++;
    if (a[1] == 'l') {
        o->load = argv[*i];
    } else if (a[1] == 's') {
        if (number(argv[*i], T89_MINSIZE, T89_MAXSIZE, &x) < 0)
            return -1;
        o->size = (int) x;
    } else {
        if (number(argv[*i], 0, 0xFFFFFFFFUL, &x) < 0)
            return -1;
        o->seed = x;
    }
    return 1;
}

void f89_usage(void)
{
    puts("  -u             allow undo (u takes back the last move)");
    puts("  -s size        board size, 2 to 8 (default 4)");
    puts("  -r seed        start the game from this number (same seed, same game)");
    puts("  -l savestring  carry on a saved game");
}

int f89_start(struct t89_game *g, const struct f89_options *o, const char *name)
{
    if (o->load) {
        if (t89_load(g, o->load) < 0) {
            printf("%s: not a valid save string\n", name);
            return -1;
        }
    } else if (t89_new(g, o->size, o->seed) < 0) {
        printf("%s: the size must be %d to %d\n", name, T89_MINSIZE, T89_MAXSIZE);
        return -1;
    }
    return 0;
}

void f89_resume(const struct t89_game *g, const char *command)
{
    char save[T89_SAVELEN];

    t89_save(g, save, sizeof save);
    printf("to carry on later: %s-l %s\n", command, save);
}
