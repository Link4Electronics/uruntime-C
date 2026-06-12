#include "uruntime.h"
#include <libgen.h>

static void print_usage(const char *self_exe_name) {
    printf("%s v" URUNTIME_VERSION "\n"
           "   Repository: " URUNTIME_REPOSITORY "\n"
           "\n"
           "   Runtime options:\n"
           "    --%s-extract [PATTERN]          Extract content from embedded filesystem image\n"
           "    --%s-extract-and-run [ARGS]    Run the %s after extraction without using FUSE\n"
           "    --%s-offset                    Print byte offset to start of embedded filesystem image\n"
           "    --%s-portable-home             Create a portable home folder to use as $HOME\n"
           "    --%s-portable-share            Create a portable share folder to use as $XDG_DATA_HOME\n"
           "    --%s-portable-config           Create a portable config folder to use as $XDG_CONFIG_HOME\n"
           "    --%s-portable-cache            Create a portable cache folder to use as $XDG_CACHE_HOME\n"
           "    --%s-help                      Print this help\n"
           "    --%s-unshare                   Try to use unshare user and mount namespaces\n"
            "    --%s-version                   Print version of %s\n"
           "    --%s-signature                 Print digital signature embedded in %s\n"
           "    --%s-addsign    'SIGN|/file'   Add digital signature to %s\n"
           "    --%s-updateinfo[rmation]       Print update info embedded in %s\n"
           "    --%s-addupdinfo 'INFO|/file'   Add update info to %s\n"
           "    --%s-envs                      Print environment variables embedded in %s\n"
           "    --%s-addenvs    'ENVS|/file'   Add environment variables to %s\n"
           "    --%s-mount                     Mount embedded filesystem image and print\n"
           "                                     mount point and wait for kill with Ctrl-C\n"
           "\n"
           "    Embedded tools options:\n"
#ifdef URUNTIME_SQUASHFS
           "      --%s-squashfuse    [ARGS]       Launch squashfuse\n"
           "      --%s-unsquashfs    [ARGS]       Launch unsquashfs\n"
           "      --%s-sqfscat       [ARGS]       Launch sqfscat\n"
#endif
#if defined(URUNTIME_SQUASHFS) && !defined(URUNTIME_LITE)
           "      --%s-mksquashfs    [ARGS]       Launch mksquashfs\n"
           "      --%s-sqfstar       [ARGS]       Launch sqfstar\n"
#endif
#ifdef URUNTIME_DWARFS
           "      --%s-dwarfs        [ARGS]       Launch dwarfs\n"
#if !defined(URUNTIME_LITE)
           "      --%s-dwarfsck      [ARGS]       Launch dwarfsck\n"
           "      --%s-mkdwarfs      [ARGS]       Launch mkdwarfs\n"
#endif
           "      --%s-dwarfsextract [ARGS]       Launch dwarfsextract\n"
#endif
           "\n"
           "    Portable home and config:\n"
           "      ...\n"
           "\n"
           "    Environment variables:\n"
           "      URUNTIME                       Path to uruntime\n"
           "      URUNTIME_DIR                   Path to uruntime directory\n"
           "      %s_UNSHARE=1             Try to use unshare user and mount namespaces\n"
           "      %s_UNSHARE_ROOT=1        Map to root (UID 0, GID 0) in user namespace\n"
           "      %s_EXTRACT_AND_RUN=1     Run the %s after extraction without FUSE\n"
           "      NO_CLEANUP=1             Do not clear unpacking directory\n"
           "      NO_UNMOUNT=1             Do not unmount mount directory\n"
           "      TMPDIR=/path             Custom path for mount/extract\n"
           "      %s_TARGET_DIR=/path      Exact path for mount/extract\n"
           "      REUSE_CHECK_DELAY=5s     Delay between checks\n"
           "      FUSERMOUNT_PROG=/path    Custom fusermount path\n"
           "      ENABLE_FUSE_DEBUG=1      Enable FUSE debug\n"
           "      TARGET_%s=/path      Operate on target %s\n"
            "      NO_MEMFDEXEC=1           Do not use memfd-exec\n",
            self_exe_name,
           ARG_PFX, ARG_PFX, SELF_NAME,
           ARG_PFX, ARG_PFX, ARG_PFX, ARG_PFX, ARG_PFX,
            ARG_PFX, ARG_PFX, ARG_PFX, SELF_NAME,
            ARG_PFX, SELF_NAME, ARG_PFX, SELF_NAME,
            ARG_PFX, SELF_NAME, ARG_PFX, SELF_NAME,
            ARG_PFX, SELF_NAME, ARG_PFX, SELF_NAME,
            ARG_PFX,
#ifdef URUNTIME_SQUASHFS
           ARG_PFX, ARG_PFX, ARG_PFX,
#endif
#if defined(URUNTIME_SQUASHFS) && !defined(URUNTIME_LITE)
           ARG_PFX, ARG_PFX,
#endif
#ifdef URUNTIME_DWARFS
           ARG_PFX,
#if !defined(URUNTIME_LITE)
           ARG_PFX, ARG_PFX,
#endif
           ARG_PFX,
#endif
           ENV_NAME, ENV_NAME, ENV_NAME, SELF_NAME,
           ENV_NAME, ENV_NAME, SELF_NAME
    );
}

static void try_read_dotenv(const char *dotenv_path, const char *embedded_envs) {
    if (embedded_envs && *embedded_envs) {
        // Parse embedded envs using putenv for each line
        char *buf = str_dup(embedded_envs);
        if (buf) {
            char *line = strtok(buf, "\n");
            while (line) {
                // Skip whitespace
                while (*line == ' ' || *line == '\t') line++;
                if (*line && *line != '#') {
                    if (strncmp(line, "unset ", 6) == 0) {
                        char *var = line + 6;
                        while (*var == ' ') var++;
                        char *end = var + strlen(var) - 1;
                        while (end > var && (*end == ' ' || *end == '\t')) end--;
                        end[1] = '\0';
                        unsetenv(var);
                    } else {
                        putenv(str_dup(line));
                    }
                }
                line = strtok(NULL, "\n");
            }
            free(buf);
        }
    }

    if (dotenv_path && access(dotenv_path, R_OK) == 0) {
        FILE *f = fopen(dotenv_path, "r");
        if (f) {
            fprintf(stderr, "Read env file: %s\n", dotenv_path);
            char line[4096];
            while (fgets(line, sizeof(line), f)) {
                char *p = line;
                while (*p == ' ' || *p == '\t') p++;
                if (*p == '#' || *p == '\n') continue;
                char *nl = strchr(p, '\n');
                if (nl) *nl = '\0';
                if (strncmp(p, "unset ", 6) == 0) {
                    char *var = p + 6;
                    while (*var == ' ') var++;
                    unsetenv(var);
                } else {
                    putenv(str_dup(p));
                }
            }
            fclose(f);
        }
    }
}

