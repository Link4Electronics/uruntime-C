#include "uruntime.h"
#include <sched.h>
#include <sys/prctl.h>
#include <sys/capability.h>

#ifndef PR_CAP_AMBIENT
#  define PR_CAP_AMBIENT       47
#  define PR_CAP_AMBIENT_RAISE 2
#endif

void restore_capabilities(void) {
    struct __user_cap_header_struct cap_hdr = {
        .version = _LINUX_CAPABILITY_VERSION_3,
        .pid = 0
    };
    struct __user_cap_data_struct cap_data;

    if (syscall(SYS_capget, &cap_hdr, &cap_data) == 0) {
        FILE *f = fopen("/proc/sys/kernel/cap_last_cap", "r");
        uint32_t last_cap = 39;
        if (f) {
            if (fscanf(f, "%u", &last_cap) != 1) last_cap = 39;
            fclose(f);
        }

        uint64_t all_caps = (1ULL << (last_cap + 1)) - 1;
        if (last_cap >= 31) {
            // Handle > 32 caps by setting full mask
            cap_data.effective = (uint32_t)-1;
            cap_data.permitted = (uint32_t)-1;
            cap_data.inheritable = (uint32_t)-1;
        } else {
            cap_data.effective = (uint32_t)all_caps;
            cap_data.permitted = (uint32_t)all_caps;
            cap_data.inheritable = (uint32_t)all_caps;
        }
        syscall(SYS_capset, &cap_hdr, &cap_data);

        for (uint32_t cap = 0; cap <= last_cap; cap++) {
            syscall(SYS_prctl, PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, (unsigned long)cap, 0UL, 0UL);
        }
    } else {
        fprintf(stderr, "Warning: failed to get capabilities: %s\n", strerror(errno));
    }
}

bool try_make_mount_private(void) {
    return mount("none", "/", "none", MS_REC | MS_PRIVATE, NULL) == 0;
}

bool try_unshare(uid_t uid, gid_t gid, const char *unshare_uid_str, const char *unshare_gid_str) {
    uid_t target_uid = unshare_uid_str ? (uid_t)strtoul(unshare_uid_str, NULL, 10) : uid;
    gid_t target_gid = unshare_gid_str ? (gid_t)strtoul(unshare_gid_str, NULL, 10) : gid;

    if (unshare(CLONE_NEWUSER | CLONE_NEWNS) != 0) {
        fprintf(stderr, "Failed to create user and mount namespaces: %s\n", strerror(errno));
        return false;
    }

    int fd = open("/proc/self/setgroups", O_WRONLY);
    if (fd >= 0) {
        write(fd, "deny", 4);
        close(fd);
    }

    char buf[128];
    int len = snprintf(buf, sizeof(buf), "%u %u 1", target_uid, uid);
    fd = open("/proc/self/uid_map", O_WRONLY);
    if (fd < 0 || write(fd, buf, len) != len) {
        if (fd >= 0) close(fd);
        fprintf(stderr, "Failed to write uid_map\n");
        return false;
    }
    close(fd);

    len = snprintf(buf, sizeof(buf), "%u %u 1", target_gid, gid);
    fd = open("/proc/self/gid_map", O_WRONLY);
    if (fd < 0 || write(fd, buf, len) != len) {
        if (fd >= 0) close(fd);
        fprintf(stderr, "Failed to write gid_map\n");
        return false;
    }
    close(fd);

    restore_capabilities();
    if (!try_make_mount_private())
        fprintf(stderr, "Warning: failed to make mount private: %s\n", strerror(errno));

    return true;
}

bool is_in_user_and_mount_namespace(void) {
    char buf[4096];
    int fd = open("/proc/self/uid_map", O_RDONLY);
    if (fd < 0) return false;
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return false;
    buf[n] = '\0';

    // Trim whitespace
    char *end = buf + n - 1;
    while (end > buf && (*end == ' ' || *end == '\t' || *end == '\n')) end--;
    end[1] = '\0';

    if (buf[0] == '\0' || strstr(buf, "0 0 4294967295") != NULL)
        return false;

    // Parse lines
    char *line = buf;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        unsigned int inside, outside, count;
        if (sscanf(line, "%u %u %u", &inside, &outside, &count) == 3) {
            if (count < 4294967295) {
                try_make_mount_private();
                return true;
            }
        }
        line = nl ? nl + 1 : NULL;
    }
    return false;
}

