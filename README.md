2089
---------------------------------------------------------------------------

2089 is a port of [2048](https://github.com/gabrielecirulli/2048) to standard C89 with a modular architecture that allows a core engine (`2089.c`) to support multiple frontends, such that it can be easily ported to a wide variety of systems and interfaces.

## Build requirements

- A C89 compiler
- Optional but recommended: POSIX (or GNU) `make`

## Building

On Unix-like systems:

```sh
make                # the text frontend (frontend/text/2089) and the tests
make check          # run the tests
```

`make` uses `cc` by default; name another compiler with `CC`, e.g. on AIX, where the
compiler usually isn't on the default `PATH`:

```sh
make CC=/usr/vac/bin/c89
```

Without `make`, compile the core (`2089.c`, `rng89.c`) together with a frontend,
telling the compiler where `2089.h` is:

```sh
cc -I. -o 2089 frontend/text/text.c 2089.c rng89.c
```

The core needs no C library at all, so it builds for freestanding targets too. The text
frontend needs only the standard C89 library: it works on any terminal, from a teletype
up (`2089 -h` for options; `-c` gives a compact display for slow lines).

## Other C implementations of 2048

There's much prior art inspiring 2089, which is far from the first attempt to implement this game in C, though it is perhaps the first attempt at an explicitly "universal" implementation capable of supporting multiple platforms, interfaces and compilers. A few other projects include:

- [2048.c](https://github.com/mevdschee/2048.c): A C99 console implementation targeting Linux, that has also been successfully used as the basis for a [Palm OS](https://github.com/nizovn/2048.prc) version
- [2048-cli](https://github.com/tiehuis/2048-cli): Another Linux console implementation with both plain VT and ncurses frontends
- [libretro-2048](https://github.com/libretro/libretro-2048): A port of 2048 to [libretro](https://www.libretro.com/index.php/api/)

2089 uses no code from these projects, unless explicitly noted.
