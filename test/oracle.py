#!/usr/bin/env python3
"""oracle.py - check the 2089 core against an independent model of the rules.

    python3 oracle.py ./replay [games]

Generates random games (size, seed, move string), plays them in this file's
own implementation and in the C core (through the replay program), and
compares the final save strings.  The model follows the rules in 2089.h, not
the C code: lines are compressed and paired off front to back.

Copyright (c) 2026 ict.  MIT License; see LICENSE.
"""
import random
import subprocess
import sys

M32 = 0xFFFFFFFF
MAXEXP = 35
DIGITS = "0123456789abcdefghijklmnopqrstuvwxyz"
LINES = {}


def mix(x):
    x &= M32
    x ^= x >> 16
    x = (x * 0x45D9F3B) & M32
    x ^= x >> 16
    x = (x * 0x45D9F3B) & M32
    x ^= x >> 16
    return x


class Game:
    def __init__(self, size, seed):
        self.n = size
        self.goal = 11
        self.cells = [0] * (size * size)
        self.score = self.moves = 0
        self.rng = mix(seed)
        self.spawn()
        self.spawn()

    def random(self):
        self.rng = (self.rng * 1664525 + 1013904223) & M32
        return self.rng >> 16

    def spawn(self):
        empty = [i for i, e in enumerate(self.cells) if e == 0]
        if not empty:
            return
        cell = empty[self.random() % len(empty)]
        self.cells[cell] = 2 if self.random() % 10 == 0 else 1

    def lines(self, d):
        """Cell indices of each line, starting at the edge tiles move towards."""
        key = (self.n, d)
        if key not in LINES:
            LINES[key] = self.makelines(d)
        return LINES[key]

    def makelines(self, d):
        n = self.n
        rows = [[r * n + c for c in range(n)] for r in range(n)]
        cols = [[r * n + c for r in range(n)] for c in range(n)]
        return {"l": rows, "r": [x[::-1] for x in rows],
                "u": cols, "d": [x[::-1] for x in cols]}[d]

    def move(self, d):
        before = list(self.cells)
        points = 0
        for line in self.lines(d):
            tiles = [self.cells[i] for i in line if self.cells[i]]
            out = []
            while tiles:
                if len(tiles) > 1 and tiles[0] == tiles[1] and tiles[0] < MAXEXP:
                    out.append(tiles[0] + 1)
                    points += 2 ** (tiles[0] + 1)
                    tiles = tiles[2:]
                else:
                    out.append(tiles.pop(0))
            out += [0] * (self.n - len(out))
            for i, e in zip(line, out):
                self.cells[i] = e
        if self.cells == before:
            return
        self.score = (self.score + points) & M32
        self.moves = (self.moves + 1) & M32
        self.spawn()

    def save(self):
        return "1%s%s%s.%x.%x.%x" % (DIGITS[self.n], DIGITS[self.goal],
                                     "".join(DIGITS[e] for e in self.cells),
                                     self.score, self.moves, self.rng)


def main():
    prog = sys.argv[1]
    count = int(sys.argv[2]) if len(sys.argv) > 2 else 10000
    rnd = random.Random(2089)
    cases = []
    for _ in range(count):
        size = rnd.choice([4, 4, 4, 2, 3, 5, 6, 7, 8])
        seed = rnd.choice([0, 1, 2, M32, rnd.getrandbits(32)])
        bias = rnd.choice(["udlr", "lldr", "uuul", "rrrd"])
        moves = "".join(rnd.choice(bias) for _ in range(rnd.randrange(1, 3000)))
        cases.append((size, seed, moves))
    stdin = "".join("%d %d %s\n" % c for c in cases)
    got = subprocess.run([prog], input=stdin, capture_output=True, text=True,
                         check=True).stdout.split("\n")
    bad = 0
    for i, (size, seed, moves) in enumerate(cases):
        g = Game(size, seed)
        for d in moves:
            g.move(d)
        if got[i] != g.save():
            bad += 1
            if bad <= 5:
                print("MISMATCH size %d seed %d after %d moves:\n  C:      %s\n  oracle: %s"
                      % (size, seed, len(moves), got[i], g.save()))
    print("%d games, %d mismatches" % (count, bad))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
