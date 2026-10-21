/*
 * board.c - draw the top-down 2089 board as palette-indexed pixels.
 *
 * This is deliberately a small software renderer, not a bitmap asset loader.
 * The tray, slots, bevels, shadows and digits all come from the same C89 code
 * on Linux and AIX.  A browser asks for one immutable image per save string.
 *
 * Author: DeepSeek V4.1 Flash 1017, October 2026.
 * MIT License; see LICENSE.
 */

#include "board.h"

#define C_PAPER       0
#define C_TRAY        1
#define C_TRAY_DARK   2
#define C_TRAY_LIGHT  3
#define C_SLOT        4
#define C_SLOT_DARK   5
#define C_SLOT_LIGHT  6
#define C_SHADOW      7
#define C_TEXT_DARK   8
#define C_TEXT_LIGHT  9
#define C_TEXT_DARK_SHADOW  10
#define C_TEXT_LIGHT_SHADOW 11
#define C_TILE        16
#define C_TILE_MAX    16

#define TRAY_X 32
#define TRAY_Y 32
#define TRAY_W 336
#define TRAY_INNER 320

static unsigned char palette[256 * 3];
static int palette_ready;

static void set_color(int index, unsigned long rgb)
{
    palette[index * 3] = (unsigned char) ((rgb >> 16) & 0xff);
    palette[index * 3 + 1] = (unsigned char) ((rgb >> 8) & 0xff);
    palette[index * 3 + 2] = (unsigned char) (rgb & 0xff);
}

static int mix_channel(int a, int b, int pct)
{
    return (a * (100 - pct) + b * pct) / 100;
}

static unsigned long mix_color(unsigned long a, unsigned long b, int pct)
{
    int ar = (int) ((a >> 16) & 0xff);
    int ag = (int) ((a >> 8) & 0xff);
    int ab = (int) (a & 0xff);
    int br = (int) ((b >> 16) & 0xff);
    int bg = (int) ((b >> 8) & 0xff);
    int bb = (int) (b & 0xff);
    unsigned long r = (unsigned long) mix_channel(ar, br, pct);
    unsigned long g = (unsigned long) mix_channel(ag, bg, pct);
    unsigned long bl = (unsigned long) mix_channel(ab, bb, pct);

    return (r << 16) | (g << 8) | bl;
}

static unsigned long base_color(int exp)
{
    switch (exp) {
    case 1:  return 0xeee4daUL;
    case 2:  return 0xede0c8UL;
    case 3:  return 0xf2b179UL;
    case 4:  return 0xf59563UL;
    case 5:  return 0xf67c5fUL;
    case 6:  return 0xf65e3bUL;
    case 7:  return 0xedcf72UL;
    case 8:  return 0xedcc61UL;
    case 9:  return 0xedc850UL;
    case 10: return 0xedc53fUL;
    case 11: return 0xedc22eUL;
    default: return 0xb58bdcUL;
    }
}

static void palette_init(void)
{
    int exp;
    unsigned long base;

    if (palette_ready)
        return;
    set_color(C_PAPER, 0xfbf7efUL);
    set_color(C_TRAY, 0xd8cebdUL);
    set_color(C_TRAY_DARK, 0xa89b89UL);
    set_color(C_TRAY_LIGHT, 0xe7ddcdUL);
    set_color(C_SLOT, 0xb9ad9cUL);
    set_color(C_SLOT_DARK, 0xa99d8dUL);
    set_color(C_SLOT_LIGHT, 0xc9bdabUL);
    set_color(C_SHADOW, 0xcdbfaeUL);
    set_color(C_TEXT_DARK, 0x4b4436UL);
    set_color(C_TEXT_LIGHT, 0xfff8eeUL);
    set_color(C_TEXT_DARK_SHADOW, 0xf4eadbUL);
    set_color(C_TEXT_LIGHT_SHADOW, 0x9a5b35UL);
    for (exp = 1; exp <= C_TILE_MAX; exp++) {
        int i = C_TILE + (exp - 1) * 3;
        base = base_color(exp);
        set_color(i, base);
        set_color(i + 1, mix_color(base, 0xffffffUL, 32));
        set_color(i + 2, mix_color(base, 0x1f1a14UL, 28));
    }
    palette_ready = 1;
}

const unsigned char *b89_palette(void)
{
    palette_init();
    return palette;
}

static void fill_rect(unsigned char *pix, int x, int y, int w, int h, int color)
{
    int xx, yy;
    unsigned char *p;

    for (yy = 0; yy < h; yy++) {
        p = pix + (y + yy) * B89_BOARD_W + x;
        for (xx = 0; xx < w; xx++)
            p[xx] = (unsigned char) color;
    }
}

static void round_rect(unsigned char *pix, int x, int y, int w, int h,
                       int radius, int color)
{
    int xx, yy;

    for (yy = 0; yy < h; yy++) {
        for (xx = 0; xx < w; xx++) {
            int dx = 0, dy = 0;

            if (xx < radius)
                dx = radius - 1 - xx;
            else if (xx >= w - radius)
                dx = xx - (w - radius);
            if (yy < radius)
                dy = radius - 1 - yy;
            else if (yy >= h - radius)
                dy = yy - (h - radius);
            if (dx != 0 || dy != 0) {
                if (dx * dx + dy * dy > radius * radius)
                    continue;
            }
            pix[(y + yy) * B89_BOARD_W + (x + xx)] = (unsigned char) color;
        }
    }
}

static int tile_index(int exp)
{
    if (exp < 1)
        return C_SLOT;
    if (exp > C_TILE_MAX)
        exp = C_TILE_MAX;
    return C_TILE + (exp - 1) * 3;
}

