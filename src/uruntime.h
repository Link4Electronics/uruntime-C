#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/mount.h>
#include <limits.h>
#include <linux/limits.h>
#include <signal.h>
#include <ctype.h>

#ifdef __linux__
#  include <linux/capability.h>
#endif

#ifndef URUNTIME_VERSION
#  define URUNTIME_VERSION "0.6.0"
#endif

#define _LINUX_CAPABILITY_VERSION_3 0x20080522
#define URUNTIME_MOUNT_DEFAULT      3
#define URUNTIME_CLEANUP_DEFAULT    1
#define URUNTIME_EXTRACT_DEFAULT    3
#define URUNTIME_UNSHARE_DEFAULT    0
#define REUSE_CHECK_DELAY_DEFAULT   "5s"
#define MAX_EXTRACT_SELF_SIZE       (350ULL * 1024 * 1024)

#ifdef URUNTIME_APPIMAGE
#  define ARG_PFX            "appimage"
#  define ENV_NAME           "APPIMAGE"
#  define SELF_NAME          "AppImage"
#else
#  define ARG_PFX            "runtime"
#  define ENV_NAME           "RUNIMAGE"
#  define SELF_NAME          "RunImage"
#endif

// Dynamic string array
typedef struct {
    char **data;
    size_t len;
    size_t cap;
} strarr_t;

#define strarr_init {NULL, 0, 0}

bool strarr_push(strarr_t *arr, const char *s);
void strarr_free(strarr_t *arr);

// Dynamic byte array
typedef struct {
    unsigned char *data;
    size_t len;
    size_t cap;
} bytearr_t;

#define bytearr_init {NULL, 0, 0}

bool bytearr_push(bytearr_t *arr, unsigned char byte);
bool bytearr_write(bytearr_t *arr, const unsigned char *buf, size_t n);
void bytearr_free(bytearr_t *arr);

// ELF structures (64-bit)
typedef struct {
    unsigned char e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
} Elf64_Shdr;

// Runtime state
typedef struct {
    char  path[PATH_MAX];
    uint64_t size;
    unsigned char *headers;
    size_t headers_len;
    char  envs[8192];
} Runtime;

// Image descriptor
typedef struct {
    char  path[PATH_MAX];
    uint64_t offset;
    bool  is_squash;
    bool  is_dwar;
} Image;

// Embedded tools
typedef struct {
    #ifdef URUNTIME_SQUASHFS
    const unsigned char *squashfuse;
    size_t squashfuse_size;
    const unsigned char *unsquashfs;
    size_t unsquashfs_size;
    #endif
    #if defined(URUNTIME_SQUASHFS) && !defined(URUNTIME_LITE)
    const unsigned char *mksquashfs;
    size_t mksquashfs_size;
    #endif
    #ifdef URUNTIME_DWARFS
    const unsigned char *dwarfs_universal;
    size_t dwarfs_universal_size;
    const unsigned char *dwarfsextract;
    size_t dwarfsextract_size;
    #endif
} Embed;

// util.c
char      *str_dup(const char *s);
char      *basename_c(const char *path);
char      *dirname_c(const char *path);
void      random_string(char *buf, size_t len);
uint64_t  hash_string(const char *data);
uint32_t  fast_hash_file(const char *path, uint64_t offset);
uint64_t  get_file_size(const char *path);
bool      is_suid_exe(const char *path);
bool      find_suid_exe(const char *name, char *buf, size_t bufsize);
bool      is_mount_point(const char *path);
bool      is_mounted(const char *path);
char     *get_env(const char *name);
bool      is_pid_exists(pid_t pid);
bool      wait_pid_exit(pid_t pid, int timeout_secs);
uint64_t  parse_time_string(const char *s);
bool      is_dir_inuse(const char *mount_point);
bool      wait_dir_notuse(const char *mount_point, int timeout_secs, int delay_ms, bool delay_check);
bool      try_setsid(void);

// elf.c
bool      read_elf_headers(const char *path, unsigned char **headers, size_t *len, uint64_t *size);
bool      get_section_data(const unsigned char *headers, size_t headers_len,
                           const char *section_name, char *buf, size_t bufsize);
bool      get_section_offset_size(const unsigned char *headers, size_t headers_len,
                                  const char *section_name, uint64_t *offset, uint64_t *size);
bool      add_section_data(const char *path, const char *section_name,
                           const unsigned char *data, size_t data_len);

// embed.c
void      embed_init(Embed *embed);
void      embed_run_tool(const Embed *embed, const char *name, char *const argv[]);
void      mfd_exec(const char *exec_name, const unsigned char *data, size_t data_size,
                    char *const argv[]);
void      mfd_exec_zst(const char *exec_name, const unsigned char *zst_data, size_t zst_size,
                       char *const argv[]);
bool      decompress_zst(const unsigned char *input, size_t input_size,
                         unsigned char **output, size_t *output_size);

// ns.c
void      restore_capabilities(void);
bool      try_make_mount_private(void);
bool      try_unshare(uid_t uid, gid_t gid, const char *unshare_uid, const char *unshare_gid);
bool      is_in_user_and_mount_namespace(void);
bool      try_setns(pid_t pid);
pid_t     read_mount_pid_file(const char *mount_point, const char *extension);
bool      write_mount_pid_file(const char *mount_point, pid_t pid, bool unshare_succeeded);
pid_t     try_reuse_unshare_mount_point(const char *mount_point);
bool      check_fuse(const char *uruntime, uid_t uid, gid_t gid,
                     bool *unshare_succeeded, bool *is_unshare);
bool      try_unmount(pid_t fuse_pid, const char *mount_point);
bool      wait_mount(pid_t pid, const char *path, int timeout_secs);
bool      create_tmp_dirs(char **dirs, size_t ndirs);
void      remove_tmp_dirs(char **dirs, size_t ndirs, bool unshare_succeeded);

// signal.c
void      signals_handler(pid_t pid, const char *mount_point, bool killpid, bool selfexit);

// main.c (shared data)
void      get_image(const char *path, uint64_t offset, Image *image);
void      mount_image(const Embed *embed, const Image *image, const char *mount_dir,
                      uid_t uid, gid_t gid);
void      extract_image(const Embed *embed, const Image *image, const char *extract_dir,
                        bool is_extract_run, const char *pattern);

#ifdef URUNTIME_DWARFS
const char *get_env_option(const char *option, const char *default_val);
#endif
