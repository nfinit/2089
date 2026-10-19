/*
 * replay.c - play games given on standard input, print each final state.
 * Used by oracle.py to compare the core with an independent model.
 *
 *     input:  size seed moves     e.g.  4 1234 udlrrd
 *     output: the save string after playing the moves (no-op moves allowed)
 *
 * Author: Claude Opus 5.5 (claude-opus-5-5), October 2026.
 * MIT License; see LICENSE.
 */

#include <stdio.h>
#include "2089.h"

int main(void)
{
    struct t89_game g;
    char moves[4096], buf[T89_SAVELEN];
    unsigned long seed;
    int size, i, dir;

    while (scanf("%d %lu %4095s", &size, &seed, moves) == 3) {
        if (t89_new(&g, size, seed) < 0) {
            puts("bad size");
            continue;
        }
        for (i = 0; moves[i]; i++) {
            switch (moves[i]) {
            case 'u': dir = T89_UP; break;
            case 'r': dir = T89_RIGHT; break;
            case 'd': dir = T89_DOWN; break;
            default:  dir = T89_LEFT; break;
            }
            t89_move(&g, dir, 0);
        }
        t89_save(&g, buf, sizeof buf);
        puts(buf);
    }
    return 0;
}
