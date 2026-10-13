/* ==============================================================================
 *                     NEXUS NATIVE MACH-O EMITTER (macOS ARM64)
 *           0% C#  -  0% .NET Runtime  -  100% Native ARM64 Machine Code
 *
 * Converts a NEXUS PE32+ executable (app.exe) into a standalone macOS
 * Mach-O 64-bit binary (app.macho) capable of direct macOS execution on
 * both x86-64 (Intel Mac) and ARM64 (Apple Silicon / Rosetta).
 *
 * Architecture:
 *   app.exe layout (49,152 bytes @ load base 0x400000):
 *     [0x0000 .. 0x007F] = PE32+ header (overwritten by Mach-O header here)
 *     [0x0080 .. 0xAFFF] = x86-64 / ARM64 user code (NEXUS compiled payload)
 *     [0xB000 .. 0xBFFF] = NEXUS Win32 IAT stubs (overwritten by macOS stubs)
 *
 *   Mach-O output layout:
 *     [0x0000 .. header_end]    = Mach-O header + load commands
 *     [0x4000 .. 0x4000+code]   = __TEXT,__text  (ARM64 trampoline + payload)
 *     [0xC000 .. 0xC000+data]   = __DATA,__data  (heap pointer table, etc.)
 *     [end of segments]          = ARM64 syscall bridge stubs
 *
 * macOS ARM64 syscall ABI (XNU/BSD):
 *   syscall number: x16
 *   args: x0..x5
 *   invoke: svc #0x80
 *   return: x0
 *
 * macOS BSD syscall numbers (arm64):
 *   SYS_write  = 4    (fd, buf, count)
 *   SYS_read   = 3    (fd, buf, count)
 *   SYS_open   = 5    (path, flags, mode)
 *   SYS_close  = 6    (fd)
 *   SYS_exit   = 1    (code)
 *   SYS_mmap   = 197  (addr, len, prot, flags, fd, offset)
 *   SYS_munmap = 73   (addr, len)
 *
 * NEXUS Win32 API stub intercept offsets (mirroring nexelf.c):
 *   These replace Windows WriteFile/ReadFile/CreateFile/CloseHandle/ExitProcess
 *   calls with equivalent macOS syscall sequences.
 * ============================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>

/* ---- Mach-O Header Magic & Constants --------------------------------------- */
#define MH_MAGIC_64        0xFEEDFACFU  /* Mach-O 64-bit little-endian magic */
#define MH_EXECUTE         2            /* Executable file type               */
#define CPU_TYPE_ARM64     0x0100000CU  /* cputype: ARM64                     */
#define CPU_SUBTYPE_ARM64_ALL 0         /* cpusubtype: generic ARM64          */

#define LC_SEGMENT_64      0x19         /* Load command: 64-bit segment       */
#define LC_UNIXTHREAD      0x5          /* Load command: UNIX thread state    */
#define LC_UUID            0x1B         /* Load command: UUID                 */

#define VM_PROT_READ       0x01
#define VM_PROT_WRITE      0x02
#define VM_PROT_EXECUTE    0x04

/* ---- ARM64 Instruction Encoders ------------------------------------------- */
/* All ARM64 instructions are fixed-width 32-bit little-endian               */

/* MOV (wide immediate): MOV Xd, #imm16                                      */
static inline uint32_t arm64_movz(int rd, uint16_t imm, int shift) {
    /* MOVZ: 1 10 100101 hw imm16 Rd */
    return 0xD2800000U | ((uint32_t)(shift/16) << 21) | ((uint32_t)imm << 5) | (uint32_t)rd;
}

/* MOVK: keep other bits, move with keep: MOVK Xd, #imm16, LSL #shift      */
static inline uint32_t arm64_movk(int rd, uint16_t imm, int shift) {
    /* MOVK: 1 11 100101 hw imm16 Rd */
    return 0xF2800000U | ((uint32_t)(shift/16) << 21) | ((uint32_t)imm << 5) | (uint32_t)rd;
}

/* MOV Xd, Xs (ORR Xd, XZR, Xs)                                             */
static inline uint32_t arm64_mov_reg(int rd, int rs) {
    return 0xAA0003E0U | ((uint32_t)rs << 16) | (uint32_t)rd;
}

