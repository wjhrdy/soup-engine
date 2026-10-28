#define _POSIX_C_SOURCE 200809L
#include "platform.h"
#include <assert.h>
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include "../soup.c"
#include "jpeg_enc.h"
#include "jpeg_dec.h"

int ml_shutdown_requested;
__typeof__(lens_info) lens_info;
static int movie, halfshutter, handles, dir_handles;
static int short_reads, read_fail, write_fail, cancel_after, alloc_fail;
static int read_fail_after, write_fail_after;
static int open_fail, create_fail, seek_fail;
static void *dma_bases[8];
static size_t allocated, peak;
static unsigned read_calls, write_calls;
static const char *card_root;
static struct card_info card = { "A" };
struct fio_dirent { DIR *dir; char path[512]; };

static const char *map_path(const char *p, char out[512])
{
    if (!strncmp(p, "A:/", 3)) {
        assert(snprintf(out, 512, "%s/%s", card_root, p + 3) < 512);
        return out;
    }
    return p;
}

int is_movie_mode(void) { return movie; }
int get_halfshutter_pressed(void) { return halfshutter; }
void msleep(int ms)
{
    if (ms == 50) { lens_info.job_state = 0; halfshutter = 0; }
    if (ms == 1 && cancel_after && !--cancel_after) stopping = 1;
}
void *fio_malloc(size_t n)
{
    if (alloc_fail) return NULL;
    size_t *p = malloc(n + sizeof(size_t));
    if (!p) return NULL;
    *p = n; allocated += n;
    if (allocated > peak) peak = allocated;
    for (unsigned i = 0; i < 8; i++) if (!dma_bases[i]) {
        dma_bases[i] = p + 1;
        return p + 1;
    }
    assert(0); return NULL;
}
void fio_free(void *p)
{
    if (p) {
        int found = 0;
        for (unsigned i = 0; i < 8; i++) if (p == dma_bases[i]) {
            dma_bases[i] = NULL; found = 1; break;
        }
        assert(found);
        size_t *base = (size_t *)p - 1; allocated -= *base; free(base);
    }
}
static int dma_base_valid(const void *p)
{
    for (unsigned i = 0; i < 8; i++) if (p == dma_bases[i]) return 1;
    return 0;
}
FILE *FIO_OpenFile(const char *p, unsigned mode)
{
    char buf[512]; p = map_path(p, buf);
    assert(mode == O_RDONLY || mode == (O_RDONLY | O_SYNC));
    if (open_fail) return NULL;
    FILE *f = fopen(p, "rb");
    if (f) handles++;
    return f;
}
FILE *FIO_CreateFile(const char *p)
{
    char buf[512]; p = map_path(p, buf);
    assert(!cr2_name(strrchr(p, '/') ? strrchr(p, '/') + 1 : p));
    /* ML's create removes an existing file. The caller must guard against it. */
    if (create_fail) return NULL;
    FILE *f = fopen(p, "wb");
    if (f) handles++;
    return f;
}
int FIO_ReadFile(FILE *f, void *p, size_t n)
{
    assert(dma_base_valid(p)); /* Odd short reads must retain the DMA base. */
    read_calls++;
    if (read_fail || (read_fail_after && !--read_fail_after)) return -1;
    if (short_reads && n > 777) n = 777;
    return (int)fread(p, 1, n, f);
}
int FIO_WriteFile(FILE *f, const void *p, size_t n)
{
    assert(dma_base_valid(p));
    write_calls++;
    if (write_fail || (write_fail_after && !--write_fail_after)) return n ? (int)n - 1 : -1;
    return (int)fwrite(p, 1, n, f);
}
int64_t FIO_SeekSkipFile(FILE *f, int64_t off, int whence)
{ return seek_fail || fseek(f, (long)off, whence) ? -1 : ftell(f); }
void FIO_CloseFile(FILE *f) { assert(f); assert(fclose(f) == 0); handles--; }
int FIO_RemoveFile(const char *p)
{
    char buf[512];
    assert(!cr2_name(strrchr(p, '/') ? strrchr(p, '/') + 1 : p));
    return remove(map_path(p, buf));
}
int FIO_RenameFile(const char *a, const char *b)
{
    char abuf[512], bbuf[512];
    assert(!cr2_name(strrchr(a, '/') ? strrchr(a, '/') + 1 : a));
    assert(!cr2_name(strrchr(b, '/') ? strrchr(b, '/') + 1 : b));
    a = map_path(a, abuf); b = map_path(b, bbuf);
    /* Refuse replacement even if the production caller's collision check regresses. */
    struct stat st; assert(stat(b, &st) != 0);
    return rename(a, b);
}
int FIO_GetFileSize(const char *p, uint32_t *n)
{
    char buf[512]; struct stat st;
    if (stat(map_path(p, buf), &st)) return -1;
    *n = (uint32_t)st.st_size; return 0;
}
size_t read_file(const char *p, void *buf, size_t n)
{
    FILE *f = FIO_OpenFile(p, O_RDONLY);
    if (!f) return 0;
    size_t got = fread(buf, 1, n, f);
    FIO_CloseFile(f); return got;
}
struct card_info *get_ml_card(void) { return &card; }
struct card_info *get_shooting_card(void) { return &card; }
struct fio_file *alloc_fio_file(void) { return malloc(sizeof(struct fio_file)); }
struct file_info convert_fio_file_info(struct fio_file *p) { return p->info; }
int FIO_FindNextEx(struct fio_dirent *d, struct fio_file *p)
{
    struct dirent *e = readdir(d->dir);
    if (!e) return -1;
    char full[1024]; struct stat st;
    snprintf(full, sizeof full, "%s/%s", d->path, e->d_name);
    assert(!stat(full, &st));
    p->info.mode = S_ISDIR(st.st_mode) ? ATTR_DIRECTORY : 0;
    p->info.size = (uint32_t)st.st_size;
    snprintf(p->info.name, sizeof p->info.name, "%s", e->d_name);
    return 0;
}
struct fio_dirent *FIO_FindFirstEx(const char *dir, struct fio_file *p)
{
    char buf[512]; dir = map_path(dir, buf);
    DIR *it = opendir(dir);
    if (!it) return NULL;
    struct fio_dirent *d = malloc(sizeof *d);
    assert(d); d->dir = it;
    snprintf(d->path, sizeof d->path, "%s", dir); dir_handles++;
    assert(FIO_FindNextEx(d, p) == 0);
    return d;
}
void FIO_FindClose(struct fio_dirent *d)
{ closedir(d->dir); free(d); dir_handles--; }

