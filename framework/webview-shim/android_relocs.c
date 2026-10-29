/*
 * Libraries with DT_ANDROID_RELR, loaded through OH's musl.
 *
 * lld writes Android's own tag numbers for RELR relocations on Android targets
 * (DT_ANDROID_RELR/RELRSZ/RELRENT); the table's encoding is the standard one. OH's musl applies
 * standard DT_RELR and DT_ANDROID_RELA -- its own libraries carry both -- but skips the Android RELR
 * tags, so every pointer the table covers keeps its link-time value. Facebook died on its first
 * library: libsuperpack-jni.so's INIT_ARRAY holds 0x2ae2c in the file, RELR is what adds the load
 * base, and the constructor call jumped to 0x2ae2c (SIGSEGV, pc == fault addr).
 *
 * The launcher retags the APK's own libraries when it stages them. This covers libraries an app
 * writes at run time (unpacked from an archive, say): such a library is loaded from a copy whose
 * three tags carry the standard numbers, and musl then relocates it and runs its constructors
 * itself.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <elf.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define WL_DT_ANDROID_RELR 0x6fffe000
#define WL_DT_ANDROID_RELRSZ 0x6fffe001
#define WL_DT_ANDROID_RELRENT 0x6fffe003
#define WL_DT_RELRSZ 35
#define WL_DT_RELR 36
#define WL_DT_RELRENT 37

/* write(2), not stdio: stdio is buffered in this process, and a crash right after loses it. */
__attribute__((visibility("hidden"), format(printf, 1, 2))) void wl_relocs_log(const char *format, ...)
{
    char line[1024];
    va_list args;
    va_start(args, format);
    int n = vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    if (n > 0) (void) !write(2, line, (size_t) n < sizeof(line) ? (size_t) n : sizeof(line) - 1);
}

/* 1 when the library at path has DT_ANDROID_RELR, which musl does not apply. */
__attribute__((visibility("hidden"))) int wl_needs_android_relocs(const char *path)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    Elf64_Ehdr eh;
    int found = 0;
    if (pread(fd, &eh, sizeof(eh), 0) == (ssize_t) sizeof(eh) && memcmp(eh.e_ident, ELFMAG, SELFMAG) == 0 &&
        eh.e_ident[EI_CLASS] == ELFCLASS64) {
        for (int i = 0; i < eh.e_phnum && !found; ++i) {
            Elf64_Phdr ph;
            if (pread(fd, &ph, sizeof(ph), (off_t) (eh.e_phoff + (uint64_t) i * sizeof(ph))) != (ssize_t) sizeof(ph)) break;
            if (ph.p_type != PT_DYNAMIC) continue;
            for (uint64_t off = 0; off + sizeof(Elf64_Dyn) <= ph.p_filesz; off += sizeof(Elf64_Dyn)) {
                Elf64_Dyn d;
                if (pread(fd, &d, sizeof(d), (off_t) (ph.p_offset + off)) != (ssize_t) sizeof(d) || d.d_tag == DT_NULL) break;
                if (d.d_tag == WL_DT_ANDROID_RELR) { found = 1; break; }
            }
        }
    }
    close(fd);
    return found;
}

/* The file offset of vaddr in the ELF image data (size bytes), or 0 when no PT_LOAD maps it. */
static uint64_t wl_file_offset(const unsigned char *data, size_t size, uint64_t vaddr)
{
    const Elf64_Ehdr *eh = (const Elf64_Ehdr *) data;
    const Elf64_Phdr *ph = (const Elf64_Phdr *) (data + eh->e_phoff);
    for (int i = 0; i < eh->e_phnum; ++i) {
        if (ph[i].p_type == PT_LOAD && ph[i].p_vaddr <= vaddr && vaddr < ph[i].p_vaddr + ph[i].p_filesz &&
            ph[i].p_offset + (vaddr - ph[i].p_vaddr) < size) {
            return ph[i].p_offset + (vaddr - ph[i].p_vaddr);
        }
    }
    return 0;
}

/*
 * Drops null and -1 entries from the init and fini arrays of the ELF image data, as the launcher
 * does for the APK's own libraries (the manifest's init_array_sanitized, which this follows):
 * bionic skips such entries, OH's musl calls them. Honor of Kings writes its libraries into
 * app_libs/; ART's loader cleans the one it opens, but libPixUI_Unity.so's dependency
 * libpixuiCurl.so, copied here beside it, had nulls, and its load jumped to address 0. Entries
 * keep their order; one that moves takes its RELA relocation along. A library with Android's
 * packed relocations is left alone.
 */
