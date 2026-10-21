/*
 * gifenc.c - GIF87a writer for 256-colour images, C89.
 *
 * The LZW compressor follows the same state machine a GIF decoder runs in
 * reverse, so the code-size changes stay in lockstep with the reader.
 *
 * Author: DeepSeek V4.1 Flash 1017, October 2026.
 */
#include <stdio.h>
#include "gifenc.h"

#define HASH_MULT 2654435761UL
#define HASH_MASK (GIFENC_HASHSIZE - 1)

static void put_u16(FILE *f, int v)
{
    fputc(v & 0xff, f);
    fputc((v >> 8) & 0xff, f);
}

static void flush_block(GifWriter *g)
{
    if (g->block_len > 0) {
        fputc(g->block_len, g->out);
        fwrite(g->block, 1, (size_t)g->block_len, g->out);
        g->block_len = 0;
    }
}

static void put_byte(GifWriter *g, int b)
{
    g->block[g->block_len] = (unsigned char)(b & 0xff);
    g->block_len++;
    if (g->block_len == 255)
        flush_block(g);
}

/*
 * Emit one code and then, using the free-entry count as it stands *before*
 * this iteration's dictionary insert, decide whether the code width grows.
 * This is the ordering every GIF decoder expects; getting it one code late
 * desynchronises the stream.
 */
static void put_code(GifWriter *g, long code)
{
    g->accum |= code << g->accum_bits;
    g->accum_bits += g->n_bits;
    while (g->accum_bits >= 8) {
        put_byte(g, (int)(g->accum & 0xff));
        g->accum >>= 8;
        g->accum_bits -= 8;
    }

    if (g->free_ent > g->max_code || g->clear_flag) {
        if (g->clear_flag) {
            g->n_bits = g->min_code_size + 1;
            g->max_code = (1L << g->n_bits) - 1;
            g->clear_flag = 0;
        } else {
            g->n_bits++;
            if (g->n_bits == 12)
                g->max_code = 4096L;
            else
                g->max_code = (1L << g->n_bits) - 1;
        }
    }
}

static void clear_table(GifWriter *g)
{
    int i;
    for (i = 0; i < GIFENC_HASHSIZE; i++)
        g->hash_key[i] = -1;
}

int gif_begin(GifWriter *g, FILE *out, int width, int height,
              const unsigned char *palette)
{
    g->out = out;
    g->width = width;
    g->height = height;
    g->rows_written = 0;
    g->error = 0;
    g->block_len = 0;
    g->accum = 0;
    g->accum_bits = 0;
    g->ent = -1;
    g->clear_flag = 0;

    g->min_code_size = 8;
    g->clear_code = 1 << g->min_code_size;   /* 256 */
    g->eoi_code = g->clear_code + 1;         /* 257 */

    fwrite("GIF87a", 1, 6, out);
    put_u16(out, width);
    put_u16(out, height);
    fputc(0x80 | 0x70 | 0x07, out);          /* global table, 8 bit, 256 */
    fputc(0, out);                            /* background colour */
    fputc(0, out);                            /* pixel aspect ratio */

    fwrite(palette, 1, 256 * 3, out);

    fputc(0x2c, out);                         /* image separator */
    put_u16(out, 0);
    put_u16(out, 0);
    put_u16(out, width);
    put_u16(out, height);
    fputc(0, out);                            /* no local table, no interlace */

    fputc(g->min_code_size, out);

    g->n_bits = g->min_code_size + 1;         /* 9 */
    g->max_code = (1L << g->n_bits) - 1;      /* 511 */
    g->free_ent = g->clear_code + 2;          /* 258 */
    clear_table(g);
    put_code(g, g->clear_code);

    if (ferror(out)) {
        g->error = 1;
        return 0;
    }
    return 1;
}

int gif_write_row(GifWriter *g, const unsigned char *row)
{
    int x;

    if (g->error)
        return 0;

    for (x = 0; x < g->width; x++) {
        long c = row[x];

        if (g->ent < 0) {
            g->ent = c;
            continue;
        }

        {
            long key = (g->ent << 8) | c;
            unsigned long h = ((unsigned long)key * HASH_MULT) & HASH_MASK;

            while (g->hash_key[h] != -1 && g->hash_key[h] != key)
                h = (h + 1) & HASH_MASK;

            if (g->hash_key[h] == key) {
                g->ent = (long)g->hash_code[h];
            } else {
                put_code(g, g->ent);
                g->ent = c;
                if (g->free_ent < 4096) {
                    g->hash_key[h] = key;
                    g->hash_code[h] = (unsigned short)g->free_ent;
                    g->free_ent++;
                } else {
                    clear_table(g);
                    g->free_ent = g->clear_code + 2;
                    g->clear_flag = 1;
                    put_code(g, g->clear_code);
                }
            }
        }
    }

    g->rows_written++;
    if (ferror(g->out)) {
        g->error = 1;
        return 0;
    }
    return 1;
}

int gif_end(GifWriter *g)
{
    if (g->error)
        return 0;

    if (g->ent >= 0)
        put_code(g, g->ent);
    put_code(g, g->eoi_code);

    if (g->accum_bits > 0)
        put_byte(g, (int)(g->accum & 0xff));
    flush_block(g);
    fputc(0, g->out);     /* block terminator */
    fputc(0x3b, g->out);  /* trailer */

    if (ferror(g->out)) {
        g->error = 1;
        return 0;
    }
    return 1;
}