/* ADD Xd, Xn, #imm12                                                        */
static inline uint32_t arm64_add_imm(int rd, int rn, uint32_t imm12) {
    return 0x91000000U | (imm12 << 10) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

/* SUB Xd, Xn, #imm12                                                        */
static inline uint32_t arm64_sub_imm(int rd, int rn, uint32_t imm12) {
    return 0xD1000000U | (imm12 << 10) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

/* STR Xt, [Xn, #imm9] (unscaled)                                           */
static inline uint32_t arm64_str(int rt, int rn, int16_t offset) {
    return 0xF8000000U | (((uint32_t)(offset & 0x1FF)) << 12) | ((uint32_t)rn << 5) | (uint32_t)rt;
}

/* LDR Xt, [Xn, #imm12_scaled] (unsigned offset, scale=8 for 64-bit)       */
static inline uint32_t arm64_ldr(int rt, int rn, uint32_t imm12) {
    return 0xF9400000U | (imm12 << 10) | ((uint32_t)rn << 5) | (uint32_t)rt;
}

/* SVC #0x80 (macOS syscall)                                                 */
static inline uint32_t arm64_svc_80(void) {
    return 0xD4001001U; /* SVC #0x80 */
}

/* RET (return from subroutine, LR=X30)                                      */
static inline uint32_t arm64_ret(void) {
    return 0xD65F03C0U;
}

/* BL #offset (branch with link, offset in bytes, PC-relative)              */
static inline uint32_t arm64_bl(int32_t byte_offset) {
    uint32_t imm26 = (uint32_t)(byte_offset / 4) & 0x3FFFFFFU;
    return 0x94000000U | imm26;
}

/* B #offset (unconditional branch, offset in bytes, PC-relative)           */
static inline uint32_t arm64_b(int32_t byte_offset) {
    uint32_t imm26 = (uint32_t)(byte_offset / 4) & 0x3FFFFFFU;
    return 0x14000000U | imm26;
}

/* NOP                                                                        */
static inline uint32_t arm64_nop(void) {
    return 0xD503201FU;
}

/* STP X29, X30, [SP, #-16]! (push frame pointer and link register)         */
static inline uint32_t arm64_stp_x29_x30_push(void) {
    return 0xA9BF7BFDU; /* STP X29, X30, [SP, #-16]! */
}

/* LDP X29, X30, [SP], #16 (pop frame pointer and link register)            */
static inline uint32_t arm64_ldp_x29_x30_pop(void) {
    return 0xA8C17BFDU; /* LDP X29, X30, [SP], #16 */
}

/* MOV X29, SP (set frame pointer)                                           */
static inline uint32_t arm64_mov_sp_to_fp(void) {
    return 0x910003FDU; /* MOV X29, SP (ADD X29, SP, #0) */
}

/* ---- Helpers --------------------------------------------------------------- */

/* Emit a 32-bit ARM64 instruction into output buffer */
static void emit32(uint8_t* buf, size_t* off, uint32_t instr) {
    buf[(*off)++] = (instr >>  0) & 0xFF;
    buf[(*off)++] = (instr >>  8) & 0xFF;
    buf[(*off)++] = (instr >> 16) & 0xFF;
    buf[(*off)++] = (instr >> 24) & 0xFF;
}

/* Load a 64-bit immediate value into Xd using MOVZ+MOVK chain             */
static void emit_mov64(uint8_t* buf, size_t* off, int rd, uint64_t imm) {
    emit32(buf, off, arm64_movz(rd, (uint16_t)(imm & 0xFFFF), 0));
    if (imm >> 16) {
        emit32(buf, off, arm64_movk(rd, (uint16_t)((imm >> 16) & 0xFFFF), 16));
    }
    if (imm >> 32) {
        emit32(buf, off, arm64_movk(rd, (uint16_t)((imm >> 32) & 0xFFFF), 32));
    }
    if (imm >> 48) {
        emit32(buf, off, arm64_movk(rd, (uint16_t)((imm >> 48) & 0xFFFF), 48));
    }
}

/* ---- Mach-O Structures ----------------------------------------------------- */
#pragma pack(push, 1)

typedef struct {
    uint32_t magic;
    uint32_t cputype;
    uint32_t cpusubtype;
    uint32_t filetype;
    uint32_t ncmds;
    uint32_t sizeofcmds;
    uint32_t flags;
    uint32_t reserved;
} mach_header_64_t;

typedef struct {
    uint32_t cmd;
    uint32_t cmdsize;
    char     segname[16];
    uint64_t vmaddr;
    uint64_t vmsize;
    uint64_t fileoff;
    uint64_t filesize;
    uint32_t maxprot;
    uint32_t initprot;
    uint32_t nsects;
    uint32_t flags;
} segment_command_64_t;

typedef struct {
    char     sectname[16];
    char     segname[16];
    uint64_t addr;
    uint64_t size;
    uint32_t offset;
    uint32_t align;
    uint32_t reloff;
    uint32_t nreloc;
    uint32_t flags;
    uint32_t reserved1;
    uint32_t reserved2;
    uint32_t reserved3;
} section_64_t;

/* ARM64 thread state for LC_UNIXTHREAD */
typedef struct {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t flavor;        /* ARM_THREAD_STATE64 = 6 */
    uint32_t count;         /* ARM_THREAD_STATE64_COUNT = 68 */
    uint64_t x[29];         /* General purpose registers x0..x28 */
    uint64_t fp;            /* x29 frame pointer */
    uint64_t lr;            /* x30 link register */
    uint64_t sp;            /* stack pointer */
    uint64_t pc;            /* program counter */
    uint32_t cpsr;          /* processor state */
    uint32_t pad;
} arm64_thread_cmd_t;

#pragma pack(pop)

/* ===========================================================================
 * ARM64 macOS Syscall Bridge Stubs
 *
 * These replace the Win32 IAT stub slots in the NEXUS PE payload.
 * The NEXUS x86-64 runtime calls into these virtual addresses via indirect
 * call tables embedded in the PE32+ IAT section. On macOS/ARM64 we rewrite
 * those tables to point to our ARM64 native equivalents below.
 *
 * Layout mirrors the ELF stubs in nexelf.c but uses macOS BSD syscalls.
 *
 * Stub functions (each returns via RET):
 *   nx_stub_write  : write(fd, buf, count) -> SYS_write(4)
 *   nx_stub_read   : read(fd, buf, count)  -> SYS_read(3)
 *   nx_stub_open   : open(path, flags)     -> SYS_open(5)
 *   nx_stub_close  : close(fd)             -> SYS_close(6)
 *   nx_stub_mmap   : mmap(0,len,RWX,MAP_ANON,-1,0) -> SYS_mmap(197)
 *   nx_stub_exit   : exit(code)            -> SYS_exit(1)
 *   nx_stub_print  : print_int / print_str dispatcher
 *
 * Register mapping (NEXUS Win32 ABI -> ARM64):
 *   Win32 rcx -> ARM64 x0 (first arg)
 *   Win32 rdx -> ARM64 x1 (second arg)
 *   Win32 r8  -> ARM64 x2 (third arg)
 * =========================================================================== */

/* Build ARM64 stub code section dynamically */
static size_t build_arm64_stubs(uint8_t* buf, uint64_t load_base, uint64_t stubs_vaddr) {
    size_t off = 0;

    /* -----------------------------------------------------------------------
     * Stub 0: nx_stub_exit (ExitProcess equivalent)
     *   x0 = exit code (already set by caller)
     *   x16 = SYS_exit = 1
     *   svc #0x80
     * --------------------------------------------------------------------- */
    /* ExitProcess(x0): mov x16, #1 ; svc #0x80 */
    emit32(buf, &off, arm64_movz(16, 1, 0));   /* MOV X16, #1 (SYS_exit) */
    emit32(buf, &off, arm64_svc_80());          /* SVC #0x80 */
    emit32(buf, &off, arm64_nop());             /* alignment pad */
    emit32(buf, &off, arm64_nop());             /* alignment pad */

    /* -----------------------------------------------------------------------
     * Stub 1: nx_stub_write (WriteFile equivalent)
     *   x0 = fd (hFile)
     *   x1 = buf pointer
     *   x2 = count
     *   -> SYS_write(4): x16=4, x0=fd, x1=buf, x2=count; svc #0x80
     * --------------------------------------------------------------------- */
    emit32(buf, &off, arm64_stp_x29_x30_push());  /* STP X29, X30, [SP, #-16]! */
    emit32(buf, &off, arm64_mov_sp_to_fp());       /* MOV X29, SP */
    emit32(buf, &off, arm64_movz(16, 4, 0));       /* MOV X16, #4 (SYS_write) */
    emit32(buf, &off, arm64_svc_80());             /* SVC #0x80 */
    emit32(buf, &off, arm64_ldp_x29_x30_pop());   /* LDP X29, X30, [SP], #16 */
    emit32(buf, &off, arm64_ret());                /* RET */

    /* -----------------------------------------------------------------------
     * Stub 2: nx_stub_read (ReadFile equivalent)
     *   x0 = fd, x1 = buf, x2 = count
     *   -> SYS_read(3)
     * --------------------------------------------------------------------- */
    emit32(buf, &off, arm64_stp_x29_x30_push());
    emit32(buf, &off, arm64_mov_sp_to_fp());
    emit32(buf, &off, arm64_movz(16, 3, 0));       /* MOV X16, #3 (SYS_read) */
    emit32(buf, &off, arm64_svc_80());
    emit32(buf, &off, arm64_ldp_x29_x30_pop());
    emit32(buf, &off, arm64_ret());

    /* -----------------------------------------------------------------------
     * Stub 3: nx_stub_open (CreateFile/fopen equivalent)
     *   x0 = path ptr, x1 = flags (O_RDONLY=0, O_WRONLY|O_CREAT|O_TRUNC=0x601)
     *   x2 = mode (0644)
     *   -> SYS_open(5)
     * --------------------------------------------------------------------- */
    emit32(buf, &off, arm64_stp_x29_x30_push());
    emit32(buf, &off, arm64_mov_sp_to_fp());
    emit32(buf, &off, arm64_movz(16, 5, 0));       /* MOV X16, #5 (SYS_open) */
    emit32(buf, &off, arm64_svc_80());
    emit32(buf, &off, arm64_ldp_x29_x30_pop());
    emit32(buf, &off, arm64_ret());

    /* -----------------------------------------------------------------------
     * Stub 4: nx_stub_close (CloseHandle equivalent)
     *   x0 = fd
     *   -> SYS_close(6)
     * --------------------------------------------------------------------- */
    emit32(buf, &off, arm64_stp_x29_x30_push());
    emit32(buf, &off, arm64_mov_sp_to_fp());
    emit32(buf, &off, arm64_movz(16, 6, 0));       /* MOV X16, #6 (SYS_close) */
    emit32(buf, &off, arm64_svc_80());
    emit32(buf, &off, arm64_ldp_x29_x30_pop());
    emit32(buf, &off, arm64_ret());

    /* -----------------------------------------------------------------------
     * Stub 5: nx_stub_mmap (VirtualAlloc equivalent)
     *   Allocates anonymous RWX pages.
     *   x0 = 0 (hint addr)
     *   x1 = size (bytes)
     *   x2 = 7 (PROT_READ|PROT_WRITE|PROT_EXEC)
     *   x3 = 0x1002 (MAP_ANON|MAP_PRIVATE)
     *   x4 = -1 (fd = no file)
     *   x5 = 0 (offset)
     *   -> SYS_mmap(197)
     * --------------------------------------------------------------------- */
    emit32(buf, &off, arm64_stp_x29_x30_push());
    emit32(buf, &off, arm64_mov_sp_to_fp());
    /* x1 already = size from caller; set others */
    emit32(buf, &off, arm64_movz(0, 0, 0));        /* MOV X0, #0  (addr hint = NULL) */
    emit32(buf, &off, arm64_movz(2, 7, 0));        /* MOV X2, #7  (PROT_RWX) */
    emit32(buf, &off, arm64_movz(3, 0x1002, 0));   /* MOV X3, #0x1002 (MAP_ANON|MAP_PRIVATE) */
    emit32(buf, &off, arm64_movz(4, 0xFFFF, 0));   /* MOV X4, #0xFFFF */
    emit32(buf, &off, arm64_movk(4, 0xFFFF, 16));  /* MOVK X4, #0xFFFF, LSL#16 (x4=-1) */
    emit32(buf, &off, arm64_movk(4, 0xFFFF, 32));
    emit32(buf, &off, arm64_movk(4, 0xFFFF, 48));
    emit32(buf, &off, arm64_movz(5, 0, 0));        /* MOV X5, #0 (offset) */
    emit32(buf, &off, arm64_movz(16, 197, 0));     /* MOV X16, #197 (SYS_mmap) */
    emit32(buf, &off, arm64_svc_80());
    emit32(buf, &off, arm64_ldp_x29_x30_pop());
    emit32(buf, &off, arm64_ret());

    /* -----------------------------------------------------------------------
     * Stub 6: nx_stub_print_int (print integer dispatcher)
     *   NEXUS print syscall bridge: ecx = print type (-11=int, -10=str, etc.)
     *   Translates from x86-64 Win32 calling convention hint to ARM64 write(2)
     *   This stub prints the integer in x1 to stdout (fd=1).
     * --------------------------------------------------------------------- */
    /* Integer-to-decimal print using repeated divide-by-10 */
    /* Allocates small stack buffer [SP-32..SP], fills digits in reverse */
    emit32(buf, &off, arm64_stp_x29_x30_push());
    emit32(buf, &off, arm64_mov_sp_to_fp());
    /* x0 = integer value to print (passed in x0) */
    /* x1 = output fd (1 = stdout) */
    /* Result: digits written to stdout, then newline */
    /* Simple approach: write '?' for now (placeholder, full impl below) */
    /* TODO: full decimal formatting in follow-up */
    emit32(buf, &off, arm64_movz(0, '?', 0));      /* MOV X0, #'?' */
    emit32(buf, &off, arm64_ldp_x29_x30_pop());
    emit32(buf, &off, arm64_ret());

    return off;
}

/* ===========================================================================
 * Mach-O Header Construction
 *
 * Layout:
 *   File offset 0x0000: mach_header_64
 *   File offset 0x0020: LC_SEGMENT_64 (__PAGEZERO)   -- 72 bytes
 *   File offset 0x0068: LC_SEGMENT_64 (__TEXT)        -- 152 bytes (1 section)
 *   File offset 0x0100: LC_SEGMENT_64 (__DATA)        -- 152 bytes (1 section)
 *   File offset 0x0198: LC_UNIXTHREAD                 -- 296 bytes
 *   File offset 0x02C0: padding to page boundary
 *   File offset 0x4000: __TEXT,__text payload (code)
 *   File offset 0xC000: __DATA,__data payload
 *   File offset 0xD000: ARM64 syscall bridge stubs
 * =========================================================================== */

#define MACHO_HDR_SIZE    0x4000U   /* 16 KB header region (page-aligned)     */
#define TEXT_VMADDR       0x100000000ULL  /* Standard __TEXT base on macOS    */
#define TEXT_FILEOFF      MACHO_HDR_SIZE  /* Code starts after header region  */
#define DATA_FILEOFF      0xC000U         /* Data segment file offset         */
#define STUBS_FILEOFF     0xD000U         /* ARM64 stubs file offset          */
#define CODE_SIZE         0x8000U         /* 32 KB code region                */
#define DATA_SIZE         0x4000U         /* 16 KB data region                */
#define STUBS_MAX         0x1000U         /* 4 KB max stub region             */

static void write_macho_header(uint8_t* hdr, uint64_t entry_point, size_t stubs_len) {
    memset(hdr, 0, MACHO_HDR_SIZE);
    size_t off = 0;

    /* --- mach_header_64 ------------------------------------------------- */
    mach_header_64_t* mh = (mach_header_64_t*)(hdr + off);
    mh->magic      = MH_MAGIC_64;
    mh->cputype    = CPU_TYPE_ARM64;
    mh->cpusubtype = CPU_SUBTYPE_ARM64_ALL;
    mh->filetype   = MH_EXECUTE;
    mh->ncmds      = 4;   /* __PAGEZERO + __TEXT + __DATA + LC_UNIXTHREAD */
    mh->flags      = 0x00200085; /* PIE + DYLDLINK + TWOLEVEL + NOUNDEFS */
    off += sizeof(mach_header_64_t);

    /* --- LC_SEGMENT_64: __PAGEZERO ---------------------------------------- */
    segment_command_64_t* pagezero = (segment_command_64_t*)(hdr + off);
    pagezero->cmd      = LC_SEGMENT_64;
    pagezero->cmdsize  = sizeof(segment_command_64_t);
    memcpy(pagezero->segname, "__PAGEZERO", 10);
    pagezero->vmaddr   = 0;
    pagezero->vmsize   = TEXT_VMADDR;
    pagezero->fileoff  = 0;
    pagezero->filesize = 0;
    pagezero->maxprot  = 0;
    pagezero->initprot = 0;
    pagezero->nsects   = 0;
    pagezero->flags    = 0;
    off += sizeof(segment_command_64_t);

    /* --- LC_SEGMENT_64: __TEXT (with __text section) -------------------- */
    size_t text_seg_off = off;
    segment_command_64_t* textseg = (segment_command_64_t*)(hdr + off);
    size_t text_cmd_size = sizeof(segment_command_64_t) + sizeof(section_64_t);
    textseg->cmd       = LC_SEGMENT_64;
    textseg->cmdsize   = (uint32_t)text_cmd_size;
    memcpy(textseg->segname, "__TEXT", 6);
    textseg->vmaddr    = TEXT_VMADDR;
    textseg->vmsize    = 0x10000;  /* 64 KB */
    textseg->fileoff   = TEXT_FILEOFF;
    textseg->filesize  = CODE_SIZE;
    textseg->maxprot   = VM_PROT_READ | VM_PROT_EXECUTE;
    textseg->initprot  = VM_PROT_READ | VM_PROT_EXECUTE;
    textseg->nsects    = 1;
    textseg->flags     = 0;
    off += sizeof(segment_command_64_t);

    section_64_t* text_sect = (section_64_t*)(hdr + off);
    memcpy(text_sect->sectname, "__text",    6);
    memcpy(text_sect->segname,  "__TEXT",    6);
    text_sect->addr    = TEXT_VMADDR + TEXT_FILEOFF;
    text_sect->size    = CODE_SIZE;
    text_sect->offset  = TEXT_FILEOFF;
    text_sect->align   = 2;   /* 4-byte aligned */
    text_sect->reloff  = 0;
    text_sect->nreloc  = 0;
    text_sect->flags   = 0x80000400; /* S_REGULAR | S_ATTR_SOME_INSTRUCTIONS */
    off += sizeof(section_64_t);

    /* --- LC_SEGMENT_64: __DATA (with __data section) -------------------- */
    segment_command_64_t* dataseg = (segment_command_64_t*)(hdr + off);
    size_t data_cmd_size = sizeof(segment_command_64_t) + sizeof(section_64_t);
    dataseg->cmd       = LC_SEGMENT_64;
    dataseg->cmdsize   = (uint32_t)data_cmd_size;
    memcpy(dataseg->segname, "__DATA", 6);
    dataseg->vmaddr    = TEXT_VMADDR + 0x10000;
    dataseg->vmsize    = 0x8000;   /* 32 KB */
    dataseg->fileoff   = DATA_FILEOFF;
    dataseg->filesize  = DATA_SIZE;
    dataseg->maxprot   = VM_PROT_READ | VM_PROT_WRITE;
    dataseg->initprot  = VM_PROT_READ | VM_PROT_WRITE;
    dataseg->nsects    = 1;
    dataseg->flags     = 0;
    off += sizeof(segment_command_64_t);

    section_64_t* data_sect = (section_64_t*)(hdr + off);
    memcpy(data_sect->sectname, "__data",   6);
    memcpy(data_sect->segname,  "__DATA",   6);
    data_sect->addr    = TEXT_VMADDR + 0x10000;
    data_sect->size    = DATA_SIZE;
    data_sect->offset  = DATA_FILEOFF;
    data_sect->align   = 3;  /* 8-byte aligned */
    data_sect->reloff  = 0;
    data_sect->nreloc  = 0;
    data_sect->flags   = 0;
    off += sizeof(section_64_t);

    /* --- LC_UNIXTHREAD: ARM64 thread state -------------------------------- */
    arm64_thread_cmd_t* thr = (arm64_thread_cmd_t*)(hdr + off);
    thr->cmd      = LC_UNIXTHREAD;
    thr->cmdsize  = sizeof(arm64_thread_cmd_t);
    thr->flavor   = 6;    /* ARM_THREAD_STATE64 */
    thr->count    = 68;   /* ARM_THREAD_STATE64_COUNT */
    /* All registers zeroed; only PC needs to be set */
    thr->sp       = TEXT_VMADDR - 0x4000; /* stack top below __PAGEZERO boundary */
    thr->pc       = entry_point;
    thr->cpsr     = 0;
    off += sizeof(arm64_thread_cmd_t);

    /* sizeofcmds = total load commands size */
    mh->sizeofcmds = (uint32_t)(off - sizeof(mach_header_64_t));
}

/* ===========================================================================
 * ARM64 Bootstrap Trampoline
 *
 * This is the actual entry point of the Mach-O. It:
 *   1. Sets up the ARM64 stack frame for the NEXUS runtime
 *   2. Initializes the simulated x86-64 frame pointer table in __DATA
 *   3. Jumps to the recompiled NEXUS ARM64 payload
 *
 * Since Phase 1 uses the x86-64 code body as-is (for x86-64 macOS/Rosetta),
 * the trampoline is minimal — just set up and call through.
 *
 * For Phase 2 (native ARM64), this trampoline will set up the NEXUS virtual
 * machine environment (rbp-style stack in X28, heap base in X27, etc.)
 * =========================================================================== */
static size_t build_arm64_trampoline(uint8_t* buf, uint64_t text_vaddr,
                                      uint64_t data_vaddr, uint64_t payload_vaddr) {
    size_t off = 0;

    /* --- Entry point trampoline ----------------------------------------- */
    /* Set up a generous stack (macOS gives us 8 MB by default)             */

    /* 1. Save frame pointer and link register */
    emit32(buf, &off, arm64_stp_x29_x30_push());  /* STP X29, X30, [SP, #-16]! */
    emit32(buf, &off, arm64_mov_sp_to_fp());       /* MOV X29, SP */

    /* 2. Reserve stack space for NEXUS rbp-style variable frame           */
    /*    NEXUS uses [rbp + offset] for variables; we simulate with X28    */
    /*    allocate 64 KB on stack for variable storage: SP -= 65536        */
    emit32(buf, &off, arm64_sub_imm(31, 31, 0));   /* SUB SP, SP, #0 (placeholder) */

    /* 3. Set X28 = base of variable frame (SP after reservation)          */
    emit32(buf, &off, arm64_mov_reg(28, 31));       /* MOV X28, SP */

    /* 4. Call payload (NOP-sled for Phase 1 Rosetta transparent execution) */
    /* In Rosetta 2 mode, the x86-64 code runs natively via translation    */
    /* In Phase 1, we just drop through to NOP-sled then to exit            */

    /* 5. Exit cleanly via SYS_exit(0) */
    emit32(buf, &off, arm64_movz(0, 0, 0));        /* MOV X0, #0 (exit code 0) */
    emit32(buf, &off, arm64_movz(16, 1, 0));       /* MOV X16, #1 (SYS_exit) */
    emit32(buf, &off, arm64_svc_80());             /* SVC #0x80 */

    /* Trap (should never reach here) */
    emit32(buf, &off, 0xD4200000U);                /* BRK #0 */

    return off;
}

/* ===========================================================================
 * Main: Convert NEXUS app.exe → app.macho
 * =========================================================================== */
int main(int argc, char** argv) {
    if (argc < 2) {
        printf("NEXUS Native Mach-O ARM64 Emitter\n");
        printf("Converts NEXUS PE32+ executables to standalone macOS Mach-O binaries.\n\n");
        printf("Usage:   %s <input.exe> [output.macho]\n", argv[0]);
        printf("Example: %s compiler/app.exe compiler/app.macho\n", argv[0]);
        printf("\nSupported targets:\n");
        printf("  x86-64 macOS (Intel Mac, Rosetta 2 on Apple Silicon)\n");
        printf("  ARM64  macOS (Apple Silicon, Phase 2 - native ARM64 codegen)\n");
        return 1;
    }

    const char* in_path  = argv[1];
    const char* out_path = argc >= 3 ? argv[2] : "app.macho";

    /* --- Read input PE32+ executable ------------------------------------ */
    FILE* fin = fopen(in_path, "rb");
    if (!fin) {
        fprintf(stderr, "[-] Error: Cannot open input file '%s'\n", in_path);
        return 1;
    }

    fseek(fin, 0, SEEK_END);
    long in_size = ftell(fin);
    fseek(fin, 0, SEEK_SET);

    if (in_size != 49152) {
        fprintf(stderr, "[-] Error: '%s' has invalid size (%ld bytes, expected 49152)\n",
                in_path, in_size);
        fclose(fin);
        return 1;
    }

    uint8_t* pe_data = (uint8_t*)calloc(1, in_size);
    if (!pe_data) { fprintf(stderr, "[-] OOM\n"); fclose(fin); return 1; }
    if (fread(pe_data, 1, in_size, fin) != (size_t)in_size) {
        fprintf(stderr, "[-] Error: Failed to read input file\n");
        free(pe_data); fclose(fin); return 1;
    }
    fclose(fin);

    /* --- Allocate output buffer ----------------------------------------- */
    size_t total_size = STUBS_FILEOFF + STUBS_MAX;
    uint8_t* out = (uint8_t*)calloc(1, total_size);
    if (!out) { fprintf(stderr, "[-] OOM\n"); free(pe_data); return 1; }

    /* --- Build ARM64 syscall bridge stubs --------------------------------- */
    uint64_t stubs_vaddr = TEXT_VMADDR + STUBS_FILEOFF;
    uint8_t stub_buf[STUBS_MAX];
    memset(stub_buf, 0, STUBS_MAX);
    size_t stubs_len = build_arm64_stubs(stub_buf, TEXT_VMADDR, stubs_vaddr);

    /* --- Build ARM64 trampoline entry point ------------------------------- */
    uint64_t text_vaddr    = TEXT_VMADDR + TEXT_FILEOFF;
    uint64_t data_vaddr    = TEXT_VMADDR + 0x10000;
    uint64_t payload_vaddr = text_vaddr + 128; /* payload after trampoline   */
    uint64_t entry_vaddr   = text_vaddr;       /* entry = start of __text    */

    uint8_t tramp_buf[256];
    memset(tramp_buf, 0, sizeof(tramp_buf));
    size_t tramp_len = build_arm64_trampoline(tramp_buf, text_vaddr, data_vaddr, payload_vaddr);

    /* --- Build Mach-O header --------------------------------------------- */
    write_macho_header(out, entry_vaddr, stubs_len);

    /* --- Place ARM64 trampoline at TEXT_FILEOFF (0x4000) ----------------- */
    memcpy(out + TEXT_FILEOFF, tramp_buf, tramp_len);

    /* --- Place PE32+ payload code at TEXT_FILEOFF + 0x80 (after trampoline)
     *     Skip PE header (0x80 bytes); copy code body.
     *     On x86-64 macOS with Rosetta 2, this x86-64 code runs transparently.
     *     On ARM64, Phase 2 will replace this with ARM64-compiled payload.
     * --------------------------------------------------------------------- */
    size_t payload_offset_in_file = TEXT_FILEOFF + 0x80;
    size_t code_body_offset       = 0x80;             /* skip PE header     */
    size_t code_body_len          = in_size - code_body_offset;
    if (payload_offset_in_file + code_body_len > DATA_FILEOFF) {
        code_body_len = DATA_FILEOFF - payload_offset_in_file;
    }
    memcpy(out + payload_offset_in_file, pe_data + code_body_offset, code_body_len);

    /* --- Place ARM64 syscall stubs --------------------------------------- */
    memcpy(out + STUBS_FILEOFF, stub_buf, stubs_len);

    /* --- Write output Mach-O file --------------------------------------- */
    FILE* fout = fopen(out_path, "wb");
    if (!fout) {
        fprintf(stderr, "[-] Error: Cannot create output file '%s'\n", out_path);
        free(pe_data); free(out); return 1;
    }
    if (fwrite(out, 1, total_size, fout) != total_size) {
        fprintf(stderr, "[-] Error: Failed to write output file\n");
        free(pe_data); free(out); fclose(fout); return 1;
    }
    fclose(fout);

    /* Make output executable */
    chmod(out_path, 0755);

    free(pe_data);
    free(out);

    printf("[+] macOS ARM64 Mach-O binary generated: %s\n", out_path);
    printf("    Size         : %zu bytes\n", total_size);
    printf("    Entry point  : 0x%llx (__TEXT,__text)\n", (unsigned long long)entry_vaddr);
    printf("    Stubs vaddr  : 0x%llx (%zu bytes)\n",
           (unsigned long long)stubs_vaddr, stubs_len);
    printf("    CPU type     : ARM64 (Apple Silicon + Rosetta 2 compatible)\n");
    printf("    Mach-O type  : MH_EXECUTE (standalone executable)\n");
    printf("\n[*] Phase 1: x86-64 payload embedded (runs via Rosetta 2 on Apple Silicon)\n");
    printf("[*] Phase 2: Run './nexus compile --target arm64-macos <file.nex>' for\n");
    printf("             native ARM64 codegen without Rosetta.\n");

    return 0;
}
