#include "uruntime.h"
#include <xxhash.h>

bool strarr_push(strarr_t *arr, const char *s) {
    if (arr->len >= arr->cap) {
        size_t newcap = arr->cap ? arr->cap * 2 : 8;
        char **newdata = realloc(arr->data, newcap * sizeof(char *));
        if (!newdata) return false;
        arr->data = newdata;
        arr->cap = newcap;
    }
    arr->data[arr->len] = strdup(s);
    if (!arr->data[arr->len]) return false;
    arr->len++;
    return true;
}

void strarr_free(strarr_t *arr) {
    for (size_t i = 0; i < arr->len; i++) free(arr->data[i]);
    free(arr->data);
    arr->data = NULL;
    arr->len = arr->cap = 0;
}

bool bytearr_push(bytearr_t *arr, unsigned char byte) {
    if (arr->len >= arr->cap) {
        size_t newcap = arr->cap ? arr->cap * 2 : 4096;
        unsigned char *newdata = realloc(arr->data, newcap);
        if (!newdata) return false;
        arr->data = newdata;
        arr->cap = newcap;
    }
    arr->data[arr->len++] = byte;
    return true;
}

bool bytearr_write(bytearr_t *arr, const unsigned char *buf, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (!bytearr_push(arr, buf[i])) return false;
    return true;
}

void bytearr_free(bytearr_t *arr) {
    free(arr->data);
    arr->data = NULL;
    arr->len = arr->cap = 0;
}

char *str_dup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

char *basename_c(const char *path) {
    if (!path || !*path) return str_dup("");
    const char *p = strrchr(path, '/');
    return str_dup(p ? p + 1 : path);
}

char *dirname_c(const char *path) {
    if (!path || !*path) return str_dup(".");
    const char *p = strrchr(path, '/');
    if (!p) return str_dup(".");
    if (p == path) return str_dup("/");
    char *result = malloc(p - path + 1);
    if (!result) return NULL;
    memcpy(result, path, p - path);
    result[p - path] = '\0';
    return result;
}

void random_string(char *buf, size_t len) {
    const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t rng = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
    for (size_t i = 0; i < len; i++) {
        rng = rng * 48271 % 0x7FFFFFFF;
        buf[i] = charset[rng % (sizeof(charset) - 1)];
    }
    buf[len] = '\0';
}

uint64_t hash_string(const char *data) {
    uint64_t h = 0;
    for (const char *p = data; *p; p++)
        h = h * 131 + (unsigned char)*p;
    return h;
}

uint32_t fast_hash_file(const char *path, uint64_t offset) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    uint64_t file_size = lseek(fd, 0, SEEK_END);
    if (file_size <= offset) { close(fd); return 0; }
    unsigned char buffer[48];
    // First 16 bytes after offset
    if (pread(fd, buffer, 16, offset) != 16) { close(fd); return 0; }
    // Middle 16 bytes
    uint64_t mid = offset + (file_size - offset) / 2;
    if (pread(fd, buffer + 16, 16, mid) != 16) { close(fd); return 0; }
    // Last 16 bytes
    uint64_t end = file_size - 16;
    if (pread(fd, buffer + 32, 16, end) != 16) { close(fd); return 0; }
    close(fd);
    return (uint32_t)XXH3_64bits(buffer, 48);
}

uint64_t get_file_size(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return (uint64_t)st.st_size;
}

bool is_suid_exe(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return false;
    return (st.st_mode & S_ISUID) && (st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH));
}

bool find_suid_exe(const char *name, char *buf, size_t bufsize) {
    const char *path_env = getenv("PATH");
    if (!path_env) return false;
    char *path_dup = str_dup(path_env);
    if (!path_dup) return false;
    bool found = false;
    for (char *dir = strtok(path_dup, ":"); dir; dir = strtok(NULL, ":")) {
        snprintf(buf, bufsize, "%s/%s", dir, name);
        if (access(buf, X_OK) == 0 && is_suid_exe(buf)) {
            found = true;
            break;
        }
    }
    free(path_dup);
    return found;
}

