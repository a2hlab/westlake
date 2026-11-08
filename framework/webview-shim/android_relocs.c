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
#include <errno.h>
#include <elf.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
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
 * IFUNC relocations (R_AARCH64_IRELATIVE). The relocated word is the answer of a resolver function in
 * the library, which bionic calls at load with the CPU's features (hwcap | _IFUNC_ARG_HWCAP, and
 * __ifunc_arg_t). OH's musl applies no such relocation: the load fails with "unsupported relocation
 * type 1032". Edge's libtelclient.so and Yandex Browser's libyandexbrowser.so carry them, in the
 * PLT's table: resolvers that choose a memory-tagging variant of a function when the CPU has MTE.
 *
 * A copy of such a library carries each one as an ordinary RELATIVE relocation to the function its
 * resolver chooses on this CPU, which musl applies. The resolvers run in a child process, with the
 * library's segments mapped at a base of their own and unrelocated: a resolver reads its arguments
 * and the features it derives from them, and a resolver that does more takes only the child down.
 * The copy is written only when every resolver answered inside the library.
 */
#define WL_R_AARCH64_RELATIVE 1027
#define WL_R_AARCH64_IRELATIVE 1032
#define WL_IFUNC_ARG_HWCAP (1ULL << 62)
#define WL_MAX_IFUNCS 256

typedef struct { unsigned long size, hwcap, hwcap2; } wl_ifunc_arg_t;   /* bionic's __ifunc_arg_t */

/* The image's RELA tables, DT_RELA and DT_JMPREL when DT_PLTREL is RELA, as file offsets and sizes. */
static int wl_rela_tables(const unsigned char *data, size_t size, uint64_t offset[2], uint64_t length[2])
{
    const Elf64_Ehdr *eh = (const Elf64_Ehdr *) data;
    if (size < sizeof(*eh) || memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0 || eh->e_ident[EI_CLASS] != ELFCLASS64 ||
        eh->e_machine != EM_AARCH64 || eh->e_phoff + (uint64_t) eh->e_phnum * sizeof(Elf64_Phdr) > size) {
        return 0;
    }
    const Elf64_Phdr *ph = (const Elf64_Phdr *) (data + eh->e_phoff);
    const Elf64_Dyn *dyn = NULL;
    size_t count = 0;
    for (int i = 0; i < eh->e_phnum; ++i) {
        if (ph[i].p_type == PT_DYNAMIC && ph[i].p_offset + ph[i].p_filesz <= size) {
            dyn = (const Elf64_Dyn *) (data + ph[i].p_offset);
            count = ph[i].p_filesz / sizeof(Elf64_Dyn);
        }
    }
    uint64_t rela = 0, rela_size = 0, jmprel = 0, plt_size = 0, plt_kind = DT_RELA;
    for (size_t k = 0; dyn != NULL && k < count && dyn[k].d_tag != DT_NULL; ++k) {
        switch (dyn[k].d_tag) {
        case DT_RELA: rela = dyn[k].d_un.d_ptr; break;
        case DT_RELASZ: rela_size = dyn[k].d_un.d_val; break;
        case DT_JMPREL: jmprel = dyn[k].d_un.d_ptr; break;
        case DT_PLTRELSZ: plt_size = dyn[k].d_un.d_val; break;
        case DT_PLTREL: plt_kind = dyn[k].d_un.d_val; break;
        default: break;
        }
    }
    int tables = 0;
    uint64_t start = rela != 0 ? wl_file_offset(data, size, rela) : 0;
    if (start != 0 && start + rela_size <= size) {
        offset[tables] = start;
        length[tables++] = rela_size;
    }
    start = jmprel != 0 && plt_kind == DT_RELA ? wl_file_offset(data, size, jmprel) : 0;
    if (start != 0 && start + plt_size <= size) {
        offset[tables] = start;
        length[tables++] = plt_size;
    }
    return tables;
}