static void wl_sanitize_init_arrays(unsigned char *data, size_t size)
{
    const Elf64_Ehdr *eh = (const Elf64_Ehdr *) data;
    if (size < sizeof(*eh) || memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0 || eh->e_ident[EI_CLASS] != ELFCLASS64 ||
        eh->e_phoff + (uint64_t) eh->e_phnum * sizeof(Elf64_Phdr) > size) {
        return;
    }
    const Elf64_Phdr *ph = (const Elf64_Phdr *) (data + eh->e_phoff);
    Elf64_Dyn *dyn = NULL;
    size_t count = 0;
    for (int i = 0; i < eh->e_phnum; ++i) {
        if (ph[i].p_type == PT_DYNAMIC && ph[i].p_offset + ph[i].p_filesz <= size) {
            dyn = (Elf64_Dyn *) (data + ph[i].p_offset);
            count = ph[i].p_filesz / sizeof(Elf64_Dyn);
        }
    }
    Elf64_Dyn *array_tag[2] = {NULL, NULL}, *size_tag[2] = {NULL, NULL};
    uint64_t rela = 0, rela_size = 0, rela_entry = sizeof(Elf64_Rela);
    for (size_t k = 0; dyn != NULL && k < count && dyn[k].d_tag != DT_NULL; ++k) {
        switch (dyn[k].d_tag) {
        case 0x6000000F: case 0x60000010: case 0x60000011: case 0x60000012: return;   /* DT_ANDROID_REL(A)* */
        case DT_INIT_ARRAY: if (array_tag[0] == NULL) array_tag[0] = &dyn[k]; break;
        case DT_FINI_ARRAY: if (array_tag[1] == NULL) array_tag[1] = &dyn[k]; break;
        case DT_INIT_ARRAYSZ: if (size_tag[0] == NULL) size_tag[0] = &dyn[k]; break;
        case DT_FINI_ARRAYSZ: if (size_tag[1] == NULL) size_tag[1] = &dyn[k]; break;
        case DT_RELA: if (rela == 0) rela = dyn[k].d_un.d_ptr; break;
        case DT_RELASZ: if (rela_size == 0) rela_size = dyn[k].d_un.d_val; break;
        case DT_RELAENT: rela_entry = dyn[k].d_un.d_val; break;
        default: break;
        }
    }
    uint64_t rela_start = rela != 0 ? wl_file_offset(data, size, rela) : 0;
    if (rela_start == 0 || rela_entry < sizeof(Elf64_Rela) || rela_start + rela_size > size) rela_size = 0;
    for (int a = 0; a < 2; ++a) {
        if (array_tag[a] == NULL || size_tag[a] == NULL) continue;
        uint64_t base = array_tag[a]->d_un.d_ptr, slots = size_tag[a]->d_un.d_val / 8;
        uint64_t start = wl_file_offset(data, size, base);
        if (start == 0 || slots == 0 || slots > 1024 || start + slots * 8 > size) continue;
        Elf64_Rela *relocated[1024];
        uint64_t values[1024];
        size_t kept[1024], kept_count = 0;
        int moves_fixed = 0;
        memset(relocated, 0, sizeof(Elf64_Rela *) * slots);
        for (uint64_t off = 0; off + sizeof(Elf64_Rela) <= rela_size; off += rela_entry) {
            Elf64_Rela *r = (Elf64_Rela *) (data + rela_start + off);
            if (base <= r->r_offset && r->r_offset < base + slots * 8 && (r->r_offset - base) % 8 == 0) {
                relocated[(r->r_offset - base) / 8] = r;
            }
        }
        memcpy(values, data + start, slots * 8);
        for (size_t i = 0; i < slots; ++i) {
            if (relocated[i] != NULL || (values[i] != 0 && values[i] != UINT64_MAX)) kept[kept_count++] = i;
        }
        if (kept_count == slots) continue;
        /* An entry without a RELA relocation is RELR-relocated in place (or absolute): it cannot
         * move, so only trailing entries are dropped then. */
        for (size_t j = 0; j < kept_count; ++j) {
            if (kept[j] != j && relocated[kept[j]] == NULL) moves_fixed = 1;
        }
        if (moves_fixed) {
            size_t last = kept_count > 0 ? kept[kept_count - 1] : 0;
            kept_count = kept_count > 0 ? last + 1 : 0;
            for (size_t j = 0; j < kept_count; ++j) kept[j] = j;
            if (kept_count == slots) continue;
        }
        for (size_t j = 0; j < kept_count; ++j) {
            if (kept[j] == j) continue;
            memcpy(data + start + 8 * j, &values[kept[j]], 8);
            if (relocated[kept[j]] != NULL) relocated[kept[j]]->r_offset = base + 8 * j;
        }
        memset(data + start + 8 * kept_count, 0, (slots - kept_count) * 8);
        size_tag[a]->d_un.d_val = 8 * kept_count;
    }
}