static void clean_state(void)
{
    stopping = movie = halfshutter = ml_shutdown_requested = 0;
    short_reads = read_fail = write_fail = cancel_after = alloc_fail = 0;
    read_fail_after = write_fail_after = 0;
    open_fail = create_fail = seek_fail = 0;
    read_calls = write_calls = 0;
    soup_enabled = 1;
    assert(!handles && !dir_handles && !allocated);
}

static int fixture_sink(void *ctx, const uint8_t *p, size_t n)
{ return fwrite(p, 1, n, ctx) == n ? 0 : -1; }

static void fixture(const char *path)
{
    uint8_t header[2048] = { 'I', 'I', 42, 0, 16, 0, 0, 0, 'C', 'R', 2, 0 };
    /* Little-endian TIFF entries, a JPEG preview and an EOS M model string. */
    uint32_t values[] = {80, 48, 6, 2048, 0, 300};
    unsigned tags[] = {0x100, 0x101, 0x103, 0x111, 0x117, 0x110};
    header[16] = 6;
    for (int i = 0; i < 6; i++) {
        int off = 18 + i * 12;
        header[off] = tags[i]; header[off+1] = tags[i] >> 8;
        header[off+2] = i == 5 ? 2 : 4;
        header[off+4] = i == 5 ? sizeof "Canon EOS M" : 1;
        for (int k = 0; k < 4; k++) header[off+8+k] = values[i] >> (8*k);
    }
    memcpy(header + 300, "Canon EOS M", sizeof "Canon EOS M");
    FILE *f = fopen(path, "wb"); assert(f);
    fwrite(header, 1, sizeof header, f);
    se_jpeg *enc = calloc(1, sizeof *enc); assert(enc);
    uint8_t strip[80 * 8 * 3], row[80 * 3];
    assert(se_jpeg_begin(enc, 80, 48, 88, strip, NULL, fixture_sink, f) == SE_OK);
    for (int y = 0; y < 48; y++) {
        for (int x = 0; x < 80*3; x++) row[x] = (x * 11 + y * 3) % 256;
        assert(se_jpeg_write_rows(enc, row, 1) == SE_OK);
    }
    assert(se_jpeg_end(enc) == SE_OK);
    uint32_t bytes = (uint32_t)ftell(f) - 2048;
    for (int k = 0; k < 4; k++) header[18+4*12+8+k] = bytes >> (8*k);
    rewind(f); fwrite(header, 1, sizeof header, f); fclose(f); free(enc);
}