/* The image's IRELATIVE relocations, at most max of them, into found; returns how many there are. */
static size_t wl_ifunc_relocations(unsigned char *data, size_t size, Elf64_Rela **found, size_t max)
{
    uint64_t offset[2], length[2];
    size_t n = 0;
    int tables = wl_rela_tables(data, size, offset, length);
    for (int t = 0; t < tables; ++t) {
        for (uint64_t at = 0; at + sizeof(Elf64_Rela) <= length[t]; at += sizeof(Elf64_Rela)) {
            Elf64_Rela *r = (Elf64_Rela *) (data + offset[t] + at);
            if (ELF64_R_TYPE(r->r_info) != WL_R_AARCH64_IRELATIVE) continue;
            if (n < max && found != NULL) found[n] = r;
            ++n;
        }
    }
    return n;
}

/* 1 when the library at path has IFUNC relocations, which musl rejects. */
__attribute__((visibility("hidden"))) int wl_needs_ifunc_resolution(const char *path)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    struct stat st;
    void *data = fstat(fd, &st) == 0 && st.st_size > 0
                     ? mmap(NULL, (size_t) st.st_size, PROT_READ, MAP_PRIVATE, fd, 0) : MAP_FAILED;
    close(fd);
    if (data == MAP_FAILED) return 0;
    size_t n = wl_ifunc_relocations((unsigned char *) data, (size_t) st.st_size, NULL, 0);
    munmap(data, (size_t) st.st_size);
    return n != 0;
}

/* The child's work: the image mapped from fd, each resolver called, each answer written to out as an
 * offset into the image (0 for an answer outside it). Exits; never returns. */
static void wl_run_resolvers(int fd, const unsigned char *data, const uint64_t *resolvers, size_t n, int out)
{
    const Elf64_Ehdr *eh = (const Elf64_Ehdr *) data;
    const Elf64_Phdr *ph = (const Elf64_Phdr *) (data + eh->e_phoff);
    uint64_t page = (uint64_t) sysconf(_SC_PAGESIZE), low = UINT64_MAX, high = 0;
    for (int i = 0; i < eh->e_phnum; ++i) {
        if (ph[i].p_type != PT_LOAD) continue;
        if ((ph[i].p_vaddr & ~(page - 1)) < low) low = ph[i].p_vaddr & ~(page - 1);
        if (ph[i].p_vaddr + ph[i].p_memsz > high) high = ph[i].p_vaddr + ph[i].p_memsz;
    }
    if (low == UINT64_MAX) _exit(2);
    high = (high + page - 1) & ~(page - 1);
    unsigned char *base = mmap(NULL, high - low, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) _exit(3);
    unsigned char *image = base - low;   /* where vaddr 0 lies */
    for (int i = 0; i < eh->e_phnum; ++i) {
        if (ph[i].p_type != PT_LOAD) continue;
        int prot = ((ph[i].p_flags & PF_R) ? PROT_READ : 0) | ((ph[i].p_flags & PF_W) ? PROT_WRITE : 0) |
                   ((ph[i].p_flags & PF_X) ? PROT_EXEC : 0);
        uint64_t start = ph[i].p_vaddr & ~(page - 1), file_end = ph[i].p_vaddr + ph[i].p_filesz;
        if (file_end > start && mmap(image + start, file_end - start, prot, MAP_PRIVATE | MAP_FIXED, fd,
                                     (off_t) (ph[i].p_offset & ~(page - 1))) == MAP_FAILED) {
            _exit(4);
        }
        uint64_t page_end = (file_end + page - 1) & ~(page - 1), mem_end = ph[i].p_vaddr + ph[i].p_memsz;
        if (mem_end > file_end && (prot & PROT_WRITE) && page_end > file_end) {
            memset(image + file_end, 0, page_end - file_end);
        }
        if (mem_end > page_end && mmap(image + page_end, ((mem_end + page - 1) & ~(page - 1)) - page_end, prot,
                                       MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS, -1, 0) == MAP_FAILED) {
            _exit(5);
        }
    }
    wl_ifunc_arg_t arg = {sizeof(arg), getauxval(AT_HWCAP), getauxval(AT_HWCAP2)};
    for (size_t i = 0; i < n; ++i) {
        uint64_t (*resolver)(uint64_t, wl_ifunc_arg_t *) =
            (uint64_t (*)(uint64_t, wl_ifunc_arg_t *)) (uintptr_t) (image + resolvers[i]);
        uint64_t answer = resolver(arg.hwcap | WL_IFUNC_ARG_HWCAP, &arg);
        uint64_t offset = answer >= (uint64_t) (uintptr_t) base && answer < (uint64_t) (uintptr_t) (base + (high - low))
                              ? answer - (uint64_t) (uintptr_t) image : 0;
        if (write(out, &offset, sizeof(offset)) != (ssize_t) sizeof(offset)) _exit(6);
    }
    _exit(0);
}

