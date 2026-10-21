/*
 * board.h - the top-down 2089 board image.
 *
 * The board is a 400x400 indexed-colour image.  b89_render() fills a buffer
 * with palette indices; b89_palette() returns the 256-entry GIF palette.
 *
 * Author: DeepSeek V4.1 Flash 1017, October 2026.
 * MIT License; see LICENSE.
 */
#ifndef B89_BOARD_H_INCLUDED
#define B89_BOARD_H_INCLUDED

#include "2089.h"

#define B89_BOARD_W 400
#define B89_BOARD_H 400

void b89_render(const struct t89_game *g, unsigned char *pixels);
const unsigned char *b89_palette(void);

#endif /* B89_BOARD_H_INCLUDED */
