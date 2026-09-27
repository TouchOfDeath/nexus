/* ==============================================================================
 *                     NEXUS NATIVE ARM64 COMPILER (macOS / Linux)
 *              0% C# - 0% .NET Runtime - 100% Native ARM64 Machine Code
 *
 * Compiles preprocessed NEXUS source code (.nex) directly into pure native
 * 32-bit ARM64 machine code, packaging it into a standalone Mach-O 64-bit
 * executable for Apple Silicon (macOS) or ELF64 for Linux aarch64.
 *
 * Architecture:
 *   Registers:
 *     x28: Variable table pointer (16 KB heap-backed variable memory)
 *     x0 : Primary accumulator / expression result / return value
 *     x1 : Secondary operand / memory address / syscall arg 1
 *     x2..x5: Syscall arguments / scratch temporaries
 *     x16: macOS BSD Syscall number (macOS ABI)
 *     x8 : Linux aarch64 Syscall number (Linux ABI)
 *     x29: Frame Pointer (FP)
 *     x30: Link Register (LR)
 *     sp : Hardware call stack (16-byte aligned)
 *
 * Calling Convention:
 *   Pure ARM64 with standard macOS and Linux syscall dispatch.
 * ============================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdarg.h>

#define MAX_VARS       2048
#define MAX_LABELS     4096
#define MAX_STRINGS    1024
#define MAX_FUNCS      512
#define MAX_BLOCK_DEPTH 128

typedef enum {
    TARGET_MACOS_ARM64 = 0,
    TARGET_LINUX_AARCH64 = 1
} TargetPlatform;

typedef struct {
    char name[64];
    int slot;
} Variable;

typedef struct {
    char name[64];
    int is_defined;
} Function;

typedef struct {
    char* text;
    int len;
    int id;
} StringLit;

typedef struct {
    int type; /* 1 = while, 2 = if, 3 = for */
    int id;
    int root_id;
    int has_else;
} BlockFrame;

static TargetPlatform g_target = TARGET_MACOS_ARM64;
static Variable g_vars[MAX_VARS];
static int g_var_count = 0;
static Function g_funcs[MAX_FUNCS];
static int g_func_count = 0;
static StringLit g_strings[MAX_STRINGS];
static int g_string_count = 0;

static BlockFrame g_block_stack[MAX_BLOCK_DEPTH];
static int g_block_depth = 0;
static int g_label_seq = 0;

/* Output assembly buffer */
static FILE* g_out = NULL;

static int resolve_var(const char* name) {
    for (int i = 0; i < g_var_count; i++) {
        if (strcmp(g_vars[i].name, name) == 0) {
            return g_vars[i].slot;
        }
    }
    if (g_var_count >= MAX_VARS) {
        fprintf(stderr, "[-] Error: Maximum variable limit exceeded (%d)\n", MAX_VARS);
        exit(1);
    }
    strncpy(g_vars[g_var_count].name, name, 63);
    g_vars[g_var_count].name[63] = '\0';
    g_vars[g_var_count].slot = g_var_count;
    return g_var_count++;
}

static int add_string(const char* str, int len) {
    char* buf = (char*)malloc(len + 1);
    int ulen = 0;
    for (int i = 0; i < len; i++) {
        if (str[i] == '\\' && i + 1 < len) {
            i++;
            switch (str[i]) {
                case 'n': buf[ulen++] = '\n'; break;
                case 'r': buf[ulen++] = '\r'; break;
                case 't': buf[ulen++] = '\t'; break;
                case '0': buf[ulen++] = '\0'; break;
                case '\\': buf[ulen++] = '\\'; break;
                case '\"': buf[ulen++] = '\"'; break;
                default:
                    buf[ulen++] = '\\';
                    buf[ulen++] = str[i];
                    break;
            }
        } else {
            buf[ulen++] = str[i];
        }
    }
    buf[ulen] = '\0';

    for (int i = 0; i < g_string_count; i++) {
        if (g_strings[i].len == ulen && memcmp(g_strings[i].text, buf, ulen) == 0) {
            free(buf);
            return g_strings[i].id;
        }
    }
    if (g_string_count >= MAX_STRINGS) {
        fprintf(stderr, "[-] Error: Maximum string literal limit exceeded\n");
        exit(1);
    }
    g_strings[g_string_count].text = buf;
    g_strings[g_string_count].len = ulen;
    g_strings[g_string_count].id = g_string_count;
    return g_string_count++;
}


static int register_func(const char* name) {
    for (int i = 0; i < g_func_count; i++) {
        if (strcmp(g_funcs[i].name, name) == 0) {
            return i;
        }
    }
    if (g_func_count >= MAX_FUNCS) {
        fprintf(stderr, "[-] Error: Maximum function limit exceeded\n");
        exit(1);
    }
    strncpy(g_funcs[g_func_count].name, name, 63);
    g_funcs[g_func_count].name[63] = '\0';
    g_funcs[g_func_count].is_defined = 0;
    return g_func_count++;
}

/* ---- ARM64 Assembly Emission Helpers --------------------------------------- */

static void emit_comment(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(g_out, "    // ");
    vfprintf(g_out, fmt, ap);
    fprintf(g_out, "\n");
    va_end(ap);
}

static void emit_inst(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(g_out, "    ");
    vfprintf(g_out, fmt, ap);
    fprintf(g_out, "\n");
    va_end(ap);
}

static void emit_label(const char* name) {
    fprintf(g_out, "%s:\n", name);
}

static void emit_mov_imm64(const char* reg, int64_t val) {
    uint64_t u = (uint64_t)val;
    uint16_t w0 = (uint16_t)(u & 0xFFFF);
    uint16_t w1 = (uint16_t)((u >> 16) & 0xFFFF);
    uint16_t w2 = (uint16_t)((u >> 32) & 0xFFFF);
    uint16_t w3 = (uint16_t)((u >> 48) & 0xFFFF);

    if (val >= 0 && val <= 65535) {
        emit_inst("mov %s, #%lld", reg, (long long)val);
        return;
    }
    emit_inst("movz %s, #0x%x, lsl #0", reg, w0);
    if (w1) emit_inst("movk %s, #0x%x, lsl #16", reg, w1);
    if (w2) emit_inst("movk %s, #0x%x, lsl #32", reg, w2);
    if (w3) emit_inst("movk %s, #0x%x, lsl #48", reg, w3);
}

static void emit_load_var(const char* reg, int slot) {
    int offset = slot * 8;
    if (offset < 4096) {
        emit_inst("ldr %s, [x28, #%d]", reg, offset);
    } else {
        emit_mov_imm64("x15", offset);
        emit_inst("ldr %s, [x28, x15]", reg);
    }
}

static void emit_store_var(const char* reg, int slot) {
    int offset = slot * 8;
    if (offset < 4096) {
        emit_inst("str %s, [x28, #%d]", reg, offset);
    } else {
        emit_mov_imm64("x15", offset);
        emit_inst("str %s, [x28, x15]", reg);
    }
}

/* ---- Runtime Syscall Bridges ----------------------------------------------- */