static int text_index(int exp)
{
    if (exp <= 2 || (exp >= 7 && exp <= 10))
        return C_TEXT_DARK;
    return C_TEXT_LIGHT;
}

static int text_shadow_index(int exp)
{
    if (exp <= 2 || (exp >= 7 && exp <= 10))
        return C_TEXT_DARK_SHADOW;
    return C_TEXT_LIGHT_SHADOW;
}

/*
 * A compact 5x7 bitmap face.  It is scaled up with whole-pixel blocks, which
 * stays crisp on a 1990s X server and needs no font files on the server.
 */
static const unsigned char digit[10][7] = {
    { 0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e },
    { 0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e },
    { 0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f },
    { 0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e },
    { 0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02 },
    { 0x1f, 0x10, 0x1e, 0x01, 0x01, 0x11, 0x0e },
    { 0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e },
    { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 },
    { 0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e },
    { 0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c }
};

static void draw_digits(unsigned char *pix, int x, int y,
                        const char *digits, int n, int scale, int color)
{
    int i, row, col;

    for (i = 0; i < n; i++) {
        int d = digits[i] - '0';
        int x0 = x + i * 6 * scale;

        for (row = 0; row < 7; row++) {
            for (col = 0; col < 5; col++) {
                if (digit[d][row] & (1 << (4 - col))) {
                    fill_rect(pix, x0 + col * scale, y + row * scale,
                              scale, scale, color);
                }
            }
        }
    }
}

static void draw_value(unsigned char *pix, int x, int y, int w, int h,
                       int exp)
{
    char digits[12];
    int n = 0;
    int scale, tw, th, x0, y0, i;
    unsigned long v = t89_value(exp);

    if (v == 0) {
        digits[n++] = '?';
    } else {
        while (v > 0 && n < (int) sizeof digits) {
            digits[n++] = (char) ('0' + (int) (v % 10));
            v /= 10;
        }
        for (i = 0; i < n / 2; i++) {
            char t = digits[i];
            digits[i] = digits[n - 1 - i];
            digits[n - 1 - i] = t;
        }
    }
    scale = 5;
    while (scale > 1 &&
           ((n * 5 + (n - 1)) * scale > w - 2 || 7 * scale > h - 2))
        scale--;
    tw = (n * 5 + (n - 1)) * scale;
    th = 7 * scale;
    x0 = x + (w - tw) / 2;
    y0 = y + (h - th) / 2;

    draw_digits(pix, x0 + 1, y0 + 1, digits, n, scale, text_shadow_index(exp));
    draw_digits(pix, x0, y0, digits, n, scale, text_index(exp));
}

static void draw_slot(unsigned char *pix, int x, int y, int size)
{
    round_rect(pix, x + 1, y + 2, size, size, 4, C_SLOT_DARK);
    round_rect(pix, x, y, size, size, 4, C_SLOT);
    fill_rect(pix, x + 3, y + 3, size - 6, 1, C_SLOT_LIGHT);
}

static void draw_tile(unsigned char *pix, int x, int y, int exp, int size)
{
    int base = tile_index(exp);
    int bevel = size >= 50 ? 2 : 1;

    round_rect(pix, x + 2, y + 3, size, size, 4, C_SHADOW);
    round_rect(pix, x, y, size, size, 4, base);
    fill_rect(pix, x + 4, y, size - 8, bevel, base + 1);
    fill_rect(pix, x, y + 4, bevel, size - 8, base + 1);
    fill_rect(pix, x + 4, y + size - bevel, size - 8, bevel, base + 2);
    fill_rect(pix, x + size - bevel, y + 4, bevel, size - 8, base + 2);
    fill_rect(pix, x + 4, y + 4, size - 8, 1, base + 1);
    draw_value(pix, x, y, size, size, exp);
}

void b89_render(const struct t89_game *g, unsigned char *pixels)
{
    int r, c, x, y, exp, n, gap, size, offset;

    palette_init();
    fill_rect(pixels, 0, 0, B89_BOARD_W, B89_BOARD_H, C_PAPER);
    round_rect(pixels, TRAY_X + 3, TRAY_Y + 5, TRAY_W, TRAY_W, 8, C_SHADOW);
    round_rect(pixels, TRAY_X, TRAY_Y, TRAY_W, TRAY_W, 8, C_TRAY_DARK);
    round_rect(pixels, TRAY_X + 1, TRAY_Y + 1, TRAY_W - 2, TRAY_W - 2,
               7, C_TRAY);
    round_rect(pixels, TRAY_X + 4, TRAY_Y + 4, TRAY_W - 8, TRAY_W - 8,
               5, C_TRAY_LIGHT);
    round_rect(pixels, TRAY_X + 8, TRAY_Y + 8, TRAY_W - 16, TRAY_W - 16,
               3, C_TRAY);

    n = g->size;
    if (n < 1)
        n = 1;
    if (n > 8)
        n = 8;
    gap = n <= 4 ? 8 : 4;
    size = (TRAY_INNER - (n - 1) * gap) / n;
    offset = TRAY_X + 8 + (TRAY_INNER - (n * size + (n - 1) * gap)) / 2;
    for (r = 0; r < n; r++) {
        for (c = 0; c < n; c++) {
            x = offset + c * (size + gap);
            y = offset + r * (size + gap);
            exp = g->cell[r * g->size + c];
            if (exp == 0)
                draw_slot(pixels, x, y, size);
            else
                draw_tile(pixels, x, y, exp, size);
        }
    }
}
