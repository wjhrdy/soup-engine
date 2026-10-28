/* GPL-2.0. Original EOS M / Canon 2.0.2, Magic Lantern module API 8. */
#include "platform.h"
#include "se_libc.h"

#define MAX_STOCKS 24
#define STOCK_TEXT 16384

static CONFIG_INT("soup.enabled", soup_enabled, 0);
static CONFIG_INT("soup.stock", soup_stock, 0);
static CONFIG_INT("soup.full", soup_full, 0);
static se_stock stocks[MAX_STOCKS];
static int stock_count;
static volatile int stopping, worker_running;
static volatile int progress;
static volatile int last_result;
const char *soup_error_detail;
static char current_file[32];
static char stock_dir[64];

int soup_cancelled(void)
{
    return stopping || ml_shutdown_requested || !soup_enabled || is_movie_mode();
}

void soup_yield(void)
{
    /* Give Canon time to shoot, and stop competing for the card during a shot. */
    msleep(1);
    while (!soup_cancelled() && (lens_info.job_state || get_halfshutter_pressed()))
        msleep(50);
}

static int safe_component(const char *s)
{
    if (!s[0] || s[0] == '.' || strlen(s) > 31) return 0;
    for (; *s; s++)
        if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') ||
              (*s >= '0' && *s <= '9') || *s == '_' || *s == '.')) return 0;
    return 1;
}

static int cr2_name(const char *name)
{
    size_t n = strlen(name);
    return safe_component(name) && n > 4 && name[n-4] == '.' &&
        (name[n-3] == 'C' || name[n-3] == 'c') &&
        (name[n-2] == 'R' || name[n-2] == 'r') && name[n-1] == '2';
}

static int load_stocks(void)
{
    char path[96];
    char *index = fio_malloc(4096), *text = fio_malloc(STOCK_TEXT);
    if (!index || !text) goto done;
    snprintf(stock_dir, sizeof stock_dir, "%s:/ML/FILM", get_ml_card()->drive_letter);
    snprintf(path, sizeof path, "%s/INDEX.TXT", stock_dir);
    uint32_t size;
    if (FIO_GetFileSize(path, &size) || size >= 4096) goto done;
    int n = (int)read_file(path, index, 4095);
    if (n <= 0 || n != (int)size) goto done;
    index[n] = 0;
    char *line = index;
    for (int i = 0; i <= n && stock_count < MAX_STOCKS; i++) {
        if (index[i] && index[i] != '\n' && index[i] != '\r') continue;
        index[i] = 0;
        if (safe_component(line)) {
            snprintf(path, sizeof path, "%s/%s", stock_dir, line);
            if (!FIO_GetFileSize(path, &size) && size > 0 && size < STOCK_TEXT) {
                int got = (int)read_file(path, text, STOCK_TEXT - 1);
                if (got == (int)size) {
                    se_stock *s = &stocks[stock_count];
                    se_stock_init(s);
                    if (se_stock_parse(text, got, s) == SE_OK && s->name[0] &&
                        s->version <= SE_STOCK_VERSION) stock_count++;
                }
            }
        }
        line = index + i + 1;
    }
done:
    if (index) fio_free(index);
    if (text) fio_free(text);
    if (soup_stock < 0 || soup_stock >= stock_count) soup_stock = 0;
    return stock_count;
}

static void on_progress(void *ctx, const char *stage, int percent)
{
    progress = percent;
}

/* 1 = produced a JPEG, 0 = skipped/not ready, negative = develop error. */
static int process_file(const char *dir, const char *name)
{
    char cr2[128], jpg[128], tmp[128];
    uint32_t before, after;
    if (soup_cancelled() || !stock_count || !cr2_name(name)) return 0;
    if (snprintf(cr2, sizeof cr2, "%s/%s", dir, name) >= (int)sizeof cr2) return 0;
    size_t n = strlen(cr2) - 4;
    if (n + sizeof ".FLM.JPG" > sizeof jpg) return 0;
    memcpy(jpg, cr2, n); memcpy(jpg + n, ".FLM.JPG", sizeof ".FLM.JPG");
    memcpy(tmp, cr2, n); memcpy(tmp + n, ".FLM.TMP", sizeof ".FLM.TMP");
    /* This includes zero-byte files: never silently replace an existing JPEG. */
    if (FIO_GetFileSize(jpg, &after) == 0) return 0;
    soup_yield();
    if (soup_cancelled() || FIO_GetFileSize(cr2, &before) || before < 16) return 0;
    msleep(500);
    if (soup_cancelled() || lens_info.job_state ||
        FIO_GetFileSize(cr2, &after) || after != before) return 0;

    /* Only this worker owns this scratch name. An interrupted develop can retry. */
    if (FIO_GetFileSize(tmp, &after) == 0 && FIO_RemoveFile(tmp)) {
        soup_error_detail = "I/O: remove temp";
        last_result = SE_E_IO;
        return SE_E_IO;
    }
    int selected = soup_stock;
    if (selected < 0 || selected >= stock_count) selected = 0;
    snprintf(current_file, sizeof current_file, "%s", name);
    progress = 0;
    int scale = soup_full == 1 ? 1 : soup_full == 2 ? 4 : 2;
    se_err rc = soup_develop(cr2, tmp, &stocks[selected], scale);
    if (rc == SE_OK && !soup_cancelled()) {
        /* Publish only a finished stream, and check for a name collision again. */
        if (FIO_GetFileSize(jpg, &after) == 0 || FIO_RenameFile(tmp, jpg)) {
            soup_error_detail = "I/O: publish JPEG";
            rc = SE_E_IO;
        }
    } else if (rc == SE_OK) {
        soup_error_detail = "Cancelled";
        rc = SE_E_IO;
    }
    if (rc != SE_OK) FIO_RemoveFile(tmp);
    last_result = rc;
    if (rc != SE_OK) printf("Soup %s: %s (%d)\n", name,
        soup_error_detail ? soup_error_detail : se_strerror(rc), rc);
    current_file[0] = 0;
    return rc == SE_OK ? 1 : rc;
}