static void emit_runtime_stubs(void) {
    /* 1. _nx_print_int: writes 64-bit integer in x0 to stdout as ASCII decimal + newline */
    emit_label("_nx_print_int");
    emit_inst("stp x29, x30, [sp, #-48]!");
    emit_inst("mov x29, sp");
    emit_inst("str x19, [sp, #16]");
    emit_inst("str x20, [sp, #24]");
    emit_inst("str x21, [sp, #32]");

    /* Buffer is at [sp - 32], we use [sp + 40] down for reverse formatting */
    emit_inst("add x19, sp, #47");       /* pointer to end of buffer */
    emit_inst("mov w20, #10");          /* newline char */
    emit_inst("strb w20, [x19]");       /* store newline at end */
    emit_inst("mov x21, #1");           /* string length = 1 (the newline) */

    /* Handle zero specifically */
    emit_inst("cmp x0, #0");
    emit_inst("b.ne 1f");
    emit_inst("sub x19, x19, #1");
    emit_inst("mov w20, #48");          /* '0' */
    emit_inst("strb w20, [x19]");
    emit_inst("add x21, x21, #1");
    emit_inst("b 4f");

    /* Handle sign */
    emit_label("1");
    emit_inst("mov x20, #0");           /* sign flag */
    emit_inst("cmp x0, #0");
    emit_inst("b.ge 2f");
    emit_inst("mov x20, #1");           /* negative */
    emit_inst("neg x0, x0");            /* abs(x0) */

    /* Digits extraction loop */
    emit_label("2");
    emit_inst("mov x10, #10");
    emit_label("3");
    emit_inst("cmp x0, #0");
    emit_inst("b.eq 5f");
    emit_inst("udiv x11, x0, x10");     /* x11 = x0 / 10 */
    emit_inst("msub x12, x11, x10, x0"); /* x12 = x0 % 10 */
    emit_inst("add w12, w12, #48");     /* ASCII digit */
    emit_inst("sub x19, x19, #1");
    emit_inst("strb w12, [x19]");
    emit_inst("add x21, x21, #1");
    emit_inst("mov x0, x11");
    emit_inst("b 3b");

    emit_label("5");
    emit_inst("cmp x20, #1");
    emit_inst("b.ne 4f");
    emit_inst("sub x19, x19, #1");
    emit_inst("mov w20, #45");          /* '-' */
    emit_inst("strb w20, [x19]");
    emit_inst("add x21, x21, #1");

    /* Write to stdout: write(1, x19, x21) */
    emit_label("4");
    emit_inst("mov x0, #1");            /* fd 1 (stdout) */
    emit_inst("mov x1, x19");           /* buf */
    emit_inst("mov x2, x21");           /* len */
    if (g_target == TARGET_MACOS_ARM64) {
        emit_inst("mov x16, #4");       /* SYS_write */
        emit_inst("svc #0x80");
    } else {
        emit_inst("mov x8, #64");       /* sys_write */
        emit_inst("svc #0");
    }

    emit_inst("ldr x21, [sp, #32]");
    emit_inst("ldr x20, [sp, #24]");
    emit_inst("ldr x19, [sp, #16]");
    emit_inst("ldp x29, x30, [sp], #48");
    emit_inst("ret");

    /* 1B. _nx_print_float: formats 64-bit double in d0 as ASCII <int>.<6_frac_digits>\n */
    emit_label("_nx_print_float");
    emit_inst("stp x29, x30, [sp, #-96]!");
    emit_inst("mov x29, sp");
    emit_inst("str x19, [sp, #16]");
    emit_inst("str x20, [sp, #24]");
    emit_inst("str x21, [sp, #32]");
    emit_inst("str d8,  [sp, #40]");
    emit_inst("str d9,  [sp, #48]");

    emit_inst("add x19, sp, #56");
    emit_inst("mov x21, #0");

    emit_inst("fcmp d0, #0.0");
    emit_inst("b.ge 1f");
    emit_inst("mov w20, #45");          /* '-' */
    emit_inst("strb w20, [x19, x21]");
    emit_inst("add x21, x21, #1");
    emit_inst("fneg d0, d0");

    emit_label("1");
    emit_inst("fcvtzu x10, d0");
    emit_inst("scvtf d1, x10");
    emit_inst("fsub d8, d0, d1");

    emit_inst("sub sp, sp, #32");
    emit_inst("mov x11, #0");
    emit_inst("cbnz x10, 2f");
    emit_inst("mov w20, #48");          /* '0' */
    emit_inst("strb w20, [sp, x11]");
    emit_inst("add x11, x11, #1");
    emit_inst("b 3f");

    emit_label("2");
    emit_inst("mov x12, #10");
    emit_label("4");
    emit_inst("cbz x10, 3f");
    emit_inst("udiv x13, x10, x12");
    emit_inst("msub x14, x13, x12, x10");
    emit_inst("add w14, w14, #48");
    emit_inst("strb w14, [sp, x11]");
    emit_inst("add x11, x11, #1");
    emit_inst("mov x10, x13");
    emit_inst("b 4b");

    emit_label("3");
    emit_inst("cbz x11, 5f");
    emit_inst("sub x11, x11, #1");
    emit_inst("ldrb w20, [sp, x11]");
    emit_inst("strb w20, [x19, x21]");
    emit_inst("add x21, x21, #1");
    emit_inst("b 3b");

    emit_label("5");
    emit_inst("add sp, sp, #32");

    emit_inst("mov w20, #46");          /* '.' */
    emit_inst("strb w20, [x19, x21]");
    emit_inst("add x21, x21, #1");

    emit_inst("movz x10, #0x4240");
    emit_inst("movk x10, #0xf, lsl #16");  /* 1000000 */
    emit_inst("scvtf d9, x10");
    emit_inst("fmul d8, d8, d9");
    emit_inst("movz x10, #0x0, lsl #0");
    emit_inst("movk x10, #0x3fe0, lsl #48"); /* 0.5 in double */
    emit_inst("fmov d9, x10");
    emit_inst("fadd d8, d8, d9");
    emit_inst("fcvtzu x10, d8");
    emit_inst("movz x11, #0x4240");
    emit_inst("movk x11, #0xf, lsl #16");
    emit_inst("cmp x10, x11");
    emit_inst("b.lo 6f");
    emit_inst("sub x10, x11, #1");

    emit_label("6");
    emit_inst("movz x12, #0x86a0");
    emit_inst("movk x12, #0x1, lsl #16"); /* 100000 */
    emit_inst("udiv x13, x10, x12");
    emit_inst("msub x10, x13, x12, x10");
    emit_inst("add w13, w13, #48");
    emit_inst("strb w13, [x19, x21]");
    emit_inst("add x21, x21, #1");

    emit_inst("mov x12, #10000");
    emit_inst("udiv x13, x10, x12");
    emit_inst("msub x10, x13, x12, x10");
    emit_inst("add w13, w13, #48");
    emit_inst("strb w13, [x19, x21]");
    emit_inst("add x21, x21, #1");

    emit_inst("mov x12, #1000");
    emit_inst("udiv x13, x10, x12");
    emit_inst("msub x10, x13, x12, x10");
    emit_inst("add w13, w13, #48");
    emit_inst("strb w13, [x19, x21]");
    emit_inst("add x21, x21, #1");

    emit_inst("mov x12, #100");
    emit_inst("udiv x13, x10, x12");
    emit_inst("msub x10, x13, x12, x10");
    emit_inst("add w13, w13, #48");
    emit_inst("strb w13, [x19, x21]");
    emit_inst("add x21, x21, #1");

    emit_inst("mov x12, #10");
    emit_inst("udiv x13, x10, x12");
    emit_inst("msub x10, x13, x12, x10");
    emit_inst("add w13, w13, #48");
    emit_inst("strb w13, [x19, x21]");
    emit_inst("add x21, x21, #1");

    emit_inst("add w10, w10, #48");
    emit_inst("strb w10, [x19, x21]");
    emit_inst("add x21, x21, #1");

    emit_inst("mov w20, #10");          /* '\n' */
    emit_inst("strb w20, [x19, x21]");
    emit_inst("add x21, x21, #1");

    /* write(1, x19, x21) */
    emit_inst("mov x0, #1");
    emit_inst("mov x1, x19");
    emit_inst("mov x2, x21");
    if (g_target == TARGET_MACOS_ARM64) {
        emit_inst("mov x16, #4");
        emit_inst("svc #0x80");
    } else {
        emit_inst("mov x8, #64");
        emit_inst("svc #0");
    }

    emit_inst("ldr d9,  [sp, #48]");
    emit_inst("ldr d8,  [sp, #40]");
    emit_inst("ldr x21, [sp, #32]");
    emit_inst("ldr x20, [sp, #24]");
    emit_inst("ldr x19, [sp, #16]");
    emit_inst("ldp x29, x30, [sp], #96");
    emit_inst("ret");

    /* 2. _nx_print_nl: writes single newline to stdout */
    emit_label("_nx_print_nl");
    emit_inst("stp x29, x30, [sp, #-16]!");
    emit_inst("mov w0, #10");
    emit_inst("strb w0, [sp, #-16]!");
    emit_inst("mov x0, #1");
    emit_inst("mov x1, sp");
    emit_inst("mov x2, #1");
    if (g_target == TARGET_MACOS_ARM64) {
        emit_inst("mov x16, #4");
        emit_inst("svc #0x80");
    } else {
        emit_inst("mov x8, #64");
        emit_inst("svc #0");
    }
    emit_inst("add sp, sp, #16");
    emit_inst("ldp x29, x30, [sp], #16");
    emit_inst("ret");

    /* 3. _nx_alloc: mmap anonymous memory (size in x0 -> result in x0) */
    emit_label("_nx_alloc");
    emit_inst("stp x29, x30, [sp, #-16]!");
    emit_inst("mov x1, x0");            /* len */
    emit_inst("mov x0, #0");            /* addr = NULL */
    emit_inst("mov x2, #3");            /* PROT_READ | PROT_WRITE */
    if (g_target == TARGET_MACOS_ARM64) {
        emit_inst("mov x3, #0x1002");   /* MAP_ANON | MAP_PRIVATE */
        emit_inst("mov x4, #-1");       /* fd = -1 */
        emit_inst("mov x5, #0");        /* offset = 0 */
        emit_inst("mov x16, #197");     /* SYS_mmap */
        emit_inst("svc #0x80");
    } else {
        emit_inst("mov x3, #0x22");     /* MAP_ANON | MAP_PRIVATE */
        emit_inst("mov x4, #-1");       /* fd = -1 */
        emit_inst("mov x5, #0");        /* offset = 0 */
        emit_inst("mov x8, #222");      /* sys_mmap */
        emit_inst("svc #0");
    }
    emit_inst("ldp x29, x30, [sp], #16");
    emit_inst("ret");

    /* 4. _nx_exit: exits process with status in x0 */
    emit_label("_nx_exit");
    if (g_target == TARGET_MACOS_ARM64) {
        emit_inst("mov x16, #1");       /* SYS_exit */
        emit_inst("svc #0x80");
    } else {
        emit_inst("mov x8, #93");       /* sys_exit */
        emit_inst("svc #0");
    }

    /* 5. _nx_strlen: returns length of null-terminated string at x0 in x0 */
    emit_label("_nx_strlen");
    emit_inst("mov x1, x0");
    emit_label("L_strlen_loop");
    emit_inst("ldrb w2, [x1]");
    emit_inst("cbz w2, L_strlen_done");
    emit_inst("add x1, x1, #1");
    emit_inst("b L_strlen_loop");
    emit_label("L_strlen_done");
    emit_inst("sub x0, x1, x0");
    emit_inst("ret");

    /* 6. _nx_print_str: prints null-terminated string at x0 + newline */
    emit_label("_nx_print_str");
    emit_inst("stp x29, x30, [sp, #-32]!");
    emit_inst("mov x29, sp");
    emit_inst("str x19, [sp, #16]");
    emit_inst("str x20, [sp, #24]");
    emit_inst("mov x19, x0");
    emit_inst("bl _nx_strlen");
    emit_inst("mov x20, x0");           /* length */
    emit_inst("mov x0, #1");            /* stdout */
    emit_inst("mov x1, x19");           /* buffer */
    emit_inst("mov x2, x20");           /* count */
    if (g_target == TARGET_MACOS_ARM64) {
        emit_inst("mov x16, #4");       /* SYS_write */
        emit_inst("svc #0x80");
    } else {
        emit_inst("mov x8, #64");       /* sys_write */
        emit_inst("svc #0");
    }
    emit_inst("bl _nx_print_nl");
    emit_inst("ldr x20, [sp, #24]");
    emit_inst("ldr x19, [sp, #16]");
    emit_inst("ldp x29, x30, [sp], #32");
    emit_inst("ret");

    /* 7. _nx_read_int: reads signed 64-bit integer from stdin into x0 */
    emit_label("_nx_read_int");
    emit_inst("stp x29, x30, [sp, #-48]!");
    emit_inst("mov x29, sp");
    emit_inst("str x19, [sp, #16]");
    emit_inst("str x20, [sp, #24]");
    emit_inst("str x21, [sp, #32]");
    emit_inst("mov x19, #0");           /* accumulator */
    emit_inst("mov x20, #0");           /* sign: 0 = positive, 1 = negative */
    emit_inst("mov x21, #0");           /* digits counter */

    /* Skip leading whitespace */
    emit_label("L_read_int_ws");
    emit_inst("mov x0, #0");            /* stdin fd 0 */
    emit_inst("add x1, sp, #40");       /* 1-byte read buffer */
    emit_inst("mov x2, #1");
    if (g_target == TARGET_MACOS_ARM64) {
        emit_inst("mov x16, #3");       /* SYS_read */
        emit_inst("svc #0x80");
    } else {
        emit_inst("mov x8, #63");       /* sys_read */
        emit_inst("svc #0");
    }
    emit_inst("cmp x0, #1");
    emit_inst("b.ne L_read_int_done");  /* EOF or error */
    emit_inst("ldrb w1, [sp, #40]");
    emit_inst("cmp w1, #32");           /* ' ' */
    emit_inst("b.eq L_read_int_ws");
    emit_inst("cmp w1, #9");            /* '\t' */
    emit_inst("b.eq L_read_int_ws");
    emit_inst("cmp w1, #10");           /* '\n' */
    emit_inst("b.eq L_read_int_ws");
    emit_inst("cmp w1, #13");           /* '\r' */
    emit_inst("b.eq L_read_int_ws");

    /* Check for leading '-' */
    emit_inst("cmp w1, #45");           /* '-' */
    emit_inst("b.ne L_read_int_digits");
    emit_inst("mov x20, #1");           /* sign = 1 */

    /* Read first char after '-' */
    emit_inst("mov x0, #0");
    emit_inst("add x1, sp, #40");
    emit_inst("mov x2, #1");
    if (g_target == TARGET_MACOS_ARM64) {
        emit_inst("mov x16, #3");       /* SYS_read */
        emit_inst("svc #0x80");
    } else {
        emit_inst("mov x8, #63");       /* sys_read */
        emit_inst("svc #0");
    }
    emit_inst("cmp x0, #1");
    emit_inst("b.ne L_read_int_done");
    emit_inst("ldrb w1, [sp, #40]");

    /* Digits accumulator */
    emit_label("L_read_int_digits");
    emit_inst("cmp w1, #48");           /* '0' */
    emit_inst("b.lo L_read_int_done");
    emit_inst("cmp w1, #57");           /* '9' */
    emit_inst("b.hi L_read_int_done");
    emit_inst("sub w1, w1, #48");
    emit_inst("mov x10, #10");
    emit_inst("mul x19, x19, x10");
    emit_inst("uxtw x1, w1");
    emit_inst("add x19, x19, x1");
    emit_inst("add x21, x21, #1");

    /* Read next character */
    emit_inst("mov x0, #0");
    emit_inst("add x1, sp, #40");
    emit_inst("mov x2, #1");
    if (g_target == TARGET_MACOS_ARM64) {
        emit_inst("mov x16, #3");       /* SYS_read */
        emit_inst("svc #0x80");
    } else {
        emit_inst("mov x8, #63");       /* sys_read */
        emit_inst("svc #0");
    }
    emit_inst("cmp x0, #1");
    emit_inst("b.ne L_read_int_done");
    emit_inst("ldrb w1, [sp, #40]");
    emit_inst("b L_read_int_digits");

    /* Completion */
    emit_label("L_read_int_done");
    emit_inst("cmp x20, #1");
    emit_inst("b.ne L_read_int_pos");
    emit_inst("neg x19, x19");
    emit_label("L_read_int_pos");
    emit_inst("mov x0, x19");
    emit_inst("ldr x21, [sp, #32]");
    emit_inst("ldr x20, [sp, #24]");
    emit_inst("ldr x19, [sp, #16]");
    emit_inst("ldp x29, x30, [sp], #48");
    emit_inst("ret");

    /* 8. _nx_file_open: path in x0 -> fd in x0 */
    emit_label("_nx_file_open");
    if (g_target == TARGET_MACOS_ARM64) {
        emit_inst("mov x1, #0");        /* O_RDONLY = 0 */
        emit_inst("mov x2, #0");        /* mode 0 */
        emit_inst("mov x16, #5");       /* SYS_open */
        emit_inst("svc #0x80");
    } else {
        emit_inst("mov x1, x0");        /* pathname */
        emit_inst("mov x0, #-100");     /* AT_FDCWD = -100 */
        emit_inst("mov x2, #0");        /* O_RDONLY = 0 */
        emit_inst("mov x3, #0");        /* mode 0 */
        emit_inst("mov x8, #56");       /* sys_openat */
        emit_inst("svc #0");
    }
    emit_inst("ret");

    /* 9. _nx_file_create: path in x0 -> fd in x0 */
    emit_label("_nx_file_create");
    if (g_target == TARGET_MACOS_ARM64) {
        emit_inst("mov x1, #0x601");    /* O_CREAT|O_WRONLY|O_TRUNC */
        emit_inst("mov x2, #0x1a4");    /* mode 0644 */
        emit_inst("mov x16, #5");       /* SYS_open */
        emit_inst("svc #0x80");
    } else {
        emit_inst("mov x1, x0");        /* pathname */
        emit_inst("mov x0, #-100");     /* AT_FDCWD = -100 */
        emit_inst("mov x2, #0x241");    /* O_CREAT|O_WRONLY|O_TRUNC */
        emit_inst("mov x3, #0x1a4");    /* mode 0644 */
        emit_inst("mov x8, #56");       /* sys_openat */
        emit_inst("svc #0");
    }
    emit_inst("ret");

    /* 10. _nx_file_read: fd in x0, buf in x1, len in x2 -> bytes read in x0 */
    emit_label("_nx_file_read");
    if (g_target == TARGET_MACOS_ARM64) {
        emit_inst("mov x16, #3");       /* SYS_read */
        emit_inst("svc #0x80");
    } else {
        emit_inst("mov x8, #63");       /* sys_read */
        emit_inst("svc #0");
    }
    emit_inst("ret");

    /* 11. _nx_file_write: fd in x0, buf in x1, len in x2 -> bytes written in x0 */
    emit_label("_nx_file_write");
    if (g_target == TARGET_MACOS_ARM64) {
        emit_inst("mov x16, #4");       /* SYS_write */
        emit_inst("svc #0x80");
    } else {
        emit_inst("mov x8, #64");       /* sys_write */
        emit_inst("svc #0");
    }
    emit_inst("ret");

    /* 12. _nx_file_close: fd in x0 -> status in x0 */
    emit_label("_nx_file_close");
    if (g_target == TARGET_MACOS_ARM64) {
        emit_inst("mov x16, #6");       /* SYS_close */
        emit_inst("svc #0x80");
    } else {
        emit_inst("mov x8, #57");       /* sys_close */
        emit_inst("svc #0");
    }
    emit_inst("ret");
}