/*
 * Writes path's copy into target, with the Android RELR tags renumbered to the standard ones when
 * retag is set, and null init and fini entries dropped. An existing copy as large and as new as
 * path is kept.
 */
__attribute__((visibility("hidden"))) int wl_write_loadable_copy(const char *path, const char *target, int retag)
{
    struct stat src, dst;
    if (stat(path, &src) != 0) return -1;
    if (stat(target, &dst) == 0 && dst.st_size == src.st_size && dst.st_mtime >= src.st_mtime) return 0;
    /* Never replace a different file of that name (the APK's own library, staged beside it). */
    if (stat(target, &dst) == 0 && dst.st_size != src.st_size) return 0;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    unsigned char *data = mmap(NULL, (size_t) src.st_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    close(fd);
    if (data == MAP_FAILED) return -1;
    Elf64_Ehdr *eh = (Elf64_Ehdr *) data;
    Elf64_Phdr *ph = (Elf64_Phdr *) (data + eh->e_phoff);
    for (int i = 0; retag && i < eh->e_phnum; ++i) {
        if (ph[i].p_type != PT_DYNAMIC || ph[i].p_offset + ph[i].p_filesz > (uint64_t) src.st_size) continue;
        Elf64_Dyn *d = (Elf64_Dyn *) (data + ph[i].p_offset);
        for (size_t k = 0; k < ph[i].p_filesz / sizeof(Elf64_Dyn) && d[k].d_tag != DT_NULL; ++k) {
            if (d[k].d_tag == WL_DT_ANDROID_RELR) d[k].d_tag = WL_DT_RELR;
            else if (d[k].d_tag == WL_DT_ANDROID_RELRSZ) d[k].d_tag = WL_DT_RELRSZ;
            else if (d[k].d_tag == WL_DT_ANDROID_RELRENT) d[k].d_tag = WL_DT_RELRENT;
        }
    }
    wl_sanitize_init_arrays(data, (size_t) src.st_size);
    char tmp[800];
    snprintf(tmp, sizeof(tmp), "%s.tmp%d", target, (int) getpid());
    int out = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0700);
    size_t put = 0;
    while (out >= 0 && put < (size_t) src.st_size) {
        ssize_t n = write(out, data + put, (size_t) src.st_size - put);
        if (n <= 0) break;
        put += (size_t) n;
    }
    if (out >= 0) close(out);
    munmap(data, (size_t) src.st_size);
    if (out < 0 || put != (size_t) src.st_size || rename(tmp, target) != 0) {
        unlink(tmp);
        return -1;
    }
    return 0;
}

/*
 * Loads path through its retagged copy at target. NULL, with the reason logged, if the copy
 * cannot be written or does not load.
 */
__attribute__((visibility("hidden"))) void *wl_dlopen_android_relocs(void *(*real_dlopen)(const char *, int),
                                                                  const char *path, const char *target, int flags)
{
    if (wl_write_loadable_copy(path, target, 1) != 0) {
        wl_relocs_log("[WESTLAKE-RELOCS] could not write %s\n", target);
        return NULL;
    }
    void *handle = real_dlopen(target, flags);
    if (handle == NULL) {
        const char *error = dlerror();
        wl_relocs_log("[WESTLAKE-RELOCS] %s: its retagged copy did not load: %s\n", path, error != NULL ? error : "?");
        return NULL;
    }
    wl_relocs_log("[WESTLAKE-RELOCS] %s: loaded with DT_ANDROID_RELR renumbered to DT_RELR\n", path);
    return handle;
}

