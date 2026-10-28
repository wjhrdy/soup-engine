/* GPL-2.0. Soup Engine's preview pipeline, adapted to ML's DryOS API.
 * Based on src/ml/payload/film.c (jiijii0). Inputs are opened read-only.
 */
#include "platform.h"
#include "cr2.h"
#include "look.h"
#include "jpeg_dec.h"
#include "jpeg_enc.h"
#include "se_libc.h"

#define HEADER_BYTES (256 * 1024)
#define IO_BYTES (256 * 1024)

typedef struct {
    FILE *input, *output;
    uint8_t *read_dma, *write_dma, *window;
    uint32_t base, length;
    uint32_t window_base, window_length;
    size_t output_used;
    se_jpeg *encoder;
    se_look_run *look;
    int width;
} soup_stream;

static int read_at(soup_stream *s, uint32_t off, void *dst, size_t n)
{
    if (FIO_SeekSkipFile(s->input, off, SEEK_SET) != off) {
        soup_error_detail = "I/O: seek CR2";
        return -1;
    }
    size_t done = 0;
    while (done < n && !soup_cancelled()) {
        size_t want = n - done < IO_BYTES ? n - done : IO_BYTES;
        /* Reuse the aligned DMA base even after an odd-length short read. */
        int k = FIO_ReadFile(s->input, s->read_dma, want);
        if (k <= 0 || (size_t)k > want) {
            soup_error_detail = "I/O: read CR2";
            return -1;
        }
        memcpy((uint8_t *)dst + done, s->read_dma, (size_t)k);
        done += (size_t)k;
    }
    return done == n ? 0 : -1;
}

static int preview_read(void *ctx, uint32_t off, void *dst, uint32_t n)
{
    soup_stream *s = ctx;
    if (n > IO_BYTES || off > s->length || n > s->length - off)
        return -1;
    uint8_t *out = dst;
    while (n && !soup_cancelled()) {
        if (off < s->window_base || off - s->window_base >= s->window_length) {
            uint32_t want = s->length - off;
            if (want > IO_BYTES) want = IO_BYTES;
            if (read_at(s, s->base + off, s->window, want)) return -1;
            s->window_base = off;
            s->window_length = want;
        }
        uint32_t k = s->window_length - (off - s->window_base);
        if (k > n) k = n;
        memcpy(out, s->window + off - s->window_base, k);
        off += k; out += k; n -= k;
    }
    return n ? -1 : 0;
}

static int flush_output(soup_stream *s)
{
    if (soup_cancelled()) return -1;
    if (s->output_used &&
        FIO_WriteFile(s->output, s->write_dma, s->output_used) != (int)s->output_used) {
        soup_error_detail = "I/O: write JPEG";
        return -1;
    }
    s->output_used = 0;
    return 0;
}

static int jpeg_write(void *ctx, const uint8_t *data, size_t n)
{
    soup_stream *s = ctx;
    while (n && !soup_cancelled()) {
        size_t k = IO_BYTES - s->output_used;
        if (k > n) k = n;
        memcpy(s->write_dma + s->output_used, data, k);
        s->output_used += k; data += k; n -= k;
        if (s->output_used == IO_BYTES && flush_output(s)) return -1;
    }
    return soup_cancelled() ? -1 : 0;
}

static int emit_row(void *ctx, const uint8_t *row, int width)
{
    soup_stream *s = ctx;
    if (soup_cancelled() || width != s->width) return -1;
    return se_jpeg_write_rows(s->encoder, row, 1) == SE_OK ? 0 : -1;
}

static int grade_rows(void *ctx, const uint8_t *rgb, int rows, int width)
{
    soup_stream *s = ctx;
    soup_yield();
    if (soup_cancelled() || width != s->width) return -1;
    return se_look_rows(s->look, rgb, rows, emit_row, s) == SE_OK ? 0 : -1;
}