/* ---- Lexer & Parser Helpers ------------------------------------------------ */

static const char* skip_ws_and_comma(const char* p);
static const char* skip_whitespace(const char* p) {
    while (*p) {
        if (*p == ' ' || *p == '\t' || *p == '\r') {
            p++;
        } else if (*p == '#') {
            while (*p && *p != '\n') p++;
        } else {
            break;
        }
    }
    return p;
}

static const char* skip_ws_and_comma(const char* p) {
    p = skip_whitespace(p);
    if (*p == ',') {
        p++;
        p = skip_whitespace(p);
    }
    return p;
}


static const char* read_ident(const char* p, char* out, size_t max_len) {
    size_t i = 0;
    while (*p && (isalnum((unsigned char)*p) || *p == '_')) {
        if (i + 1 < max_len) {
            out[i++] = *p;
        }
        p++;
    }
    out[i] = '\0';
    return p;
}

static int parse_expression(const char** pp, const char* dest_reg);

/* Parse a single term (identifier, number, or function call) */
static const char* parse_term(const char* p, const char* reg) {
    p = skip_whitespace(p);
    if (!*p) return p;

    /* Negative number */
    if (*p == '-' && isdigit((unsigned char)p[1])) {
        char* endp;
        long long val = strtoll(p, &endp, 0);
        emit_mov_imm64(reg, val);
        return endp;
    }
    /* Positive number */
    if (isdigit((unsigned char)*p)) {
        char* endp;
        long long val = strtoll(p, &endp, 0);
        emit_mov_imm64(reg, val);
        return endp;
    }
    /* String literal */
    if (*p == '"') {
        p++;
        const char* start = p;
        while (*p && *p != '"') {
            if (*p == '\\' && p[1]) p += 2;
            else p++;
        }
        int slen = (int)(p - start);
        int sid = add_string(start, slen);
        if (*p == '"') p++;
        char lbl[64];
        snprintf(lbl, sizeof(lbl), "L_str_%d", sid);
        if (g_target == TARGET_MACOS_ARM64) {
            emit_inst("adrp %s, %s@PAGE", reg, lbl);
            emit_inst("add %s, %s, %s@PAGEOFF", reg, reg, lbl);
        } else {
            emit_inst("adrp %s, %s", reg, lbl);
            emit_inst("add %s, %s, :lo12:%s", reg, reg, lbl);
        }
        return p;
    }
    /* String or identifier */
    if (isalpha((unsigned char)*p) || *p == '_') {
        char id[64];
        p = read_ident(p, id, sizeof(id));
        p = skip_whitespace(p);

        /* Check for built-ins */
        if (strcmp(id, "alloc") == 0) {
            p = parse_term(p, "x0");
            emit_inst("bl _nx_alloc");
            if (strcmp(reg, "x0") != 0) {
                emit_inst("mov %s, x0", reg);
            }
            return p;
        }
        if (strcmp(id, "abs") == 0) {
            char l_abs[64];
            snprintf(l_abs, sizeof(l_abs), "L_abs_skip_%d", ++g_label_seq);
            p = parse_term(p, "x0");
            emit_inst("cmp x0, #0");
            emit_inst("b.ge %s", l_abs);
            emit_inst("neg x0, x0");
            emit_label(l_abs);
            if (strcmp(reg, "x0") != 0) {
                emit_inst("mov %s, x0", reg);
            }
            return p;
        }
        if (strcmp(id, "len") == 0) {
            p = skip_whitespace(p);
            if (*p == '"') {
                p++;
                const char* start = p;
                while (*p && *p != '"') {
                    if (*p == '\\' && p[1]) p += 2;
                    else p++;
                }
                int slen = (int)(p - start);
                int sid = add_string(start, slen);
                if (*p == '"') p++;
                emit_mov_imm64(reg, g_strings[sid].len);
                return p;
            } else {
                p = parse_term(p, "x0");
                emit_inst("bl _nx_strlen");
                if (strcmp(reg, "x0") != 0) {
                    emit_inst("mov %s, x0", reg);
                }
                return p;
            }
        }
        if (strcmp(id, "os_argc") == 0) {
            int slot = resolve_var("_nx_argc");
            emit_load_var(reg, slot);
            return p;
        }
        if (strcmp(id, "os_argv") == 0) {
            int slot = resolve_var("_nx_argv");
            p = skip_whitespace(p);
            if (*p && *p != '#' && *p != '\n' && *p != '+' && *p != '-' && *p != '*' && *p != '/') {
                p = parse_term(p, "x14");
                emit_inst("lsl x14, x14, #3");
                emit_load_var("x15", slot);
                emit_inst("ldr %s, [x15, x14]", reg);
            } else {
                emit_load_var(reg, slot);
            }
            return p;
        }
        if (strcmp(id, "load") == 0 || strcmp(id, "load64") == 0 ||
            strcmp(id, "load32") == 0 || strcmp(id, "load16") == 0) {
            int width = 8;
            if (strcmp(id, "load") == 0) width = 1;
            else if (strcmp(id, "load16") == 0) width = 2;
            else if (strcmp(id, "load32") == 0) width = 4;
            else if (strcmp(id, "load64") == 0) width = 8;
            p = skip_whitespace(p);
            if (*p == '[') p++;
            /* Parse base pointer */
            p = skip_whitespace(p);
            char base[64];
            p = read_ident(p, base, sizeof(base));
            int bslot = resolve_var(base);
            emit_load_var("x13", bslot);

            p = skip_whitespace(p);
            if (*p == '+') {
                p = skip_whitespace(p + 1);
                /* Can be number or variable */
                if (isdigit((unsigned char)*p)) {
                    char* endp;
                    long long off = strtoll(p, &endp, 0);
                    p = skip_whitespace(endp);
                    if (*p == '*') {
                        p = skip_whitespace(p + 1);
                        long long scale = strtoll(p, &endp, 0);
                        p = skip_whitespace(endp);
                        off *= scale;
                    }
                    emit_mov_imm64("x14", off);
                    emit_inst("add x13, x13, x14");
                } else if (isalpha((unsigned char)*p) || *p == '_') {
                    char vname[64];
                    p = read_ident(p, vname, sizeof(vname));
                    int vslot = resolve_var(vname);
                    emit_load_var("x14", vslot);
                    p = skip_whitespace(p);
                    if (*p == '*') {
                        p = skip_whitespace(p + 1);
                        char* endp;
                        long long scale = strtoll(p, &endp, 0);
                        p = skip_whitespace(endp);
                        if (scale == 8) {
                            emit_inst("lsl x14, x14, #3");
                        } else if (scale == 4) {
                            emit_inst("lsl x14, x14, #2");
                        } else if (scale == 2) {
                            emit_inst("lsl x14, x14, #1");
                        } else {
                            emit_mov_imm64("x15", scale);
                            emit_inst("mul x14, x14, x15");
                        }
                    }
                    emit_inst("add x13, x13, x14");
                }
            }
            p = skip_whitespace(p);
            if (*p == ']') p++;

            if (width == 8) {
                emit_inst("ldr %s, [x13]", reg);
            } else if (width == 4) {
                emit_inst("ldr w14, [x13]");
                emit_inst("uxtw %s, w14", reg);
            } else if (width == 2) {
                emit_inst("ldrh w14, [x13]");
                emit_inst("uxtw %s, w14", reg);
            } else {
                emit_inst("ldrb w14, [x13]");
                emit_inst("uxtw %s, w14", reg);
            }
            return p;
        }

        /* Normal variable read */
        int slot = resolve_var(id);
        emit_load_var(reg, slot);
        return p;
    }
    return p;
}

