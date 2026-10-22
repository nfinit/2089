# Makefile for 2089: POSIX make, any C89 compiler.
#
#   make                 build the text frontend and the tests, and the
#                        curses frontend if a curses library can be found
#   make text            the text frontend and the tests only
#   make curses          the curses frontend, without looking first
#   make x11             the X11 frontends: x2089 (Xlib only), and xm2089 if
#                        Motif can be found (make xlib, make motif: one each);
#                        make x11 X11INC=-I/usr/X11R6/include X11LIB="-L/usr/X11R6/lib -lX11"
#   make run             build, then play: the full program if it was built,
#                        else the text frontend (make run ARGS="-u -s 5")
#   make check           run the tests: the core's and the frontends' shared code
#   make install         install 2089 (and x2089 and xm2089, if built) in
#                        $(PREFIX)/bin, their manual pages in $(PREFIX)/man/man6:
#                        make x11 install PREFIX=$HOME
#   make install-cde     put xm2089 on the CDE desktop (see below)
#   make CC=c89 CFLAGS=-O
#
# Developer checks (gcc): make strict, make strict-x11

CC     = cc
CFLAGS = -O
CURSES = -lcurses
X11INC =
X11LIB = -lX11
MOTIFLIB = -lXm -lXt -lX11
PREFIX = /usr/local
BINDIR = $(PREFIX)/bin
MANDIR = $(PREFIX)/man

CORE = 2089.o rng89.o
FRONT = frontend/common.o frontend/text/text.o

# Sources outside the top directory include "2089.h" with no path, so that
# every platform's build can say where it is (-I here).

# Look for a curses that links (CURSES first, then common alternatives),
# then build the curses frontend with it, or say why not.  Only the probe
# decides: errors in the curses frontend itself still stop the build.
CANDIDATES = "$(CURSES)" "-lncurses" "-lncurses -ltinfo" "-lcurses -ltermcap"

all: text
	@lib=; tried=; for l in $(CANDIDATES); do \
	    tried="$$tried [$$l]"; \
	    if $(CC) $(CFLAGS) -o frontend/curses/probe frontend/curses/probe.c $$l \
	        >/dev/null 2>&1; then lib=$$l; break; fi; \
	done; rm -f frontend/curses/probe; \
	if test -n "$$lib"; then \
	    $(MAKE) CC="$(CC)" CFLAGS="$(CFLAGS)" CURSES="$$lib" curses; \
	else \
	    echo "2089: no curses library found; tried$$tried."; \
	    echo "2089: built the text frontend only; for curses, name the library:"; \
	    echo "2089:     make CURSES='-L/some/lib -lcurses'"; \
	fi

text: frontend/text/2089 test/t89test test/front test/replay

2089.o: 2089.c 2089.h rng89.h
	$(CC) $(CFLAGS) -c 2089.c

rng89.o: rng89.c rng89.h
	$(CC) $(CFLAGS) -c rng89.c

frontend/common.o: frontend/common.c frontend/common.h 2089.h
	$(CC) $(CFLAGS) -I. -c -o $@ frontend/common.c

frontend/text/text.o: frontend/text/text.c frontend/common.h 2089.h
	$(CC) $(CFLAGS) -I. -Ifrontend -c -o $@ frontend/text/text.c

# The program is frontend/main.c: plain, it is the text frontend; with
# T89_CURSES and curses.c it plays full screen, with line mode to fall back on.
frontend/text/2089: frontend/main.c frontend/common.h 2089.h $(CORE) $(FRONT)
	$(CC) $(CFLAGS) -I. -Ifrontend -o $@ frontend/main.c $(FRONT) $(CORE)

curses: frontend/curses/2089

# Play whichever program was built.  "|| :" keeps make quiet when a game
# ends with a non-zero status (an interrupt, a bad save string).
ARGS =
run: all
	@if test -x frontend/curses/2089; then frontend/curses/2089 $(ARGS); \
	else frontend/text/2089 $(ARGS); fi || :

frontend/curses/2089: frontend/main.c frontend/curses/curses.c frontend/common.h 2089.h \
		$(CORE) $(FRONT)
	$(CC) $(CFLAGS) -DT89_CURSES -I. -Ifrontend -o $@ frontend/main.c \
	    frontend/curses/curses.c $(FRONT) $(CORE) $(CURSES)

# The X frontends share the board (frontend/x11/xboard.c).  Motif is looked
# for as curses is: only the probe decides.
XBOARD = frontend/x11/xboard.c frontend/x11/xboard.h frontend/common.h 2089.h \
	$(CORE) frontend/common.o
