/*
 * front.c - tests for the hosted frontends' shared code (frontend/common.c):
 * keys, escape sequences, options and tile text.  C89.
 *
 * These are the parts where the bugs have been (arrows read as letters,
 * strtoul clamping), and the core's tests don't reach them.
 *
 * Author: Claude Opus 5.5 (claude-opus-5-5), October 2026.
 * MIT License; see LICENSE.
 */

#include <stdio.h>
#include <string.h>
#include "common.h"

static int failures = 0, checks = 0;

static void fail(const char *what, const char *detail)
{
    if (failures < 20)
        printf("FAIL: %s %s\n", what, detail);
    failures++;
}

#define CHECK(cond, what, detail) \
    do { checks++; if (!(cond)) fail(what, detail); } while (0)

/* Every key in both cases, and every other byte as no command. */
static void test_keys(void)
{
    static const struct { const char *keys; int cmd; } map[] = {
        { "wk8", F89_UP }, { "dl6", F89_RIGHT }, { "sj2", F89_DOWN },
        { "ah4", F89_LEFT }, { "u", F89_UNDO }, { "n", F89_NEW },
        { "p", F89_SAVE }, { "?", F89_HELP }, { "c", F89_DISPLAY },
        { "q", F89_QUIT }
    };
    char what[32];
    int ch, i, want;
    const char *k;

    for (ch = 0; ch < 256; ch++) {
        want = 0;
        for (i = 0; i < (int) (sizeof map / sizeof map[0]); i++)
            for (k = map[i].keys; *k; k++)
                if (ch == *k || (*k >= 'a' && *k <= 'z' && ch == *k - 'a' + 'A'))
                    want = map[i].cmd;
        sprintf(what, "f89_key(%d)", ch);
        CHECK(f89_key(ch) == want, what, "");
    }
    CHECK(f89_dir(F89_UP) == T89_UP && f89_dir(F89_RIGHT) == T89_RIGHT
          && f89_dir(F89_DOWN) == T89_DOWN && f89_dir(F89_LEFT) == T89_LEFT,
          "f89_dir", "moves");
    for (i = 0; i <= F89_QUIT + 1; i++)
        if (i < F89_UP || i > F89_LEFT)
            CHECK(f89_dir(i) == -1, "f89_dir", "not a move");
    for (ch = 0; ch < 256; ch++)
        CHECK(f89_intro(ch) == (ch == 0x1B || ch == 0x9B || ch == 0x8F), "f89_intro", "");
}

/* One sequence: the command it gives and the bytes it uses. */
static void esc(const char *s, int cmd, int len)
{
    char what[64];
    int i, n = -1, got;

    got = f89_escape(s, &n);
    strcpy(what, "f89_escape(");
    for (i = 0; s[i] && i < 12; i++)
        sprintf(what + strlen(what), (unsigned char) s[i] < 0x20
                || (unsigned char) s[i] > 0x7E ? "\\%o" : "%c", (unsigned char) s[i]);
    strcat(what, ")");
    CHECK(got == cmd && n == len, what, "");
}

static void test_escapes(void)
{
    static const char finals[] = "ABCD";
    static const int moves[] = { F89_UP, F89_DOWN, F89_RIGHT, F89_LEFT };
    char s[16];
    int i;

    for (i = 0; i < 4; i++) {
        sprintf(s, "\033[%c", finals[i]);       /* ANSI, VT100 cursor mode */
        esc(s, moves[i], 3);
        sprintf(s, "\033O%c", finals[i]);       /* VT100 application mode */
        esc(s, moves[i], 3);
        sprintf(s, "\033%c", finals[i]);        /* VT52 */
        esc(s, moves[i], 2);
        sprintf(s, "\233%c", finals[i]);        /* 8-bit CSI */
        esc(s, moves[i], 2);
        sprintf(s, "\217%c", finals[i]);        /* 8-bit SS3 */
        esc(s, moves[i], 2);
        sprintf(s, "\033[1;2%c", finals[i]);    /* xterm shift-arrow */
        esc(s, moves[i], 6);
        sprintf(s, "\033[%cd", finals[i]);      /* what follows isn't used */
        esc(s, moves[i], 3);
    }
    esc("\033[11~", 0, 5);                      /* F1: skipped whole */
    esc("\033[24;5~x", 0, 7);
    esc("\033OP", 0, 3);                        /* PF1 */
    esc("\033[Z", 0, 3);                        /* back tab */
    esc("\033", 0, 1);                          /* cut short */
    esc("\033[", 0, 2);
    esc("\033[1;", 0, 4);
    esc("\033O", 0, 2);
    esc("\033\n", 0, 1);
    esc("\033[\nA", 0, 2);                      /* a newline ends it */
    esc("\033[12\n", 0, 4);
    esc("\233", 0, 1);
    esc("\2331;2A", F89_UP, 5);

    /* Whole sequences, not just their parameters (DeepSeek's review). */
    for (i = 0; i < 5; i++) {
        sprintf(s, "\033[[%c", "ABCDE"[i]);     /* Linux console F1-F5 */
        esc(s, 0, 4);
        sprintf(s, "\233[%ch", "ABCDE"[i]);
        esc(s, 0, 3);
    }
    esc("\033[[", 0, 3);                        /* cut short */
    esc("\033[?", 0, 3);
    esc("\033[?25h", 0, 6);                     /* private parameters */
    esc("\033[?25ld", 0, 6);
    esc("\033[?1;2c", 0, 7);
    esc("\033[<0;1;1M", 0, 9);
    esc("\033[>1A", 0, 5);                      /* not an arrow */
    esc("\033[ q", 0, 4);                       /* intermediate */
    esc("\033[1 A", 0, 5);
    esc("\033(B", 0, 3);                        /* ESC with intermediate */
    esc("\033(", 0, 2);
    esc("\033\033[A", 0, 1);                    /* another sequence ends it */
    esc("\033[1\033[A", 0, 3);
    esc("\033[1\233A", 0, 3);
    esc("\033[1\rA", 0, 3);                     /* so does a control */
    esc("\033O2A", F89_UP, 4);
}