/* Parse chained binary expression: term (op term)* */
static int parse_expression(const char** pp, const char* dest_reg) {
    const char* p = *pp;
    p = parse_term(p, dest_reg);
    while (1) {
        p = skip_whitespace(p);
        if (*p != '+' && *p != '-' && *p != '*' && *p != '/' && *p != '%' &&
            *p != '&' && *p != '|' && *p != '^') {
            break;
        }
        char op = *p++;
        p = parse_term(p, "x1");
        switch (op) {
            case '+': emit_inst("add %s, %s, x1", dest_reg, dest_reg); break;
            case '-': emit_inst("sub %s, %s, x1", dest_reg, dest_reg); break;
            case '*': emit_inst("mul %s, %s, x1", dest_reg, dest_reg); break;
            case '/': emit_inst("sdiv %s, %s, x1", dest_reg, dest_reg); break;
            case '%':
                emit_inst("sdiv x2, %s, x1", dest_reg);
                emit_inst("msub %s, x2, x1, %s", dest_reg, dest_reg);
                break;
            case '&': emit_inst("and %s, %s, x1", dest_reg, dest_reg); break;
            case '|': emit_inst("orr %s, %s, x1", dest_reg, dest_reg); break;
            case '^': emit_inst("eor %s, %s, x1", dest_reg, dest_reg); break;
        }
    }
    *pp = p;
    return 0;
}