/* Resolves the n IFUNC relocations of the image data (whose file is fd) into RELATIVE ones. 0 on success. */
static int wl_resolve_ifuncs(int fd, unsigned char *data, Elf64_Rela **relocs, size_t n, const char *path)
{
    uint64_t resolvers[WL_MAX_IFUNCS], answers[WL_MAX_IFUNCS];
    for (size_t i = 0; i < n; ++i) resolvers[i] = (uint64_t) relocs[i]->r_addend;
    int channel[2];
    if (pipe2(channel, O_CLOEXEC) != 0) return -1;
    pid_t child = fork();
    if (child < 0) {
        close(channel[0]);
        close(channel[1]);
        return -1;
    }
    if (child == 0) {
        close(channel[0]);
        wl_run_resolvers(fd, data, resolvers, n, channel[1]);
    }
    close(channel[1]);
    size_t got = 0;
    while (got < n * sizeof(uint64_t)) {
        ssize_t r = read(channel[0], (unsigned char *) answers + got, n * sizeof(uint64_t) - got);
        if (r > 0) got += (size_t) r;
        else if (r < 0 && errno == EINTR) continue;
        else break;
    }
    close(channel[0]);
    int status = 0;
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    if (got != n * sizeof(uint64_t) || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        wl_relocs_log("[WESTLAKE-RELOCS] %s: its IFUNC resolvers did not all answer (status 0x%x)\n", path, status);
        return -1;
    }
    for (size_t i = 0; i < n; ++i) {
        if (answers[i] == 0) {
            wl_relocs_log("[WESTLAKE-RELOCS] %s: IFUNC resolver at 0x%llx answered outside the library\n", path,
                          (unsigned long long) resolvers[i]);
            return -1;
        }
    }
    for (size_t i = 0; i < n; ++i) {
        relocs[i]->r_info = ELF64_R_INFO(0, WL_R_AARCH64_RELATIVE);
        relocs[i]->r_addend = (Elf64_Sxword) answers[i];
    }
    wl_relocs_log("[WESTLAKE-RELOCS] %s: %zu IFUNC relocations resolved for this CPU\n", path, n);
    return 0;
}

/*
 * Section headers OH's loader reads. bionic and upstream musl load a library from its program headers
 * and dynamic section alone; OH's musl refuses one without section headers ("Invalid argument") and
 * takes the dynamic string table through the dynamic symbol section's link. Baidu's location SDK
 * (liblocSDK8b.so, in Haochang) ships its section headers scrambled on purpose: section 0 marked
 * DYNAMIC, and the symbol section with no size or entry size, linked to it. On the board its load
 * faulted inside the loader (strlen of a DT_NEEDED name at 0x125c, the table's base unset); a copy
 * whose section 0 is null and whose symbol section is the one DT_SYMTAB names, linked to the string
 * table DT_STRTAB names, loaded.
 *
 * A section 0 that is not null is not itself the fault. Packers commonly leave a minimal table whose
 * section 0 is the symbol section, linked to the string table after it (17 libraries in 8 of 982
 * apps, among them Edge's, iQIYI's and Hang Seng's security libraries and Wizardry's il2cpp): OH's
 * loader follows that, and three of those apps draw. What selects a library for repair is a symbol
 * section not linked to a string table: Baidu's, and an obfuscated libloader.so in two other apps
 * whose symbol section is linked to the hash table, both of which crash on the main thread at start.
 */