static void try_set_portable_dir(const char *dir, const char *env_var, const char *default_path) {
    struct stat st;
    if (stat(dir, &st) != 0 || !S_ISDIR(st.st_mode)) return;

    char real_env_var[PATH_MAX];
    snprintf(real_env_var, sizeof(real_env_var), "REAL_%s", env_var);

    if (!getenv(real_env_var)) {
        const char *current = getenv(env_var);
        if (current) {
            setenv(real_env_var, current, 1);
        } else if (default_path) {
            const char *home = getenv("HOME");
            if (home) {
                char default_dir[PATH_MAX];
                snprintf(default_dir, sizeof(default_dir), "%s/%s", home, default_path);
                setenv(real_env_var, default_dir, 1);
            }
        }
    }

    fprintf(stderr, "Setting $%s to %s\n", env_var, dir);
    setenv(env_var, dir, 1);
}

void get_image(const char *path, uint64_t offset, Image *image) {
    memset(image, 0, sizeof(*image));
    strncpy(image->path, path, sizeof(image->path) - 1);
    image->offset = offset;

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "Failed to open image: %s\n", path);
        exit(1);
    }

    // Seek to offset and read magic
    unsigned char magic[4];
    if (pread(fd, magic, 4, offset) != 4) {
        close(fd);
        fprintf(stderr, "Failed to read image magic at offset %lu\n", (unsigned long)offset);
        exit(1);
    }
    close(fd);

    if (memcmp(magic, "DWAR", 4) == 0)
        image->is_dwar = true;
    else if (memcmp(magic, "hsqs", 4) == 0)
        image->is_squash = true;

    if (!image->is_squash && !image->is_dwar) {
        fprintf(stderr, "SquashFS or DwarFS image not found at offset %lu!\n", (unsigned long)offset);
        exit(1);
    }
}

#ifdef URUNTIME_DWARFS
const char *get_env_option(const char *option, const char *default_val) {
    const char *env = getenv(option);
    if (env && *env) {
        // Take first value if comma-separated
        const char *comma = strchr(env, ',');
        if (comma) {
            static char buf[256];
            size_t len = comma - env;
            if (len > sizeof(buf) - 1) len = sizeof(buf) - 1;
            memcpy(buf, env, len);
            buf[len] = '\0';
            return buf;
        }
        return env;
    }
    return default_val;
}

static char *get_dwfs_cachesize(void) {
    static char buf[32];
    const char *env = get_env_option("DWARFS_CACHESIZE", NULL);
    if (env) {
        strncpy(buf, env, sizeof(buf) - 1);
        return buf;
    }

    // Auto-detect based on available memory
    long pages = sysconf(_SC_AVPHYS_PAGES);
    long page_size = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && page_size > 0) {
        double available_mb = (double)pages * (double)page_size / (1024.0 * 1024.0) / 1.3;
        const int sizes[] = {1536, 1024, 896, 768, 640, 512, 384, 256, 128, 64};
        for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
            if (available_mb > sizes[i]) {
                snprintf(buf, sizeof(buf), "%dM", sizes[i]);
                return buf;
            }
        }
        snprintf(buf, sizeof(buf), "32M");
    } else {
        strncpy(buf, "1024M", sizeof(buf) - 1);
    }
    return buf;
}

static char *get_dwfs_workers(const char *cachesize, int cpus) {
    static char buf[16];
    const char *env = get_env_option("DWARFS_WORKERS", NULL);
    if (env) {
        strncpy(buf, env, sizeof(buf) - 1);
        return buf;
    }

    if (strcmp(cachesize, "1536M") == 0 || strcmp(cachesize, "1024M") == 0)
        snprintf(buf, sizeof(buf), "%d", cpus);
    else if (strcmp(cachesize, "896M") == 0)
        snprintf(buf, sizeof(buf), "2");
    else
        snprintf(buf, sizeof(buf), "1");

    return buf;
}
#endif

void mount_image(const Embed *embed, const Image *image, const char *mount_dir,
                  uid_t uid, gid_t gid) {
    if (is_mounted(mount_dir)) return;

    if (image->is_dwar) {
#ifdef URUNTIME_DWARFS
        const char *cachesize = get_dwfs_cachesize();
        int cpus = (int)sysconf(_SC_NPROCESSORS_ONLN);
        if (cpus < 1) cpus = 1;
        const char *workers = get_dwfs_workers(cachesize, cpus);
        const char *blocksize = get_env_option("DWARFS_BLOCKSIZE", "512K");
        const char *readahead = get_env_option("DWARFS_READAHEAD", "32M");

        // Build args
        strarr_t args = strarr_init;
        strarr_push(&args, "dwarfs");
        strarr_push(&args, image->path);
        strarr_push(&args, mount_dir);
        strarr_push(&args, "-f");

        char uid_opt[64], gid_opt[64], offset_opt[128];
        snprintf(uid_opt, sizeof(uid_opt), "uid=%u", uid);
        snprintf(gid_opt, sizeof(gid_opt), "gid=%u", gid);
        snprintf(offset_opt, sizeof(offset_opt), "offset=%lu,cachesize=%s,workers=%s",
                 (unsigned long)image->offset, cachesize, workers);

        strarr_push(&args, "-o");
        strarr_push(&args, uid_opt);
        strarr_push(&args, "-o");
        strarr_push(&args, offset_opt);
        strarr_push(&args, "-o");
        strarr_push(&args, "ro,nodev,tidy_strategy=time,seq_detector=1,cache_files");

        char blocksize_opt[64], readahead_opt[64];
        snprintf(blocksize_opt, sizeof(blocksize_opt), "blocksize=%s", blocksize);
        snprintf(readahead_opt, sizeof(readahead_opt), "readahead=%s", readahead);
        strarr_push(&args, "-o");
        strarr_push(&args, blocksize_opt);
        strarr_push(&args, "-o");
        strarr_push(&args, readahead_opt);

        if (strcmp(cachesize, "1536M") == 0 || strcmp(cachesize, "1024M") == 0) {
            strarr_push(&args, "-o");
            strarr_push(&args, "clone_fd,tidy_interval=2s,tidy_max_age=10s");
        } else {
            strarr_push(&args, "-o");
            strarr_push(&args, "tidy_interval=500ms,tidy_max_age=1s");
        }

        if (getenv("ENABLE_FUSE_DEBUG") && strcmp(getenv("ENABLE_FUSE_DEBUG"), "1") == 0) {
            strarr_push(&args, "-o");
            strarr_push(&args, "debuglevel=debug");
        } else {
            strarr_push(&args, "-o");
            strarr_push(&args, "debuglevel=error");
        }

        if (getenv("DWARFS_PRELOAD_ALL") && strcmp(getenv("DWARFS_PRELOAD_ALL"), "1") == 0) {
            strarr_push(&args, "-o");
            strarr_push(&args, "preload_all");
        } else {
            strarr_push(&args, "-o");
            strarr_push(&args, "preload_category=hotness");
        }

        const char *analysis_file = getenv("DWARFS_ANALYSIS_FILE");
        if (analysis_file && *analysis_file) {
            char af_opt[PATH_MAX + 32];
            snprintf(af_opt, sizeof(af_opt), "analysis_file=%s", analysis_file);
            strarr_push(&args, "-o");
            strarr_push(&args, af_opt);
        }

        const char *use_mmap = getenv("DWARFS_USE_MMAP");
        if (use_mmap && strcmp(use_mmap, "1") == 0) {
            strarr_push(&args, "-o");
            strarr_push(&args, "block_allocator=mmap");
        } else {
            strarr_push(&args, "-o");
            strarr_push(&args, "block_allocator=malloc");
        }

        // Build argv array
        char **argv = malloc((args.len + 1) * sizeof(char *));
        for (size_t i = 0; i < args.len; i++)
            argv[i] = args.data[i];
        argv[args.len] = NULL;

        mfd_exec_zst("dwarfs", embed->dwarfs_universal, embed->dwarfs_universal_size, argv);
        free(argv);
        strarr_free(&args);
#endif
    } else {
#ifdef URUNTIME_SQUASHFS
        strarr_t args = strarr_init;
        strarr_push(&args, "squashfuse");
        strarr_push(&args, (char *)image->path);
        strarr_push(&args, (char *)mount_dir);
        strarr_push(&args, "-f");

        char uid_opt[64], gid_opt[64], offset_opt[64];
        snprintf(uid_opt, sizeof(uid_opt), "uid=%u", uid);
        snprintf(gid_opt, sizeof(gid_opt), "gid=%u", gid);
        snprintf(offset_opt, sizeof(offset_opt), "offset=%lu", (unsigned long)image->offset);

        strarr_push(&args, "-o");
        strarr_push(&args, "ro,nodev");
        strarr_push(&args, "-o");
        strarr_push(&args, uid_opt);
        strarr_push(&args, "-o");
        strarr_push(&args, gid_opt);
        strarr_push(&args, "-o");
        strarr_push(&args, offset_opt);

        if (getenv("ENABLE_FUSE_DEBUG") && strcmp(getenv("ENABLE_FUSE_DEBUG"), "1") == 0) {
            strarr_push(&args, "-o");
            strarr_push(&args, "debug");
        }

        char **argv = malloc((args.len + 1) * sizeof(char *));
        for (size_t i = 0; i < args.len; i++)
            argv[i] = args.data[i];
        argv[args.len] = NULL;

        mfd_exec_zst("squashfuse", embed->squashfuse, embed->squashfuse_size, argv);
        free(argv);
        strarr_free(&args);
#endif
    }
}