/* Checks if a string contains floating-point operators or float literals */
static int is_float_expr(const char* p) {
    if (strstr(p, "+.") || strstr(p, "-.") || strstr(p, "*.") || strstr(p, "/.") ||
        strstr(p, "==.") || strstr(p, "!=.") || strstr(p, "<.") || strstr(p, ">.") ||
        strstr(p, "<=.") || strstr(p, ">=.") || strstr(p, "fsqrt")) {
        return 1;
    }
    const char* s = p;
    while (*s) {
        if (*s == '.' && isdigit((unsigned char)s[1])) {
            if (s > p && isdigit((unsigned char)s[-1])) {
                return 1;
            }
        }
        s++;
    }
    return 0;
}

static const char* parse_float_expression(const char** pp, const char* dest_freg);

/* Parse a single float term (literal, variable, fsqrt, or parenthesized expr) */
static const char* parse_float_term(const char* p, const char* freg) {
    p = skip_whitespace(p);
    if (!*p) return p;

    /* Handle parentheses: ( <float_expr> ) */
    if (*p == '(') {
        p++;
        p = parse_float_expression(&p, freg);
        p = skip_whitespace(p);
        if (*p == ')') p++;
        return p;
    }

    /* Handle fsqrt <term> */
    if (strncmp(p, "fsqrt ", 6) == 0) {
        p += 6;
        p = parse_float_term(p, freg);
        emit_inst("fsqrt %s, %s", freg, freg);
        return p;
    }

    /* Check for negative sign */
    int is_neg = 0;
    if (*p == '-' && (isdigit((unsigned char)p[1]) || p[1] == '.')) {
        is_neg = 1;
        p++;
    }

    /* Number literal (float like 1.5 or int like 2) */
    if (isdigit((unsigned char)*p) || (*p == '.' && isdigit((unsigned char)p[1]))) {
        char* endp;
        double val = strtod(p, &endp);
        if (is_neg) val = -val;
        uint64_t bits;
        memcpy(&bits, &val, sizeof(bits));
        emit_mov_imm64("x10", bits);
        emit_inst("fmov %s, x10", freg);
        return endp;
    }

    /* Variable or identifier */
    if (isalpha((unsigned char)*p) || *p == '_') {
        char id[64];
        p = read_ident(p, id, sizeof(id));
        p = skip_whitespace(p);

        /* load64 [ptr + ...] for float arrays */
        if (strcmp(id, "load64") == 0) {
            p = skip_whitespace(p);
            if (*p == '[') p++;
            char base[64];
            p = read_ident(p, base, sizeof(base));
            int bslot = resolve_var(base);
            emit_load_var("x13", bslot);
            p = skip_whitespace(p);
            if (*p == '+') {
                p = skip_whitespace(p + 1);
                if (isdigit((unsigned char)*p)) {
                    char* endp;
                    long long off = strtoll(p, &endp, 0);
                    p = skip_whitespace(endp);
                    if (*p == '*') {
                        p = skip_whitespace(p + 1);
                        long long scale = strtoll(p, &endp, 0);
                        p = skip_whitespace(endp);
                        off *= scale;
                    }
                    emit_mov_imm64("x14", off);
                    emit_inst("add x13, x13, x14");
                } else if (isalpha((unsigned char)*p) || *p == '_') {
                    char vname[64];
                    p = read_ident(p, vname, sizeof(vname));
                    int vslot = resolve_var(vname);
                    emit_load_var("x14", vslot);
                    p = skip_whitespace(p);
                    if (*p == '*') {
                        p = skip_whitespace(p + 1);
                        char* endp;
                        long long scale = strtoll(p, &endp, 0);
                        p = skip_whitespace(endp);
                        if (scale == 8) {
                            emit_inst("lsl x14, x14, #3");
                        } else {
                            emit_mov_imm64("x15", scale);
                            emit_inst("mul x14, x14, x15");
                        }
                    }
                    emit_inst("add x13, x13, x14");
                }
            }
            p = skip_whitespace(p);
            if (*p == ']') p++;
            emit_inst("ldr %s, [x13]", freg);
            if (is_neg) {
                emit_inst("fneg %s, %s", freg, freg);
            }
            return p;
        }

        int slot = resolve_var(id);
        emit_inst("ldr %s, [x28, #%d]", freg, slot * 8);
        if (is_neg) {
            emit_inst("fneg %s, %s", freg, freg);
        }
        return p;
    }

    return p;
}

/* Parse chained binary float expression: term ((+.|-.|*.|/.) term)* */
static const char* parse_float_expression(const char** pp, const char* dest_freg) {
    const char* p = *pp;
    p = parse_float_term(p, dest_freg);
    while (1) {
        p = skip_whitespace(p);
        if (strncmp(p, "+.", 2) == 0 || strncmp(p, "-.", 2) == 0 ||
            strncmp(p, "*.", 2) == 0 || strncmp(p, "/.", 2) == 0) {
            char op = p[0];
            p += 2;
            emit_inst("sub sp, sp, #16");
            emit_inst("str %s, [sp]", dest_freg);
            p = parse_float_term(p, "d1");
            emit_inst("ldr %s, [sp]", dest_freg);
            emit_inst("add sp, sp, #16");

            if (op == '+') emit_inst("fadd %s, %s, d1", dest_freg, dest_freg);
            else if (op == '-') emit_inst("fsub %s, %s, d1", dest_freg, dest_freg);
            else if (op == '*') emit_inst("fmul %s, %s, d1", dest_freg, dest_freg);
            else if (op == '/') emit_inst("fdiv %s, %s, d1", dest_freg, dest_freg);
        } else {
            break;
        }
    }
    *pp = p;
    return p;
}

/* Parse condition expression: <lhs> <relop> <rhs> -> sets condition flags */
static const char* parse_condition(const char* p, char* out_relop, int is_f) {
    p = skip_whitespace(p);
    if (is_f || strstr(p, "==.") || strstr(p, "!=.") || strstr(p, "<.") ||
        strstr(p, "<=.") || strstr(p, ">.") || strstr(p, ">=.")) {
        p = parse_float_expression(&p, "d0");
        p = skip_whitespace(p);

        char op[8] = {0};
        int i = 0;
        while (*p == '=' || *p == '!' || *p == '<' || *p == '>' || *p == '.') {
            if (i < 7) op[i++] = *p;
            p++;
        }
        op[i] = '\0';
        strcpy(out_relop, op);

        p = parse_float_expression(&p, "d1");
        emit_inst("fcmp d0, d1");
        return p;
    }

    p = parse_term(p, "x0");
    p = skip_whitespace(p);

    char op[4] = {0};
    int i = 0;
    while (*p == '=' || *p == '!' || *p == '<' || *p == '>') {
        if (i < 3) op[i++] = *p;
        p++;
    }
    op[i] = '\0';
    strcpy(out_relop, op);

    p = parse_term(p, "x1");
    emit_inst("cmp x0, x1");
    return p;
}

/* Emit conditional branch that jumps when condition is FALSE (to skip the block) */
static void emit_branch_false(const char* relop, const char* target_label) {
    if (strcmp(relop, "==") == 0 || strcmp(relop, "==.") == 0)      emit_inst("b.ne %s", target_label);
    else if (strcmp(relop, "!=") == 0 || strcmp(relop, "!=.") == 0) emit_inst("b.eq %s", target_label);
    else if (strcmp(relop, "<") == 0  || strcmp(relop, "<.") == 0)  emit_inst("b.ge %s", target_label);
    else if (strcmp(relop, "<=") == 0 || strcmp(relop, "<=.") == 0) emit_inst("b.gt %s", target_label);
    else if (strcmp(relop, ">") == 0  || strcmp(relop, ">.") == 0)  emit_inst("b.le %s", target_label);
    else if (strcmp(relop, ">=") == 0 || strcmp(relop, ">=.") == 0) emit_inst("b.lt %s", target_label);
    else                                                             emit_inst("b.eq %s", target_label);
}

/* Compile raw kernel syscall: syscall<N> <nr>, <arg1>, <arg2>, ... */
static void compile_syscall(const char* p, int dest_slot) {
    p += 7; /* skip "syscall" */
    int explicit_nargs = -1;
    if (isdigit((unsigned char)*p)) {
        explicit_nargs = *p - '0';
        p++;
    }
    p = skip_ws_and_comma(p);

    /* 1. Parse syscall number into x9 */
    p = parse_term(p, "x9");
    p = skip_ws_and_comma(p);

    /* 2. Parse up to 6 arguments into x10, x11, x12, x13, x14, x15 */
    const char* arg_regs[6] = {"x10", "x11", "x12", "x13", "x14", "x15"};
    int nargs = 0;
    while (*p && *p != '#' && *p != '\n' && nargs < 6) {
        if (explicit_nargs >= 0 && nargs >= explicit_nargs) break;
        p = parse_term(p, arg_regs[nargs++]);
        p = skip_ws_and_comma(p);
    }

    /* Move syscall number into platform syscall register */
    if (g_target == TARGET_MACOS_ARM64) {
        emit_inst("mov x16, x9");
    } else {
        emit_inst("mov x8, x9");
    }

    /* Move arguments into x0..x5 */
    const char* call_regs[6] = {"x0", "x1", "x2", "x3", "x4", "x5"};
    for (int i = 0; i < nargs; i++) {
        emit_inst("mov %s, %s", call_regs[i], arg_regs[i]);
    }

    /* Dispatch */
    if (g_target == TARGET_MACOS_ARM64) {
        emit_inst("svc #0x80");
    } else {
        emit_inst("svc #0");
    }

    if (dest_slot >= 0) {
        emit_store_var("x0", dest_slot);
    }
}

