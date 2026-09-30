/*
 * Libraries with DT_ANDROID_RELR, loaded through OH's musl.
 *
 * OH's musl applies DT_ANDROID_RELA (APS2 packed) and standard DT_RELR -- its own system libraries
 * carry both -- but not DT_ANDROID_RELR, the tag lld writes for RELR on Android targets. musl skips
 * it, so every pointer it should fix up keeps its link-time value. Facebook died on its first
 * library: libsuperpack-jni.so's INIT_ARRAY holds 0x2ae2c in the file, RELR is what adds the load
 * base, and the constructor call jumped to 0x2ae2c (SIGSEGV, pc == fault addr). Meta's apps
 * (Facebook, Messenger, Instagram, Threads) ship such libraries throughout.
 *
 * Such a library is loaded from a copy whose dynamic section hides, from musl, the RELR table and
 * the constructors (DT_INIT, DT_INIT_ARRAY). musl maps it and applies everything else; this file
 * then applies the RELR table the way bionic's linker does and runs the constructors, in bionic's
 * order.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <link.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <unistd.h>

#define WL_DT_ANDROID_RELR 0x6fffe000
#define WL_DT_ANDROID_RELRSZ 0x6fffe001
/* An OS-specific tag nothing assigns: musl ignores it, and it keeps the original tag's value. */
#define WL_DT_HIDDEN 0x6000ff00

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

struct wl_hidden {
    uint64_t init, init_array, init_array_size;
    uint64_t relr, relr_size;
};

/* A private, copy-on-write mapping of the file: cheap to read a few headers from, and writable. */
static int read_file(const char *path, unsigned char **data, size_t *size)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    struct stat st;
    if (fd < 0) return -1;
    if (fstat(fd, &st) != 0 || st.st_size < (off_t) sizeof(Elf64_Ehdr)) { close(fd); return -1; }
    void *map = mmap(NULL, (size_t) st.st_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map == MAP_FAILED) return -1;
    *data = map;
    *size = (size_t) st.st_size;
    return 0;
}

static void release_file(unsigned char *data, size_t size)
{
    munmap(data, size);
}

/* The dynamic section of an in-memory ELF64 file, or NULL. */
static Elf64_Dyn *file_dynamic(unsigned char *data, size_t size, size_t *count)
{
    Elf64_Ehdr *eh = (Elf64_Ehdr *) data;
    if (memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0 || eh->e_ident[EI_CLASS] != ELFCLASS64 ||
        eh->e_phoff + (uint64_t) eh->e_phnum * sizeof(Elf64_Phdr) > size) return NULL;
    Elf64_Phdr *ph = (Elf64_Phdr *) (data + eh->e_phoff);
    for (int i = 0; i < eh->e_phnum; ++i) {
        if (ph[i].p_type == PT_DYNAMIC && ph[i].p_offset + ph[i].p_filesz <= size) {
            *count = ph[i].p_filesz / sizeof(Elf64_Dyn);
            return (Elf64_Dyn *) (data + ph[i].p_offset);
        }
    }
    return NULL;
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

/*
 * Writes path's copy with the hidden tags retagged into target, recording their values. The
 * values are read from the original, so an existing copy is rewritten only when it is stale.
 */
static int write_hidden_copy(const char *path, const char *target, struct wl_hidden *h)
{
    unsigned char *data;
    size_t size, count;
    if (read_file(path, &data, &size) != 0) return -1;
    Elf64_Dyn *dyn = file_dynamic(data, size, &count);
    if (dyn == NULL) { release_file(data, size); return -1; }
    memset(h, 0, sizeof(*h));
    for (size_t i = 0; i < count && dyn[i].d_tag != DT_NULL; ++i) {
        uint64_t v = dyn[i].d_un.d_val;
        int hide = 1;
        switch (dyn[i].d_tag) {
        case DT_INIT: h->init = v; break;
        case DT_INIT_ARRAY: h->init_array = v; break;
        case DT_INIT_ARRAYSZ: h->init_array_size = v; break;
        case WL_DT_ANDROID_RELR: h->relr = v; break;
        case WL_DT_ANDROID_RELRSZ: h->relr_size = v; break;
        default: hide = 0;
        }
        if (hide) dyn[i].d_tag = WL_DT_HIDDEN;
    }
    struct stat src, dst;
    int fresh = stat(path, &src) == 0 && stat(target, &dst) == 0 && dst.st_size == src.st_size &&
                dst.st_mtime >= src.st_mtime;
    int rc = 0;
    if (!fresh) {
        char tmp[800];
        snprintf(tmp, sizeof(tmp), "%s.tmp%d", target, (int) getpid());
        int out = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0700);
        size_t put = 0;
        while (out >= 0 && put < size) {
            ssize_t n = write(out, data + put, size - put);
            if (n <= 0) break;
            put += (size_t) n;
        }
        if (out >= 0) close(out);
        if (out < 0 || put != size || rename(tmp, target) != 0) { unlink(tmp); rc = -1; }
    }
    release_file(data, size);
    return rc;
}

