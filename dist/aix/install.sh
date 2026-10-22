#!/bin/sh
# install.sh - install 2089 from an AIX binary distribution.
#
#   ./install.sh [-p prefix] [-c] [-d dtdir] [-n]
#
#   -p prefix   where the programs and manual pages go: prefix/bin and
#               prefix/man/man6 (default /usr/local)
#   -c          put 2089 on the CDE desktop too: for everyone, in
#               /etc/dt/appconfig, when run as root; else in ~/.dt
#   -d dtdir    with -c, use dtdir for the CDE files instead
#   -n          show what would be done, and do nothing
#
# Author: Claude Opus 5.5 (claude-opus-5-5), October 2026.
# MIT License; see LICENSE.

prefix=/usr/local
cde=no
dtdir=
dtlang=
run=

usage() {
    echo "usage: ./install.sh [-p prefix] [-c] [-d dtdir] [-n]" >&2
    exit 2
}

while test $# -gt 0; do
    case "$1" in
    -p) test $# -gt 1 || usage; prefix=$2; shift ;;
    -c) cde=yes ;;
    -d) test $# -gt 1 || usage; dtdir=$2; shift ;;
    -n) run=echo ;;
    *)  usage ;;
    esac
    shift
done

case "$prefix" in
/*) ;;
*)  echo "install.sh: the prefix must be a full path: $prefix" >&2; exit 2 ;;
esac

cd `dirname "$0"` || exit 1
if test ! -f bin/xm2089; then
    echo "install.sh: run it from the unpacked 2089 directory" >&2
    exit 1
fi

bindir=$prefix/bin
mandir=$prefix/man/man6

# copy FILE DIR MODE: AIX won't overwrite a program that's running, so the
# old copy is removed first.
copy() {
    $run rm -f "$2/`basename $1`" &&
    $run cp "$1" "$2/" &&
    $run chmod $3 "$2/`basename $1`" || exit 1
}

$run mkdir -p "$bindir" "$mandir" || exit 1
for p in 2089 x2089 xm2089; do
    copy bin/$p "$bindir" 755
    copy man/man6/$p.6 "$mandir" 644
done
echo "2089: programs in $bindir, manual pages in $mandir"

if test $cde = yes; then
    if test -z "$dtdir"; then
        if test "`id -u`" = 0; then
            dtdir=/etc/dt/appconfig
            dtlang=/C
        else
            dtdir=$HOME/.dt
        fi
    fi
    $run mkdir -p "$dtdir/types$dtlang" "$dtdir/icons$dtlang" "$dtdir/help$dtlang" \
        "$dtdir/appmanager$dtlang/Games" || exit 1
    if test -z "$run"; then
        sed "s|@BINDIR@|$bindir|g" cde/types/2089.dt > "$dtdir/types$dtlang/2089.dt" || exit 1
        chmod 644 "$dtdir/types$dtlang/2089.dt"
    else
        echo "sed s|@BINDIR@|$bindir|g cde/types/2089.dt > $dtdir/types$dtlang/2089.dt"
    fi
    for i in 2089.l.pm 2089.m.pm 2089.t.pm; do
        copy cde/icons/$i "$dtdir/icons$dtlang" 644
    done
    copy cde/help/2089.sdl "$dtdir/help$dtlang" 644
    copy cde/appmanager/Xm2089 "$dtdir/appmanager$dtlang/Games" 755
    echo "2089: CDE files in $dtdir; choose Reload Actions and Reload"
    echo "      Applications in Application Manager's Desktop_Tools, or log in again"
fi

case ":$MANPATH:" in
*":$prefix/man:"*) ;;
*)  echo "2089: for man 2089, add $prefix/man to MANPATH" \
        "(MANPATH=$prefix/man:/usr/share/man:/usr/dt/man)" ;;
esac
exit 0