void extract_image(const Embed *embed, const Image *image, const char *extract_dir,
                    bool is_extract_run, const char *pattern) {
    // Check if already extracted
    if (is_extract_run) {
        DIR *d = opendir(extract_dir);
        if (d) {
            struct dirent *entry;
            while ((entry = readdir(d)) != NULL) {
                if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
                    closedir(d);
                    return;
                }
            }
            closedir(d);
        }
    }

    char actual_dir[PATH_MAX];
#ifdef URUNTIME_APPIMAGE
    char applink_dir[PATH_MAX];
    snprintf(applink_dir, sizeof(applink_dir), "%s/squashfs-root", extract_dir);
    if (!is_extract_run)
        snprintf(actual_dir, sizeof(actual_dir), "%s/AppDir", extract_dir);
    else
        snprintf(actual_dir, sizeof(actual_dir), "%s", extract_dir);
#else
    if (!is_extract_run)
        snprintf(actual_dir, sizeof(actual_dir), "%s/RunDir", extract_dir);
    else
        snprintf(actual_dir, sizeof(actual_dir), "%s", extract_dir);
#endif

    if (mkdir(actual_dir, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "Failed to create extract dir: %s: %s\n", actual_dir, strerror(errno));
        exit(1);
    }

#ifdef URUNTIME_APPIMAGE
    if (!is_extract_run) {
        unlink(applink_dir);
        if (symlink(actual_dir, applink_dir) != 0)
            fprintf(stderr, "Warning: failed to create squashfs-root symlink: %s\n", strerror(errno));
    }
#endif

    if (image->is_dwar) {
#ifdef URUNTIME_DWARFS
        const char *cachesize = get_dwfs_cachesize();
        int cpus = (int)sysconf(_SC_NPROCESSORS_ONLN);
        if (cpus < 1) cpus = 1;

        strarr_t args = strarr_init;
        strarr_push(&args, "dwarfsextract");
        strarr_push(&args, "--input");
        strarr_push(&args, image->path);
        strarr_push(&args, "--log-level=error");

        char cache_opt[64];
        snprintf(cache_opt, sizeof(cache_opt), "--cache-size=%s", cachesize);
        strarr_push(&args, cache_opt);

        char offset_opt[64];
        snprintf(offset_opt, sizeof(offset_opt), "--image-offset=%lu", (unsigned long)image->offset);
        strarr_push(&args, offset_opt);

        char workers_opt[64];
        snprintf(workers_opt, sizeof(workers_opt), "--num-workers=%d", cpus);
        strarr_push(&args, workers_opt);

        strarr_push(&args, "--output");
        strarr_push(&args, actual_dir);
        strarr_push(&args, "--stdout-progress");

        if (pattern) {
            strarr_push(&args, "--pattern");
            strarr_push(&args, pattern);
        }

        char **argv = malloc((args.len + 1) * sizeof(char *));
        for (size_t i = 0; i < args.len; i++)
            argv[i] = args.data[i];
        argv[args.len] = NULL;

        if (embed->dwarfsextract && embed->dwarfsextract_size)
            mfd_exec_zst("dwarfsextract", embed->dwarfsextract, embed->dwarfsextract_size, argv);
        else
            mfd_exec_zst("dwarfsextract", embed->dwarfs_universal, embed->dwarfs_universal_size, argv);
        free(argv);
        strarr_free(&args);
#endif
    } else {
#ifdef URUNTIME_SQUASHFS
        strarr_t args = strarr_init;
        strarr_push(&args, "unsquashfs");
        strarr_push(&args, "-f");
        strarr_push(&args, "-d");
        strarr_push(&args, actual_dir);

        char offset_opt[64];
        snprintf(offset_opt, sizeof(offset_opt), "-o");
        strarr_push(&args, offset_opt);
        char offset_val[64];
        snprintf(offset_val, sizeof(offset_val), "%lu", (unsigned long)image->offset);
        strarr_push(&args, offset_val);

        strarr_push(&args, image->path);

        if (pattern) strarr_push(&args, pattern);

        char **argv = malloc((args.len + 1) * sizeof(char *));
        for (size_t i = 0; i < args.len; i++)
            argv[i] = args.data[i];
        argv[args.len] = NULL;

        mfd_exec_zst("unsquashfs", embed->unsquashfs, embed->unsquashfs_size, argv);
        free(argv);
        strarr_free(&args);
#endif
    }
}

