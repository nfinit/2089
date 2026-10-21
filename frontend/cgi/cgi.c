/*
 * cgi.c - the generic 2089 CGI frontend.
 *
 * The save string is the whole game and lives in the URL.  A move is a GET:
 *
 *     2089.cgi?s=<save>&m=left
 *
 * There is no session, cookie or server-side game state; the browser Back
 * button is the only undo.  A new game is ?n=4.  This first version draws the
 * board as a server-composited GIF; the skin is a later frontend layer, not
 * a dependency of the rules.
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
#include "board.h"
#include "gifenc.h"

#define CGI_QUERY_MAX 1024

static char cgi_self[128] = "2089.cgi";

struct cgi_query {
    char save[T89_SAVELEN];
    int  has_save;
    int  has_move;
    int  dir;
    int  has_size;
    int  size;
    int  has_view;
    int  gif;
};

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
        } else if (strcmp(p, "v") == 0) {
            if (strcmp(eq, "gif") == 0)
                q->gif = 1;
            else
                return -1;
            q->has_view = 1;
        }
        p = amp == 0 ? 0 : amp + 1;
    }
    return 0;
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

    printf("<TABLE BORDER=1 CELLPADDING=4 CELLSPACING=0 WIDTH=\"100%%\" "
           "BGCOLOR=\"#f3eadc\"><TR>\n");
    for (i = 0; i < 4; i++) {
        printf("<TD ALIGN=CENTER WIDTH=\"25%%\"><FONT SIZE=2>");
        move_link(save, dir[i], label[i]);
        printf("</FONT></TD>\n");
    }
    printf("</TR></TABLE>\n");
}

static void gif_board(const struct t89_game *g)
{
    static unsigned char pixels[B89_BOARD_W * B89_BOARD_H];
    GifWriter gw;
    int y;

    printf("Content-Type: image/gif\n");
    printf("Cache-Control: public, max-age=31536000\n");
    printf("Pragma: public\n\n");
    b89_render(g, pixels);
    if (!gif_begin(&gw, stdout, B89_BOARD_W, B89_BOARD_H, b89_palette()))
        return;
    for (y = 0; y < B89_BOARD_H; y++) {
        if (!gif_write_row(&gw, pixels + y * B89_BOARD_W))
            return;
    }
    gif_end(&gw);
}

static void page(const struct t89_game *g, const char *save, int blocked)
{
    char best[12];
    const char *move_word;
    int over, won;

    f89_tile(best, t89_best(g));
    move_word = g->moves == 1 ? "move" : "moves";
    over = t89_over(g);
    won = t89_won(g);
    head("2089");

    printf("<CENTER><TABLE WIDTH=680 BORDER=0 CELLPADDING=0 CELLSPACING=0 "
           "BGCOLOR=\"#fbf7ef\"><TR><TD>\n");
    printf("<IMG SRC=\"banner.gif?v=1\" WIDTH=680 HEIGHT=120 BORDER=0 ALT=\"2089\">\n");
    printf("<TABLE WIDTH=680 BORDER=0 CELLPADDING=0 CELLSPACING=0 "
           "BGCOLOR=\"#fbf7ef\"><TR><TD>\n");
    printf("<TABLE WIDTH=\"100%%\" BORDER=0 CELLPADDING=0 CELLSPACING=0 "
           "BGCOLOR=\"#fbf7ef\"><TR><TD>&nbsp;&nbsp;\n");
    printf("<FONT SIZE=1 COLOR=\"#6b6256\">[ <A HREF=\"/\">deepseek</A> | "
           "<B>2089</B> ]</FONT>\n");
    printf("</TD></TR></TABLE>\n");
    printf("<TABLE WIDTH=\"100%%\" BORDER=0 CELLPADDING=6 CELLSPACING=0 "
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
    printf(" &nbsp;|&nbsp; Top tile <B>%s</B> &nbsp;|&nbsp; %lu %s",
           best, g->moves, move_word);
    printf("</FONT></TD></TR></TABLE>\n");

    printf("<TABLE WIDTH=\"100%%\" BORDER=0 CELLPADDING=18 CELLSPACING=0>"
           "<TR><TD>\n");
    printf("<TABLE BORDER=0 CELLPADDING=0 CELLSPACING=0 WIDTH=\"100%%\">"
           "<TR>\n");
    printf("<TD VALIGN=TOP WIDTH=400>\n");
    printf("<IMG SRC=\"%s?s=%s&amp;v=gif\" WIDTH=400 HEIGHT=400 BORDER=0 "
           "ALT=\"2089 board\">\n", cgi_self, save);
    printf("</TD><TD VALIGN=TOP WIDTH=20>&nbsp;</TD><TD VALIGN=TOP>\n");
    printf("<FONT SIZE=1 COLOR=\"#8a7e6e\">THIS GAME</FONT><BR>\n");
    printf("<FONT SIZE=6 COLOR=\"#a95d2a\"><B>%lu</B></FONT><BR>\n", g->score);
    printf("<FONT SIZE=1 COLOR=\"#7a6f60\">%lu %s &middot; top tile %s</FONT>\n",
           g->moves, move_word, best);
    printf("<BR><BR><FONT SIZE=1 COLOR=\"#8a7e6e\">MOVE</FONT><BR>\n");
    move_buttons(save);
    printf("<BR><A HREF=\"%s?n=4\"><B>New game</B></A><BR>\n", cgi_self);
    printf("<A HREF=\"how.html\">How to play</A>\n");
    printf("<BR><BR><FONT SIZE=1 COLOR=\"#5e5548\">\n");
    printf("<B>No JavaScript. No cookies.</B> The whole game is in the link.\n");
    printf("<BR><BR>Save: <FONT FACE=\"Courier New,Courier\" SIZE=1>%s</FONT>\n",
           save);
    printf("</FONT>\n");
    printf("</TD></TR></TABLE>\n");
    printf("</TD></TR></TABLE>\n");
    printf("<HR><FONT SIZE=1 COLOR=\"#8a7e6e\">2089 %s &middot; engine by Claude Opus 5.5 "
           "&middot; web front end by DeepSeek V4.1 Flash 1017</FONT>\n",
           T89_VERSION);
    printf("</TD></TR></TABLE>\n");
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
    if (q.has_view && q.gif) {
        gif_board(&g);
        return 0;
    }
    if (t89_save(&g, save, sizeof save) < 0) {
        bad_request("The game could not be saved.");
        return 0;
    }
    page(&g, save, blocked);
    return 0;
}