/* Calls fn with each DT_NEEDED name of the ELF file at path (bare names only). */
static void wl_for_each_needed(const char *path, void (*fn)(const char *name, void *ctx), void *ctx)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    struct stat st;
    if (fd < 0) return;
    if (fstat(fd, &st) != 0 || st.st_size < (off_t) sizeof(Elf64_Ehdr)) { close(fd); return; }
    unsigned char *data = mmap(NULL, (size_t) st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (data == MAP_FAILED) return;
    const Elf64_Ehdr *eh = (const Elf64_Ehdr *) data;
    const Elf64_Phdr *ph = (const Elf64_Phdr *) (data + eh->e_phoff);
    const Elf64_Dyn *dyn = NULL;
    size_t count = 0;
    uint64_t strtab = 0, stroff = 0, strsz = 0;
    if (memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0 || eh->e_ident[EI_CLASS] != ELFCLASS64 ||
        eh->e_phoff + (uint64_t) eh->e_phnum * sizeof(Elf64_Phdr) > (uint64_t) st.st_size) goto done;
    for (int i = 0; i < eh->e_phnum; ++i) {
        if (ph[i].p_type == PT_DYNAMIC && ph[i].p_offset + ph[i].p_filesz <= (uint64_t) st.st_size) {
            dyn = (const Elf64_Dyn *) (data + ph[i].p_offset);
            count = ph[i].p_filesz / sizeof(Elf64_Dyn);
        }
    }
    for (size_t k = 0; dyn != NULL && k < count && dyn[k].d_tag != DT_NULL; ++k) {
        if (dyn[k].d_tag == DT_STRTAB) strtab = dyn[k].d_un.d_ptr;
        if (dyn[k].d_tag == DT_STRSZ) strsz = dyn[k].d_un.d_val;
    }
    for (int i = 0; strtab != 0 && i < eh->e_phnum; ++i) {
        if (ph[i].p_type == PT_LOAD && ph[i].p_vaddr <= strtab && strtab < ph[i].p_vaddr + ph[i].p_filesz) {
            stroff = ph[i].p_offset + (strtab - ph[i].p_vaddr);
        }
    }
    if (stroff == 0 || stroff + strsz > (uint64_t) st.st_size) goto done;
    for (size_t k = 0; k < count && dyn[k].d_tag != DT_NULL; ++k) {
        if (dyn[k].d_tag != DT_NEEDED || dyn[k].d_un.d_val >= strsz) continue;
        const char *name = (const char *) data + stroff + dyn[k].d_un.d_val;
        if (strchr(name, '/') != NULL || strnlen(name, strsz - dyn[k].d_un.d_val) >= 256) continue;
        fn(name, ctx);
    }
done:
    munmap(data, (size_t) st.st_size);
}

static void wl_copy_apk_needed_from_apk(const char *name, void *ctx);
static void wl_copy_apk_one(const char *name, void *ctx);

struct wl_apk_needed {
    const char *sibling_dir;    /* the library's own directory, whose siblings are copied anyway */
    int sibling_dir_len;
    const char *apk_lib_dir;
    const char *dir;
    int (*shared)(const char *name);
    int depth;
};

static void wl_copy_apk_one(const char *name, void *ctx)
{
    struct wl_apk_needed *c = (struct wl_apk_needed *) ctx;
    char sibling[1024], packaged[1024], target[1024];
    snprintf(sibling, sizeof(sibling), "%.*s/%s", c->sibling_dir_len, c->sibling_dir, name);
    snprintf(packaged, sizeof(packaged), "%s/%s", c->apk_lib_dir, name);
    if (access(sibling, R_OK) == 0 || access(packaged, R_OK) != 0 || c->shared(name)) return;
    snprintf(target, sizeof(target), "%s/%s", c->dir, name);
    if (access(target, R_OK) == 0) return;   /* copied already, with what it needs */
    int rc = wl_write_loadable_copy(packaged, target, wl_needs_android_relocs(packaged));
    wl_relocs_log("[WESTLAKE-LOADER] %s: the APK's copy beside it in %s: %s\n", name, c->dir,
                  rc == 0 ? "copied" : "failed");
    if (rc == 0 && c->depth < 6) {
        struct wl_apk_needed next = *c;
        next.depth = c->depth + 1;
        wl_for_each_needed(packaged, wl_copy_apk_needed_from_apk, &next);
    }
}

/* A dependency of an APK library being copied: everything beside that library is in the APK
 * directory, so the "beside" test (which would skip all of them) is left out. */
static void wl_copy_apk_needed_from_apk(const char *name, void *ctx)
{
    struct wl_apk_needed *c = (struct wl_apk_needed *) ctx;
    struct wl_apk_needed self = *c;
    self.sibling_dir = "/nonexistent";
    self.sibling_dir_len = (int) strlen(self.sibling_dir);
    wl_copy_apk_one(name, &self);
}

