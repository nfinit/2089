2089
---------------------------------------------------------------------------

2089 is a port of [2048](https://github.com/gabrielecirulli/2048) to standard C89 with a modular architecture that allows a core engine (`2089.c`) to support multiple frontends, such that it can be easily ported to a wide variety of systems and interfaces.

## Build requirements

- A C89 compiler
- Optional but recommended: POSIX (or GNU) `make`

## Other C implementations of 2048

There's much prior art inspiring 2089, which is far from the first attempt to implement this game in C, though it is perhaps the first attempt at an explicitly "universal" implementation capable of supporting multiple platforms, interfaces and compilers. A few other projects include:

- [2048.c](https://github.com/mevdschee/2048.c): A C99 console implementation targeting Linux, that has also been successfully used as the basis for a [Palm OS](https://github.com/nizovn/2048.prc) version
- [2048-cli](https://github.com/tiehuis/2048-cli): Another Linux console implementation with both plain VT and ncurses frontends
- [libretro-2048](https://github.com/libretro/libretro-2048): A port of 2048 to [libretro](https://www.libretro.com/index.php/api/)

2089 uses no code from these projects, unless explicitly noted.
