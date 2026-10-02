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

/*
 * Writes path's copy into target, with the Android RELR tags renumbered to the standard ones when
 * retag is set. An existing copy as large and as new as path is kept.
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
