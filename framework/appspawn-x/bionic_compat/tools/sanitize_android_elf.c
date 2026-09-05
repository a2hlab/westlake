/*
 * Sanitize Android ELF metadata that older OH musl loaders interpret
 * differently from Android's dynamic linker.
 *
 * Android skips null and -1 entries in DT_INIT_ARRAY and DT_FINI_ARRAY.  Some
 * APK DSOs append null sentinels after real constructors/destructors, while
 * the older OH loader calls every entry and jumps to address zero.  Preserve
 * the real function prefix and trim only an unrelocated null/-1 suffix.
 */

#include <elf.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char *message) {
    fprintf(stderr, "sanitize_android_elf: %s\n", message);
    exit(1);
}

static size_t vaddr_to_offset(const Elf64_Phdr *phdrs, size_t phnum,
                              Elf64_Addr address, size_t size,
                              size_t file_size) {
    for (size_t i = 0; i < phnum; ++i) {
        const Elf64_Phdr *ph = &phdrs[i];
        if (ph->p_type != PT_LOAD || address < ph->p_vaddr) continue;
        Elf64_Xword delta = address - ph->p_vaddr;
        if (delta > ph->p_filesz || size > ph->p_filesz - delta) continue;
        if (ph->p_offset > file_size || delta > file_size - ph->p_offset ||
            size > file_size - ph->p_offset - delta) {
            continue;
        }
        return (size_t)(ph->p_offset + delta);
    }
    fail("DT_INIT_ARRAY is not backed by a file LOAD segment");
    return 0;
}

