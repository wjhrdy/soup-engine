/* Independent libjpeg checks for 4:2:0 sampling, dimensions and decoded pixels. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <jpeglib.h>
#include "jpeg_enc.h"

typedef struct { uint8_t *data; size_t used, cap; } output;
static int sink(void *ctx, const uint8_t *p, size_t n)
{
    output *o = ctx;
    if (o->used + n > o->cap) return -1;
    memcpy(o->data + o->used, p, n); o->used += n; return 0;
}

static void decode(const uint8_t *jpg, size_t bytes, int w, int h, int flat)
{
    struct jpeg_decompress_struct d;
    struct jpeg_error_mgr errors;
    d.err = jpeg_std_error(&errors);
    jpeg_create_decompress(&d);
    jpeg_mem_src(&d, jpg, bytes);
    assert(jpeg_read_header(&d, TRUE) == JPEG_HEADER_OK);
    assert(d.num_components == 3);
    assert(d.comp_info[0].h_samp_factor == 2 && d.comp_info[0].v_samp_factor == 2);
    for (int c = 1; c < 3; c++)
        assert(d.comp_info[c].h_samp_factor == 1 && d.comp_info[c].v_samp_factor == 1);
    if (w) assert(d.image_width == (unsigned)w && d.image_height == (unsigned)h);
    d.out_color_space = JCS_RGB;
    assert(jpeg_start_decompress(&d));
    uint8_t *row = malloc(d.output_width * 3); assert(row);
    while (d.output_scanline < d.output_height) {
        JSAMPROW rows[] = {row};
        assert(jpeg_read_scanlines(&d, rows, 1) == 1);
        if (flat) for (unsigned x = 0; x < d.output_width; x++) {
            const int expected[] = {100, 150, 200};
            for (int c = 0; c < 3; c++) assert(abs(row[x*3+c] - expected[c]) <= 3);
        }
    }
    assert(jpeg_finish_decompress(&d));
    assert(errors.num_warnings == 0);
    free(row); jpeg_destroy_decompress(&d);
}

int main(int argc, char **argv)
{
    if (argc == 2) {
        FILE *f = fopen(argv[1], "rb"); assert(f);
        assert(!fseek(f, 0, SEEK_END)); long n = ftell(f); assert(n > 0); rewind(f);
        uint8_t *jpg = malloc((size_t)n); assert(jpg);
        assert(fread(jpg, 1, n, f) == (size_t)n); fclose(f);
        decode(jpg, n, 0, 0, 0); free(jpg); return 0;
    }
    const int sizes[][2] = {{1,1}, {7,9}, {15,17}, {17,15}, {61,37}, {2592,17}};
    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        int w = sizes[i][0], h = sizes[i][1];
        uint8_t *rgb = malloc((size_t)w*h*3), *strip = malloc(SE_JPEG_STRIP_BYTES(w,1));
        se_jpeg *j = calloc(1, sizeof *j); assert(rgb && strip && j);
        output o = {malloc((size_t)w*h*16 + 65536), 0, (size_t)w*h*16 + 65536}; assert(o.data);
        for (int pattern = 0; pattern < 2; pattern++) {
            for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
                uint8_t *p = rgb + ((size_t)y*w+x)*3;
                p[0] = pattern ? x*251/(w ? w : 1) : 100;
                p[1] = pattern ? y*253/(h ? h : 1) : 150;
                p[2] = pattern ? (x*31 + y*47) % 256 : 200;
            }
            o.used = 0;
            assert(se_jpeg_begin2(j,w,h,95,strip,NULL,sink,&o,1) == SE_OK);
            assert(se_jpeg_write_rows(j,rgb,h) == SE_OK);
            assert(se_jpeg_end(j) == SE_OK);
            decode(o.data,o.used,w,h,!pattern);
        }
        free(o.data); free(j); free(strip); free(rgb);
    }
    puts("PASS: libjpeg decodes 12 flat/colour 4:2:0 fixtures without warnings or dimension loss");
    return 0;
}