/* Whether a dynamic symbol section is not linked to a string table as OH's loader expects. */
static int wl_bad_section_headers(const unsigned char *data, size_t size)
{
    const Elf64_Ehdr *eh = (const Elf64_Ehdr *) data;
    if (size < sizeof(*eh) || memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0 || eh->e_ident[EI_CLASS] != ELFCLASS64 ||
        eh->e_machine != EM_AARCH64 || eh->e_shnum < 3 || eh->e_shentsize != sizeof(Elf64_Shdr) ||
        eh->e_shoff == 0 || eh->e_shoff + (uint64_t) eh->e_shnum * sizeof(Elf64_Shdr) > size) {
        return 0;
    }
    const Elf64_Shdr *sh = (const Elf64_Shdr *) (data + eh->e_shoff);
    for (int i = 0; i < eh->e_shnum; ++i) {
        if (sh[i].sh_type != SHT_DYNSYM) continue;
        if (sh[i].sh_link >= eh->e_shnum || sh[sh[i].sh_link].sh_type != SHT_STRTAB ||
            sh[i].sh_entsize != sizeof(Elf64_Sym)) {
            return 1;
        }
    }
    return 0;
}

/* The number of dynamic symbols, from DT_HASH, else DT_GNU_HASH; 0 when neither can tell. */
static uint64_t wl_dynamic_symbol_count(const unsigned char *data, size_t size, uint64_t hash, uint64_t gnu_hash)
{
    uint64_t at = hash != 0 ? wl_file_offset(data, size, hash) : 0;
    if (at != 0 && at + 8 <= size) return *(const uint32_t *) (data + at + 4);   /* nchain */
    at = gnu_hash != 0 ? wl_file_offset(data, size, gnu_hash) : 0;
    if (at == 0 || at + 16 > size) return 0;
    const uint32_t *header = (const uint32_t *) (data + at);
    uint32_t buckets = header[0], offset = header[1], bloom = header[2];
    uint64_t bucket_at = at + 16 + (uint64_t) bloom * 8, chain_at = bucket_at + (uint64_t) buckets * 4;
    if (chain_at > size) return 0;
    uint32_t last = 0;
    for (uint32_t b = 0; b < buckets; ++b) {
        uint32_t index = *(const uint32_t *) (data + bucket_at + 4 * (uint64_t) b);
        if (index > last) last = index;
    }
    if (last < offset) return offset;
    while (chain_at + 4 * (uint64_t) (last - offset) + 4 <= size &&
           !(*(const uint32_t *) (data + chain_at + 4 * (uint64_t) (last - offset)) & 1)) {
        ++last;
    }
    return (uint64_t) last + 1;
}

/* Rewrites scrambled section headers in their own slots: section 0 null, then the dynamic symbol table
 * and its string table where the dynamic section puts them, the section-name table left in its slot,
 * and every other slot null. */