bool try_setns(pid_t pid) {
    char original_cwd[PATH_MAX];
    if (!getcwd(original_cwd, sizeof(original_cwd)))
        original_cwd[0] = '\0';

    int pidfd = syscall(SYS_pidfd_open, (long)pid, 0);
    if (pidfd < 0) {
        fprintf(stderr, "Failed to open pidfd: %s - mount point reuse unavailable\n", strerror(errno));
        return false;
    }

    int result = setns(pidfd, CLONE_NEWNS | CLONE_NEWUSER);
    close(pidfd);

    if (result != 0) {
        fprintf(stderr, "Failed to enter namespaces via pidfd: %s\n", strerror(errno));
        return false;
    }

    restore_capabilities();
    if (!try_make_mount_private())
        fprintf(stderr, "Warning: failed to make mount private: %s\n", strerror(errno));

    if (original_cwd[0] && chdir(original_cwd) != 0)
        fprintf(stderr, "Warning: failed to restore working directory\n");

    return true;
}

pid_t read_mount_pid_file(const char *mount_point, const char *extension) {
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s.%s", mount_point, extension);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    pid_t pid = 0;
    fscanf(f, "%d", &pid);
    fclose(f);
    return pid;
}

bool write_mount_pid_file(const char *mount_point, pid_t pid, bool unshare_succeeded) {
    const char *ext = unshare_succeeded ? "un.pid" : "pid";
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s.%s", mount_point, ext);
    FILE *f = fopen(path, "w");
    if (!f) return false;
    fprintf(f, "%d", pid);
    fclose(f);
    return true;
}

pid_t try_reuse_unshare_mount_point(const char *mount_point) {
    pid_t pid = read_mount_pid_file(mount_point, "un.pid");
    if (pid <= 0) return 0;

    if (!is_pid_exists(pid)) {
        char un_pid_path[PATH_MAX];
        snprintf(un_pid_path, sizeof(un_pid_path), "%s.un.pid", mount_point);
        unlink(un_pid_path);
        return 0;
    }

    if (try_setns(pid) && is_mounted(mount_point))
        return pid;

    return 0;
}

static bool add_to_path(const char *dir) {
    const char *old_path = getenv("PATH");
    if (!old_path || !*old_path) {
        return setenv("PATH", dir, 1) == 0;
    }
    if (strstr(old_path, dir)) return true;  // already in path
    char new_path[PATH_MAX * 2];
    snprintf(new_path, sizeof(new_path), "%s:%s", dir, old_path);
    return setenv("PATH", new_path, 1) == 0;
}

bool check_fuse(const char *uruntime_path, uid_t uid, gid_t gid,
                 bool *unshare_succeeded, bool *is_unshare) {
    // Check if /dev/fuse is accessible
    if (access("/dev/fuse", R_OK | W_OK) != 0)
        return false;

    if (uid == 0 || *unshare_succeeded || is_in_user_and_mount_namespace())
        return true;

    // Create fusermount directory and symlinks
    char tmp_path_dir[PATH_MAX];
    snprintf(tmp_path_dir, sizeof(tmp_path_dir), "/tmp/.path%u", uid);

    struct stat st;
    if (stat(tmp_path_dir, &st) == 0) {
        // Clean up old symlinks
        DIR *d = opendir(tmp_path_dir);
        if (d) {
            struct dirent *entry;
            while ((entry = readdir(d)) != NULL) {
                if (strcmp(entry->d_name, "fusermount") == 0 ||
                    strcmp(entry->d_name, "fusermount3") == 0) {
                    char symlink_path[PATH_MAX];
                    snprintf(symlink_path, sizeof(symlink_path), "%s/%s", tmp_path_dir, entry->d_name);
                    char target[PATH_MAX];
                    ssize_t tlen = readlink(symlink_path, target, sizeof(target) - 1);
                    if (tlen > 0) {
                        target[tlen] = '\0';
                        if (is_suid_exe(target)) continue;
                        if (strcmp(target, uruntime_path) == 0) continue;
                    }
                    unlink(symlink_path);
                }
            }
            closedir(d);
        }
        add_to_path(tmp_path_dir);
    }

    const char *fusermount_prog = getenv("FUSERMOUNT_PROG");
    bool is_fusermount = true;

    if (fusermount_prog && *fusermount_prog && is_suid_exe(fusermount_prog)) {
        if (mkdir(tmp_path_dir, 0755) != 0 && errno != EEXIST) return false;
        char *base = basename_c(fusermount_prog);
        char link_path[PATH_MAX];
        snprintf(link_path, sizeof(link_path), "%s/%s", tmp_path_dir, base);
        unlink(link_path);
        symlink(fusermount_prog, link_path);
        add_to_path(tmp_path_dir);
        free(base);
    } else {
        const char *fusermounts[] = {"fusermount", "fusermount3"};
        for (size_t i = 0; i < 2; i++) {
            if (find_suid_exe(fusermounts[i], NULL, 0)) continue;

            // Try the other one
            const char *fallback = (i == 0) ? "fusermount3" : "fusermount";
            char fpath[PATH_MAX];
            if (find_suid_exe(fallback, fpath, sizeof(fpath))) {
                if (mkdir(tmp_path_dir, 0755) != 0 && errno != EEXIST) break;
                char link_path[PATH_MAX];
                snprintf(link_path, sizeof(link_path), "%s/%s", tmp_path_dir, fusermounts[i]);
                unlink(link_path);
                symlink(fpath, link_path);
                add_to_path(tmp_path_dir);
                break;
            }
            is_fusermount = false;
        }
    }

    if (!is_fusermount) {
        fprintf(stderr, "SUID fusermount not found in PATH, trying to unshare...\n");
        *is_unshare = true;
        if (try_unshare(uid, gid, NULL, NULL)) {
            *unshare_succeeded = true;
            return true;
        }

        // Fallback: symlink to self
        if (mkdir(tmp_path_dir, 0755) == 0 || errno == EEXIST) {
            for (size_t i = 0; i < 2; i++) {
                const char *fm = (i == 0) ? "fusermount" : "fusermount3";
                char link_path[PATH_MAX];
                snprintf(link_path, sizeof(link_path), "%s/%s", tmp_path_dir, fm);
                unlink(link_path);
                symlink(uruntime_path, link_path);
            }
        }
    }

    return true;
}

