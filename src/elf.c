#include "uruntime.h"

static bool read_u16(const unsigned char *p, uint16_t *v) {
    memcpy(v, p, sizeof(uint16_t));
    return true;
}

static bool read_u32(const unsigned char *p, uint32_t *v) {
    memcpy(v, p, sizeof(uint32_t));
    return true;
}

static bool read_u64(const unsigned char *p, uint64_t *v) {
    memcpy(v, p, sizeof(uint64_t));
    return true;
}

bool read_elf_headers(const char *path, unsigned char **headers, size_t *len, uint64_t *size) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return false;

    unsigned char ehdr_buf[64];
    if (read(fd, ehdr_buf, 64) != 64) { close(fd); return false; }

    // Verify ELF magic
    if (ehdr_buf[0] != 0x7f || ehdr_buf[1] != 'E' || ehdr_buf[2] != 'L' || ehdr_buf[3] != 'F') {
        close(fd); return false;
    }

    uint64_t shoff;
    uint16_t shnum, shentsize;
    read_u64(ehdr_buf + 40, &shoff);     // e_shoff
    read_u16(ehdr_buf + 60, &shnum);     // e_shnum
    read_u16(ehdr_buf + 58, &shentsize); // e_shentsize

    uint64_t sh_table_size = (uint64_t)shnum * shentsize;

    // Find the last section end to determine runtime size
    if (lseek(fd, shoff, SEEK_SET) == (off_t)-1) { close(fd); return false; }

    uint64_t last_end = 0;
    for (uint16_t i = 0; i < shnum; i++) {
        unsigned char shdr_buf[64];
        if (read(fd, shdr_buf, 64) != 64) { close(fd); return false; }
        uint64_t sh_offset, sh_size;
        read_u64(shdr_buf + 24, &sh_offset); // sh_offset
        read_u64(shdr_buf + 32, &sh_size);   // sh_size
        uint64_t end = sh_offset + sh_size;
        if (end > last_end) last_end = end;
    }

    uint64_t section_table_end = shoff + sh_table_size;
    *size = section_table_end > last_end ? section_table_end : last_end;

    // Read all headers (must include section data beyond the section header table,
    // e.g. sections added by objcopy --add-section like .envs, .upd_info)
    uint64_t read_size = *size;
    unsigned char *buf = malloc(read_size);
    if (!buf) { close(fd); return false; }

    if (lseek(fd, 0, SEEK_SET) == (off_t)-1) { free(buf); close(fd); return false; }
    if ((size_t)read(fd, buf, read_size) != read_size) { free(buf); close(fd); return false; }

    close(fd);
    *headers = buf;
    *len = read_size;
    return true;
}

static bool find_section(const unsigned char *headers, const char *section_name,
                          uint64_t *out_offset, uint64_t *out_size) {
    // Parse ELF header
    unsigned char ehdr[64];
    memcpy(ehdr, headers, 64);

    uint64_t shoff;
    uint16_t shnum, shentsize, shstrndx;
    read_u64(ehdr + 40, &shoff);
    read_u16(ehdr + 60, &shnum);
    read_u16(ehdr + 58, &shentsize);
    read_u16(ehdr + 62, &shstrndx);

    // Get string table
    Elf64_Shdr *shstrtab = (Elf64_Shdr *)(headers + shoff + shstrndx * shentsize);
    uint64_t strtab_off, strtab_size;
    read_u64((const unsigned char *)&shstrtab->sh_offset, &strtab_off);
    read_u64((const unsigned char *)&shstrtab->sh_size, &strtab_size);
    const char *strtab = (const char *)(headers + strtab_off);

    // Find section
    for (uint16_t i = 0; i < shnum; i++) {
        const Elf64_Shdr *shdr = (const Elf64_Shdr *)(headers + shoff + i * shentsize);
        uint32_t sh_name;
        read_u32((const unsigned char *)&shdr->sh_name, &sh_name);
        const char *name = strtab + sh_name;

        if (strcmp(name, section_name) == 0) {
            read_u64((const unsigned char *)&shdr->sh_offset, out_offset);
            read_u64((const unsigned char *)&shdr->sh_size, out_size);
            return true;
        }
    }
    return false;
}

bool get_section_data(const unsigned char *headers, size_t headers_len,
                       const char *section_name, char *buf, size_t bufsize) {
    uint64_t offset = 0, size = 0;
    if (!find_section(headers, section_name, &offset, &size))
        return false;
    if (offset + size > headers_len) return false;
    size_t copy = size < bufsize - 1 ? size : bufsize - 1;
    memcpy(buf, headers + offset, copy);
    buf[copy] = '\0';
    // Trim trailing nulls
    for (ssize_t i = (ssize_t)copy - 1; i >= 0; i--) {
        if (buf[i] == '\0') buf[i] = '\0';
        else break;
    }
    return true;
}

bool get_section_offset_size(const unsigned char *headers, size_t headers_len,
                              const char *section_name, uint64_t *offset, uint64_t *size) {
    return find_section(headers, section_name, offset, size);
}

static bool copy_file(int src, int dst) {
    unsigned char buf[65536];
    ssize_t nread;
    while ((nread = read(src, buf, sizeof(buf))) > 0) {
        ssize_t written = 0;
        while (written < nread) {
            ssize_t n = write(dst, buf + written, (size_t)(nread - written));
            if (n < 0) return false;
            written += n;
        }
    }
    return nread == 0;
}

bool add_section_data(const char *path, const char *section_name,
                       const unsigned char *data, size_t data_len) {
    unsigned char *headers = NULL;
    size_t headers_len = 0;
    uint64_t runtime_size = 0;

    if (!read_elf_headers(path, &headers, &headers_len, &runtime_size)) {
        free(headers);
        return false;
    }

    uint64_t offset = 0, orig_size = 0;
    if (!find_section(headers, section_name, &offset, &orig_size)) {
        free(headers);
        return false;
    }

    if (data_len > orig_size) {
        free(headers);
        errno = ENOSPC;
        return false;
    }

    // Running binaries reject O_WRONLY with ETXTBUSY.
    // Work around it by copying to a temp file, modifying, then renaming.
    struct stat st;
    if (stat(path, &st) < 0) { free(headers); return false; }

    char temp_path[PATH_MAX];
    snprintf(temp_path, sizeof(temp_path), "%s.tmp", path);

    int src = open(path, O_RDONLY);
    if (src < 0) { free(headers); return false; }

    int dst = open(temp_path, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode & 0777);
    if (dst < 0) { close(src); free(headers); return false; }

    bool ok = copy_file(src, dst);
    close(src);
    if (!ok) { close(dst); unlink(temp_path); free(headers); return false; }

    // Write section data at the correct offset in the temp file
    if (lseek(dst, (off_t)offset, SEEK_SET) == (off_t)-1) {
        close(dst); unlink(temp_path); free(headers); return false;
    }
    if (write(dst, data, data_len) != (ssize_t)data_len) {
        close(dst); unlink(temp_path); free(headers); return false;
    }

    // Pad with zeros if needed
    if (data_len < orig_size) {
        unsigned char zero = 0;
        for (uint64_t i = data_len; i < orig_size; i++) {
            if (write(dst, &zero, 1) != 1) break;
        }
    }

    close(dst);

    if (rename(temp_path, path) < 0) {
        unlink(temp_path);
        free(headers);
        return false;
    }

    free(headers);
    return true;
}
