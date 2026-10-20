/*
 * probe.c - does this system have the X/Open curses 2089 needs?  make builds
 * it to decide whether to build the curses frontend; it is never installed.
 * It only has to compile and link: the calls are never made.
 *
 * Author: Claude Opus 5.5 (claude-opus-5-5), October 2026.
 * MIT License; see LICENSE.
 */

#define _XOPEN_SOURCE

#include <curses.h>
#include <term.h>

int main(int argc, char **argv)
{
    int err;

    (void) argv;
    if (argc > 99) {            /* never true, but the compiler can't know */
        setupterm((char *) 0, 1, &err);
        (void) tigetstr("cup");
        initscr();
        endwin();
    }
    return 0;
}