bool try_unmount(pid_t fuse_pid, const char *mount_point) {
    if (fuse_pid > 0) {
        usleep(100000);
        if (!is_pid_exists(fuse_pid)) return false;
    }

    if (!is_mount_point(mount_point)) {
        fprintf(stderr, "%s: not mounted!\n", mount_point);
        return false;
    }

    // Try umount syscall first
    if (umount(mount_point) == 0) return true;

    bool is_busy = (errno == EBUSY);

    // Try fusermount
    const char *fusermounts[] = {"fusermount", "fusermount3"};
    char *args[] = {"-u", (char *)mount_point, NULL};

    for (size_t i = 0; i < 2 && !is_busy; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            execvp(fusermounts[i], args);
            _exit(1);
        } else if (pid > 0) {
            int status;
            waitpid(pid, &status, 0);
            if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
                return true;
        }
    }

    // Try umount command
    if (!is_busy) {
        pid_t pid = fork();
        if (pid == 0) {
            execlp("umount", "umount", mount_point, NULL);
            _exit(1);
        } else if (pid > 0) {
            int status;
            waitpid(pid, &status, 0);
            if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
                return true;
        }
    }

    // Try SIGTERM to FUSE process
    if (fuse_pid > 0 && !is_busy) {
        kill(fuse_pid, SIGTERM);
        return true;
    }

    fprintf(stderr, "Failed to unmount: %s\n", mount_point);
    if (fuse_pid > 0 && is_mount_point(mount_point))
        fprintf(stderr, "Unmount it manually!\n");

    return false;
}

bool wait_mount(pid_t pid, const char *path, int timeout_secs) {
    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);

    while (!is_mounted(path)) {
        if (!is_pid_exists(pid)) {
            fprintf(stderr, "The mount process ended unexpectedly! PID: %d\n", pid);
            return false;
        }
        clock_gettime(CLOCK_MONOTONIC, &now);
        if ((now.tv_sec - start.tv_sec) >= timeout_secs) {
            fprintf(stderr, "Timeout reached while waiting for mount: %s\n", path);
            return false;
        }
        usleep(2000);
    }
    return true;
}

bool create_tmp_dirs(char **dirs, size_t ndirs) {
    if (ndirs == 0) return false;

    // Create directories from parent to child (reverse order)
    for (size_t i = ndirs; i > 0; i--) {
        char *dir = dirs[i - 1];
        struct stat st;
        if (stat(dir, &st) == 0) continue;
        if (mkdir(dir, 0700) != 0) {
            fprintf(stderr, "Failed to create tmp dir: %s: %s\n", dir, strerror(errno));
            return false;
        }
    }

    for (size_t i = 0; i < ndirs; i++) {
        chmod(dirs[i], 0700);
    }
    return true;
}

void remove_tmp_dirs(char **dirs, size_t ndirs, bool unshare_succeeded) {
    if (ndirs == 0) return;

    if (!is_mounted(dirs[0])) {
        const char *ext = unshare_succeeded ? "un.pid" : "pid";
        char pid_path[PATH_MAX];
        snprintf(pid_path, sizeof(pid_path), "%s.%s", dirs[0], ext);
        unlink(pid_path);
    }

    for (size_t i = 0; i < ndirs; i++)
        rmdir(dirs[i]);
}
