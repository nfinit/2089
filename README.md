2089: An industry-standard implementation of 2048
---------------------------------------------------------------------------

2089 is a port of [2048](https://github.com/gabrielecirulli/2048) to standard C89 with a modular architecture that allows a core engine (`2089.c`) to support multiple frontends, such that it can be easily ported to a wide variety of systems and interfaces.

2089 is currently developed and tested on Linux (with GCC) and AIX 4.3.3 (with XL C,) with legacy terminal support tested using Ericom's PowerTerm 525 on Windows NT.

## Build requirements

- A C89 compiler
- Optional but recommended: POSIX (or GNU) `make`

## Building

On Unix-like systems:

```sh
make                # the text frontend and the tests, plus the full program
                    # (frontend/curses/2089) if a curses library can be found
make text           # the text frontend and the tests only
make run            # build, then play (make run ARGS="-u -s 5" passes options)
make check          # run the tests
make cgi            # the generic CGI frontend, frontend/cgi/2089.cgi
make check-cgi      # build the CGI frontend and run its HTTP-shape checks
```

`make` uses `cc` by default; name another compiler with `CC`:

```sh
make CC=c89
```

Without `make`, compile the core (`2089.c`, `rng89.c`) with the program
(`frontend/main.c`, `frontend/common.c`) and line mode (`frontend/text/text.c`), telling
the compiler where the headers are; for the full program, add `-DT89_CURSES`,
`frontend/curses/curses.c` and the curses library:

```sh
cc -I. -Ifrontend -o 2089 frontend/main.c frontend/text/text.c frontend/common.c 2089.c rng89.c
cc -I. -Ifrontend -DT89_CURSES -o 2089 frontend/main.c frontend/text/text.c \
    frontend/curses/curses.c frontend/common.c 2089.c rng89.c -lcurses
```

The core needs no C library at all, so it builds for freestanding targets too. The text
frontend needs only the standard C89 library: it works on any terminal, from a teletype
up (`2089 -h` for options; `-k` gives a compact display for slow lines). The full
program plays full screen through curses, and falls back to line mode by itself when
the terminal can't (a pipe, an unknown or hardcopy terminal, a screen too small for the
board) or when asked with `-t`. It needs an X/Open curses (SVR4 curses, ncurses, NetBSD curses,
PDCurses); if the library isn't found (`make` tries `-lcurses`, `-lncurses`, and those with `-ltinfo` or
`-ltermcap`), name it: `make CURSES='-L/opt/lib -lcurses'`.

Full screen, the best score for each board size is kept in `~/.2089`, as the save
string of the game that made it (so `2089 -l` brings it back). To keep it somewhere
else, build with `-DT89_SCORES='"/path/to/file"'`.

## CGI frontend

The generic CGI frontend keeps the whole game in the URL. Build it with
`make cgi`, then run it under any CGI server (or set `QUERY_STRING` by hand):

```sh
QUERY_STRING='' frontend/cgi/2089.cgi          # a new 4x4 game
QUERY_STRING='n=4' frontend/cgi/2089.cgi      # the same, explicitly
```

The save string is the state; a move is a GET such as
`2089.cgi?s=<save>&m=left`. There are no sessions, cookies or server-side
games, and the browser Back button is the only undo. The frontend draws an
HTML table board so that the rules can be tested before the server-composited
GIF board and the obsidian skin are added. Links use the CGI `SCRIPT_NAME`, so
the program can be installed under any path or name.

## Other C implementations of 2048

There's much prior art inspiring 2089, which is far from the first attempt to implement this game in C, though it is perhaps the first attempt at an explicitly "universal" implementation capable of supporting multiple platforms, interfaces and compilers. A few other projects include:

- [2048.c](https://github.com/mevdschee/2048.c): A C99 console implementation targeting Linux, that has also been successfully used as the basis for a [Palm OS](https://github.com/nizovn/2048.prc) version
- [2048-cli](https://github.com/tiehuis/2048-cli): Another Linux console implementation with both plain VT and ncurses frontends
- [libretro-2048](https://github.com/libretro/libretro-2048): A port of 2048 to [libretro](https://www.libretro.com/index.php/api/)

2089 uses no code from these projects, unless explicitly noted.