XFLAGS = $(X11INC) -I. -Ifrontend -Ifrontend/x11

x11: xlib
	@if $(CC) $(CFLAGS) $(X11INC) -o frontend/x11/motif/probe frontend/x11/motif/probe.c \
	    $(MOTIFLIB) >/dev/null 2>&1; then \
	    rm -f frontend/x11/motif/probe; \
	    $(MAKE) CC="$(CC)" CFLAGS="$(CFLAGS)" X11INC="$(X11INC)" MOTIFLIB="$(MOTIFLIB)" motif; \
	else \
	    rm -f frontend/x11/motif/probe; \
	    echo "2089: no Motif found (tried $(MOTIFLIB)); built x2089 only."; \
	    echo "2089: for xm2089, name it: make x11 MOTIFLIB='-L/some/lib -lXm -lXt -lX11'"; \
	fi

xlib: frontend/x11/xlib/x2089

motif: frontend/x11/motif/xm2089

frontend/x11/xlib/x2089: frontend/x11/xlib/xlib.c $(XBOARD)
	$(CC) $(CFLAGS) $(XFLAGS) -o $@ frontend/x11/xlib/xlib.c frontend/x11/xboard.c \
	    frontend/common.o $(CORE) $(X11LIB)

frontend/x11/motif/xm2089: frontend/x11/motif/motif.c $(XBOARD)
	$(CC) $(CFLAGS) $(XFLAGS) -o $@ frontend/x11/motif/motif.c frontend/x11/xboard.c \
	    frontend/common.o $(CORE) $(MOTIFLIB)

test/t89test: test/test.c 2089.h $(CORE)
	$(CC) $(CFLAGS) -I. -o $@ test/test.c $(CORE)

test/front: test/front.c frontend/common.h 2089.h frontend/common.o $(CORE)
	$(CC) $(CFLAGS) -I. -Ifrontend -o $@ test/front.c frontend/common.o $(CORE)

test/replay: test/replay.c 2089.h $(CORE)
	$(CC) $(CFLAGS) -I. -o $@ test/replay.c $(CORE)

# 2089 is the full program if it was built, else the text frontend.  The old
# copy is removed first: some systems (AIX) won't overwrite a program that's
# running.  DESTDIR is for packaging: the files go there, under BINDIR and
# MANDIR.  The manual pages are -man source, which man formats itself.
install: all
	mkdir -p $(DESTDIR)$(BINDIR) $(DESTDIR)$(MANDIR)/man6
	cp man/2089.6 $(DESTDIR)$(MANDIR)/man6/2089.6
	chmod 644 $(DESTDIR)$(MANDIR)/man6/2089.6
	rm -f $(DESTDIR)$(BINDIR)/2089
	if test -x frontend/curses/2089; then cp frontend/curses/2089 $(DESTDIR)$(BINDIR)/2089; \
	else cp frontend/text/2089 $(DESTDIR)$(BINDIR)/2089; fi
	chmod 755 $(DESTDIR)$(BINDIR)/2089
	for p in frontend/x11/xlib/x2089 frontend/x11/motif/xm2089; do \
	    if test -x $$p; then b=`basename $$p`; rm -f $(DESTDIR)$(BINDIR)/$$b && \
	        cp $$p $(DESTDIR)$(BINDIR)/$$b && chmod 755 $(DESTDIR)$(BINDIR)/$$b && \
	        cp man/$$b.6 $(DESTDIR)$(MANDIR)/man6/$$b.6 && \
	        chmod 644 $(DESTDIR)$(MANDIR)/man6/$$b.6 || exit 1; fi; \
	done