struct find_base { const char *path; uintptr_t base; const ElfW(Phdr) *phdr; int phnum; };

static int find_base_cb(struct dl_phdr_info *info, size_t size, void *arg)
{
    (void) size;
    struct find_base *f = arg;
    if (info->dlpi_name != NULL && strcmp(info->dlpi_name, f->path) == 0) {
        f->base = info->dlpi_addr;
        f->phdr = info->dlpi_phdr;
        f->phnum = info->dlpi_phnum;
        return 1;
    }
    return 0;
}

/* Makes every page of the loaded library's writable-or-RELRO ranges writable, or restores them. */
static void set_relro_writable(const struct find_base *f, int writable)
{
    long page = sysconf(_SC_PAGESIZE);
    for (int i = 0; i < f->phnum; ++i) {
        if (f->phdr[i].p_type != PT_GNU_RELRO) continue;
        uintptr_t start = (f->base + f->phdr[i].p_vaddr) & ~(uintptr_t) (page - 1);
        uintptr_t end = (f->base + f->phdr[i].p_vaddr + f->phdr[i].p_memsz + (uintptr_t) page - 1) &
                        ~(uintptr_t) (page - 1);
        mprotect((void *) start, end - start, writable ? PROT_READ | PROT_WRITE : PROT_READ);
    }
}

static void apply_relr(uintptr_t base, const uint64_t *relr, uint64_t size)
{
    uint64_t *where = NULL;
    for (uint64_t i = 0; i < size / sizeof(uint64_t); ++i) {
        uint64_t entry = relr[i];
        if ((entry & 1) == 0) {
            where = (uint64_t *) (base + entry);
            *where++ += base;
        } else {
            for (uint64_t bits = entry >> 1, k = 0; bits != 0; bits >>= 1, ++k) {
                if (bits & 1) where[k] += base;
            }
            where += 63;
        }
    }
}

static pthread_mutex_t g_android_relocs_lock = PTHREAD_MUTEX_INITIALIZER;

/*
 * Loads path through its hidden-tag copy at target (see the file comment). Returns NULL, with
 * dlerror() describing it, if musl cannot load the copy.
 */