bool is_mount_point(const char *path) {
    struct stat path_st, parent_st;
    if (lstat(path, &path_st) != 0) return false;
    char parent[PATH_MAX];
    char *dir = dirname_c(path);
    if (!dir) return false;
    snprintf(parent, sizeof(parent), "%s", dir);
    free(dir);
    // Handle root specially
    if (strcmp(parent, path) == 0) return true;
    if (stat(parent, &parent_st) != 0) return false;
    return path_st.st_dev != parent_st.st_dev;
}

bool is_mounted(const char *path) {
    if (!is_mount_point(path)) return false;
    // Check if mount is alive by opening the directory
    int fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd >= 0) {
        close(fd);
        return true;
    }
    // Broken mount (ENOTCONN, ESTALE, EIO)
    if (errno == ENOTCONN || errno == ESTALE || errno == EIO) {
        try_unmount(-1, path);
        return false;
    }
    return false;
}

bool is_broken_mount_errno(int e) {
    return e == ENOTCONN || e == ESTALE || e == EIO;
}

char *get_env(const char *name) {
    const char *v = getenv(name);
    return v ? str_dup(v) : NULL;
}

bool is_pid_exists(pid_t pid) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d", pid);
    struct stat st;
    if (stat(path, &st) == 0) return true;
    // Also check with kill 0
    if (kill(pid, 0) == 0) return true;
    return false;
}

bool wait_pid_exit(pid_t pid, int timeout_secs) {
    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);
    while (is_pid_exists(pid)) {
        if (timeout_secs > 0) {
            clock_gettime(CLOCK_MONOTONIC, &now);
            if ((now.tv_sec - start.tv_sec) >= timeout_secs)
                return false;
        }
        usleep(10000); // 10ms
    }
    return true;
}

uint64_t parse_time_string(const char *s) {
    if (!s || !*s) return 1;
    if (strcmp(s, "inf") == 0) return UINT64_MAX;
    char *end;
    unsigned long val = strtoul(s, &end, 10);
    if (end == s) return 1;
    switch (*end) {
        case 's': return val;
        case 'm': return val * 60;
        case 'h': return val * 3600;
        case '\0': return val;
        default: return 1;
    }
}

bool is_dir_inuse(const char *mount_point) {
    DIR *proc = opendir("/proc");
    if (!proc) return false;
    struct dirent *entry;
    while ((entry = readdir(proc)) != NULL) {
        if (entry->d_type == DT_UNKNOWN) {
            struct stat st;
            char buf[64];
            snprintf(buf, sizeof(buf), "/proc/%s", entry->d_name);
            if (lstat(buf, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
        } else if (entry->d_type != DT_DIR) continue;
        char exe_path[PATH_MAX];
        char link_path[PATH_MAX];
        snprintf(exe_path, sizeof(exe_path), "/proc/%s/exe", entry->d_name);
        ssize_t len = readlink(exe_path, link_path, sizeof(link_path) - 1);
        if (len > 0) {
            link_path[len] = '\0';
            if (strncmp(link_path, mount_point, strlen(mount_point)) == 0) {
                closedir(proc);
                return true;
            }
        }
    }
    closedir(proc);
    return false;
}

bool wait_dir_notuse(const char *mount_point, int timeout_secs, int delay_ms, bool delay_check) {
    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);
    int delay = delay_ms > 0 ? delay_ms : 100;
    bool is_timeout = timeout_secs > 0;

    if (delay_check) {
        usleep(delay * 1000);
        for (int i = 0; i < 5; i++) {
            if (is_dir_inuse(mount_point)) break;
            usleep(200000); // 200ms
            if (i == 4) return true;
        }
        return true;
    }

    while (true) {
        if (!is_dir_inuse(mount_point)) return true;
        if (is_timeout) {
            clock_gettime(CLOCK_MONOTONIC, &now);
            if ((now.tv_sec - start.tv_sec) >= timeout_secs)
                return false;
        }
        usleep(delay * 1000);
    }
}

bool try_setsid(void) {
    return setsid() != -1;
}