/* f89_cslen: 0 until the sequence is complete, for reading it a byte at a time. */
static void test_cslen(void)
{
    static const char *seqs[] = {
        "\033[A", "\033OB", "\033C", "\233D", "\217A", "\033[1;2A", "\033[11~",
        "\033[[A", "\033[?25h", "\033[<0;1;1M", "\033(B", "\033[ q", 0
    };
    char s[16];
    int i, n, k, ok;

    for (i = 0; seqs[i]; i++) {
        n = (int) strlen(seqs[i]);
        ok = 1;
        for (k = 1; k < n; k++) {
            memcpy(s, seqs[i], (size_t) k);
            s[k] = '\0';
            if (f89_cslen(s) != 0)
                ok = 0;
        }
        CHECK(ok && f89_cslen(seqs[i]) == n, "f89_cslen", seqs[i] + 1);
    }
    CHECK(f89_cslen("\033\033") == 1 && f89_cslen("\033[1\n") == 3, "f89_cslen", "ended");
}

/* f89_option on one argument list: what it returns, how far it moves *i. */
static void opt(const char *a, const char *v, int ret, int used,
                struct f89_options *o)
{
    char *argv[3], what[64];
    int argc = v ? 3 : 2, i = 1, got;

    argv[0] = (char *) "2089";
    argv[1] = (char *) a;
    argv[2] = (char *) v;
    f89_defaults(o);
    got = f89_option(argc, argv, &i, o);
    sprintf(what, "f89_option(%.8s %.20s)", a, v ? v : "<none>");
    CHECK(got == ret && (ret != 1 || i == used), what, "");
}

static void test_options(void)
{
    static const char *badsize[] = {
        "0", "1", "9", "10", "", "-", "-4", "+4", " 4", "4 ", "4x", "0x4", "1 2", 0
    };
    static const char *badseed[] = {
        "4294967296", "99999999999", "18446744073709551616", "-1", "+1", "",
        "-", "12abc", "1 2", " 1", "0x10", 0
    };
    struct f89_options o;
    int i;

    f89_defaults(&o);
    CHECK(o.size == T89_SIZE && o.load == 0 && o.undo == 0 && o.line == 0
          && o.compact == 0 && o.style == F89_COLOUR && o.forced == 0 && o.ascii == 0
          && o.cells == F89_CELLS_AUTO, "f89_defaults", "");

    opt("-u", 0, 1, 1, &o);
    CHECK(o.undo == 1, "-u", "sets undo");
    for (i = T89_MINSIZE; i <= T89_MAXSIZE; i++) {
        char v[4];

        sprintf(v, "%d", i);
        opt("-s", v, 1, 2, &o);
        CHECK(o.size == i, "-s", v);
    }
    opt("-s", "04", 1, 2, &o);
    CHECK(o.size == 4, "-s", "04");
    for (i = 0; badsize[i]; i++)
        opt("-s", badsize[i], -1, 0, &o);
    opt("-r", "0", 1, 2, &o);
    CHECK(o.seed == 0, "-r", "0");
    opt("-r", "4294967295", 1, 2, &o);
    CHECK(o.seed == 0xFFFFFFFFUL, "-r", "4294967295");
    for (i = 0; badseed[i]; i++)
        opt("-r", badseed[i], -1, 0, &o);
    opt("-l", "14b0000101000000000.0.0.ede0adcb", 1, 2, &o);
    CHECK(o.load && strcmp(o.load, "14b0000101000000000.0.0.ede0adcb") == 0, "-l", "");
    opt("-l", "junk", 1, 2, &o);                /* checked later, by f89_start */
    opt("-s", 0, -1, 0, &o);                    /* values missing */
    opt("-r", 0, -1, 0, &o);
    opt("-l", 0, -1, 0, &o);
    opt("-x", 0, 0, 0, &o);                     /* not ours */
    opt("-ss", "4", 0, 0, &o);
    opt("-", 0, 0, 0, &o);
    opt("s", "4", 0, 0, &o);
    opt("-L", "4", 0, 0, &o);
}

static void test_tiles(void)
{
    char buf[16];
    int e;

    f89_tile(buf, 0);
    CHECK(buf[0] == '\0', "f89_tile", "empty");
    f89_tile(buf, 1);
    CHECK(strcmp(buf, "2") == 0, "f89_tile", "2");
    f89_tile(buf, 11);
    CHECK(strcmp(buf, "2048") == 0, "f89_tile", "2048");
    f89_tile(buf, 31);
    CHECK(strcmp(buf, "2147483648") == 0, "f89_tile", "2^31");
    f89_tile(buf, 32);
    CHECK(strcmp(buf, "2^32") == 0, "f89_tile", "2^32");
    for (e = 0; e <= T89_MAXEXP; e++) {
        f89_tile(buf, e);
        CHECK(strlen(buf) < 12, "f89_tile", "fits 12 characters");
    }
    f89_tile(buf, T89_MAXEXP);
    CHECK(strcmp(buf, "2^35") == 0, "f89_tile", "2^35");
}

int main(void)
{
    printf("2089 frontend %s tests\n", T89_VERSION);
    test_keys();
    test_escapes();
    test_cslen();
    test_options();
    test_tiles();
    printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