__attribute__((visibility("hidden"))) void *wl_dlopen_android_relocs(void *(*real_dlopen)(const char *, int),
                                                                  const char *path, const char *target, int flags)
{
    pthread_mutex_lock(&g_android_relocs_lock);
    /* Loaded before: its relocations are applied (RELR adds, so never twice). */
    void *handle = real_dlopen(target, flags | RTLD_NOLOAD);
    if (handle != NULL) { pthread_mutex_unlock(&g_android_relocs_lock); return handle; }
    struct wl_hidden h;
    if (write_hidden_copy(path, target, &h) != 0) {
        pthread_mutex_unlock(&g_android_relocs_lock);
        wl_relocs_log("[WESTLAKE-RELOCS] could not write %s\n", target);
        return NULL;
    }
    handle = real_dlopen(target, flags);
    if (handle == NULL) {
        const char *error = dlerror();
        pthread_mutex_unlock(&g_android_relocs_lock);
        wl_relocs_log("[WESTLAKE-RELOCS] %s: its copy did not load: %s\n", path, error != NULL ? error : "?");
        return NULL;
    }
    struct find_base f = {target, 0, NULL, 0};
    dl_iterate_phdr(find_base_cb, &f);
    if (f.phdr == NULL) {
        pthread_mutex_unlock(&g_android_relocs_lock);
        wl_relocs_log("[WESTLAKE-RELOCS] %s loaded but not found among the loaded objects\n", target);
        return handle;
    }
    set_relro_writable(&f, 1);
    apply_relr(f.base, (const uint64_t *) (f.base + h.relr), h.relr_size);
    set_relro_writable(&f, 0);
    wl_relocs_log("[WESTLAKE-RELOCS] %s: applied its DT_ANDROID_RELR (%llu bytes) for musl; running its constructors\n",
            path, (unsigned long long) h.relr_size);
    pthread_mutex_unlock(&g_android_relocs_lock);
    /* bionic: DT_INIT, then DT_INIT_ARRAY in order, skipping 0 and -1 entries. */
    typedef void (*Ctor)(int, char **, char **);
    extern char **environ;
    if (h.init != 0) ((Ctor) (f.base + h.init))(0, NULL, environ);
    const uintptr_t *array = (const uintptr_t *) (f.base + h.init_array);
    for (uint64_t i = 0; h.init_array != 0 && i < h.init_array_size / sizeof(uintptr_t); ++i) {
        if (array[i] != 0 && array[i] != (uintptr_t) -1) ((Ctor) array[i])(0, NULL, environ);
    }
    return handle;
}

/*
 * musl loads a DT_NEEDED dependency itself, bypassing dlopen, so a dependency with DT_ANDROID_RELR
 * (Messenger's libc++_shared.so) would be mapped unrelocated. Before path is loaded,
 * each such dependency beside it is loaded through load (the shim's dlopen), which relocates it;
 * musl then finds it loaded under its name. Dependencies are followed a few levels deep.
 */
__attribute__((visibility("hidden"))) void wl_preload_relocated_deps(void *(*load)(const char *, int),
                                                                   const char *path, int flags, int depth)
{
    const char *slash = strrchr(path, '/');
    unsigned char *data;
    size_t size, count;
    if (depth > 4 || slash == NULL || read_file(path, &data, &size) != 0) return;
    Elf64_Dyn *dyn = file_dynamic(data, size, &count);
    Elf64_Ehdr *eh = (Elf64_Ehdr *) data;
    Elf64_Phdr *ph = (Elf64_Phdr *) (data + eh->e_phoff);
    uint64_t strtab = 0, strsz = 0, stroff = 0;
    for (size_t i = 0; dyn != NULL && i < count && dyn[i].d_tag != DT_NULL; ++i) {
        if (dyn[i].d_tag == DT_STRTAB) strtab = dyn[i].d_un.d_ptr;
        if (dyn[i].d_tag == DT_STRSZ) strsz = dyn[i].d_un.d_val;
    }
    for (int i = 0; dyn != NULL && strtab != 0 && i < eh->e_phnum; ++i) {
        if (ph[i].p_type == PT_LOAD && ph[i].p_vaddr <= strtab && strtab < ph[i].p_vaddr + ph[i].p_filesz) {
            stroff = ph[i].p_offset + (strtab - ph[i].p_vaddr);
        }
    }
    for (size_t i = 0; stroff != 0 && i < count && dyn[i].d_tag != DT_NULL; ++i) {
        if (dyn[i].d_tag != DT_NEEDED || dyn[i].d_un.d_val >= strsz || stroff + strsz > size) continue;
        const char *name = (const char *) data + stroff + dyn[i].d_un.d_val;
        if (strchr(name, '/') != NULL || strnlen(name, strsz - dyn[i].d_un.d_val) >= 256) continue;
        char dep[1024];
        snprintf(dep, sizeof(dep), "%.*s/%s", (int) (slash - path), path, name);
        if (access(dep, R_OK) != 0) continue;
        if (wl_needs_android_relocs(dep)) {
            wl_relocs_log("[WESTLAKE-RELOCS] %s needs %s first\n", path, dep);
            load(dep, flags);
        }
        else wl_preload_relocated_deps(load, dep, flags, depth + 1);
    }
    release_file(data, size);
}