int main(int argc, char *argv[]) {
    Embed embed;
    embed_init(&embed);

    // Get program name (argv[0] basename)
    char *arg0 = argv[0];
    char *arg0_name = basename_c(arg0);

    if (!arg0_name) { fprintf(stderr, "Failed to get program name\n"); return 1; }

    // Check if invoked as an embedded tool via symlink/hardlink
    const char *tool_names[] = {
#ifdef URUNTIME_SQUASHFS
        "squashfuse", "unsquashfs", "sqfscat",
#endif
#if defined(URUNTIME_SQUASHFS) && !defined(URUNTIME_LITE)
        "mksquashfs", "sqfstar",
#endif
#ifdef URUNTIME_DWARFS
        "dwarfs", "dwarfsck", "mkdwarfs", "dwarfsextract",
#endif
        "fusermount", "fusermount3",
        NULL
    };

    for (int i = 0; tool_names[i]; i++) {
        if (strcmp(arg0_name, tool_names[i]) == 0) {
            // Handle fusermount passthrough specially
            if (strcmp(arg0_name, "fusermount") == 0 || strcmp(arg0_name, "fusermount3") == 0) {
                bool umount = false;
                char mount_point[PATH_MAX] = "";
                for (int j = 1; j < argc; j++) {
                    if (strcmp(argv[j], "-u") == 0 || strcmp(argv[j], "--unmount") == 0)
                        umount = true;
                    else if (argv[j][0] != '-')
                        strncpy(mount_point, argv[j], sizeof(mount_point) - 1);
                }

                // Filter out /tmp/.path* from PATH
                const char *current_path = getenv("PATH");
                if (current_path) {
                    char new_path[PATH_MAX * 4] = "";
                    char *path_dup = str_dup(current_path);
                    if (path_dup) {
                        char *dir = strtok(path_dup, ":");
                        while (dir) {
                            if (strncmp(dir, "/tmp/.path", 10) != 0) {
                                if (new_path[0]) strcat(new_path, ":");
                                strcat(new_path, dir);
                            }
                            dir = strtok(NULL, ":");
                        }
                        free(path_dup);
                    }
                    setenv("PATH", new_path, 1);
                }

                if (umount && mount_point[0]) {
                    if (!try_unmount(-1, mount_point)) return 1;
                    return 0;
                }

                execvp(arg0_name, argv);
                fprintf(stderr, "Failed to execute %s: %s\n", arg0_name, strerror(errno));
                return 1;
            }

            embed_run_tool(&embed, arg0_name, argv);
            return 0;
        }
    }

    // Build exec_args (remove argv[0])
    int exec_argc = argc - 1;
    char **exec_args = exec_argc > 0 ? argv + 1 : NULL;
    char *arg1 = exec_args ? exec_args[0] : NULL;

    // Flags that don't need runtime context
    if (arg1) {
        if (strcmp(arg1, "--version") == 0 ||
            strcmp(arg1, "--uruntime-version") == 0) {
            printf("v" URUNTIME_VERSION "\n");
            return 0;
        }
        char flag[256];
        snprintf(flag, sizeof(flag), "--%s-version", ARG_PFX);
        if (strcmp(arg1, flag) == 0) {
            printf("v" URUNTIME_VERSION "\n");
            return 0;
        }
        if (strcmp(arg1, "--help") == 0 ||
            strcmp(arg1, "--uruntime-help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        snprintf(flag, sizeof(flag), "--%s-help", ARG_PFX);
        if (strcmp(arg1, flag) == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }

    // Get self path
    char self_exe[PATH_MAX];
    ssize_t self_len = readlink("/proc/self/exe", self_exe, sizeof(self_exe) - 1);
    if (self_len < 0) {
        fprintf(stderr, "Failed to get self exe path\n");
        return 1;
    }
    self_exe[self_len] = '\0';

    // Check TARGET_* env var
    char target_env_name[64];
    snprintf(target_env_name, sizeof(target_env_name), "TARGET_%s", ENV_NAME);
    const char *target_env = getenv(target_env_name);
    char *target_image = NULL;
    if (target_env && *target_env && access(target_env, R_OK) == 0)
        target_image = str_dup(target_env);

    const char *self_exe_str = target_image ? target_image : self_exe;

    // Read runtime headers
    unsigned char *runtime_headers = NULL;
    size_t runtime_headers_len = 0;
    uint64_t runtime_size = 0;
    if (!read_elf_headers(self_exe_str, &runtime_headers, &runtime_headers_len, &runtime_size)) {
        fprintf(stderr, "Failed to read ELF headers from %s\n", self_exe_str);
        free(target_image);
        return 1;
    }

    // Get uruntime dir
    char *uruntime_dir = dirname_c(self_exe);
    char *self_exe_dir = dirname_c(self_exe_str);
    char *self_exe_name = basename_c(self_exe_str);

    // Portable directories
    char portable_home[PATH_MAX], portable_share[PATH_MAX],
         portable_config[PATH_MAX], portable_cache[PATH_MAX];
    snprintf(portable_home, sizeof(portable_home), "%s/%s.home", self_exe_dir, self_exe_name);
    snprintf(portable_share, sizeof(portable_share), "%s/%s.share", self_exe_dir, self_exe_name);
    snprintf(portable_config, sizeof(portable_config), "%s/%s.config", self_exe_dir, self_exe_name);
    snprintf(portable_cache, sizeof(portable_cache), "%s/%s.cache", self_exe_dir, self_exe_name);

    setenv("URUNTIME", self_exe, 1);
    setenv("URUNTIME_DIR", uruntime_dir, 1);

    // Read embedded envs
    char embedded_envs[8192] = "";
    get_section_data(runtime_headers, runtime_headers_len, ".envs",
                     embedded_envs, sizeof(embedded_envs));

    // Dotenv file
    char self_exe_dotenv[PATH_MAX];
    snprintf(self_exe_dotenv, sizeof(self_exe_dotenv), "%s/%s.env", self_exe_dir, self_exe_name);
    try_read_dotenv(self_exe_dotenv, embedded_envs);

    // Parse default options from compile-time constants
    bool is_mount_only = false;
    bool is_extract_run = false;
    bool is_noclenup = false;  // Will be set based on options
    bool is_unshare = false;   // Will be set based on options

    // Portable dir creation flags
    if (arg1) {
        char flag_home[256], flag_share[256], flag_config[256], flag_cache[256];
        snprintf(flag_home, sizeof(flag_home), "--%s-portable-home", ARG_PFX);
        snprintf(flag_share, sizeof(flag_share), "--%s-portable-share", ARG_PFX);
        snprintf(flag_config, sizeof(flag_config), "--%s-portable-config", ARG_PFX);
        snprintf(flag_cache, sizeof(flag_cache), "--%s-portable-cache", ARG_PFX);

        if (strcmp(arg1, flag_home) == 0) {
            mkdir(portable_home, 0755);
            printf("Portable home directory created: %s\n", portable_home);
            free(runtime_headers); free(target_image); free(uruntime_dir);
            free(self_exe_dir); free(self_exe_name);
            return 0;
        }
        if (strcmp(arg1, flag_share) == 0) {
            mkdir(portable_share, 0755);
            printf("Portable share directory created: %s\n", portable_share);
            free(runtime_headers); free(target_image); free(uruntime_dir);
            free(self_exe_dir); free(self_exe_name);
            return 0;
        }
        if (strcmp(arg1, flag_config) == 0) {
            mkdir(portable_config, 0755);
            printf("Portable config directory created: %s\n", portable_config);
            free(runtime_headers); free(target_image); free(uruntime_dir);
            free(self_exe_dir); free(self_exe_name);
            return 0;
        }
        if (strcmp(arg1, flag_cache) == 0) {
            mkdir(portable_cache, 0755);
            printf("Portable cache directory created: %s\n", portable_cache);
            free(runtime_headers); free(target_image); free(uruntime_dir);
            free(self_exe_dir); free(self_exe_name);
            return 0;
        }

        // --offset
        char offset_flag[256];
        snprintf(offset_flag, sizeof(offset_flag), "--%s-offset", ARG_PFX);
        if (strcmp(arg1, offset_flag) == 0) {
            printf("%lu\n", (unsigned long)runtime_size);
            free(runtime_headers); free(target_image); free(uruntime_dir);
            free(self_exe_dir); free(self_exe_name);
            return 0;
        }

        // --updateinfo / --updateinformation
        char updinfo_flag[256], updinfo_flag2[256];
        snprintf(updinfo_flag, sizeof(updinfo_flag), "--%s-updateinfo", ARG_PFX);
        snprintf(updinfo_flag2, sizeof(updinfo_flag2), "--%s-updateinformation", ARG_PFX);
        if (strcmp(arg1, updinfo_flag) == 0 || strcmp(arg1, updinfo_flag2) == 0) {
            char info[4096] = "";
            if (get_section_data(runtime_headers, runtime_headers_len, ".upd_info", info, sizeof(info)))
                printf("%s\n", info);
            else
                fprintf(stderr, "Failed to get update info\n");
            free(runtime_headers); free(target_image); free(uruntime_dir);
            free(self_exe_dir); free(self_exe_name);
            return 0;
        }

        // --addupdinfo
        char addupdinfo_flag[256];
        snprintf(addupdinfo_flag, sizeof(addupdinfo_flag), "--%s-addupdinfo", ARG_PFX);
        if (strcmp(arg1, addupdinfo_flag) == 0 && exec_argc > 1) {
            const unsigned char *data = (const unsigned char *)exec_args[1];
            size_t len = strlen(exec_args[1]);
            if (access(exec_args[1], R_OK) == 0) {
                // Read from file
                FILE *f = fopen(exec_args[1], "rb");
                if (f) {
                    data = malloc(65536);
                    len = fread((void *)data, 1, 65536, f);
                    fclose(f);
                }
            }
            if (!add_section_data(self_exe_str, ".upd_info", data, len))
                fprintf(stderr, "Failed to add update info\n");
            if (data != (const unsigned char *)exec_args[1])
                free((void *)data);
            free(runtime_headers); free(target_image); free(uruntime_dir);
            free(self_exe_dir); free(self_exe_name);
            return 0;
        }

        // --signature
        char sig_flag[256];
        snprintf(sig_flag, sizeof(sig_flag), "--%s-signature", ARG_PFX);
        if (strcmp(arg1, sig_flag) == 0) {
            char sig[4096] = "";
            if (get_section_data(runtime_headers, runtime_headers_len, ".sha256_sig", sig, sizeof(sig)))
                printf("%s\n", sig);
            else
                fprintf(stderr, "Failed to get signature\n");
            free(runtime_headers); free(target_image); free(uruntime_dir);
            free(self_exe_dir); free(self_exe_name);
            return 0;
        }

        // --addsign
        char addsign_flag[256];
        snprintf(addsign_flag, sizeof(addsign_flag), "--%s-addsign", ARG_PFX);
        if (strcmp(arg1, addsign_flag) == 0 && exec_argc > 1) {
            const unsigned char *data = (const unsigned char *)exec_args[1];
            size_t len = strlen(exec_args[1]);
            if (access(exec_args[1], R_OK) == 0) {
                FILE *f = fopen(exec_args[1], "rb");
                if (f) {
                    data = malloc(65536);
                    len = fread((void *)data, 1, 65536, f);
                    fclose(f);
                }
            }
            if (!add_section_data(self_exe_str, ".sha256_sig", data, len))
                fprintf(stderr, "Failed to add signature\n");
            if (data != (const unsigned char *)exec_args[1])
                free((void *)data);
            free(runtime_headers); free(target_image); free(uruntime_dir);
            free(self_exe_dir); free(self_exe_name);
            return 0;
        }

        // --envs
        char envs_flag[256];
        snprintf(envs_flag, sizeof(envs_flag), "--%s-envs", ARG_PFX);
        if (strcmp(arg1, envs_flag) == 0) {
            printf("%s\n", embedded_envs);
            free(runtime_headers); free(target_image); free(uruntime_dir);
            free(self_exe_dir); free(self_exe_name);
            return 0;
        }

        // --addenvs
        char addenvs_flag[256];
        snprintf(addenvs_flag, sizeof(addenvs_flag), "--%s-addenvs", ARG_PFX);
        if (strcmp(arg1, addenvs_flag) == 0 && exec_argc > 1) {
            const unsigned char *data = (const unsigned char *)exec_args[1];
            size_t len = strlen(exec_args[1]);
            if (access(exec_args[1], R_OK) == 0) {
                FILE *f = fopen(exec_args[1], "rb");
                if (f) {
                    data = malloc(65536);
                    len = fread((void *)data, 1, 65536, f);
                    fclose(f);
                }
            }
            if (!add_section_data(self_exe_str, ".envs", data, len))
                fprintf(stderr, "Failed to add envs\n");
            if (data != (const unsigned char *)exec_args[1])
                free((void *)data);
            free(runtime_headers); free(target_image); free(uruntime_dir);
            free(self_exe_dir); free(self_exe_name);
            return 0;
        }

        // --extract-and-run
        char extract_run_flag[256];
        snprintf(extract_run_flag, sizeof(extract_run_flag), "--%s-extract-and-run", ARG_PFX);
        if (strcmp(arg1, extract_run_flag) == 0) {
            exec_args++; exec_argc--;
            is_extract_run = true;
        }

        // --unshare
        char unshare_flag[256];
        snprintf(unshare_flag, sizeof(unshare_flag), "--%s-unshare", ARG_PFX);
        if (strcmp(arg1, unshare_flag) == 0) {
            exec_args++; exec_argc--;
            is_unshare = true;
        }
    }

    // Find the image embedded in the binary
    Image image;
    get_image(self_exe_str, runtime_size, &image);

    // --extract and --mount handling
    if (arg1) {
        char extract_flag[256], mount_flag[256];
        snprintf(extract_flag, sizeof(extract_flag), "--%s-extract", ARG_PFX);
        snprintf(mount_flag, sizeof(mount_flag), "--%s-mount", ARG_PFX);

        if (strcmp(arg1, extract_flag) == 0) {
            char *pattern = exec_argc > 1 ? exec_args[1] : NULL;
            extract_image(&embed, &image, ".", false, pattern);
            free(runtime_headers); free(target_image); free(uruntime_dir);
            free(self_exe_dir); free(self_exe_name);
            return 0;
        }
        if (strcmp(arg1, mount_flag) == 0) {
            is_mount_only = true;
        }

        // Check embedded tool flags
#ifdef URUNTIME_SQUASHFS
        char sf_flag[256], unsf_flag[256], scat_flag[256];
        snprintf(sf_flag, sizeof(sf_flag), "--%s-squashfuse", ARG_PFX);
        snprintf(unsf_flag, sizeof(unsf_flag), "--%s-unsquashfs", ARG_PFX);
        snprintf(scat_flag, sizeof(scat_flag), "--%s-sqfscat", ARG_PFX);
        if (strcmp(arg1, sf_flag) == 0) {
            embed_run_tool(&embed, "squashfuse", exec_args + 1);
            free(runtime_headers); return 0;
        }
        if (strcmp(arg1, unsf_flag) == 0) {
            embed_run_tool(&embed, "unsquashfs", exec_args + 1);
            free(runtime_headers); return 0;
        }
        if (strcmp(arg1, scat_flag) == 0) {
            embed_run_tool(&embed, "sqfscat", exec_args + 1);
            free(runtime_headers); return 0;
        }
#endif
#if defined(URUNTIME_SQUASHFS) && !defined(URUNTIME_LITE)
        char mksf_flag[256], sfstar_flag[256];
        snprintf(mksf_flag, sizeof(mksf_flag), "--%s-mksquashfs", ARG_PFX);
        snprintf(sfstar_flag, sizeof(sfstar_flag), "--%s-sqfstar", ARG_PFX);
        if (strcmp(arg1, mksf_flag) == 0) {
            embed_run_tool(&embed, "mksquashfs", exec_args + 1);
            free(runtime_headers); return 0;
        }
        if (strcmp(arg1, sfstar_flag) == 0) {
            embed_run_tool(&embed, "sqfstar", exec_args + 1);
            free(runtime_headers); return 0;
        }
#endif
#ifdef URUNTIME_DWARFS
        char dw_flag[256], dwck_flag[256], mkdw_flag[256], dwextr_flag[256];
        snprintf(dw_flag, sizeof(dw_flag), "--%s-dwarfs", ARG_PFX);
        snprintf(dwck_flag, sizeof(dwck_flag), "--%s-dwarfsck", ARG_PFX);
        snprintf(mkdw_flag, sizeof(mkdw_flag), "--%s-mkdwarfs", ARG_PFX);
        snprintf(dwextr_flag, sizeof(dwextr_flag), "--%s-dwarfsextract", ARG_PFX);
        if (strcmp(arg1, dw_flag) == 0) {
            embed_run_tool(&embed, "dwarfs", exec_args + 1);
            free(runtime_headers); return 0;
        }
#if !defined(URUNTIME_LITE)
        if (strcmp(arg1, dwck_flag) == 0) {
            embed_run_tool(&embed, "dwarfsck", exec_args + 1);
            free(runtime_headers); return 0;
        }
        if (strcmp(arg1, mkdw_flag) == 0) {
            embed_run_tool(&embed, "mkdwarfs", exec_args + 1);
            free(runtime_headers); return 0;
        }
#endif
        if (strcmp(arg1, dwextr_flag) == 0) {
            embed_run_tool(&embed, "dwarfsextract", exec_args + 1);
            free(runtime_headers); return 0;
        }
#endif
    }

    // Check URUNTIME_EXTRACT level
    const char *uruntime_extract_env = getenv("URUNTIME_EXTRACT");
    int uruntime_extract = 0;
    if (uruntime_extract_env) uruntime_extract = atoi(uruntime_extract_env);

    char extract_run_env[64];
    snprintf(extract_run_env, sizeof(extract_run_env), "%s_EXTRACT_AND_RUN", ENV_NAME);
    if (getenv(extract_run_env))
        is_extract_run = true;

    // Check URUNTIME_CLEANUP
    const char *cleanup_env = getenv("URUNTIME_CLEANUP");
    is_noclenup = (cleanup_env && strcmp(cleanup_env, "1") != 0);

    // Check URUNTIME_UNSHARE
    const char *unshare_env = getenv("URUNTIME_UNSHARE");
    is_unshare = is_unshare || (unshare_env && strcmp(unshare_env, "1") == 0);

    // Reuse check delay
    const char *reuse_check_delay_env = getenv("REUSE_CHECK_DELAY");
    char reuse_check_delay[64] = "";
    if (reuse_check_delay_env) strncpy(reuse_check_delay, reuse_check_delay_env, sizeof(reuse_check_delay) - 1);

    // URUNTIME_MOUNT levels
    const char *mount_env = getenv("URUNTIME_MOUNT");
    int mount_level = mount_env ? atoi(mount_env) : 3;

    bool is_remp_mount = false;
    if (mount_level == 0) {
        is_remp_mount = true;
        if (!reuse_check_delay[0])
            strcpy(reuse_check_delay, is_extract_run ? "5s" : "inf");
    } else if (mount_level == 2) {
        is_remp_mount = true;
        if (!reuse_check_delay[0])
            strcpy(reuse_check_delay, "30m");
    } else if (mount_level == 3) {
        is_remp_mount = true;
        if (!reuse_check_delay[0])
            strcpy(reuse_check_delay, "5s");
    }

    if (reuse_check_delay[0] && strcmp(reuse_check_delay, "0") == 0)
        is_remp_mount = false;

    // Target dir
    char target_dir_env_name[64];
    snprintf(target_dir_env_name, sizeof(target_dir_env_name), "%s_TARGET_DIR", ENV_NAME);
    const char *target_dir = getenv(target_dir_env_name);
    bool target_dir_is_empty = !target_dir || !*target_dir;

    // Build tmp dir path
    char tmp_dir[PATH_MAX];
    char *tmp_dirs[8];
    size_t n_tmp_dirs = 0;

    uid_t uid = getuid();
    gid_t gid = getgid();

    if (target_dir_is_empty) {
        const char *tmp_env = getenv("TMPDIR");
        if (tmp_env && *tmp_env)
            snprintf(tmp_dir, sizeof(tmp_dir), "%s", tmp_env);
        else
            snprintf(tmp_dir, sizeof(tmp_dir), "/tmp");

        char first5name[6] = "";
        strncpy(first5name, self_exe_name, 5);
        first5name[5] = '\0';
        // Filter to alphanumeric only
        for (int i = 0; first5name[i]; i++)
            if (!isalnum((unsigned char)first5name[i])) first5name[i] = '_';

        char self_hash[32] = "";
        if (is_extract_run || is_remp_mount) {
            uint64_t h = hash_string(embedded_envs) +
                         fast_hash_file(image.path, image.offset) + uid;
            snprintf(self_hash, sizeof(self_hash), "%llu", (unsigned long long)h);
        }

#ifdef URUNTIME_APPIMAGE
        char tmp_dir_name[256];
        if (is_extract_run && !is_mount_only)
            snprintf(tmp_dir_name, sizeof(tmp_dir_name), "appimage_extracted_%s%s", first5name, self_hash);
        else if (is_remp_mount)
            snprintf(tmp_dir_name, sizeof(tmp_dir_name), ".mount_%sremp%s", first5name, self_hash);
        else {
            char rstr[16];
            random_string(rstr, 6);
            snprintf(tmp_dir_name, sizeof(tmp_dir_name), ".mount_%s%s", first5name, rstr);
        }
        char full_tmp[PATH_MAX];
        snprintf(full_tmp, sizeof(full_tmp), "%s/%s", tmp_dir, tmp_dir_name);
        strncpy(tmp_dir, full_tmp, sizeof(tmp_dir) - 1);
        tmp_dirs[n_tmp_dirs++] = tmp_dir;
#else
        char ruid_dir[PATH_MAX], mnt_dir[PATH_MAX];
        snprintf(ruid_dir, sizeof(ruid_dir), "%s/.r%u", tmp_dir, uid);
        snprintf(mnt_dir, sizeof(mnt_dir), "%s/mnt", ruid_dir);

        char tmp_dir_name[256];
        if (is_extract_run && !is_mount_only)
            snprintf(tmp_dir_name, sizeof(tmp_dir_name), "%sextr%s", first5name, self_hash);
        else if (is_remp_mount)
            snprintf(tmp_dir_name, sizeof(tmp_dir_name), "%sremp%s", first5name, self_hash);
        else {
            char rstr[16];
            random_string(rstr, 6);
            snprintf(tmp_dir_name, sizeof(tmp_dir_name), "%s%s", first5name, rstr);
        }

        snprintf(tmp_dir, sizeof(tmp_dir), "%s/%s", mnt_dir, tmp_dir_name);
        tmp_dirs[n_tmp_dirs++] = str_dup(tmp_dir);
        tmp_dirs[n_tmp_dirs++] = str_dup(mnt_dir);
        tmp_dirs[n_tmp_dirs++] = str_dup(ruid_dir);
#endif
    } else {
        strncpy(tmp_dir, target_dir, sizeof(tmp_dir) - 1);
        unsetenv(target_dir_env_name);
        tmp_dirs[n_tmp_dirs++] = tmp_dir;
    }

    // Unshare UID/GID
    char unshare_uid_env_name[128], unshare_gid_env_name[128];
    snprintf(unshare_uid_env_name, sizeof(unshare_uid_env_name), "%s_UNSHARE_UID", ENV_NAME);
    snprintf(unshare_gid_env_name, sizeof(unshare_gid_env_name), "%s_UNSHARE_GID", ENV_NAME);

    char unshare_root_env_name[128];
    snprintf(unshare_root_env_name, sizeof(unshare_root_env_name), "%s_UNSHARE_ROOT", ENV_NAME);
    const char *unshare_root = getenv(unshare_root_env_name);

    const char *unshare_uid_str = NULL, *unshare_gid_str = NULL;

    if (unshare_root && strcmp(unshare_root, "1") == 0) {
        unshare_uid_str = "0";
        unshare_gid_str = "0";
    } else {
        const char *u = getenv(unshare_uid_env_name);
        const char *g = getenv(unshare_gid_env_name);
        if (u && *u) { unshare_uid_str = u; is_unshare = true; }
        if (g && *g) { unshare_gid_str = g; is_unshare = true; }
    }

    char unshare_env_name[128];
    snprintf(unshare_env_name, sizeof(unshare_env_name), "%s_UNSHARE", ENV_NAME);
    if (!unshare_uid_str && !unshare_gid_str) {
        const char *u = getenv(unshare_env_name);
        if (u && strcmp(u, "1") == 0) is_unshare = true;
    }

    bool unshare_succeeded = false;
    bool is_unshare_remp = false;
    pid_t child_pid = 0;
    bool is_tmpdir_exists = false;

    // Try to reuse existing mount
    if (is_remp_mount && !is_extract_run) {
        pid_t pid = try_reuse_unshare_mount_point(tmp_dir);
        if (pid > 0) {
            is_tmpdir_exists = true;
            unshare_succeeded = true;
            is_unshare_remp = true;
            child_pid = pid;
        }
    }

    if (!is_unshare_remp) {
        is_tmpdir_exists = is_mounted(tmp_dir);
        if (!is_tmpdir_exists) {
            struct stat st;
            if (stat(tmp_dir, &st) == 0) is_tmpdir_exists = true;
        }
    }

    if (is_remp_mount && !is_extract_run && !is_unshare_remp) {
        child_pid = read_mount_pid_file(tmp_dir, "pid");
    }

    // Main mount/extract logic
    if (!is_tmpdir_exists) {
        if (!is_unshare_remp && is_unshare) {
            unshare_succeeded = try_unshare(uid, gid, unshare_uid_str, unshare_gid_str);
        }

        if ((!is_extract_run || is_mount_only) &&
            !check_fuse(self_exe, uid, gid, &unshare_succeeded, &is_unshare)) {
            // FUSE not available, try extract
            if (!is_mount_only && (uruntime_extract == 2 ||
                (uruntime_extract == 3 && get_file_size(self_exe_str) <= MAX_EXTRACT_SELF_SIZE))) {
                is_extract_run = true;
            } else {
                fprintf(stderr,
                    "Cannot mount %s, please check your FUSE setup.\n"
                    "You might still be able to extract the contents of this %s\n"
                    "if you run it with the --%s-extract option\n",
                    SELF_NAME, SELF_NAME, ARG_PFX);
                free(runtime_headers); free(target_image);
                free(uruntime_dir); free(self_exe_dir); free(self_exe_name);
                return 1;
            }
        }

        if (is_mount_only)
            is_extract_run = false;
        else if (is_extract_run)
            is_noclenup = (getenv("NO_CLEANUP") && strcmp(getenv("NO_CLEANUP"), "1") == 0);

        if (!is_extract_run) {
            const char *no_unmount = getenv("NO_UNMOUNT");
            if (no_unmount && strcmp(no_unmount, "1") == 0) {
                is_remp_mount = true;
                strcpy(reuse_check_delay, "inf");
            }
        }

        // Ensure temp directories exist before mount/extract
        if (!create_tmp_dirs(tmp_dirs, n_tmp_dirs)) {
            fprintf(stderr, "Failed to create temp directories\n");
            free(runtime_headers); free(target_image);
            free(uruntime_dir); free(self_exe_dir); free(self_exe_name);
            return 1;
        }

        // Fork child for mount/extract
        pid_t forked = fork();
        if (forked == 0) {
            // Child
            try_setsid();
            if (unshare_succeeded) restore_capabilities();
            dup2(STDERR_FILENO, STDOUT_FILENO);

            if (is_extract_run)
                extract_image(&embed, &image, tmp_dir, is_extract_run, NULL);
            else
                mount_image(&embed, &image, tmp_dir, uid, gid);

            // Should not reach here
            _exit(1);
        } else if (forked > 0) {
            child_pid = forked;

            if (is_extract_run) {
                int status;
                waitpid(child_pid, &status, 0);
                if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
                    remove_tmp_dirs(tmp_dirs, n_tmp_dirs, unshare_succeeded);
                    free(runtime_headers); free(target_image);
                    free(uruntime_dir); free(self_exe_dir); free(self_exe_name);
                    return 1;
                }
            } else {
                if (!wait_mount(child_pid, tmp_dir, 1)) {
                    remove_tmp_dirs(tmp_dirs, n_tmp_dirs, unshare_succeeded);

                    // Fallback: retry with unshare or extract
                    char *new_args[exec_argc + 1];
                    for (int i = 0; i < exec_argc; i++)
                        new_args[i] = exec_args[i];
                    new_args[exec_argc] = NULL;

                    if (!unshare_succeeded && !is_unshare) {
                        fprintf(stderr, "Trying to unshare...\n");
                        char env_buf[128];
                        snprintf(env_buf, sizeof(env_buf), "%s_UNSHARE=1", ENV_NAME);
                        putenv(str_dup(env_buf));
                    } else {
                        if (!is_mount_only && (uruntime_extract == 2 ||
                            (uruntime_extract == 3 && get_file_size(self_exe_str) <= MAX_EXTRACT_SELF_SIZE))) {
                            fprintf(stderr, "Trying to extract and run...\n");
                            if (is_unshare && !unshare_succeeded) {
                                unsetenv(unshare_env_name);
                                unsetenv(unshare_root_env_name);
                                unsetenv(unshare_uid_env_name);
                                unsetenv(unshare_gid_env_name);
                            }
                            char env_buf[128];
                            snprintf(env_buf, sizeof(env_buf), "%s_EXTRACT_AND_RUN=1", ENV_NAME);
                            putenv(str_dup(env_buf));
                        }
                    }

                    execvp(self_exe_str, new_args);
                    fprintf(stderr, "Failed to execute %s: %s\n", self_exe_str, strerror(errno));
                    free(runtime_headers); free(target_image);
                    free(uruntime_dir); free(self_exe_dir); free(self_exe_name);
                    return 1;
                }
            }

            if (is_remp_mount && !is_extract_run) {
                write_mount_pid_file(tmp_dir, child_pid, unshare_succeeded);
            }
        } else {
            fprintf(stderr, "Fork error: %s\n", strerror(errno));
            free(runtime_headers); free(target_image);
            free(uruntime_dir); free(self_exe_dir); free(self_exe_name);
            return 1;
        }
    }

    // --mount only mode
    if (is_mount_only) {
        if (unshare_succeeded && (!is_tmpdir_exists || is_unshare_remp))
            printf("/proc/%d/root%s\n", child_pid, tmp_dir);
        else
            printf("%s\n", tmp_dir);
        // Wait for Ctrl-C
        signals_handler(child_pid, tmp_dir, false, false);
        free(runtime_headers); free(target_image);
        free(uruntime_dir); free(self_exe_dir); free(self_exe_name);
        return 0;
    }

    int exit_code = 0;

    // Run the target application
    {
        char run_path[PATH_MAX];
#ifdef URUNTIME_APPIMAGE
        snprintf(run_path, sizeof(run_path), "%s/AppRun", tmp_dir);
        struct stat rp_st;
        if (stat(run_path, &rp_st) != 0) {
            fprintf(stderr, "AppRun not found: %s\n", run_path);
            remove_tmp_dirs(tmp_dirs, n_tmp_dirs, unshare_succeeded);
            free(runtime_headers); free(target_image);
            free(uruntime_dir); free(self_exe_dir); free(self_exe_name);
            return 1;
        }
        setenv("ARGV0", arg0, 1);
        setenv("APPDIR", tmp_dir, 1);
        setenv("APPIMAGE", self_exe_str, 1);
        char offset_str[32];
        snprintf(offset_str, sizeof(offset_str), "%lu", (unsigned long)runtime_size);
        setenv("APPOFFSET", offset_str, 1);
#else
        snprintf(run_path, sizeof(run_path), "%s/static/bash", tmp_dir);
        struct stat rp_st;
        if (stat(run_path, &rp_st) != 0) {
            fprintf(stderr, "Static bash not found: %s\n", run_path);
            remove_tmp_dirs(tmp_dirs, n_tmp_dirs, unshare_succeeded);
            free(runtime_headers); free(target_image);
            free(uruntime_dir); free(self_exe_dir); free(self_exe_name);
            return 1;
        }
        setenv("ARG0", arg0, 1);
        setenv("RUNDIR", tmp_dir, 1);
        setenv("RUNIMAGE", self_exe_str, 1);
        char offset_str[32];
        snprintf(offset_str, sizeof(offset_str), "%lu", (unsigned long)runtime_size);
        setenv("RUNOFFSET", offset_str, 1);
#endif

        // Set OWD (original working directory)
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)))
            setenv("OWD", cwd, 1);

        // Portable directories
        try_set_portable_dir(portable_share, "XDG_DATA_HOME", ".local/share");
        try_set_portable_dir(portable_config, "XDG_CONFIG_HOME", ".config");
        try_set_portable_dir(portable_cache, "XDG_CACHE_HOME", ".cache");
        try_set_portable_dir(portable_home, "HOME", NULL);

        // Build args for the target program
        char **run_argv = malloc((exec_argc + 1) * sizeof(char *));
        if (!run_argv) {
            fprintf(stderr, "Memory allocation failed\n");
            return 1;
        }
        char run_first_arg[PATH_MAX + 32];
        if (realpath(run_path, run_first_arg) == NULL)
            strncpy(run_first_arg, run_path, sizeof(run_first_arg) - 1);
        run_argv[0] = run_first_arg;

#ifdef URUNTIME_APPIMAGE
        for (int i = 1; i < exec_argc; i++)
            run_argv[i] = exec_args[i];
        run_argv[exec_argc] = NULL;
#else
        // For RunImage: prepend Run.sh as the interpreter
        char runsh_path[PATH_MAX + 32];
        snprintf(runsh_path, sizeof(runsh_path), "%s/Run.sh", tmp_dir);
        run_argv[0] = runsh_path;
        run_argv[1] = run_first_arg;
        for (int i = 1; i < exec_argc; i++)
            run_argv[i + 1] = exec_args[i];
        run_argv[exec_argc + 1] = NULL;
        exec_argc++;
#endif

        pid_t run_pid = fork();
        if (run_pid == 0) {
            execvp(run_first_arg, run_argv);
            fprintf(stderr, "Failed to execute %s: %s\n", run_first_arg, strerror(errno));
            _exit(1);
        } else if (run_pid > 0) {
            // Signal handler in a sub-thread isn't easily available,
            // so we handle signals by forking
            pid_t sig_pid = fork();
            if (sig_pid == 0) {
                signals_handler(run_pid, tmp_dir, true, false);
                _exit(0);
            }

            int status;
            waitpid(run_pid, &status, 0);
            if (WIFEXITED(status))
                exit_code = WEXITSTATUS(status);
            else if (WIFSIGNALED(status))
                exit_code = 128 + WTERMSIG(status);
        } else {
            fprintf(stderr, "Fork error: %s\n", strerror(errno));
            exit_code = 1;
        }

        free(run_argv);
    }

    // If tmp_dir already existed, just exit
    if (is_tmpdir_exists) {
        free(runtime_headers); free(target_image);
        free(uruntime_dir); free(self_exe_dir); free(self_exe_name);
        return exit_code;
    }

    // Fork cleanup process
    pid_t cleanup_pid = fork();
    if (cleanup_pid == 0) {
        try_setsid();
        if (unshare_succeeded) restore_capabilities();

        // Fork another for signal handling
        pid_t sig_pid2 = fork();
        if (sig_pid2 == 0) {
            signals_handler(child_pid, tmp_dir, false, true);
            _exit(0);
        }

        bool is_mount = !is_extract_run && !is_mount_only;
        uint64_t delay_secs = reuse_check_delay[0] ? parse_time_string(reuse_check_delay) : UINT64_MAX;

        if (is_extract_run) {
            if (!is_noclenup && delay_secs != UINT64_MAX) {
                wait_dir_notuse(tmp_dir, 0, 0, true);
                // Remove directory contents recursively
                char cmd[PATH_MAX + 64];
                snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", tmp_dir);
                system(cmd);
            }
        } else if (!is_remp_mount && is_mount) {
            wait_dir_notuse(tmp_dir, 0, 0, false);
            try_unmount(child_pid, tmp_dir);
        } else if (is_remp_mount && is_mount && delay_secs != UINT64_MAX) {
            wait_dir_notuse(tmp_dir, 0, 0, true);
            try_unmount(child_pid, tmp_dir);
        }

        if (is_mount)
            wait_pid_exit(child_pid, 1);

        remove_tmp_dirs(tmp_dirs, n_tmp_dirs, unshare_succeeded);
        _exit(0);
    }

    // Parent exits immediately
    free(runtime_headers); free(target_image);
    free(uruntime_dir); free(self_exe_dir); free(self_exe_name);
    return exit_code;
}
