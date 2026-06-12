#include "uruntime.h"
#include "embed_config.h"
#include <zstd.h>

#ifndef MFD_CLOEXEC
#  define MFD_CLOEXEC 0x0002U
#endif

#ifndef SYS_memfd_create
#  if defined(__x86_64__)
#    define SYS_memfd_create 319
#  elif defined(__aarch64__)
#    define SYS_memfd_create 279
#  elif defined(__arm__)
#    define SYS_memfd_create 385
#  elif defined(__i386__)
#    define SYS_memfd_create 356
#  elif defined(__riscv)
#    define SYS_memfd_create 279
#  elif defined(__powerpc64__)
#    define SYS_memfd_create 360
#  else
#    define SYS_memfd_create 319
#  endif
#endif

extern char **environ;

// Embedded tool data (defined in generated embed_data.c)
#ifdef HAVE_squashfuse_DATA
extern const unsigned char squashfuse_data[];
extern const size_t squashfuse_data_size;
#endif
#ifdef HAVE_unsquashfs_DATA
extern const unsigned char unsquashfs_data[];
extern const size_t unsquashfs_data_size;
#endif
#ifdef HAVE_mksquashfs_DATA
extern const unsigned char mksquashfs_data[];
extern const size_t mksquashfs_data_size;
#endif
#ifdef HAVE_dwarfs_universal_DATA
extern const unsigned char dwarfs_universal_data[];
extern const size_t dwarfs_universal_data_size;
#endif
#ifdef HAVE_dwarfsextract_DATA
extern const unsigned char dwarfsextract_data[];
extern const size_t dwarfsextract_data_size;
#endif

void embed_init(Embed *embed) {
    memset(embed, 0, sizeof(*embed));
#ifdef HAVE_squashfuse_DATA
    embed->squashfuse = squashfuse_data;
    embed->squashfuse_size = squashfuse_data_size;
#endif
#ifdef HAVE_unsquashfs_DATA
    embed->unsquashfs = unsquashfs_data;
    embed->unsquashfs_size = unsquashfs_data_size;
#endif
#ifdef HAVE_mksquashfs_DATA
    embed->mksquashfs = mksquashfs_data;
    embed->mksquashfs_size = mksquashfs_data_size;
#endif
#ifdef HAVE_dwarfs_universal_DATA
    embed->dwarfs_universal = dwarfs_universal_data;
    embed->dwarfs_universal_size = dwarfs_universal_data_size;
#endif
#ifdef HAVE_dwarfsextract_DATA
    embed->dwarfsextract = dwarfsextract_data;
    embed->dwarfsextract_size = dwarfsextract_data_size;
#endif
}

bool decompress_zst(const unsigned char *input, size_t input_size,
                     unsigned char **output, size_t *output_size) {
    unsigned long long uncomp_size = ZSTD_getFrameContentSize(input, input_size);
    if (uncomp_size == ZSTD_CONTENTSIZE_UNKNOWN || uncomp_size == ZSTD_CONTENTSIZE_ERROR) {
        ZSTD_DCtx *dctx = ZSTD_createDCtx();
        if (!dctx) return false;

        ZSTD_inBuffer in = {input, input_size, 0};
        size_t buf_size = ZSTD_DStreamOutSize();
        unsigned char *buf = malloc(buf_size);
        if (!buf) { ZSTD_freeDCtx(dctx); return false; }

        bytearr_t result = bytearr_init;
        while (in.pos < in.size) {
            ZSTD_outBuffer out = {buf, buf_size, 0};
            size_t ret = ZSTD_decompressStream(dctx, &out, &in);
            if (ZSTD_isError(ret)) {
                free(buf); bytearr_free(&result); ZSTD_freeDCtx(dctx);
                return false;
            }
            if (!bytearr_write(&result, buf, out.pos)) {
                free(buf); bytearr_free(&result); ZSTD_freeDCtx(dctx);
                return false;
            }
            if (ret == 0) break;
        }
        free(buf); ZSTD_freeDCtx(dctx);
        *output = result.data;
        *output_size = result.len;
        return true;
    }

    *output = malloc(uncomp_size);
    if (!*output) return false;

    size_t ret = ZSTD_decompress(*output, uncomp_size, input, input_size);
    if (ZSTD_isError(ret)) {
        free(*output); *output = NULL;
        return false;
    }
    *output_size = ret;
    return true;
}