/*
 * Copies into dir each DT_NEEDED library of path, transitively, that is not beside it, that the
 * APK packages, and that the written-library namespace does not share with the default namespace
 * (shared(name) says which it does: those already loaded there). An APK library not yet loaded,
 * found by name through the default namespace, came from the default path, which reaches
 * /system/lib64 before the APK directory: Messenger's libearlystartup.so, the first library it
 * loads, needs libc++_shared.so and libbreakpad.so, and both ended up bound to OH's libc++
 * (std::__h, not std::__ndk1). Android loads each from the app's library path.
 */
__attribute__((visibility("hidden"))) void wl_copy_apk_needed(const char *path, const char *apk_lib_dir,
                                                              const char *dir, int (*shared)(const char *name))
{
    const char *slash = strrchr(path, '/');
    if (slash == NULL || apk_lib_dir == NULL || dir == NULL || shared == NULL) return;
    struct wl_apk_needed ctx = { path, (int) (slash - path), apk_lib_dir, dir, shared, 0 };
    wl_for_each_needed(path, wl_copy_apk_one, &ctx);
}

/*
 * Copies path's DT_NEEDED siblings (files of those names beside it) into dir, retagged where they
 * have DT_ANDROID_RELR, following their own DT_NEEDED a few levels down, so a loader searching dir
 * by name finds each one.
 */
__attribute__((visibility("hidden"))) void wl_copy_needed_siblings(const char *path, const char *dir, int depth)
{
    const char *slash = strrchr(path, '/');
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    struct stat st;
    if (depth > 6 || slash == NULL || fd < 0) { if (fd >= 0) close(fd); return; }
    if (fstat(fd, &st) != 0 || st.st_size < (off_t) sizeof(Elf64_Ehdr)) { close(fd); return; }
    unsigned char *data = mmap(NULL, (size_t) st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (data == MAP_FAILED) return;
    const Elf64_Ehdr *eh = (const Elf64_Ehdr *) data;
    const Elf64_Phdr *ph = (const Elf64_Phdr *) (data + eh->e_phoff);
    const Elf64_Dyn *dyn = NULL;
    size_t count = 0;
    uint64_t strtab = 0, stroff = 0, strsz = 0;
    if (memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0 || eh->e_ident[EI_CLASS] != ELFCLASS64 ||
        eh->e_phoff + (uint64_t) eh->e_phnum * sizeof(Elf64_Phdr) > (uint64_t) st.st_size) goto done;
    for (int i = 0; i < eh->e_phnum; ++i) {
        if (ph[i].p_type == PT_DYNAMIC && ph[i].p_offset + ph[i].p_filesz <= (uint64_t) st.st_size) {
            dyn = (const Elf64_Dyn *) (data + ph[i].p_offset);
            count = ph[i].p_filesz / sizeof(Elf64_Dyn);
        }
    }
    for (size_t k = 0; dyn != NULL && k < count && dyn[k].d_tag != DT_NULL; ++k) {
        if (dyn[k].d_tag == DT_STRTAB) strtab = dyn[k].d_un.d_ptr;
        if (dyn[k].d_tag == DT_STRSZ) strsz = dyn[k].d_un.d_val;
    }
    for (int i = 0; strtab != 0 && i < eh->e_phnum; ++i) {
        if (ph[i].p_type == PT_LOAD && ph[i].p_vaddr <= strtab && strtab < ph[i].p_vaddr + ph[i].p_filesz) {
            stroff = ph[i].p_offset + (strtab - ph[i].p_vaddr);
        }
    }
    if (stroff == 0 || stroff + strsz > (uint64_t) st.st_size) goto done;
    for (size_t k = 0; k < count && dyn[k].d_tag != DT_NULL; ++k) {
        if (dyn[k].d_tag != DT_NEEDED || dyn[k].d_un.d_val >= strsz) continue;
        const char *name = (const char *) data + stroff + dyn[k].d_un.d_val;
        if (strchr(name, '/') != NULL || strnlen(name, strsz - dyn[k].d_un.d_val) >= 256) continue;
        char sibling[1024], target[1024];
        snprintf(sibling, sizeof(sibling), "%.*s/%s", (int) (slash - path), path, name);
        snprintf(target, sizeof(target), "%s/%s", dir, name);
        if (access(sibling, R_OK) != 0) continue;
        if (wl_write_loadable_copy(sibling, target, wl_needs_android_relocs(sibling)) == 0) {
            wl_copy_needed_siblings(sibling, dir, depth + 1);
        }
    }
done:
    munmap(data, (size_t) st.st_size);
}