static void wl_repair_section_headers(unsigned char *data, size_t size)
{
    Elf64_Ehdr *eh = (Elf64_Ehdr *) data;
    const Elf64_Phdr *ph = (const Elf64_Phdr *) (data + eh->e_phoff);
    const Elf64_Dyn *dyn = NULL;
    size_t count = 0;
    for (int i = 0; i < eh->e_phnum; ++i) {
        if (ph[i].p_type == PT_DYNAMIC && ph[i].p_offset + ph[i].p_filesz <= size) {
            dyn = (const Elf64_Dyn *) (data + ph[i].p_offset);
            count = ph[i].p_filesz / sizeof(Elf64_Dyn);
        }
    }
    uint64_t symtab = 0, strtab = 0, strsz = 0, hash = 0, gnu_hash = 0;
    for (size_t k = 0; dyn != NULL && k < count && dyn[k].d_tag != DT_NULL; ++k) {
        switch (dyn[k].d_tag) {
        case DT_SYMTAB: symtab = dyn[k].d_un.d_ptr; break;
        case DT_STRTAB: strtab = dyn[k].d_un.d_ptr; break;
        case DT_STRSZ: strsz = dyn[k].d_un.d_val; break;
        case DT_HASH: hash = dyn[k].d_un.d_ptr; break;
        case DT_GNU_HASH: gnu_hash = dyn[k].d_un.d_ptr; break;
        default: break;
        }
    }
    uint64_t symbols = wl_dynamic_symbol_count(data, size, hash, gnu_hash);
    if (symbols == 0 && strtab > symtab) symbols = (strtab - symtab) / sizeof(Elf64_Sym);
    uint64_t sym_at = symtab != 0 ? wl_file_offset(data, size, symtab) : 0;
    uint64_t str_at = strtab != 0 ? wl_file_offset(data, size, strtab) : 0;
    if (sym_at == 0 || str_at == 0 || symbols == 0 || str_at + strsz > size) return;
    Elf64_Shdr *sh = (Elf64_Shdr *) (data + eh->e_shoff);
    int names = eh->e_shstrndx < eh->e_shnum ? eh->e_shstrndx : -1;
    int sym_slot = -1, str_slot = -1;
    for (int i = 1; i < eh->e_shnum; ++i) {
        if (i == names) continue;
        if (sym_slot < 0) sym_slot = i;
        else if (str_slot < 0) str_slot = i;
    }
    if (sym_slot < 0 || str_slot < 0) return;
    Elf64_Word sym_name = sh[sym_slot].sh_name, str_name = sh[str_slot].sh_name;
    for (int i = 0; i < eh->e_shnum; ++i) {
        if (i != names) memset(&sh[i], 0, sizeof(sh[i]));
    }
    sh[sym_slot] = (Elf64_Shdr) {sym_name, SHT_DYNSYM, SHF_ALLOC, symtab, sym_at, symbols * sizeof(Elf64_Sym),
                                 (Elf64_Word) str_slot, 1, 8, sizeof(Elf64_Sym)};
    sh[str_slot] = (Elf64_Shdr) {str_name, SHT_STRTAB, SHF_ALLOC, strtab, str_at, strsz, 0, 0, 1, 0};
    if (names < 0) eh->e_shstrndx = SHN_UNDEF;
}

/* 1 when the library at path has scrambled section headers, which OH's loader trips on. */
__attribute__((visibility("hidden"))) int wl_needs_section_repair(const char *path)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    struct stat st;
    void *data = fstat(fd, &st) == 0 && st.st_size > 0
                     ? mmap(NULL, (size_t) st.st_size, PROT_READ, MAP_PRIVATE, fd, 0) : MAP_FAILED;
    close(fd);
    if (data == MAP_FAILED) return 0;
    int bad = wl_bad_section_headers((const unsigned char *) data, (size_t) st.st_size);
    munmap(data, (size_t) st.st_size);
    return bad;
}

/*
 * Writes path's copy into target, with the Android RELR tags renumbered to the standard ones when
 * retag is set, null init and fini entries dropped, IFUNC relocations resolved and scrambled section
 * headers repaired. An existing copy as large and as new as path is kept.
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
    if (data == MAP_FAILED) {
        close(fd);
        return -1;
    }
    Elf64_Rela *ifuncs[WL_MAX_IFUNCS];
    size_t ifunc_count = wl_ifunc_relocations(data, (size_t) src.st_size, ifuncs, WL_MAX_IFUNCS);
    int resolved = ifunc_count == 0 ? 0
                   : ifunc_count > WL_MAX_IFUNCS ? -1 : wl_resolve_ifuncs(fd, data, ifuncs, ifunc_count, path);
    close(fd);
    if (resolved != 0) {
        munmap(data, (size_t) src.st_size);
        return -1;
    }
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
    if (wl_bad_section_headers(data, (size_t) src.st_size)) {
        wl_repair_section_headers(data, (size_t) src.st_size);
        wl_relocs_log("[WESTLAKE-RELOCS] %s: scrambled section headers rewritten for the loader\n", path);
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