void mfd_exec_zst(const char *exec_name, const unsigned char *zst_data, size_t zst_size,
                   char *const argv[]) {
    setenv("LC_ALL", "C", 1);
    const char *mc = getenv("MALLOC_CONF");
    if (!mc || !*mc)
        setenv("MALLOC_CONF", "background_thread:true,dirty_decay_ms:1000,muzzy_decay_ms:1000", 0);

    unsigned char *decompressed = NULL;
    size_t decompressed_size = 0;
    if (!decompress_zst(zst_data, zst_size, &decompressed, &decompressed_size)) {
        fprintf(stderr, "Failed to decompress %s\n", exec_name);
        _exit(1);
    }

    mfd_exec(exec_name, decompressed, decompressed_size, argv);
    free(decompressed);
}

void mfd_exec(const char *exec_name, const unsigned char *data, size_t data_size,
               char *const argv[]) {
    int memfd = syscall(SYS_memfd_create, exec_name, MFD_CLOEXEC);
    if (memfd < 0) {
        fprintf(stderr, "Failed to create memfd for %s: %s\n", exec_name, strerror(errno));
        _exit(1);
    }

    if (write(memfd, data, data_size) != (ssize_t)data_size) {
        fprintf(stderr, "Failed to write to memfd for %s: %s\n", exec_name, strerror(errno));
        close(memfd); _exit(1);
    }

    lseek(memfd, 0, SEEK_SET);
    fexecve(memfd, argv, environ);

    // Fallback via /proc/self/fd
    char fdpath[64];
    snprintf(fdpath, sizeof(fdpath), "/proc/self/fd/%d", memfd);
    execve(fdpath, argv, environ);

    fprintf(stderr, "Failed to execute %s: %s\n", exec_name, strerror(errno));
    close(memfd);
    _exit(1);
}

static void safe_mfd_exec(const char *name, const unsigned char *data, size_t size,
                           char *const argv[]) {
    if (!data || !size) {
        fprintf(stderr, "%s: embedded tool not available\n", name);
        _exit(1);
    }
    mfd_exec_zst(name, data, size, argv);
}

void embed_run_tool(const Embed *embed, const char *name, char *const argv[]) {
#ifdef URUNTIME_SQUASHFS
    if (strcmp(name, "squashfuse") == 0)
        safe_mfd_exec("squashfuse", embed->squashfuse, embed->squashfuse_size, argv);
    else if (strcmp(name, "unsquashfs") == 0)
        safe_mfd_exec("unsquashfs", embed->unsquashfs, embed->unsquashfs_size, argv);
    else if (strcmp(name, "sqfscat") == 0)
        safe_mfd_exec("sqfscat", embed->unsquashfs, embed->unsquashfs_size, argv);
#endif
#if defined(URUNTIME_SQUASHFS) && !defined(URUNTIME_LITE)
    if (strcmp(name, "mksquashfs") == 0 || strcmp(name, "sqfstar") == 0)
        safe_mfd_exec(name, embed->mksquashfs, embed->mksquashfs_size, argv);
#endif
#ifdef URUNTIME_DWARFS
    if (strcmp(name, "dwarfs") == 0 || strcmp(name, "dwarfsck") == 0 ||
        strcmp(name, "mkdwarfs") == 0)
        safe_mfd_exec(name, embed->dwarfs_universal, embed->dwarfs_universal_size, argv);
    else if (strcmp(name, "dwarfsextract") == 0)
        safe_mfd_exec(name, embed->dwarfsextract, embed->dwarfsextract_size, argv);
#endif
}
