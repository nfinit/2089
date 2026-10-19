/*
 * rng89.h - a small portable random number generator for C89.
 *
 * rng89 gives the same sequence on every platform and compiler: 16-, 32- or
 * 64-bit int and long, either byte order, ASCII or EBCDIC.  Like the 2089
 * core it was written for, it is freestanding: no headers, no library calls,
 * no static data.  The whole state is one unsigned long that the caller
 * owns, so any number of independent generators can exist, and saving one is
 * saving a number.  Two files (rng89.h, rng89.c) and nothing else; take them
 * into other projects as they are.
 *
 * The generator is a 32-bit linear congruential generator (the constants from
 * Numerical Recipes, period 2^32) returning the high 16 bits of its state.
 * r89_seed passes the seed through an integer hash first, so that nearby
 * seeds (1, 2, 3, or successive clock readings) start unrelated sequences.
 * Good for games and simulations; not for cryptography.
 *
 * Known answers (in hex), to check a port:
 *     seed 0:          3c6e 4750 d1cc aaf9 6252 9f2e ...
 *     seed 1:          37a4 1c47 eb19 ede0 cee9 507f ...
 *     seed 0xFFFFFFFF: 5484 fd56 fe3e a944 a7e9 4afd ...
 *
 * Copyright (c) 2026 ict.  MIT License; see LICENSE.
 */

#ifndef R89_H
#define R89_H

/* Start a sequence.  Any seed is fine, 0 included; only its low 32 bits count. */
void r89_seed(unsigned long *state, unsigned long seed);

/* The next number in the sequence, 0..65535. */
unsigned int r89_next(unsigned long *state);

#endif
