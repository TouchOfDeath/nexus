/* ==============================================================================
 *                       NEXUS NATIVE ELF64 EMITTER (LINUX)
 *           0% C#  -  0% .NET Runtime  -  100% Native x86-64 Machine Code
 *
 * Converts any standalone NEXUS PE32+ executable (app.exe) into a standalone,
 * native Linux ELF64 binary (app.elf) capable of direct kernel execution.
 * Supports optional DWARF v4 debug sections (.debug_info, .debug_abbrev,
 * .debug_line) via `--dwarf` flag (Feature #477).
 * ============================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>

#pragma pack(push, 1)
typedef struct {
    uint8_t  e_ident[16];
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
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} Elf64_Phdr;

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
#pragma pack(pop)

// 607 bytes of native bare-metal x86-64 syscall bridge stubs:
static const uint8_t g_stubs[] = {
    0x48, 0xc7, 0xc6, 0x00, 0xb0, 0x40, 0x00, 0x48, 0xc7, 0xc7, 0x00, 0xc0, 0x40, 0x00, 0x48, 0xc7,
    0xc1, 0x00, 0x10, 0x00, 0x00, 0xfc, 0xf3, 0xa4, 0x48, 0xc7, 0xc6, 0xff, 0xaf, 0x40, 0x00, 0x48,
    0xc7, 0xc7, 0xff, 0xbd, 0x40, 0x00, 0x48, 0xc7, 0xc1, 0x00, 0xae, 0x00, 0x00, 0xfd, 0xf3, 0xa4,
    0xfc, 0x48, 0x8d, 0x05, 0x6b, 0x00, 0x00, 0x00, 0x48, 0x89, 0x04, 0x25, 0x28, 0xc0, 0x40, 0x00,
    0x48, 0x8d, 0x05, 0x85, 0x00, 0x00, 0x00, 0x48, 0x89, 0x04, 0x25, 0x30, 0xc0, 0x40, 0x00, 0x48,
    0x8d, 0x05, 0x53, 0x01, 0x00, 0x00, 0x48, 0x89, 0x04, 0x25, 0x38, 0xc0, 0x40, 0x00, 0x48, 0x8d,
    0x05, 0xd7, 0x00, 0x00, 0x00, 0x48, 0x89, 0x04, 0x25, 0x40, 0xc0, 0x40, 0x00, 0x48, 0x8d, 0x05,
    0x3e, 0x01, 0x00, 0x00, 0x48, 0x89, 0x04, 0x25, 0x48, 0xc0, 0x40, 0x00, 0x48, 0x8d, 0x05, 0x73,
    0x01, 0x00, 0x00, 0x48, 0x89, 0x04, 0x25, 0x50, 0xc0, 0x40, 0x00, 0x48, 0x8d, 0x05, 0x9f, 0x01,
    0x00, 0x00, 0x48, 0x89, 0x04, 0x25, 0x58, 0xc0, 0x40, 0x00, 0x48, 0xc7, 0xc0, 0x00, 0x10, 0x40,
    0x00, 0xff, 0xe0, 0x83, 0xf9, 0xf5, 0x75, 0x06, 0xb8, 0x65, 0x00, 0x00, 0x00, 0xc3, 0x83, 0xf9,
    0xf6, 0x75, 0x06, 0xb8, 0x64, 0x00, 0x00, 0x00, 0xc3, 0x83, 0xf9, 0xf4, 0x75, 0x06, 0xb8, 0x66,
    0x00, 0x00, 0x00, 0xc3, 0x48, 0xc7, 0xc0, 0xff, 0xff, 0xff, 0xff, 0xc3, 0x53, 0x55, 0x57, 0x56,
    0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x41, 0x51, 0x48, 0x89, 0xcb, 0x48, 0x83, 0xfb,
    0x65, 0x75, 0x07, 0x48, 0xc7, 0xc3, 0x01, 0x00, 0x00, 0x00, 0x48, 0x83, 0xfb, 0x64, 0x75, 0x07,
    0x48, 0xc7, 0xc3, 0x00, 0x00, 0x00, 0x00, 0x48, 0x89, 0xdf, 0x48, 0x89, 0xd6, 0x4c, 0x89, 0xc2,
    0xb8, 0x01, 0x00, 0x00, 0x00, 0x0f, 0x05, 0x41, 0x59, 0x4d, 0x85, 0xc9, 0x74, 0x08, 0x48, 0x85,
    0xc0, 0x78, 0x03, 0x41, 0x89, 0x01, 0x48, 0x85, 0xc0, 0x78, 0x12, 0xb8, 0x01, 0x00, 0x00, 0x00,
    0x41, 0x5f, 0x41, 0x5e, 0x41, 0x5d, 0x41, 0x5c, 0x5e, 0x5f, 0x5d, 0x5b, 0xc3, 0x31, 0xc0, 0x41,
    0x5f, 0x41, 0x5e, 0x41, 0x5d, 0x41, 0x5c, 0x5e, 0x5f, 0x5d, 0x5b, 0xc3, 0x53, 0x55, 0x57, 0x56,
    0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x41, 0x51, 0x48, 0x89, 0xcb, 0x48, 0x83, 0xfb,
    0x64, 0x75, 0x07, 0x48, 0xc7, 0xc3, 0x00, 0x00, 0x00, 0x00, 0x48, 0x83, 0xfb, 0x65, 0x75, 0x07,
    0x48, 0xc7, 0xc3, 0x01, 0x00, 0x00, 0x00, 0x48, 0x89, 0xdf, 0x48, 0x89, 0xd6, 0x4c, 0x89, 0xc2,
    0x31, 0xc0, 0x0f, 0x05, 0x41, 0x59, 0x4d, 0x85, 0xc9, 0x74, 0x08, 0x48, 0x85, 0xc0, 0x78, 0x03,
    0x41, 0x89, 0x01, 0x48, 0x85, 0xc0, 0x78, 0x12, 0xb8, 0x01, 0x00, 0x00, 0x00, 0x41, 0x5f, 0x41,
    0x5e, 0x41, 0x5d, 0x41, 0x5c, 0x5e, 0x5f, 0x5d, 0x5b, 0xc3, 0x31, 0xc0, 0x41, 0x5f, 0x41, 0x5e,
    0x41, 0x5d, 0x41, 0x5c, 0x5e, 0x5f, 0x5d, 0x5b, 0xc3, 0x89, 0xcf, 0xb8, 0x3c, 0x00, 0x00, 0x00,
    0x0f, 0x05, 0x53, 0x55, 0x57, 0x56, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x31, 0xff,
    0x48, 0x89, 0xd6, 0xba, 0x07, 0x00, 0x00, 0x00, 0x41, 0xba, 0x22, 0x00, 0x00, 0x00, 0x49, 0xc7,
    0xc0, 0xff, 0xff, 0xff, 0xff, 0x45, 0x31, 0xc9, 0xb8, 0x09, 0x00, 0x00, 0x00, 0x0f, 0x05, 0x48,
    0x3d, 0x01, 0xf0, 0xff, 0xff, 0x76, 0x02, 0x31, 0xc0, 0x41, 0x5f, 0x41, 0x5e, 0x41, 0x5d, 0x41,
    0x5c, 0x5e, 0x5f, 0x5d, 0x5b, 0xc3, 0x53, 0x55, 0x57, 0x56, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56,
    0x41, 0x57, 0x48, 0x89, 0xcf, 0xf7, 0xc2, 0x00, 0x00, 0x00, 0x40, 0x74, 0x0c, 0xbe, 0x41, 0x02,
    0x00, 0x00, 0xba, 0xed, 0x01, 0x00, 0x00, 0xeb, 0x04, 0x31, 0xf6, 0x31, 0xd2, 0xb8, 0x02, 0x00,
    0x00, 0x00, 0x0f, 0x05, 0x41, 0x5f, 0x41, 0x5e, 0x41, 0x5d, 0x41, 0x5c, 0x5e, 0x5f, 0x5d, 0x5b,
    0xc3, 0x53, 0x55, 0x57, 0x56, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xf9,
    0x03, 0x72, 0x0a, 0x48, 0x89, 0xcf, 0xb8, 0x03, 0x00, 0x00, 0x00, 0x0f, 0x05, 0xb8, 0x01, 0x00,
    0x00, 0x00, 0x41, 0x5f, 0x41, 0x5e, 0x41, 0x5d, 0x41, 0x5c, 0x5e, 0x5f, 0x5d, 0x5b, 0xc3,
};

// Minimal DWARF v4 .debug_info, .debug_abbrev, and .debug_line payloads (Feature #477)
static const uint8_t g_dwarf_abbrev[] = {
    0x01, 0x11, 0x00,       // Abbrev 1: DW_TAG_compile_unit, DW_CHILDREN_no
    0x25, 0x08,             // DW_AT_producer, DW_FORM_string
    0x13, 0x0b,             // DW_AT_language, DW_FORM_data1
    0x11, 0x01,             // DW_AT_low_pc, DW_FORM_addr
    0x12, 0x07,             // DW_AT_high_pc, DW_FORM_data8
    0x10, 0x17,             // DW_AT_stmt_list, DW_FORM_sec_offset
    0x00, 0x00, 0x00        // End of abbrev table
};

static const uint8_t g_dwarf_info[] = {
    34, 0, 0, 0,            // unit_length = 34 bytes
    4, 0,                   // version = 4 (DWARF v4)
    0, 0, 0, 0,             // debug_abbrev_offset = 0
    8,                      // address_size = 8
    1,                      // abbrev code = 1 (DW_TAG_compile_unit)
    'N','E','X','C', 0,     // DW_AT_producer = "NEXC"
    0x0c,                   // DW_AT_language = DW_LANG_C99
    0x00, 0x10, 0x40, 0x00, 0, 0, 0, 0, // DW_AT_low_pc = 0x401000
    0x00, 0xd0, 0x00, 0x00, 0, 0, 0, 0, // DW_AT_high_pc = 0xd000
    0, 0, 0, 0              // DW_AT_stmt_list = 0
};

static const uint8_t g_dwarf_line[] = {
    45, 0, 0, 0,            // unit_length = 45 bytes
    4, 0,                   // version = 4
    25, 0, 0, 0,            // header_length = 25
    1,                      // minimum_instruction_length = 1
    1,                      // maximum_operations_per_instruction = 1
    1,                      // default_is_stmt = 1
    (uint8_t)-5,            // line_base = -5
    14,                     // line_range = 14
    13,                     // opcode_base = 13
    0, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 1, // standard_opcode_lengths
    0,                      // include_directories (empty)
    'c','o','d','e','.','n','e','x', 0, 0, 0, 0, // file_names: "code.nex"
    0,                      // end of file_names
    // Line program: DW_LNE_set_address(0x401000), advance line, DW_LNE_end_sequence
    0, 9, 2, 0x00, 0x10, 0x40, 0x00, 0, 0, 0, 0,
    1,                      // DW_LNS_copy
    0, 1, 1                 // DW_LNE_end_sequence
};

static const char g_shstrtab[] = "\0.text\0.debug_abbrev\0.debug_info\0.debug_line\0.shstrtab\0";

int main(int argc, char** argv) {
    int emit_dwarf = 0;
    const char* in_path = NULL;
    const char* out_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--dwarf") == 0 || strcmp(argv[i], "-g") == 0) {
            emit_dwarf = 1;
        } else if (!in_path) {
            in_path = argv[i];
        } else if (!out_path) {
            out_path = argv[i];
        }
    }

    if (!in_path || !out_path) {
        printf("NEXUS Native ELF64 Emitter\n");
        printf("Usage: %s [--dwarf] <input_app.exe> <output_app.elf>\n", argv[0]);
        return 1;
    }

    FILE* fin = fopen(in_path, "rb");
    if (!fin) {
        fprintf(stderr, "[-] Error: Failed to open input binary '%s'\n", in_path);
        return 1;
    }

    fseek(fin, 0, SEEK_END);
    long in_size = ftell(fin);
    fseek(fin, 0, SEEK_SET);

    if (in_size != 49152 && in_size != 245760) {
        fprintf(stderr, "[-] Error: Input binary '%s' has invalid size (%ld bytes)\n", in_path, in_size);
        fclose(fin);
        return 1;
    }
    uint32_t shift = (uint32_t)(in_size - 49152);

    uint8_t* pe_data = (uint8_t*)malloc(in_size);
    if (fread(pe_data, 1, in_size, fin) != (size_t)in_size) {
        fprintf(stderr, "[-] Error: Failed to read input file\n");
        free(pe_data);
        fclose(fin);
        return 1;
    }
    fclose(fin);

    if (pe_data[0] != 'M' || pe_data[1] != 'Z') {
        fprintf(stderr, "[-] Error: Input binary '%s' is not a valid PE executable (missing MZ signature)\n", in_path);
        free(pe_data);
        return 1;
    }

    size_t stubs_len = sizeof(g_stubs);
    size_t stubs_off = 0xE000 + shift;
    size_t load_size = stubs_off + stubs_len;
    size_t extra_dwarf = emit_dwarf ? (sizeof(g_dwarf_abbrev) + sizeof(g_dwarf_info) + sizeof(g_dwarf_line) + sizeof(g_shstrtab) + 6 * sizeof(Elf64_Shdr) + 64) : 0;
    size_t elf_size = load_size + extra_dwarf;
    uint8_t* elf_data = (uint8_t*)calloc(1, elf_size);

    // 1. Copy original binary data
    memcpy(elf_data, pe_data, in_size);
    free(pe_data);

    // 2. Append syscall bridge stubs (patching section offsets when shift > 0)
    memcpy(elf_data + stubs_off, g_stubs, stubs_len);
    if (shift > 0) {
        static const int patch_offs[] = { 3, 10, 27, 34, 41, 60, 75, 90, 105, 120, 135, 150 };
        for (int pi = 0; pi < 12; pi++) {
            uint32_t *p32 = (uint32_t*)(elf_data + stubs_off + patch_offs[pi]);
            *p32 += shift;
        }
    }

    // 3. Construct ELF64 Header at offset 0x0000
    Elf64_Ehdr* ehdr = (Elf64_Ehdr*)elf_data;
    ehdr->e_ident[0] = 0x7F;
    ehdr->e_ident[1] = 'E';
    ehdr->e_ident[2] = 'L';
    ehdr->e_ident[3] = 'F';
    ehdr->e_ident[4] = 2; // 64-bit architecture
    ehdr->e_ident[5] = 1; // Little endian
    ehdr->e_ident[6] = 1; // ELF version 1
    ehdr->e_ident[7] = 0; // System V ABI
    ehdr->e_type = 2;     // ET_EXEC
    ehdr->e_machine = 0x3E; // EM_X86_64
    ehdr->e_version = 1;
    ehdr->e_entry = 0x40E000 + shift; // Entry point: syscall stubs bootstrap
    ehdr->e_phoff = 64;   // Program header immediately follows ELF header
    ehdr->e_shoff = 0;
    ehdr->e_flags = 0;
    ehdr->e_ehsize = sizeof(Elf64_Ehdr);
    ehdr->e_phentsize = sizeof(Elf64_Phdr);
    ehdr->e_phnum = 1;    // Single comprehensive PT_LOAD segment
    ehdr->e_shentsize = sizeof(Elf64_Shdr);
    ehdr->e_shnum = 0;
    ehdr->e_shstrndx = 0;

    // 4. Construct Program Header (PT_LOAD) at offset 0x0040
    Elf64_Phdr* phdr = (Elf64_Phdr*)(elf_data + 64);
    phdr->p_type = 1;     // PT_LOAD
    phdr->p_flags = 7;    // PF_R | PF_W | PF_X
    phdr->p_offset = 0;
    phdr->p_vaddr = 0x400000;
    phdr->p_paddr = 0x400000;
    phdr->p_filesz = load_size;
    phdr->p_memsz = 0x100000; // 1 MB virtual address space (heap & stack headroom)
    phdr->p_align = 0x1000;   // 4 KB page alignment

    // 5. Optional DWARF v4 debug sections (.debug_abbrev, .debug_info, .debug_line)
    if (emit_dwarf) {
        size_t cur = (load_size + 7) & ~(size_t)7;
        size_t off_abbrev = cur;
        memcpy(elf_data + cur, g_dwarf_abbrev, sizeof(g_dwarf_abbrev));
        cur += sizeof(g_dwarf_abbrev);

        size_t off_info = cur;
        memcpy(elf_data + cur, g_dwarf_info, sizeof(g_dwarf_info));
        cur += sizeof(g_dwarf_info);

        size_t off_line = cur;
        memcpy(elf_data + cur, g_dwarf_line, sizeof(g_dwarf_line));
        cur += sizeof(g_dwarf_line);

        size_t off_shstr = cur;
        memcpy(elf_data + cur, g_shstrtab, sizeof(g_shstrtab));
        cur += sizeof(g_shstrtab);

        cur = (cur + 7) & ~(size_t)7;
        size_t shoff = cur;
        Elf64_Shdr* sh = (Elf64_Shdr*)(elf_data + shoff);
        // [0] SHT_NULL
        // [1] .text
        sh[1].sh_name = 1; sh[1].sh_type = 1; sh[1].sh_flags = 6;
        sh[1].sh_addr = 0x401000; sh[1].sh_offset = 0x1000;
        sh[1].sh_size = 0xD000 + shift; sh[1].sh_addralign = 16;
        // [2] .debug_abbrev
        sh[2].sh_name = 7; sh[2].sh_type = 1; sh[2].sh_offset = off_abbrev;
        sh[2].sh_size = sizeof(g_dwarf_abbrev); sh[2].sh_addralign = 1;
        // [3] .debug_info
        sh[3].sh_name = 21; sh[3].sh_type = 1; sh[3].sh_offset = off_info;
        sh[3].sh_size = sizeof(g_dwarf_info); sh[3].sh_addralign = 1;
        // [4] .debug_line
        sh[4].sh_name = 33; sh[4].sh_type = 1; sh[4].sh_offset = off_line;
        sh[4].sh_size = sizeof(g_dwarf_line); sh[4].sh_addralign = 1;
        // [5] .shstrtab
        sh[5].sh_name = 45; sh[5].sh_type = 3; sh[5].sh_offset = off_shstr;
        sh[5].sh_size = sizeof(g_shstrtab); sh[5].sh_addralign = 1;

        ehdr->e_shoff = shoff;
        ehdr->e_shnum = 6;
        ehdr->e_shstrndx = 5;
        elf_size = shoff + 6 * sizeof(Elf64_Shdr);
    }

    FILE* fout = fopen(out_path, "wb");
    if (!fout) {
        fprintf(stderr, "[-] Error: Failed to open output file '%s'\n", out_path);
        free(elf_data);
        return 1;
    }

    if (fwrite(elf_data, 1, elf_size, fout) != elf_size) {
        fprintf(stderr, "[-] Error: Failed to write output file\n");
        free(elf_data);
        fclose(fout);
        return 1;
    }
    fclose(fout);
    free(elf_data);

    // Make output binary executable (chmod 0755)
    chmod(out_path, 0755);

    printf("[+] Standalone native Linux ELF64 binary generated: %s (%zu bytes)\n", out_path, elf_size);
    return 0;
}