static size_t trim_trailing_sentinels(
        unsigned char *data, size_t file_size,
        const Elf64_Phdr *phdrs, size_t phnum,
        Elf64_Addr array_address, Elf64_Dyn *array_size_tag,
        const Elf64_Rela *relas, size_t rela_count, const char *array_name) {
    if (array_address == 0 || array_size_tag == NULL) return 0;

    size_t array_size = (size_t)array_size_tag->d_un.d_val;
    if (array_size == 0) return 0;
    if (array_size % sizeof(Elf64_Addr) != 0) {
        fail("constructor/destructor array size is malformed");
    }

    size_t array_offset = vaddr_to_offset(phdrs, phnum, array_address,
                                          array_size, file_size);
    Elf64_Addr *entries = (Elf64_Addr *)(data + array_offset);
    size_t entry_count = array_size / sizeof(Elf64_Addr);
    size_t retained_count = entry_count;
    while (retained_count > 0 &&
           (entries[retained_count - 1] == 0 ||
            entries[retained_count - 1] == UINT64_MAX)) {
        --retained_count;
    }
    if (retained_count == entry_count) return 0;
    for (size_t i = 0; i < retained_count; ++i) {
        if (entries[i] == 0 || entries[i] == UINT64_MAX) {
            fail("constructor/destructor array has an embedded null/-1 entry");
        }
    }

    Elf64_Addr trimmed_address =
        array_address + retained_count * sizeof(Elf64_Addr);
    for (size_t i = 0; i < rela_count; ++i) {
        if (relas[i].r_offset >= trimmed_address &&
            relas[i].r_offset < array_address + array_size) {
            fail("trailing constructor/destructor sentinel has a relocation");
        }
    }

    array_size_tag->d_un.d_val = retained_count * sizeof(Elf64_Addr);
    size_t removed = entry_count - retained_count;
    printf("trimmed %zu trailing null/-1 %s entr%s\n", removed, array_name,
           removed == 1 ? "y" : "ies");
    return removed;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s INPUT OUTPUT\n", argv[0]);
        return 2;
    }

    FILE *input = fopen(argv[1], "rb");
    if (input == NULL) fail(strerror(errno));
    if (fseek(input, 0, SEEK_END) != 0) fail("cannot seek input");
    long input_size = ftell(input);
    if (input_size < (long)sizeof(Elf64_Ehdr)) fail("input is too small");
    if (fseek(input, 0, SEEK_SET) != 0) fail("cannot rewind input");

    size_t size = (size_t)input_size;
    unsigned char *data = malloc(size);
    if (data == NULL) fail("out of memory");
    if (fread(data, 1, size, input) != size) fail("cannot read input");
    fclose(input);

    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)data;
    if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0 ||
        ehdr->e_ident[EI_CLASS] != ELFCLASS64 ||
        ehdr->e_ident[EI_DATA] != ELFDATA2LSB ||
        ehdr->e_machine != EM_AARCH64) {
        fail("expected a little-endian ELF64 AArch64 shared object");
    }
    if (ehdr->e_phentsize != sizeof(Elf64_Phdr) ||
        ehdr->e_phoff > size ||
        (size_t)ehdr->e_phnum > (size - ehdr->e_phoff) / sizeof(Elf64_Phdr)) {
        fail("invalid program header table");
    }

    Elf64_Phdr *phdrs = (Elf64_Phdr *)(data + ehdr->e_phoff);
    Elf64_Dyn *dynamic = NULL;
    size_t dynamic_count = 0;
    for (size_t i = 0; i < ehdr->e_phnum; ++i) {
        if (phdrs[i].p_type != PT_DYNAMIC) continue;
        if (phdrs[i].p_offset > size ||
            phdrs[i].p_filesz > size - phdrs[i].p_offset ||
            phdrs[i].p_filesz % sizeof(Elf64_Dyn) != 0) {
            fail("invalid PT_DYNAMIC segment");
        }
        dynamic = (Elf64_Dyn *)(data + phdrs[i].p_offset);
        dynamic_count = phdrs[i].p_filesz / sizeof(Elf64_Dyn);
        break;
    }
    if (dynamic == NULL) fail("PT_DYNAMIC is missing");

    Elf64_Dyn *init_array_size_tag = NULL;
    Elf64_Dyn *fini_array_size_tag = NULL;
    Elf64_Addr init_array_address = 0;
    Elf64_Addr fini_array_address = 0;
    Elf64_Addr rela_address = 0;
    Elf64_Xword rela_size = 0;
    for (size_t i = 0; i < dynamic_count && dynamic[i].d_tag != DT_NULL; ++i) {
        if (dynamic[i].d_tag == DT_INIT_ARRAY) {
            init_array_address = dynamic[i].d_un.d_ptr;
        } else if (dynamic[i].d_tag == DT_INIT_ARRAYSZ) {
            init_array_size_tag = &dynamic[i];
        } else if (dynamic[i].d_tag == DT_FINI_ARRAY) {
            fini_array_address = dynamic[i].d_un.d_ptr;
        } else if (dynamic[i].d_tag == DT_FINI_ARRAYSZ) {
            fini_array_size_tag = &dynamic[i];
        } else if (dynamic[i].d_tag == DT_RELA) {
            rela_address = dynamic[i].d_un.d_ptr;
        } else if (dynamic[i].d_tag == DT_RELASZ) {
            rela_size = dynamic[i].d_un.d_val;
        }
    }
    if ((init_array_address == 0 || init_array_size_tag == NULL) &&
        (fini_array_address == 0 || fini_array_size_tag == NULL)) {
        fail("DT_INIT_ARRAY and DT_FINI_ARRAY metadata are missing");
    }

    const Elf64_Rela *relas = NULL;
    size_t rela_count = 0;
    if (rela_address != 0 || rela_size != 0) {
        if (rela_address == 0 || rela_size % sizeof(Elf64_Rela) != 0) {
            fail("malformed RELA table");
        }
        size_t rela_offset = vaddr_to_offset(phdrs, ehdr->e_phnum,
                                             rela_address, rela_size, size);
        relas = (const Elf64_Rela *)(data + rela_offset);
        rela_count = rela_size / sizeof(Elf64_Rela);
    }

    size_t removed = 0;
    removed += trim_trailing_sentinels(
        data, size, phdrs, ehdr->e_phnum, init_array_address,
        init_array_size_tag, relas, rela_count, "DT_INIT_ARRAY");
    removed += trim_trailing_sentinels(
        data, size, phdrs, ehdr->e_phnum, fini_array_address,
        fini_array_size_tag, relas, rela_count, "DT_FINI_ARRAY");
    if (removed == 0) fail("no trailing null/-1 entries found");

    FILE *output = fopen(argv[2], "wb");
    if (output == NULL) fail(strerror(errno));
    if (fwrite(data, 1, size, output) != size || fclose(output) != 0) {
        fail("cannot write output");
    }
    free(data);
    printf("sanitized Android ELF arrays: %s -> %s\n", argv[1], argv[2]);
    return 0;
}
