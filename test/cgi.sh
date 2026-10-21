#!/bin/sh
# HTTP-shape checks for the generic CGI frontend.  Run from the repo root
# after "make cgi"; make check-cgi does both.
set -e

prog=frontend/cgi/2089.cgi
tmp=${TMPDIR:-/tmp}/2089-cgi.$$
trap 'rm -f "$tmp"' 0

test -x "$prog"

QUERY_STRING='' "$prog" > "$tmp"
grep -q '^Content-Type: text/html' "$tmp"
grep -q '<TITLE>2089</TITLE>' "$tmp"
grep -q 'No JavaScript' "$tmp"

save=`sed -n 's/.*[?&]s=\([0-9a-z.]*\)&amp;m=up.*/\1/p' "$tmp" | head -1`
test -n "$save"

QUERY_STRING="s=$save&m=up" SCRIPT_NAME=/2089/play.cgi "$prog" > "$tmp"
grep -q '^Content-Type: text/html' "$tmp"
grep -q 'Score' "$tmp"
grep -q '/2089/play.cgi?s=' "$tmp"

QUERY_STRING='s=not-a-save' "$prog" > "$tmp"
grep -q 'not a valid 2089 game' "$tmp"

QUERY_STRING='n=9' "$prog" > "$tmp"
grep -q 'bad request' "$tmp"

QUERY_STRING='n=4' "$prog" > "$tmp"
grep -q '<TITLE>2089</TITLE>' "$tmp"
grep -q 'New game' "$tmp"

QUERY_STRING='n=4&v=gif' "$prog" > "$tmp"
grep -q '^Content-Type: image/gif' "$tmp"
grep -q 'GIF87a' "$tmp"
test `wc -c < "$tmp"` -gt 1000

QUERY_STRING='n=8&v=gif' "$prog" > "$tmp"
grep -q '^Content-Type: image/gif' "$tmp"
grep -q 'GIF87a' "$tmp"
test `wc -c < "$tmp"` -gt 1000

QUERY_STRING='n=4&v=html' "$prog" > "$tmp"
grep -q 'bad request' "$tmp"

echo "cgi: ok"