int main(int argc, char **argv)
{
    assert(argc == 6);
    card_root = argv[1];
    assert(load_stocks() == 13);
    soup_enabled = 1;
    assert(cr2_name("IMG_1234.CR2") && cr2_name("_MG_9999.cr2"));
    assert(!cr2_name("../IMG_1234.CR2") && !cr2_name("IMG_1234.FLM.JPG"));
    assert(!safe_component("..") && !safe_component("A:/anything.fs"));

    /* A real EOS M CR2, including >8 KB stream refills and partial reads. */
    int selected = atoi(argv[4]), scale = atoi(argv[5]);
    short_reads = 1;
    assert(soup_develop(argv[2], argv[3], &stocks[selected], scale) == SE_OK);
    clean_state();
    char tmp[512]; snprintf(tmp, sizeof tmp, "%s/failure.tmp", card_root);
    /* Normal reads/writes must be batched rather than one call per 4/8 KB. */
    assert(soup_develop(argv[2], tmp, &stocks[selected], scale) == SE_OK);
    uint32_t source_bytes, output_bytes;
    assert(!FIO_GetFileSize(argv[2], &source_bytes) && !FIO_GetFileSize(tmp, &output_bytes));
    assert(read_calls <= (source_bytes / (256 * 1024)) + 2);
    assert(write_calls == (output_bytes + 256 * 1024 - 1) / (256 * 1024));
    printf("BATCHED stock=%d scale=%d: reads=%u writes=%u bytes=%u\n",
           selected, scale, read_calls, write_calls, output_bytes);
    assert(!remove(tmp)); clean_state();
    alloc_fail = 1;
    assert(soup_develop(argv[2], tmp, &stocks[0], 2) == SE_E_NOMEM);
    clean_state();
    read_fail = 1;
    assert(soup_develop(argv[2], tmp, &stocks[0], 2) == SE_E_IO);
    assert(!strcmp(soup_error_detail, "I/O: read CR2"));
    clean_state();
    open_fail = 1;
    assert(soup_develop(argv[2], tmp, &stocks[0], 2) == SE_E_IO);
    assert(!strcmp(soup_error_detail, "I/O: open CR2")); clean_state();
    seek_fail = 1;
    assert(soup_develop(argv[2], tmp, &stocks[0], 2) == SE_E_IO);
    assert(!strcmp(soup_error_detail, "I/O: seek CR2")); clean_state();
    create_fail = 1;
    assert(soup_develop(argv[2], tmp, &stocks[0], 2) == SE_E_IO);
    assert(!strcmp(soup_error_detail, "I/O: create JPEG")); clean_state();
    write_fail = 1;
    assert(soup_develop(argv[2], tmp, &stocks[0], 2) != SE_OK);
    assert(!strcmp(soup_error_detail, "I/O: write JPEG"));
    assert(access(tmp, F_OK) != 0); clean_state();
    read_fail_after = 2;
    assert(soup_develop(argv[2], tmp, &stocks[0], 2) != SE_OK);
    assert(access(tmp, F_OK) != 0); clean_state();
    write_fail_after = 2;
    assert(soup_develop(argv[2], tmp, &stocks[0], 2) != SE_OK);
    assert(access(tmp, F_OK) != 0); clean_state();
    cancel_after = 4;
    assert(soup_develop(argv[2], tmp, &stocks[0], 2) != SE_OK);
    assert(access(tmp, F_OK) != 0); clean_state();
    /* The existence guard must protect files from ML's destructive create. */
    FILE *f = fopen(tmp, "wb"); assert(f); fputs("keep", f); fclose(f);
    assert(soup_develop(argv[2], tmp, &stocks[0], 2) == SE_E_IO);
    assert(!strcmp(soup_error_detail, "I/O: temp exists"));
    char bytes[5] = {0}; f = fopen(tmp, "rb"); assert(f);
    assert(fread(bytes, 1, 4, f) == 4); fclose(f); assert(!strcmp(bytes, "keep"));
    remove(tmp); clean_state();
    assert(soup_develop("/no-such-cr2", tmp, &stocks[0], 2) != SE_OK);
    assert(soup_develop(argv[2], tmp, &stocks[0], 3) == SE_E_UNSUPPORTED);
    clean_state();

    /* Small fixtures exercise the actual directory worker, including a
     * burst spanning folder rollover and Adobe RGB's _MG_ prefix. */
    char path[512];
    const char *names[] = {"100CANON/IMG_9999", "101CANON/_MG_0001", "101CANON/IMG_0002"};
    for (int i = 0; i < 3; i++) {
        snprintf(path, sizeof path, "%s/DCIM/%s.CR2", card_root, names[i]); fixture(path);
    }
    soup_full = 2;
    assert(process_file("A:/DCIM/100CANON", "IMG_9999.CR2") == 1);
    snprintf(path, sizeof path, "%s/DCIM/100CANON/IMG_9999.FLM.JPG", card_root);
    FILE *quarter = fopen(path, "rb"); assert(quarter);
    uint8_t quarter_header[2048]; size_t quarter_bytes = fread(quarter_header, 1, sizeof quarter_header, quarter);
    fclose(quarter);
    se_jdec_info quarter_info;
    assert(se_jpeg_dec_info(quarter_header, quarter_bytes, &quarter_info) == SE_OK);
    assert(quarter_info.width == 20 && quarter_info.height == 12);
    assert(!remove(path)); soup_full = 0;
    snprintf(path, sizeof path, "%s/DCIM/101CANON/IMG_0002.FLM.JPG", card_root);
    f = fopen(path, "wb"); assert(f); fclose(f);
    lens_info.job_state = halfshutter = 1;
    scan_card();
    assert(!lens_info.job_state && !halfshutter);
    uint32_t n;
    assert(!FIO_GetFileSize("A:/DCIM/100CANON/IMG_9999.FLM.JPG", &n) && n > 0);
    assert(!FIO_GetFileSize("A:/DCIM/101CANON/_MG_0001.FLM.JPG", &n) && n > 0);
    assert(!FIO_GetFileSize("A:/DCIM/101CANON/IMG_0002.FLM.JPG", &n) && n == 0);
    assert(process_file("A:/DCIM/101CANON", "IMG_0002.CR2") == 0);
    assert(process_file("A:/DCIM/100CANON", "IMG_9999.CR2") == 0);
    clean_state();
    movie = 1; assert(soup_cancelled());
    movie = 0; ml_shutdown_requested = 1; assert(soup_cancelled());
    ml_shutdown_requested = 0; soup_enabled = 0; assert(soup_cancelled());
    clean_state();
    printf("PASS stock=%d scale=%d; I/O failures, cancel, preservation, DCIM scan (DMA peak %zu)\n",
           selected, scale, peak);
    return 0;
}