se_err soup_develop(const char *cr2, const char *tmp,
                    const se_stock *stock, int scale)
{
    soup_stream s = {0};
    uint8_t *header = NULL, *work = NULL;
    se_cr2_info info;
    se_jdec_info ji;
    se_jpeg_exif ex;
    se_look look;
    se_arena arena, look_arena;
    uint32_t size = 0;
    int ow, oh, created = 0;
    se_err rc = SE_E_IO;

    soup_error_detail = NULL;
    if (scale != 1 && scale != 2 && scale != 4) return SE_E_UNSUPPORTED;
    if (FIO_GetFileSize(cr2, &size) || size < 16) return SE_E_BADFILE;
    s.input = FIO_OpenFile(cr2, O_RDONLY | O_SYNC);
    if (!s.input) { soup_error_detail = "I/O: open CR2"; goto done; }
    size_t got = size < HEADER_BYTES ? size : HEADER_BYTES;
    header = malloc(got);
    s.read_dma = fio_malloc(IO_BYTES);
    s.write_dma = fio_malloc(IO_BYTES);
    s.window = malloc(IO_BYTES);
    if (!header || !s.read_dma || !s.write_dma || !s.window) { rc = SE_E_NOMEM; goto done; }
    if (read_at(&s, 0, header, got)) goto done;

    rc = se_cr2_find_preview(header, got, &s.base, &s.length, NULL, NULL);
    if (rc != SE_OK) goto done;
    if (s.base > size || s.length > size - s.base || s.length < 4) {
        rc = SE_E_TRUNCATED; goto done;
    }
    /* Metadata parsing is optional for this JPEG path; it needs no RAW
     * colour matrix, black level, sensor calibration or white balance. */
    se_cr2_parse(header, got, &info);
    if (strcmp(info.model, "Canon EOS M")) {
        rc = SE_E_UNSUPPORTED; goto done;
    }
    size_t hdrlen = got - s.base;
    if (hdrlen > s.length) hdrlen = s.length;
    rc = se_jpeg_dec_info(header + s.base, hdrlen, &ji);
    if (rc != SE_OK) goto done;
    if (ji.width < 1 || ji.height < 1 || ji.width > 6000 || ji.height > 4000) {
        rc = SE_E_UNSUPPORTED; goto done;
    }
    se_jpeg_scaled_size(ji.width, ji.height, scale, &ow, &oh);
    se_look_from_stock(stock, scale, &look);
    size_t look_bytes = se_look_fit(&look, ow, SE_LOOK_BUDGET);
    /* MCU planes, RGB rows, encoder strip and Huffman tables, with slack
     * for row padding. Bounded even for the EOS M's full 5184px preview. */
    size_t scratch_bytes = (size_t)ow * 160 + 65536;
    work = malloc(scratch_bytes + look_bytes);
    if (!work) { rc = SE_E_NOMEM; goto done; }
    se_arena_init(&arena, work, scratch_bytes);
    se_arena_init(&look_arena, work + scratch_bytes, look_bytes);
    s.encoder = SE_NEW(&arena, se_jpeg, 1);
    uint8_t *strip = SE_NEW(&arena, uint8_t, SE_JPEG_STRIP_BYTES(ow, 1));
    if (!s.encoder || !strip) { rc = SE_E_NOMEM; goto done; }
    rc = se_look_begin(&s.look, &look, ow, &look_arena);
    if (rc != SE_OK) goto done;
    s.width = ow;

    memset(&ex, 0, sizeof ex);
    ex.make = "Canon";
    ex.model = info.model;
    ex.software = stock->name;
    ex.datetime = info.datetime;
    ex.exposure_time = info.exposure_time;
    ex.aperture = info.aperture;
    ex.iso = info.iso;

    /* DryOS open flags are not POSIX; use ML's supported create API.
     * Only this worker owns the scratch path; refuse an existing file. */
    uint32_t existing;
    if (!FIO_GetFileSize(tmp, &existing)) {
        soup_error_detail = "I/O: temp exists";
        rc = SE_E_IO; goto done;
    }
    s.output = FIO_CreateFile(tmp);
    if (!s.output) { soup_error_detail = "I/O: create JPEG"; rc = SE_E_IO; goto done; }
    created = 1;
    rc = se_jpeg_begin2(s.encoder, ow, oh, 88, strip, &ex, jpeg_write, &s, 1);
    if (rc == SE_OK)
        rc = se_jpeg_decode_stream_scaled(header + s.base, hdrlen, s.length,
                    preview_read, &s, scale, &arena, grade_rows, &s, &ji);
    if (rc == SE_OK) rc = se_look_end(s.look, emit_row, &s);
    if (rc == SE_OK) rc = se_jpeg_end(s.encoder);
    if (rc == SE_OK && flush_output(&s)) rc = SE_E_IO;

done:
    if (rc != SE_OK && soup_cancelled()) soup_error_detail = "Cancelled";
    if (s.output) FIO_CloseFile(s.output);
    if (s.input) FIO_CloseFile(s.input);
    if (rc != SE_OK && created) FIO_RemoveFile(tmp);
    if (header) free(header);
    if (s.read_dma) fio_free(s.read_dma);
    if (s.write_dma) fio_free(s.write_dma);
    if (s.window) free(s.window);
    if (work) free(work);
    return rc;
}