/* ---- Statement Parser ------------------------------------------------------ */

static void compile_line(const char* line) {
    const char* p = skip_whitespace(line);
    if (!*p || *p == '#' || *p == '\n') return;

    /* 1. let <dest> = <expr> */
    if (strncmp(p, "let ", 4) == 0) {
        p += 4;
        p = skip_whitespace(p);
        char dest[64];
        p = read_ident(p, dest, sizeof(dest));
        p = skip_whitespace(p);
        if (*p == '=') p++;
        p = skip_whitespace(p);

        int slot = resolve_var(dest);

        /* Check for built-ins in let RHS */
        if (strncmp(p, "file_open ", 10) == 0 || strncmp(p, "file_open\t", 10) == 0) {
            p += 10;
            p = parse_term(p, "x0");
            emit_inst("bl _nx_file_open");
            emit_store_var("x0", slot);
            return;
        }
        if (strncmp(p, "file_create ", 12) == 0 || strncmp(p, "file_create\t", 12) == 0) {
            p += 12;
            p = parse_term(p, "x0");
            emit_inst("bl _nx_file_create");
            emit_store_var("x0", slot);
            return;
        }
        if (strncmp(p, "file_read ", 10) == 0 || strncmp(p, "file_read\t", 10) == 0) {
            p += 10;
            p = parse_term(p, "x19");
            p = skip_ws_and_comma(p);
            p = parse_term(p, "x1");
            p = skip_ws_and_comma(p);
            p = parse_term(p, "x2");
            emit_inst("mov x0, x19");
            emit_inst("bl _nx_file_read");
            emit_store_var("x0", slot);
            return;
        }
        if (strncmp(p, "file_close ", 11) == 0 || strncmp(p, "file_close\t", 11) == 0) {
            p += 11;
            p = parse_term(p, "x0");
            emit_inst("bl _nx_file_close");
            emit_store_var("x0", slot);
            return;
        }
        if (!strncmp(p, "syscall", 7)) {
            compile_syscall(p, slot);
            return;
        }
        if (!strncmp(p, "fadd ", 5) || !strncmp(p, "fsub ", 5) ||
            !strncmp(p, "fmul ", 5) || !strncmp(p, "fdiv ", 5)) {
            char fop = p[1];
            p += 5;
            p = parse_float_term(p, "d0");
            p = skip_whitespace(p);
            p = parse_float_term(p, "d1");
            if (fop == 'a') emit_inst("fadd d0, d0, d1");
            else if (fop == 's') emit_inst("fsub d0, d0, d1");
            else if (fop == 'm') emit_inst("fmul d0, d0, d1");
            else if (fop == 'd') emit_inst("fdiv d0, d0, d1");
            emit_inst("str d0, [x28, #%d]", slot * 8);
            return;
        }
        if (!strncmp(p, "fsqrt ", 6)) {
            p += 6;
            p = parse_float_term(p, "d0");
            emit_inst("fsqrt d0, d0");
            emit_inst("str d0, [x28, #%d]", slot * 8);
            return;
        }
        if (!strncmp(p, "fneg ", 5)) {
            p += 5;
            p = parse_float_term(p, "d0");
            emit_inst("fneg d0, d0");
            emit_inst("str d0, [x28, #%d]", slot * 8);
            return;
        }
        if (!strncmp(p, "itof ", 5)) {
            p += 5;
            p = parse_term(p, "x0");
            emit_inst("scvtf d0, x0");
            emit_inst("str d0, [x28, #%d]", slot * 8);
            return;
        }
        if (!strncmp(p, "ftoi ", 5)) {
            p += 5;
            p = parse_float_term(p, "d0");
            emit_inst("fcvtzs x0, d0");
            emit_store_var("x0", slot);
            return;
        }
        if (is_float_expr(p)) {
            parse_float_expression(&p, "d0");
            emit_inst("str d0, [x28, #%d]", slot * 8);
        } else {
            parse_expression(&p, "x0");
            emit_store_var("x0", slot);
        }
        return;
    }

    /* 2. store [ptr + idx] val OR store16/store32/store64 [ptr + idx] val */
    if (strncmp(p, "store ", 6) == 0 || strncmp(p, "store64 ", 8) == 0 ||
        strncmp(p, "store32 ", 8) == 0 || strncmp(p, "store16 ", 8) == 0) {
        int width = 1;
        if (strncmp(p, "store64 ", 8) == 0) { width = 8; p += 8; }
        else if (strncmp(p, "store32 ", 8) == 0) { width = 4; p += 8; }
        else if (strncmp(p, "store16 ", 8) == 0) { width = 2; p += 8; }
        else { width = 1; p += 6; }
        p = skip_whitespace(p);
        if (*p == '[') p++;

        char base[64];
        p = read_ident(p, base, sizeof(base));
        int bslot = resolve_var(base);
        emit_load_var("x13", bslot);

        p = skip_whitespace(p);
        if (*p == '+') {
            p = skip_whitespace(p + 1);
            if (isdigit((unsigned char)*p)) {
                char* endp;
                long long off = strtoll(p, &endp, 0);
                p = skip_whitespace(endp);
                if (*p == '*') {
                    p = skip_whitespace(p + 1);
                    long long scale = strtoll(p, &endp, 0);
                    p = skip_whitespace(endp);
                    off *= scale;
                }
                emit_mov_imm64("x14", off);
                emit_inst("add x13, x13, x14");
            } else if (isalpha((unsigned char)*p) || *p == '_') {
                char vname[64];
                p = read_ident(p, vname, sizeof(vname));
                int vslot = resolve_var(vname);
                emit_load_var("x14", vslot);
                p = skip_whitespace(p);
                if (*p == '*') {
                    p = skip_whitespace(p + 1);
                    char* endp;
                    long long scale = strtoll(p, &endp, 0);
                    p = skip_whitespace(endp);
                    if (scale == 8) {
                        emit_inst("lsl x14, x14, #3");
                    } else if (scale == 4) {
                        emit_inst("lsl x14, x14, #2");
                    } else if (scale == 2) {
                        emit_inst("lsl x14, x14, #1");
                    } else {
                        emit_mov_imm64("x15", scale);
                        emit_inst("mul x14, x14, x15");
                    }
                }
                emit_inst("add x13, x13, x14");
            }
        }
        p = skip_whitespace(p);
        if (*p == ']') p++;
        p = skip_whitespace(p);

        /* Value to store */
        parse_expression(&p, "x0");
        if (width == 8) {
            emit_inst("str x0, [x13]");
        } else if (width == 4) {
            emit_inst("str w0, [x13]");
        } else if (width == 2) {
            emit_inst("strh w0, [x13]");
        } else {
            emit_inst("strb w0, [x13]");
        }
        return;
    }

    /* 2B. print_float ... */
    if (strncmp(p, "print_float ", 12) == 0 || strcmp(p, "print_float") == 0) {
        if (strncmp(p, "print_float ", 12) == 0) {
            p += 12;
            p = skip_whitespace(p);
            parse_float_expression(&p, "d0");
            emit_inst("bl _nx_print_float");
            return;
        }
    }

    /* 3. print ... */
    if (strncmp(p, "print ", 6) == 0 || strcmp(p, "print") == 0) {
        if (strncmp(p, "print ", 6) == 0) {
            p += 6;
            p = skip_whitespace(p);
        } else {
            p += 5;
            emit_inst("bl _nx_print_nl");
            return;
        }

        /* String literal: print "..." */
        if (*p == '"') {
            p++;
            const char* start = p;
            while (*p && *p != '"') {
                if (*p == '\\' && p[1]) p += 2;
                else p++;
            }
            int slen = (int)(p - start);
            int sid = add_string(start, slen);
            if (*p == '"') p++;

            char lbl[64];
            snprintf(lbl, sizeof(lbl), "L_str_%d", sid);
            if (g_target == TARGET_MACOS_ARM64) {
                emit_inst("adrp x0, %s@PAGE", lbl);
                emit_inst("add x0, x0, %s@PAGEOFF", lbl);
            } else {
                emit_inst("adrp x0, %s", lbl);
                emit_inst("add x0, x0, :lo12:%s", lbl);
            }
            emit_inst("bl _nx_print_str");
            return;
        }

        /* Expression integer: print <expr> */
        parse_expression(&p, "x0");
        emit_inst("bl _nx_print_int");
        return;
    }

    /* 4. while <cond> { or while_f <cond> { */
    if (strncmp(p, "while ", 6) == 0 || strncmp(p, "while_f ", 8) == 0) {
        int is_f = (strncmp(p, "while_f ", 8) == 0);
        p += is_f ? 8 : 6;
        int id = ++g_label_seq;
        char l_start[64], l_end[64], relop[8];
        snprintf(l_start, sizeof(l_start), "L_while_start_%d", id);
        snprintf(l_end, sizeof(l_end), "L_while_end_%d", id);

        emit_label(l_start);
        p = parse_condition(p, relop, is_f);
        emit_branch_false(relop, l_end);

        /* Push to block stack */
        g_block_stack[g_block_depth].type = 1; /* while */
        g_block_stack[g_block_depth].id = id;
        g_block_stack[g_block_depth].has_else = 0;
        g_block_depth++;
        return;
    }

    /* 5. if <cond> { or if_f <cond> { */
    if (strncmp(p, "if ", 3) == 0 || strncmp(p, "if_f ", 5) == 0) {
        int is_f = (strncmp(p, "if_f ", 5) == 0);
        p += is_f ? 5 : 3;
        int id = ++g_label_seq;
        char l_else[64], relop[8];
        snprintf(l_else, sizeof(l_else), "L_if_else_%d", id);

        p = parse_condition(p, relop, is_f);
        emit_branch_false(relop, l_else);

        /* Push to block stack */
        g_block_stack[g_block_depth].type = 2; /* if */
        g_block_stack[g_block_depth].id = id;
        g_block_stack[g_block_depth].root_id = id;
        g_block_stack[g_block_depth].has_else = 0;
        g_block_depth++;
        return;
    }

    /* 6. } else if <cond> { or } else if_f <cond> { */
    if (strncmp(p, "} else if ", 10) == 0 || strncmp(p, "} else if_f ", 12) == 0) {
        int is_f = (strncmp(p, "} else if_f ", 12) == 0);
        p += is_f ? 12 : 10;
        if (g_block_depth <= 0) return;
        int id = g_block_stack[g_block_depth - 1].id;
        int root_id = g_block_stack[g_block_depth - 1].root_id;
        char l_end[64], l_prev_else[64], l_new_else[64], relop[8];
        snprintf(l_end, sizeof(l_end), "L_if_end_%d", root_id);
        snprintf(l_prev_else, sizeof(l_prev_else), "L_if_else_%d", id);

        int new_id = ++g_label_seq;
        snprintf(l_new_else, sizeof(l_new_else), "L_if_else_%d", new_id);

        emit_inst("b %s", l_end);
        emit_label(l_prev_else);

        p = parse_condition(p, relop, is_f);
        emit_branch_false(relop, l_new_else);

        g_block_stack[g_block_depth - 1].id = new_id;
        return;
    }

    /* 7. } else { */
    if (strncmp(p, "} else {", 8) == 0 || strncmp(p, "} else", 6) == 0) {
        if (g_block_depth <= 0) return;
        int id = g_block_stack[g_block_depth - 1].id;
        int root_id = g_block_stack[g_block_depth - 1].root_id;
        char l_end[64], l_else[64];
        snprintf(l_end, sizeof(l_end), "L_if_end_%d", root_id);
        snprintf(l_else, sizeof(l_else), "L_if_else_%d", id);

        emit_inst("b %s", l_end);
        emit_label(l_else);
        g_block_stack[g_block_depth - 1].has_else = 1;
        return;
    }

    /* 8. } (close block) */
    if (*p == '}') {
        if (g_block_depth <= 0) return;
        g_block_depth--;
        BlockFrame top = g_block_stack[g_block_depth];

        if (top.type == 1) {
            /* while */
            char l_start[64], l_end[64];
            snprintf(l_start, sizeof(l_start), "L_while_start_%d", top.id);
            snprintf(l_end, sizeof(l_end), "L_while_end_%d", top.id);
            emit_inst("b %s", l_start);
            emit_label(l_end);
        } else if (top.type == 2) {
            /* if */
            char l_end[64], l_else[64];
            int root_id = top.root_id;
            snprintf(l_end, sizeof(l_end), "L_if_end_%d", root_id);
            snprintf(l_else, sizeof(l_else), "L_if_else_%d", top.id);
            if (!top.has_else) {
                emit_label(l_else);
            }
            emit_label(l_end);
        } else if (top.type == 4) {
            /* fn end */
            emit_inst("ldp x29, x30, [sp], #16");
            emit_inst("ret");
            char l_skip[64];
            snprintf(l_skip, sizeof(l_skip), "L_fn_skip_%d", top.id);
            emit_label(l_skip);
        }
        return;
    }

    /* 9. break */
    if (strncmp(p, "break", 5) == 0) {
        for (int i = g_block_depth - 1; i >= 0; i--) {
            if (g_block_stack[i].type == 1) {
                char l_end[64];
                snprintf(l_end, sizeof(l_end), "L_while_end_%d", g_block_stack[i].id);
                emit_inst("b %s", l_end);
                return;
            }
        }
        return;
    }

    /* 10. continue */
    if (strncmp(p, "continue", 8) == 0) {
        for (int i = g_block_depth - 1; i >= 0; i--) {
            if (g_block_stack[i].type == 1) {
                char l_start[64];
                snprintf(l_start, sizeof(l_start), "L_while_start_%d", g_block_stack[i].id);
                emit_inst("b %s", l_start);
                return;
            }
        }
        return;
    }

    /* 11. fn <name> { */
    if (strncmp(p, "fn ", 3) == 0) {
        p += 3;
        p = skip_whitespace(p);
        char fname[64];
        p = read_ident(p, fname, sizeof(fname));

        int id = ++g_label_seq;
        char l_skip[64];
        snprintf(l_skip, sizeof(l_skip), "L_fn_skip_%d", id);
        emit_inst("b %s", l_skip);

        char flbl[128];
        snprintf(flbl, sizeof(flbl), "fn_%s", fname);
        emit_label(flbl);
        emit_inst("stp x29, x30, [sp, #-16]!");
        emit_inst("mov x29, sp");

        g_block_stack[g_block_depth].type = 4; /* fn */
        g_block_stack[g_block_depth].id = id;
        g_block_stack[g_block_depth].has_else = 0;
        g_block_depth++;
        return;
    }

    /* 12. call <name> */
    if (strncmp(p, "call ", 5) == 0) {
        p += 5;
        p = skip_whitespace(p);
        char fname[64];
        p = read_ident(p, fname, sizeof(fname));
        char flbl[128];
        snprintf(flbl, sizeof(flbl), "fn_%s", fname);
        emit_inst("bl %s", flbl);
        return;
    }

    /* 13. return */
    if (strncmp(p, "return", 6) == 0) {
        p += 6;
        p = skip_whitespace(p);
        if (*p && *p != '#' && *p != '\n') {
            if (is_float_expr(p)) {
                parse_float_expression(&p, "d0");
            } else {
                parse_expression(&p, "x0");
            }
        }
        int in_fn = 0;
        for (int i = g_block_depth - 1; i >= 0; i--) {
            if (g_block_stack[i].type == 4) {
                in_fn = 1;
                break;
            }
        }
        if (in_fn) {
            emit_inst("ldp x29, x30, [sp], #16");
            emit_inst("ret");
        } else {
            emit_inst("mov x0, #0");
            emit_inst("bl _nx_exit");
        }
        return;
    }

    /* 14. file_read <fd> <buf> <len> */
    if (strncmp(p, "file_read ", 10) == 0 || strncmp(p, "file_read\t", 10) == 0) {
        p += 10;
        p = parse_term(p, "x19");
        p = skip_ws_and_comma(p);
        p = parse_term(p, "x1");
        p = skip_ws_and_comma(p);
        p = parse_term(p, "x2");
        emit_inst("mov x0, x19");
        emit_inst("bl _nx_file_read");
        return;
    }

    /* 15. file_write <fd> <buf> <len> */
    if (strncmp(p, "file_write ", 11) == 0 || strncmp(p, "file_write\t", 11) == 0) {
        p += 11;
        p = parse_term(p, "x19");
        p = skip_ws_and_comma(p);
        p = parse_term(p, "x1");
        p = skip_ws_and_comma(p);
        p = parse_term(p, "x2");
        emit_inst("mov x0, x19");
        emit_inst("bl _nx_file_write");
        return;
    }

    /* 16. file_close <fd> */
    if (strncmp(p, "file_close ", 11) == 0 || strncmp(p, "file_close\t", 11) == 0) {
        p += 11;
        p = parse_term(p, "x0");
        emit_inst("bl _nx_file_close");
        return;
    }

    /* 17. read <var> */
    if (strncmp(p, "read ", 5) == 0 || strncmp(p, "read\t", 5) == 0) {
        p += 5;
        p = skip_whitespace(p);
        char vname[64];
        p = read_ident(p, vname, sizeof(vname));
        int vslot = resolve_var(vname);
        emit_inst("bl _nx_read_int");
        emit_store_var("x0", vslot);
        return;
    }

    /* 18. print_str <var> OR print_str "..." */
    if (strncmp(p, "print_str ", 10) == 0 || strncmp(p, "print_str\t", 10) == 0) {
        p += 10;
        p = skip_whitespace(p);
        if (*p == '"') {
            p++;
            const char* start = p;
            while (*p && *p != '"') {
                if (*p == '\\' && p[1]) p += 2;
                else p++;
            }
            int slen = (int)(p - start);
            int sid = add_string(start, slen);
            if (*p == '"') p++;
            char lbl[64];
            snprintf(lbl, sizeof(lbl), "L_str_%d", sid);
            if (g_target == TARGET_MACOS_ARM64) {
                emit_inst("adrp x0, %s@PAGE", lbl);
                emit_inst("add x0, x0, %s@PAGEOFF", lbl);
            } else {
                emit_inst("adrp x0, %s", lbl);
                emit_inst("add x0, x0, :lo12:%s", lbl);
            }
        } else {
            char vname[64];
            p = read_ident(p, vname, sizeof(vname));
            int vslot = resolve_var(vname);
            emit_load_var("x0", vslot);
        }
        emit_inst("bl _nx_print_str");
        return;
    }

    /* 19. syscall statement */
    if (!strncmp(p, "syscall", 7)) {
        compile_syscall(p, -1);
        return;
    }
}

