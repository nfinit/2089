# Makefile for 2089: POSIX make, any C89 compiler.
#
#   make                 build the text frontend (frontend/text/2089) and the tests
#   make check           run the core's tests
#   make CC=c89 CFLAGS=-O
#
# Developer checks (gcc): make strict

CC     = cc
CFLAGS = -O

CORE = 2089.o rng89.o

all: frontend/text/2089 test/t89test test/replay

2089.o: 2089.c 2089.h rng89.h
	$(CC) $(CFLAGS) -c 2089.c

rng89.o: rng89.c rng89.h
	$(CC) $(CFLAGS) -c rng89.c

frontend/text/2089: frontend/text/text.c 2089.h $(CORE)
	$(CC) $(CFLAGS) -o $@ frontend/text/text.c $(CORE)

test/t89test: test/test.c 2089.h $(CORE)
	$(CC) $(CFLAGS) -o $@ test/test.c $(CORE)

test/replay: test/replay.c 2089.h $(CORE)
	$(CC) $(CFLAGS) -o $@ test/replay.c $(CORE)

check: test/t89test
	test/t89test

# Slow (about a minute): compare 1000 games with an independent Python model.
oracle: test/replay
	python3 test/oracle.py test/replay 1000

# Every warning gcc has for C89, and a freestanding core with no undefined
# symbols at any optimisation level (it must not call the C library; note
# that some compilers still call their own helpers for division etc.).
STRICT = -std=c89 -pedantic -Wall -Wextra -Wconversion -Wshadow \
	-Wstrict-prototypes -Wmissing-prototypes -Werror
strict:
	for o in -O0 -O2 -O3 -Os; do \
	    for f in 2089 rng89; do \
	        gcc $(STRICT) $$o -ffreestanding -fno-builtin -c $$f.c -o /tmp/$$f-strict.o \
	        && test -z "`nm -u /tmp/$$f-strict.o | grep -v r89_`" \
	        || { nm -u /tmp/$$f-strict.o; exit 1; }; \
	    done; \
	done
	@# C89 promises only 6 significant, case-blind characters in external names.
	@d=`nm -g --defined-only /tmp/2089-strict.o /tmp/rng89-strict.o | awk 'NF == 3 {print tolower(substr($$3,1,6))}' | sort | uniq -d`; \
	test -z "$$d" || { echo "external names clash in 6 characters: $$d"; exit 1; }
	gcc -std=c89 -pedantic -Wall -Wextra -Werror -o /tmp/2089-text frontend/text/text.c 2089.c rng89.c
	gcc -std=c89 -pedantic -Wall -Wextra -Werror -o /tmp/2089-test test/test.c 2089.c rng89.c
	gcc -std=c89 -pedantic -Wall -Wextra -Werror -o /tmp/2089-replay test/replay.c 2089.c rng89.c
	/tmp/2089-test

clean:
	rm -f $(CORE) frontend/text/2089 test/t89test test/replay