check: test/t89test test/front
	test/t89test
	test/front

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
	@# C89 promises only 6 significant, case-blind characters in external
	@# names: the core's, and the frontends' with them.
	for f in frontend/main frontend/common frontend/text/text frontend/curses/curses; do \
	    gcc $(STRICT) -DT89_CURSES -I. -Ifrontend -c $$f.c -o /tmp/2089-`basename $$f`.o \
	    || exit 1; \
	done
	@d=`nm -g --defined-only /tmp/2089-strict.o /tmp/rng89-strict.o | awk 'NF == 3 {print tolower(substr($$3,1,6))}' | sort | uniq -d`; \
	test -z "$$d" || { echo "external names clash in 6 characters: $$d"; exit 1; }
	@d=`nm -g --defined-only /tmp/2089-strict.o /tmp/rng89-strict.o /tmp/2089-main.o \
	    /tmp/2089-common.o /tmp/2089-text.o /tmp/2089-curses.o \
	    | awk 'NF == 3 {print tolower(substr($$3,1,6))}' | sort | uniq -d`; \
	test -z "$$d" || { echo "external names clash in 6 characters: $$d"; exit 1; }
	gcc $(STRICT) -I. -Ifrontend -o /tmp/2089-text frontend/main.c \
	    frontend/text/text.c frontend/common.c 2089.c rng89.c
	gcc $(STRICT) -DT89_CURSES -I. -Ifrontend -o /tmp/2089-curses frontend/main.c \
	    frontend/curses/curses.c frontend/text/text.c frontend/common.c 2089.c rng89.c $(CURSES)
	gcc -std=c89 -pedantic -Wall -Wextra -Werror -I. -o /tmp/2089-test test/test.c 2089.c rng89.c
	gcc -std=c89 -pedantic -Wall -Wextra -Werror -I. -o /tmp/2089-replay test/replay.c 2089.c rng89.c
	gcc $(STRICT) -I. -Ifrontend -o /tmp/2089-front test/front.c frontend/common.c 2089.c rng89.c
	/tmp/2089-test
	/tmp/2089-front

# xm2089 on the CDE desktop: the action and icons that put 2089 in
# Application Manager (a Games group) and open saved games from File Manager,
# and the help volume (built with CDE's dthelptag).  For yourself, in
# ~/.dt; for everyone, as root:
#     make install-cde DTDIR=/etc/dt/appconfig DTLANG=/C
# The action runs $(BINDIR)/xm2089, so install that too (make install).
# Then choose Reload Actions and Reload Applications in Application
# Manager's Desktop_Tools (or log out and in).
DTDIR = $(HOME)/.dt
DTLANG =
CDE = frontend/x11/motif/cde
DTHELPTAG = /usr/dt/bin/dthelptag

install-cde:
	mkdir -p $(DESTDIR)$(DTDIR)/types$(DTLANG) $(DESTDIR)$(DTDIR)/icons$(DTLANG) \
	    $(DESTDIR)$(DTDIR)/help$(DTLANG) $(DESTDIR)$(DTDIR)/appmanager$(DTLANG)/Games
	sed 's|@BINDIR@|$(BINDIR)|g' $(CDE)/types/2089.dt > $(DESTDIR)$(DTDIR)/types$(DTLANG)/2089.dt
	cp $(CDE)/icons/2089.l.pm $(CDE)/icons/2089.m.pm $(CDE)/icons/2089.t.pm \
	    $(DESTDIR)$(DTDIR)/icons$(DTLANG)/
	cd $(CDE)/help && PATH=/usr/dt/bin:$$PATH $(DTHELPTAG) 2089 >/dev/null
	cp $(CDE)/help/2089.sdl $(DESTDIR)$(DTDIR)/help$(DTLANG)/2089.sdl
	cd $(CDE)/help && PATH=/usr/dt/bin:$$PATH $(DTHELPTAG) -clean 2089 >/dev/null
	rm -f $(DESTDIR)$(DTDIR)/appmanager$(DTLANG)/Games/Xm2089
	echo '# 2089: runs the Xm2089 action' > $(DESTDIR)$(DTDIR)/appmanager$(DTLANG)/Games/Xm2089
	chmod 755 $(DESTDIR)$(DTDIR)/appmanager$(DTLANG)/Games/Xm2089
	cd $(DESTDIR)$(DTDIR) && chmod 644 types$(DTLANG)/2089.dt icons$(DTLANG)/2089.?.pm \
	    help$(DTLANG)/2089.sdl

# The X11 frontends under the same warnings (needs the X and Motif headers).
strict-x11:
	gcc $(STRICT) $(XFLAGS) -o /tmp/2089-xlib frontend/x11/xlib/xlib.c \
	    frontend/x11/xboard.c frontend/common.c 2089.c rng89.c $(X11LIB)
	gcc $(STRICT) $(XFLAGS) -o /tmp/2089-motif frontend/x11/motif/motif.c \
	    frontend/x11/xboard.c frontend/common.c 2089.c rng89.c $(MOTIFLIB)

clean:
	rm -f $(CORE) $(FRONT) frontend/text/2089 frontend/curses/2089 frontend/curses/probe \
	    frontend/x11/xlib/x2089 frontend/x11/motif/xm2089 frontend/x11/motif/probe test/t89test test/front test/replay
