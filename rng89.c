/*
 * rng89.c - a small portable random number generator for C89.  See rng89.h.
 *
 * Every result is reduced to 32 bits with & M32, so a 64-bit long gives the
 * same numbers as a 32-bit one, and the output fits a 16-bit unsigned int.
 *
 * Copyright (c) 2026 ict.  MIT License; see LICENSE.
 */

#include "rng89.h"

#define M32 0xFFFFFFFFUL

void r89_seed(unsigned long *state, unsigned long seed)
{
    unsigned long x = seed & M32;

    /* An integer hash, so that seeds 1, 2, 3... start unrelated sequences. */
    x ^= x >> 16;
    x = (x * 0x45D9F3BUL) & M32;
    x ^= x >> 16;
    x = (x * 0x45D9F3BUL) & M32;
    x ^= x >> 16;
    *state = x;
}

unsigned int r89_next(unsigned long *state)
{
    *state = (*state * 1664525UL + 1013904223UL) & M32;
    return (unsigned int) ((*state >> 16) & 0xFFFFU);
}
