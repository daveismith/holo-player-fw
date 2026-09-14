/*
 * `storage`: the LittleFS volume from the console -- usage, a listing, and a sequential
 * throughput test, which is the number that decides whether clips can stream from it.
 */
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_littlefs.h"
#include "esp_timer.h"
#include "cmd_storage.h"

#define BENCH_CHUNK      (16 * 1024)
#define BENCH_DEFAULT_KB 512

static const char *s_mount;
static const char *s_label;

static double mb_per_s(size_t bytes, int64_t us)
{
    return us > 0 ? ((double)bytes / 1048576.0) / ((double)us / 1e6) : 0.0;
}

static int storage_df(void)
{
    size_t total = 0, used = 0;
    esp_err_t err = esp_littlefs_info(s_label, &total, &used);
    if (err != ESP_OK) {
        printf("storage: %s\n", esp_err_to_name(err));
        return 1;
    }
    printf("%s (%s): %u KB used of %u KB, %u KB free\n", s_mount, s_label,
           (unsigned)(used / 1024), (unsigned)(total / 1024), (unsigned)((total - used) / 1024));
    return 0;
}

static int storage_ls(const char *path)
{
    char dir_path[128];
    if (path == NULL) {
        strlcpy(dir_path, s_mount, sizeof(dir_path));
    } else if (path[0] == '/') {
        strlcpy(dir_path, path, sizeof(dir_path));
    } else {
        snprintf(dir_path, sizeof(dir_path), "%s/%s", s_mount, path);
    }
    DIR *dir = opendir(dir_path);
    if (dir == NULL) {
        printf("storage: cannot open %s\n", dir_path);
        return 1;
    }
    struct dirent *de;
    unsigned count = 0;
    while ((de = readdir(dir)) != NULL) {
        char full[sizeof(dir_path) + 1 + sizeof(de->d_name)];
        snprintf(full, sizeof(full), "%s/%s", dir_path, de->d_name);
        struct stat st;
        if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) {
            printf("%10s  %s/\n", "<dir>", de->d_name);
        } else {
            printf("%10ld  %s\n", (long)(stat(full, &st) == 0 ? st.st_size : -1), de->d_name);
        }
        count++;
    }
    closedir(dir);
    if (count == 0) {
        printf("(empty)\n");
    }
    return 0;
}

/* Write a file of `kb` KB in 16 KB chunks, read it back the same way, and delete it. The
 * write figure includes erasing; the read figure is what streaming a clip would see. */
static int storage_bench(int kb)
{
    if (kb <= 0) {
        kb = BENCH_DEFAULT_KB;
    }
    const size_t total = (size_t)kb * 1024;
    uint8_t *buf = heap_caps_malloc(BENCH_CHUNK, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (buf == NULL) {
        printf("storage: no memory for a %d byte buffer\n", BENCH_CHUNK);
        return 1;
    }
    for (size_t i = 0; i < BENCH_CHUNK; i++) {
        buf[i] = (uint8_t)(i * 31 + 7);
    }

    char path[64];
    snprintf(path, sizeof(path), "%s/.bench", s_mount);
    printf("writing %d KB to %s...\n", kb, path);
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        printf("storage: cannot create %s\n", path);
        free(buf);
        return 1;
    }
    int64_t t0 = esp_timer_get_time();
    size_t done = 0;
    while (done < total) {
        size_t n = total - done < BENCH_CHUNK ? total - done : BENCH_CHUNK;
        ssize_t w = write(fd, buf, n);
        if (w != (ssize_t)n) {
            printf("storage: write failed at %u bytes (volume full?)\n", (unsigned)done);
            close(fd);
            unlink(path);
            free(buf);
            return 1;
        }
        done += n;
    }
    fsync(fd);
    close(fd);
    const int64_t write_us = esp_timer_get_time() - t0;

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        printf("storage: cannot reopen %s\n", path);
        unlink(path);
        free(buf);
        return 1;
    }
    bool ok = true;
    t0 = esp_timer_get_time();
    done = 0;
    for (;;) {
        ssize_t r = read(fd, buf, BENCH_CHUNK);
        if (r <= 0) {
            break;
        }
        done += (size_t)r;
    }
    const int64_t read_us = esp_timer_get_time() - t0;
    close(fd);
    /* Checked after the timing, so it costs the figure nothing: the last chunk read must
     * still hold the pattern. */
    for (size_t i = 0; i < 64 && ok; i++) {
        ok = buf[i] == (uint8_t)(i * 31 + 7);
    }
    unlink(path);
    free(buf);

    printf("write: %u KB in %lld ms, %.3f MB/s (includes erase)\n", (unsigned)(total / 1024),
           (long long)(write_us / 1000), mb_per_s(total, write_us));
    printf("read:  %u KB in %lld ms, %.3f MB/s%s\n", (unsigned)(done / 1024),
           (long long)(read_us / 1000), mb_per_s(done, read_us),
           done != total ? "  *** SHORT READ ***" : ok ? "" : "  *** DATA MISMATCH ***");
    return done == total && ok ? 0 : 1;
}

static int storage_cmd(int argc, char **argv)
{
    if (argc < 2 || strcmp(argv[1], "df") == 0) {
        return storage_df();
    }
    if (strcmp(argv[1], "ls") == 0) {
        return storage_ls(argc > 2 ? argv[2] : NULL);
    }
    if (strcmp(argv[1], "bench") == 0) {
        return storage_bench(argc > 2 ? atoi(argv[2]) : 0);
    }
    printf("usage: storage [df | ls [path] | bench [kb]]\n");
    return 1;
}

void register_storage(const char *mount_path, const char *partition_label)
{
    s_mount = mount_path;
    s_label = partition_label;
    const esp_console_cmd_t cmd = {
        .command = "storage",
        .help = "The LittleFS volume: df (usage), ls [path], bench [kb] (sequential write/read MB/s, default 512 KB)",
        .hint = "[df | ls [path] | bench [kb]]",
        .func = storage_cmd,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
