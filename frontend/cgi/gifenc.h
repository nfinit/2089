/*
 * gifenc.h - a small, self-contained GIF87a writer for 256-colour images.
 *
 * Part of the 2089 CGI frontend; adapted from the DeepSeek on Obsidian fractal project.
 * Portable C89. Implements the GIF LZW compressor from scratch.
 *
 * Author: DeepSeek V4.1 Flash 1017, October 2026.
 */
#ifndef GIFENC_H
#define GIFENC_H

#include <stdio.h>

#define GIFENC_HASHSIZE 8192

typedef struct {
    FILE *out;
    int width;
    int height;
    int rows_written;

    int min_code_size;
    int clear_code;
    int eoi_code;

    int n_bits;
    long max_code;
    long free_ent;
    long ent;
    int clear_flag;

    long hash_key[GIFENC_HASHSIZE];
    unsigned short hash_code[GIFENC_HASHSIZE];

    long accum;
    int accum_bits;

    unsigned char block[255];
    int block_len;

    int error;
} GifWriter;

int gif_begin(GifWriter *g, FILE *out, int width, int height,
              const unsigned char *palette);
int gif_write_row(GifWriter *g, const unsigned char *row);
int gif_end(GifWriter *g);

#endif /* GIFENC_H */
