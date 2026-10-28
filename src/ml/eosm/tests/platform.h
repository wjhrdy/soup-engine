#ifndef SOUP_TEST_PLATFORM_H
#define SOUP_TEST_PLATFORM_H
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>

#define CONFIG_INT(name, var, value) int var = value
#define O_RDONLY 0
#define O_WRONLY 1
#define O_CREAT 0100
#define O_EXCL 0200
#define O_SYNC 010000
#define ATTR_DIRECTORY 0x10
#define IS_ERROR(p) ((uintptr_t)(p) & 1)
struct card_info { char *drive_letter; };
struct file_info { unsigned mode; uint32_t size; char name[128]; };
struct fio_file { struct file_info info; };
struct fio_dirent;
extern int ml_shutdown_requested;
extern struct { int job_state; } lens_info;
int is_movie_mode(void);
int get_halfshutter_pressed(void);
void msleep(int ms);
void *fio_malloc(size_t n);
void fio_free(void *p);
FILE *FIO_OpenFile(const char *path, unsigned mode);
FILE *FIO_CreateFile(const char *path);
int FIO_ReadFile(FILE *f, void *p, size_t n);
int FIO_WriteFile(FILE *f, const void *p, size_t n);
int64_t FIO_SeekSkipFile(FILE *f, int64_t off, int whence);
void FIO_CloseFile(FILE *f);
int FIO_RemoveFile(const char *path);
int FIO_RenameFile(const char *a, const char *b);
int FIO_GetFileSize(const char *path, uint32_t *n);
size_t read_file(const char *path, void *buf, size_t n);
struct card_info *get_ml_card(void);
struct card_info *get_shooting_card(void);
struct fio_file *alloc_fio_file(void);
struct file_info convert_fio_file_info(struct fio_file *p);
struct fio_dirent *FIO_FindFirstEx(const char *dir, struct fio_file *p);
int FIO_FindNextEx(struct fio_dirent *d, struct fio_file *p);
void FIO_FindClose(struct fio_dirent *d);
#endif