/* ---- Program Emission Entry Point ------------------------------------------ */

int compile_nexus_to_arm64(const char* in_source, const char* out_asm) {
    FILE* fin = fopen(in_source, "r");
    if (!fin) {
        fprintf(stderr, "[-] Error: Cannot open input source '%s'\n", in_source);
        return 1;
    }

    g_out = fopen(out_asm, "w");
    if (!g_out) {
        fprintf(stderr, "[-] Error: Cannot open output assembly '%s'\n", out_asm);
        fclose(fin);
        return 1;
    }

    /* Header directives */
    fprintf(g_out, "// ==============================================================================\n");
    fprintf(g_out, "//       NEXUS Native ARM64 Assembly Output (0%% libc - 0%% Runtime)\n");
    fprintf(g_out, "// ==============================================================================\n");
    fprintf(g_out, ".global _start\n");
    fprintf(g_out, ".p2align 2\n\n");

    /* Runtime helpers section */
    fprintf(g_out, ".text\n");
    emit_runtime_stubs();

    /* Main entry point */
    fprintf(g_out, "\n// --- Application Entry Point (_start) --------------------------------------\n");
    emit_label("_start");
    emit_inst("ldr x19, [sp]");          /* x19 = argc from OS entry stack */
    emit_inst("add x20, sp, #8");        /* x20 = argv from OS entry stack */
    emit_inst("stp x29, x30, [sp, #-16]!");
    emit_inst("mov x29, sp");

    /* Allocate variable table: 64 KB anonymous memory for variables */
    emit_comment("Allocate 64 KB zeroed memory for NEXUS variable table (x28)");
    emit_mov_imm64("x0", 65536);
    emit_inst("bl _nx_alloc");
    emit_inst("mov x28, x0");          /* x28 = base of variable table */

    /* Initialize _nx_argc and _nx_argv */
    int argc_slot = resolve_var("_nx_argc");
    int argv_slot = resolve_var("_nx_argv");
    emit_store_var("x19", argc_slot);
    emit_store_var("x20", argv_slot);

    /* Read and parse input file line by line */
    char line[1024];
    while (fgets(line, sizeof(line), fin)) {
        compile_line(line);
    }
    fclose(fin);

    /* Exit cleanly */
    emit_comment("Clean application exit (0)");
    emit_inst("mov x0, #0");
    emit_inst("bl _nx_exit");

    /* Read-Only Data Section (string literals) */
    if (g_string_count > 0) {
        fprintf(g_out, "\n// --- Read-Only Data Section (.rodata) -------------------------------------\n");
        if (g_target == TARGET_MACOS_ARM64) {
            fprintf(g_out, ".section __TEXT,__cstring,cstring_literals\n");
        } else {
            fprintf(g_out, ".section .rodata\n");
        }

        for (int i = 0; i < g_string_count; i++) {
            fprintf(g_out, "L_str_%d:\n", g_strings[i].id);
            fprintf(g_out, "    .asciz \"");
            for (int j = 0; j < g_strings[i].len; j++) {
                unsigned char c = (unsigned char)g_strings[i].text[j];
                if (c == '\n') fprintf(g_out, "\\n");
                else if (c == '\r') fprintf(g_out, "\\r");
                else if (c == '\t') fprintf(g_out, "\\t");
                else if (c == '\"') fprintf(g_out, "\\\"");
                else if (c == '\\') fprintf(g_out, "\\\\");
                else if (c >= 32 && c <= 126) fputc(c, g_out);
                else fprintf(g_out, "\\x%02x", c);
            }
            fprintf(g_out, "\"\n");
        }
    }

    fclose(g_out);
    return 0;
}

