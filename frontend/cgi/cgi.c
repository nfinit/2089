/*
 * cgi.c - the generic 2089 CGI frontend.
 *
 * The save string is the whole game and lives in the URL.  A move is a GET:
 *
 *     2089.cgi?s=<save>&m=left
 *
 * There is no session, cookie or server-side game state; the browser Back
 * button is the only undo.  A new game is ?n=4.  This first version draws the
 * board as an HTML table; the obsidian skin and the server-composited GIF
 * board are a later frontend layer, not a dependency of the rules.
 *
 * Author: DeepSeek V4.1 Flash 1017, October 2026.
 * MIT License; see LICENSE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "2089.h"
#include "common.h"

#define CGI_QUERY_MAX 1024
#define CGI_BOARD_PX  330

static char cgi_self[128] = "2089.cgi";

struct cgi_query {
    char save[T89_SAVELEN];
    int  has_save;
    int  has_move;
    int  dir;
    int  has_size;
    int  size;
};

static const char *tile_bg(int exp)
{
    switch (exp) {
    case 0:  return "#b9ad9c";
    case 1:  return "#eee4da";
    case 2:  return "#ede0c8";
    case 3:  return "#f2b179";
    case 4:  return "#f59563";
    case 5:  return "#f67c5f";
    case 6:  return "#f65e3b";
    case 7:  return "#edcf72";
    case 8:  return "#edcc61";
    case 9:  return "#edc850";
    case 10: return "#edc53f";
    case 11: return "#edc22e";
    default: return "#b58bdc";
    }
}

static const char *tile_fg(int exp)
{
    if (exp == 0)
        return "#b9ad9c";
    if (exp <= 2 || (exp >= 7 && exp <= 10))
        return "#4b4436";
    return "#fff8ee";
}

static void head(const char *title)
{
    printf("Content-Type: text/html\n");
    printf("Cache-Control: no-cache\n");
    printf("Pragma: no-cache\n\n");
    printf("<HTML><HEAD><TITLE>%s</TITLE></HEAD>\n", title);
    printf("<BODY BGCOLOR=\"#0e1020\" TEXT=\"#2b2620\" LINK=\"#1f5f8b\" "
           "VLINK=\"#b05a2a\" ALINK=\"#b05a2a\">\n");
}

static void foot(void)
{
    printf("</BODY></HTML>\n");
}

static void bad_request(const char *why)
{
    head("2089 - bad request");
    printf("<CENTER><TABLE WIDTH=680 BORDER=0 CELLPADDING=14 CELLSPACING=0 "
           "BGCOLOR=\"#fbf7ef\"><TR><TD>\n");
    printf("<FONT SIZE=5 COLOR=\"#a95d2a\"><B>2089</B></FONT><BR>\n");
    printf("<FONT SIZE=2>%s</FONT><P>\n", why);
    printf("<FONT SIZE=2><A HREF=\"%s?n=4\">Start a new game</A></FONT>\n",
           cgi_self);
    printf("</TD></TR></TABLE></CENTER>\n");
    foot();
}

static int digit_string(const char *s, int min, int max, int *out)
{
    unsigned long v;
    char *end;

    if (*s < '0' || *s > '9')
        return -1;
    v = strtoul(s, &end, 10);
    if (*end != '\0' || v < (unsigned long) min || v > (unsigned long) max)
        return -1;
    *out = (int) v;
    return 0;
}

static int parse_query(struct cgi_query *q)
{
    const char *env = getenv("QUERY_STRING");
    char buf[CGI_QUERY_MAX + 1];
    char *p, *amp, *eq;
    int n;

    memset(q, 0, sizeof *q);
    q->size = T89_SIZE;
    if (env == 0)
        return 0;
    n = (int) strlen(env);
    if (n > CGI_QUERY_MAX)
        return -1;
    memcpy(buf, env, (size_t) n + 1);

    p = buf;
    while (p != 0 && *p != '\0') {
        amp = strchr(p, '&');
        if (amp != 0)
            *amp = '\0';
        eq = strchr(p, '=');
        if (eq != 0) {
            *eq = '\0';
            eq++;
        } else {
            eq = p + strlen(p);
        }
        if (strcmp(p, "s") == 0) {
            if ((int) strlen(eq) >= T89_SAVELEN)
                return -1;
            strcpy(q->save, eq);
            q->has_save = 1;
        } else if (strcmp(p, "m") == 0) {
            if (strcmp(eq, "up") == 0)
                q->dir = T89_UP;
            else if (strcmp(eq, "right") == 0)
                q->dir = T89_RIGHT;
            else if (strcmp(eq, "down") == 0)
                q->dir = T89_DOWN;
            else if (strcmp(eq, "left") == 0)
                q->dir = T89_LEFT;
            else
                return -1;
            q->has_move = 1;
        } else if (strcmp(p, "n") == 0) {
            if (digit_string(eq, T89_MINSIZE, T89_MAXSIZE, &q->size) < 0)
                return -1;
            q->has_size = 1;
        }
        p = amp == 0 ? 0 : amp + 1;
    }
    return 0;
}

static void board(const struct t89_game *g)
{
    char t[12];
    int r, c, exp, cell;
    int px = CGI_BOARD_PX;

    cell = (px - (g->size + 1) * 4) / g->size;
    if (cell < 24)
        cell = 24;
    printf("<TABLE BORDER=0 CELLPADDING=4 CELLSPACING=4 "
           "BGCOLOR=\"#d8cebd\"><TR>\n");
    for (r = 0; r < g->size; r++) {
        if (r > 0)
            printf("</TR><TR>\n");
        for (c = 0; c < g->size; c++) {
            exp = g->cell[r * g->size + c];
            f89_tile(t, exp);
            printf("<TD WIDTH=%d HEIGHT=%d ALIGN=CENTER VALIGN=MIDDLE "
                   "BGCOLOR=\"%s\">", cell, cell, tile_bg(exp));
            if (exp != 0) {
                int fs = strlen(t) <= 2 ? 5 : (cell >= 64 ? 4 : 3);
                printf("<FONT SIZE=%d COLOR=\"%s\"><B>%s</B></FONT>",
                       fs, tile_fg(exp), t);
            }
            printf("</TD>\n");
        }
    }
    printf("</TR></TABLE>\n");
}

static void move_link(const char *save, const char *dir, const char *label)
{
    printf("<A HREF=\"%s?s=%s&amp;m=%s\">%s</A>\n",
           cgi_self, save, dir, label);
}

static void move_buttons(const char *save)
{
    static const char *dir[4] = { "up", "left", "right", "down" };
    static const char *label[4] = { "Up", "Left", "Right", "Down" };
    int i;

    printf("<TABLE BORDER=1 CELLPADDING=4 CELLSPACING=0 "
           "BGCOLOR=\"#f3eadc\"><TR>\n");
    for (i = 0; i < 4; i++) {
        printf("<TD ALIGN=CENTER WIDTH=58><FONT SIZE=2>");
        move_link(save, dir[i], label[i]);
        printf("</FONT></TD>\n");
    }
    printf("</TR></TABLE>\n");
}

static void page(const struct t89_game *g, const char *save, int blocked)
{
    char best[12];
    int over, won;

    f89_tile(best, t89_best(g));
    over = t89_over(g);
    won = t89_won(g);
    head("2089");

    printf("<CENTER><TABLE WIDTH=680 BORDER=0 CELLPADDING=14 CELLSPACING=0 "
           "BGCOLOR=\"#fbf7ef\"><TR><TD>\n");
    printf("<CENTER><FONT SIZE=6 COLOR=\"#2b2620\"><B>2089</B></FONT><BR>\n");
    printf("<FONT SIZE=2>2048 for one PowerPC 604e</FONT><BR>\n");
    printf("<FONT SIZE=1 COLOR=\"#8a7e6e\">AIX 4.3.3 / 250 MHZ 604E</FONT></CENTER>\n");
    printf("<HR>\n");

    printf("<TABLE WIDTH=\"100%%\" BORDER=0 CELLPADDING=0 CELLSPACING=0 "
           "BGCOLOR=\"#f3eadc\"><TR><TD><FONT SIZE=2>\n");
    if (won)
        printf("<B>You reached 2048.</B>");
    else if (over)
        printf("<B>No moves left.</B>");
    else if (blocked)
        printf("<B>That direction is blocked.</B>");
    else
        printf("<B>Your move.</B>");
    printf(" &nbsp;|&nbsp; Score <B><FONT COLOR=\"#a95d2a\">%lu</FONT></B>",
           g->score);
    printf(" &nbsp;|&nbsp; Top tile <B>%s</B> &nbsp;|&nbsp; %lu moves",
           best, g->moves);
    printf("</FONT></TD></TR></TABLE><BR>\n");

    printf("<TABLE BORDER=0 CELLPADDING=0 CELLSPACING=0 WIDTH=\"100%%\"><TR>\n");
    printf("<TD VALIGN=TOP WIDTH=360>\n");
    board(g);
    printf("<BR>\n");
    move_buttons(save);
    printf("</TD><TD VALIGN=TOP WIDTH=20>&nbsp;</TD><TD VALIGN=TOP>\n");

    printf("<FONT SIZE=1 COLOR=\"#8a7e6e\">THIS GAME</FONT><BR>\n");
    printf("<FONT SIZE=5 COLOR=\"#a95d2a\"><B>%lu</B></FONT><BR>\n", g->score);
    printf("<FONT SIZE=1 COLOR=\"#7a6f60\">%lu moves &middot; top tile %s</FONT>\n",
           g->moves, best);
    printf("<BR><BR><A HREF=\"%s?n=4\"><B>New game</B></A>\n", cgi_self);
    printf("<BR><BR><FONT SIZE=1 COLOR=\"#5e5548\">\n");
    printf("<B>No JavaScript. No cookies.</B> The whole game is in the link.\n");
    printf("<BR><BR>Save: <FONT FACE=\"Courier New,Courier\" SIZE=1>%s</FONT>\n",
           save);
    printf("</FONT>\n");

    printf("</TD></TR></TABLE>\n");
    printf("<HR><FONT SIZE=1 COLOR=\"#8a7e6e\">2089 %s &middot; engine by Claude Opus 5.5 "
           "&middot; web front end by DeepSeek V4.1 Flash 1017</FONT>\n",
           T89_VERSION);
    printf("</TD></TR></TABLE></CENTER>\n");
    foot();
}

int main(void)
{
    struct cgi_query q;
    struct t89_game g;
    const char *script;
    struct t89_result r;
    char save[T89_SAVELEN];
    int blocked = 0;

    script = getenv("SCRIPT_NAME");
    if (script != 0 && script[0] == '/' && strlen(script) < sizeof cgi_self) {
        strcpy(cgi_self, script);
    }

    if (parse_query(&q) < 0) {
        bad_request("The request was not a valid 2089 game.");
        return 0;
    }
    if (q.has_save) {
        if (t89_load(&g, q.save) < 0) {
            bad_request("That save string is not a valid 2089 game.");
            return 0;
        }
    } else if (t89_new(&g, q.size, f89_seed(0)) < 0) {
        bad_request("The board size must be 2 to 8.");
        return 0;
    }
    if (q.has_move) {
        if (!t89_move(&g, q.dir, &r))
            blocked = 1;
    }
    if (t89_save(&g, save, sizeof save) < 0) {
        bad_request("The game could not be saved.");
        return 0;
    }
    page(&g, save, blocked);
    return 0;
}