static void scan_dir(const char *dir)
{
    struct fio_file *entry = alloc_fio_file();
    if (!entry) return;
    struct fio_dirent *it = FIO_FindFirstEx(dir, entry);
    if (it && !IS_ERROR(it)) {
        do {
            struct file_info f = convert_fio_file_info(entry);
            if (!(f.mode & ATTR_DIRECTORY)) process_file(dir, f.name);
            if (soup_cancelled()) break;
        } while (FIO_FindNextEx(it, entry) == 0);
        FIO_FindClose(it);
    }
    free(entry);
}

static void scan_card(void)
{
    struct card_info *card = get_shooting_card();
    if (!card) return;
    char root[16], dir[64];
    snprintf(root, sizeof root, "%s:/DCIM", card->drive_letter);
    struct fio_file *entry = alloc_fio_file();
    if (!entry) return;
    struct fio_dirent *it = FIO_FindFirstEx(root, entry);
    if (it && !IS_ERROR(it)) {
        do {
            struct file_info f = convert_fio_file_info(entry);
            /* Directory enumeration handles folder changes, Adobe RGB prefixes,
             * bursts and file number wrap without guessing the next filename. */
            if ((f.mode & ATTR_DIRECTORY) && safe_component(f.name)) {
                snprintf(dir, sizeof dir, "%s/%s", root, f.name);
                scan_dir(dir);
            }
            if (soup_cancelled()) break;
        } while (FIO_FindNextEx(it, entry) == 0);
        FIO_FindClose(it);
    }
    free(entry);
}

#ifndef SOUP_HOST_TEST
static void soup_worker(void *unused)
{
    while (!stopping && !ml_shutdown_requested) {
        if (!soup_cancelled() && stock_count) scan_card();
        msleep(1000);
    }
    worker_running = 0;
}

static MENU_SELECT_FUNC(select_stock)
{
    if (stock_count) soup_stock = (soup_stock + delta + stock_count) % stock_count;
}

static MENU_UPDATE_FUNC(show_stock)
{
    int i = soup_stock;
    MENU_SET_VALUE("%s", i >= 0 && i < stock_count ? stocks[i].name : "No stocks");
}

static MENU_UPDATE_FUNC(show_status)
{
    if (!stock_count) MENU_SET_VALUE("No film stocks");
    else if (current_file[0]) MENU_SET_VALUE("%s %d%%", current_file, progress);
    else if (last_result) MENU_SET_VALUE("%s", soup_error_detail ? soup_error_detail : se_strerror(last_result));
    else MENU_SET_VALUE("%s", soup_enabled ? "Watching DCIM" : "Off");
}

static struct menu_entry soup_menu[] = {
    {
        .name = "Soup Engine", .priv = &soup_enabled, .max = 1,
        .depends_on = DEP_PHOTO_MODE,
        .help = "Develop card CR2 previews to separate film JPEGs.",
        .help2 = "Includes existing CR2 files. Originals are kept. Video is paused.",
        .children = (struct menu_entry[]) {
            { .name = "Film stock", .select = select_stock, .update = show_stock,
              .help = "Film stocks loaded from ML/FILM/INDEX.TXT at startup." },
            { .name = "Output size", .priv = &soup_full, .max = 2,
              .choices = CHOICES("Half preview", "Full preview", "Quarter preview"),
              .help = "Quarter is fastest (~1296x864). Half and full remain available." },
            { .name = "Status", .update = show_status,
              .help = "Failed frames are retried; turn Soup Engine off to cancel." },
            MENU_EOL
        },
    },
};

static unsigned int soup_init(void)
{
    if (!is_camera("EOSM", "2.0.2")) return 1;
    stopping = 0;
    load_stocks();
    se_set_progress(on_progress, NULL);
    menu_add("Shoot", soup_menu, COUNT(soup_menu));
    worker_running = 1;
    if (!task_create("soup", 0x1f, 0x10000, soup_worker, NULL)) {
        worker_running = 0;
        menu_remove("Shoot", soup_menu, COUNT(soup_menu));
        return 1;
    }
    return 0;
}

static unsigned int soup_deinit(void)
{
    stopping = 1;
    /* Wait until the worker has closed its handles and released its memory. */
    while (worker_running) msleep(20);
    menu_remove("Shoot", soup_menu, COUNT(soup_menu));
    se_set_progress(NULL, NULL);
    return 0;
}

MODULE_INFO_START()
    MODULE_INIT(soup_init)
    MODULE_DEINIT(soup_deinit)
MODULE_INFO_END()

MODULE_CONFIGS_START()
    MODULE_CONFIG(soup_enabled)
    MODULE_CONFIG(soup_stock)
    MODULE_CONFIG(soup_full)
MODULE_CONFIGS_END()
#endif