/* ===========================================================================
 * CLI Driver
 * =========================================================================== */

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("NEXUS Native ARM64 Ahead-Of-Time Compiler\n");
        printf("Usage: %s [--target <macos|arm64-macos|linux|aarch64-linux>] <source.nex> [output.macho]\n", argv[0]);
        return 1;
    }

    int argi = 1;
    if (strcmp(argv[argi], "--target") == 0) {
        argi++;
        if (argi >= argc) {
            fprintf(stderr, "[-] Error: Expected target name after --target\n");
            return 1;
        }
        if (strcmp(argv[argi], "macos") == 0 || strcmp(argv[argi], "arm64-macos") == 0 || strcmp(argv[argi], "macos-arm64") == 0) {
            g_target = TARGET_MACOS_ARM64;
        } else if (strcmp(argv[argi], "linux") == 0 || strcmp(argv[argi], "aarch64-linux") == 0 || strcmp(argv[argi], "linux-aarch64") == 0 || strcmp(argv[argi], "arm64-linux") == 0) {
            g_target = TARGET_LINUX_AARCH64;
        } else {
            fprintf(stderr, "[-] Error: Unsupported target '%s' (use 'macos' or 'linux')\n", argv[argi]);
            return 1;
        }
        argi++;
    }

    if (argi >= argc) {
        fprintf(stderr, "[-] Error: Missing input source file\n");
        return 1;
    }

    const char* in_source = argv[argi++];
    const char* out_bin = (argi < argc) ? argv[argi++] :
        (g_target == TARGET_MACOS_ARM64 ? "app.macho" : "app.aarch64.elf");

    char asm_path[256];
    snprintf(asm_path, sizeof(asm_path), "%s.s", out_bin);

    printf("[+] Compiling NEXUS to Native ARM64: %s -> %s\n", in_source, out_bin);
    if (compile_nexus_to_arm64(in_source, asm_path) != 0) {
        return 1;
    }
    printf("    Generated ARM64 Assembly: %s\n", asm_path);

    /* Invoke clang and ld64.lld or ld.lld to assemble and link */
    char cmd[1024];
    char obj_path[256];
    snprintf(obj_path, sizeof(obj_path), "%s.o", out_bin);

    if (g_target == TARGET_MACOS_ARM64) {
        /* Assemble */
        snprintf(cmd, sizeof(cmd),
                 "clang -target arm64-apple-macos11 -c \"%s\" -o \"%s\"",
                 asm_path, obj_path);
        if (system(cmd) != 0) {
            fprintf(stderr, "[-] Error: Assembly failed with command: %s\n", cmd);
            return 1;
        }
        /* Link with ld64.lld */
        snprintf(cmd, sizeof(cmd),
                 "ld64.lld-18 -arch arm64 -platform_version macos 11.0 11.0 -e _start \"%s\" -o \"%s\"",
                 obj_path, out_bin);
        if (system(cmd) != 0) {
            fprintf(stderr, "[-] Error: Linking failed with command: %s\n", cmd);
            return 1;
        }
    } else {
        /* Linux aarch64 */
        snprintf(cmd, sizeof(cmd),
                 "clang -target aarch64-linux-gnu -c \"%s\" -o \"%s\"",
                 asm_path, obj_path);
        if (system(cmd) != 0) {
            fprintf(stderr, "[-] Error: Assembly failed with command: %s\n", cmd);
            return 1;
        }
        snprintf(cmd, sizeof(cmd),
                 "ld.lld -m aarch64linux \"%s\" -o \"%s\"",
                 obj_path, out_bin);
        if (system(cmd) != 0) {
            fprintf(stderr, "[-] Error: Linking failed with command: %s\n", cmd);
            return 1;
        }
    }

    chmod(out_bin, 0755);
    unlink(obj_path);

    struct stat st;
    stat(out_bin, &st);
    printf("[+] Standalone Native ARM64 Executable Ready: %s (%ld bytes)\n", out_bin, (long)st.st_size);
    printf("    Target: %s\n", g_target == TARGET_MACOS_ARM64 ? "macOS ARM64 (Apple Silicon Mach-O)" : "Linux aarch64 (ELF64)");

    return 0;
}
