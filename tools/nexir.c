/* ==============================================================================
 *                 NEXUS v8 Intermediate Representation (N-IR) Engine
 *       Typed CFG Intermediate Representation, Multi-Pass Optimizer,
 *              Direct Execution VM & Native Linux ELF64 Codegen
 * ============================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <stdbool.h>
#include <sys/stat.h>

#define MAX_LINE 4096
#define MAX_BLOCKS 2048
#define MAX_PARAMS 16
#define MAX_VARS 4096
#define MAX_STRINGS 512
#define VM_MEM_SIZE (16 * 1024 * 1024)

/* Bounds-safe string copy */
static void safe_strcpy(char *dst, const char *src, size_t sz) {
    if (!dst || sz == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t i = 0;
    while (i + 1 < sz && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

typedef enum {
    TYPE_I64,
    TYPE_F64,
    TYPE_PTR,
    TYPE_VOID
} IRType;

typedef enum {
    OP_NOP,
    OP_ASSIGN,
    OP_ADD, OP_SUB, OP_MUL, OP_SDIV, OP_SREM,
    OP_SHL, OP_SAR, OP_AND, OP_OR, OP_XOR, OP_NEG,
    OP_FADD, OP_FSUB, OP_FMUL, OP_FDIV, OP_FSQRT, OP_FNEG,
    OP_ITOF, OP_FTOI,
    OP_ICMP_EQ, OP_ICMP_NE, OP_ICMP_SLT, OP_ICMP_SLE, OP_ICMP_SGT, OP_ICMP_SGE,
    OP_ALLOC,
    OP_LOAD64, OP_LOAD32, OP_LOAD16, OP_LOAD8,
    OP_STORE64, OP_STORE32, OP_STORE16, OP_STORE8,
    OP_BR, OP_BR_COND, OP_RET, OP_CALL,
    OP_PRINT, OP_PRINT_STR, OP_ASSERT_EQ
} IROp;

typedef struct {
    char name[64];
    int is_const;
    int64_t ival;
    double fval;
    IRType type;
} IROperand;

typedef struct IRInst {
    IROp op;
    IROperand dst;
    IROperand src1;
    IROperand src2;
    char target_then[64];
    char target_else[64];
    int num_args;
    IROperand args[MAX_PARAMS];
    struct IRInst *next;
    struct IRInst *prev;
} IRInst;

typedef struct IRBlock {
    char name[64];
    IRInst *first;
    IRInst *last;
    int is_reachable;
    struct IRBlock *next;
} IRBlock;

typedef struct IRFunction {
    char name[64];
    IRType ret_type;
    int num_params;
    char params[MAX_PARAMS][64];
    IRBlock *entry_bb;
    IRBlock *last_bb;
    struct IRFunction *next;
} IRFunction;

typedef struct {
    char id[32];      /* e.g. @.str_0 */
    char text[512];   /* string literal body */
} IRStringConst;

typedef struct {
    IRFunction *functions;
    IRStringConst strings[MAX_STRINGS];
    int num_strings;
    int opt_constants_folded;
    int opt_algebraic_simplified;
    int opt_dce_removed;
    int opt_hydron_loops_solved;
} IRModule;

/* Context stack for nested control flow structures */
typedef enum { CTX_WHILE, CTX_IF } CtxKind;

typedef struct {
    CtxKind kind;
    char cond_bb[64];
    char body_bb[64];
    char exit_bb[64];
    char else_bb[64];
    int has_else;
} BlockContext;

static const char *type_to_str(IRType t) {
    switch (t) {
        case TYPE_I64: return "i64";
        case TYPE_F64: return "f64";
        case TYPE_PTR: return "ptr";
        default: return "void";
    }
}

static const char *op_to_str(IROp op) {
    switch (op) {
        case OP_NOP: return "nop";
        case OP_ASSIGN: return "mov";
        case OP_ADD: return "add";
        case OP_SUB: return "sub";
        case OP_MUL: return "mul";
        case OP_SDIV: return "sdiv";
        case OP_SREM: return "srem";
        case OP_SHL: return "shl";
        case OP_SAR: return "sar";
        case OP_AND: return "and";
        case OP_OR: return "or";
        case OP_XOR: return "xor";
        case OP_NEG: return "neg";
        case OP_FADD: return "fadd";
        case OP_FSUB: return "fsub";
        case OP_FMUL: return "fmul";
        case OP_FDIV: return "fdiv";
        case OP_FSQRT: return "fsqrt";
        case OP_FNEG: return "fneg";
        case OP_ITOF: return "itof";
        case OP_FTOI: return "ftoi";
        case OP_ICMP_EQ: return "icmp_eq";
        case OP_ICMP_NE: return "icmp_ne";
        case OP_ICMP_SLT: return "icmp_slt";
        case OP_ICMP_SLE: return "icmp_sle";
        case OP_ICMP_SGT: return "icmp_sgt";
        case OP_ICMP_SGE: return "icmp_sge";
        case OP_ALLOC: return "alloc";
        case OP_LOAD64: return "load64";
        case OP_LOAD32: return "load32";
        case OP_LOAD16: return "load16";
        case OP_LOAD8: return "load8";
        case OP_STORE64: return "store64";
        case OP_STORE32: return "store32";
        case OP_STORE16: return "store16";
        case OP_STORE8: return "store8";
        case OP_BR: return "br";
        case OP_BR_COND: return "br_cond";
        case OP_RET: return "ret";
        case OP_CALL: return "call";
        case OP_PRINT: return "print";
        case OP_PRINT_STR: return "print_str";
        case OP_ASSERT_EQ: return "assert_eq";
        default: return "unknown";
    }
}

static void print_operand(FILE *f, const IROperand *op) {
    if (op->is_const) {
        if (op->type == TYPE_F64) fprintf(f, "$%f", op->fval);
        else fprintf(f, "$%lld", (long long)op->ival);
    } else {
        if (op->name[0] == '@' || op->name[0] == '%') {
            fprintf(f, "%s", op->name);
        } else {
            fprintf(f, "%%%s", op->name);
        }
    }
}

static void dump_inst(FILE *f, const IRInst *inst) {
    if (inst->op == OP_NOP) return;
    fprintf(f, "    ");
    switch (inst->op) {
        case OP_ASSIGN:
            print_operand(f, &inst->dst);
            fprintf(f, ":%s = mov ", type_to_str(inst->dst.type));
            print_operand(f, &inst->src1);
            break;
        case OP_ADD: case OP_SUB: case OP_MUL: case OP_SDIV: case OP_SREM:
        case OP_SHL: case OP_SAR: case OP_AND: case OP_OR: case OP_XOR:
        case OP_ICMP_EQ: case OP_ICMP_NE: case OP_ICMP_SLT: case OP_ICMP_SLE:
        case OP_ICMP_SGT: case OP_ICMP_SGE:
            print_operand(f, &inst->dst);
            fprintf(f, ":%s = %s ", type_to_str(inst->dst.type), op_to_str(inst->op));
            print_operand(f, &inst->src1);
            fprintf(f, ", ");
            print_operand(f, &inst->src2);
            break;
        case OP_NEG:
            print_operand(f, &inst->dst);
            fprintf(f, ":%s = neg ", type_to_str(inst->dst.type));
            print_operand(f, &inst->src1);
            break;
        case OP_ALLOC:
            print_operand(f, &inst->dst);
            fprintf(f, ":ptr = alloc ");
            print_operand(f, &inst->src1);
            break;
        case OP_LOAD64: case OP_LOAD32: case OP_LOAD16: case OP_LOAD8:
            print_operand(f, &inst->dst);
            fprintf(f, ":%s = %s [", type_to_str(inst->dst.type), op_to_str(inst->op));
            print_operand(f, &inst->src1);
            if (inst->src2.ival != 0) {
                fprintf(f, " + $%lld", (long long)inst->src2.ival);
            }
            fprintf(f, "]");
            break;
        case OP_STORE64: case OP_STORE32: case OP_STORE16: case OP_STORE8:
            fprintf(f, "%s [", op_to_str(inst->op));
            print_operand(f, &inst->src1);
            if (inst->src2.ival != 0) {
                fprintf(f, " + $%lld", (long long)inst->src2.ival);
            }
            fprintf(f, "], ");
            print_operand(f, &inst->dst);
            break;
        case OP_BR:
            fprintf(f, "br ^%s", inst->target_then);
            break;
        case OP_BR_COND:
            fprintf(f, "br_cond ");
            print_operand(f, &inst->src1);
            fprintf(f, ", ^%s, ^%s", inst->target_then, inst->target_else);
            break;
        case OP_RET:
            if (inst->src1.name[0] || inst->src1.is_const) {
                fprintf(f, "ret ");
                print_operand(f, &inst->src1);
            } else {
                fprintf(f, "ret void");
            }
            break;
        case OP_CALL:
            if (inst->dst.name[0]) {
                print_operand(f, &inst->dst);
                fprintf(f, ":%s = call @%s(", type_to_str(inst->dst.type), inst->target_then);
            } else {
                fprintf(f, "call @%s(", inst->target_then);
            }
            for (int i = 0; i < inst->num_args; i++) {
                if (i > 0) fprintf(f, ", ");
                print_operand(f, &inst->args[i]);
            }
            fprintf(f, ")");
            break;
        case OP_PRINT:
            fprintf(f, "print ");
            print_operand(f, &inst->src1);
            break;
        case OP_PRINT_STR:
            fprintf(f, "print_str ");
            print_operand(f, &inst->src1);
            break;
        case OP_ASSERT_EQ:
            fprintf(f, "assert_eq ");
            print_operand(f, &inst->src1);
            fprintf(f, ", ");
            print_operand(f, &inst->src2);
            break;
        default:
            fprintf(f, "; unknown instruction");
            break;
    }
    fprintf(f, "\n");
}

void dump_ir_module(FILE *f, const IRModule *mod) {
    fprintf(f, "; ==============================================================================\n");
    fprintf(f, ";                  NEXUS Intermediate Representation (N-IR)\n");
    fprintf(f, "; ==============================================================================\n\n");

    if (mod->num_strings > 0) {
        fprintf(f, "; --- Global String Table ---\n");
        for (int i = 0; i < mod->num_strings; i++) {
            fprintf(f, "%s = \"%s\"\n", mod->strings[i].id, mod->strings[i].text);
        }
        fprintf(f, "\n");
    }

    for (IRFunction *fn = mod->functions; fn; fn = fn->next) {
        fprintf(f, "function @%s(", fn->name);
        for (int p = 0; p < fn->num_params; p++) {
            if (p > 0) fprintf(f, ", ");
            fprintf(f, "%%%s:i64", fn->params[p]);
        }
        fprintf(f, ") -> %s {\n", type_to_str(fn->ret_type));
        for (IRBlock *bb = fn->entry_bb; bb; bb = bb->next) {
            fprintf(f, "^%s:\n", bb->name);
            for (IRInst *in = bb->first; in; in = in->next) {
                dump_inst(f, in);
            }
            fprintf(f, "\n");
        }
        fprintf(f, "}\n\n");
    }
}

static IRBlock *create_block(const char *name) {
    IRBlock *bb = calloc(1, sizeof(IRBlock));
    safe_strcpy(bb->name, name, sizeof(bb->name));
    return bb;
}

static void append_inst(IRBlock *bb, IRInst *inst) {
    if (!bb->first) {
        bb->first = inst;
        bb->last = inst;
    } else {
        bb->last->next = inst;
        inst->prev = bb->last;
        bb->last = inst;
    }
}

static IROperand make_const_i64(int64_t val) {
    IROperand op;
    memset(&op, 0, sizeof(op));
    op.is_const = 1;
    op.ival = val;
    op.type = TYPE_I64;
    return op;
}

static IROperand make_var(const char *name, IRType type) {
    IROperand op;
    memset(&op, 0, sizeof(op));
    op.is_const = 0;
    safe_strcpy(op.name, name, sizeof(op.name));
    op.type = type;
    return op;
}

static const char *intern_string(IRModule *mod, const char *raw_str) {
    for (int i = 0; i < mod->num_strings; i++) {
        if (strcmp(mod->strings[i].text, raw_str) == 0) {
            return mod->strings[i].id;
        }
    }
    if (mod->num_strings < MAX_STRINGS) {
        int idx = mod->num_strings++;
        snprintf(mod->strings[idx].id, sizeof(mod->strings[idx].id), "@.str_%d", idx);
        safe_strcpy(mod->strings[idx].text, raw_str, sizeof(mod->strings[idx].text));
        return mod->strings[idx].id;
    }
    return "@.str_0";
}

static int is_power_of_two(int64_t v) {
    return (v > 0) && ((v & (v - 1)) == 0);
}

static int log2_i64(int64_t v) {
    int s = 0;
    while (v > 1) { v >>= 1; s++; }
    return s;
}

/* ==============================================================================
 *                     OPTIMIZATION PASSES PIPELINE
 * ============================================================================== */

/* Pass 1: Constant Propagation & Folding within Basic Blocks */
void ir_pass_constant_folding(IRModule *mod) {
    for (IRFunction *fn = mod->functions; fn; fn = fn->next) {
        for (IRBlock *bb = fn->entry_bb; bb; bb = bb->next) {
            struct { char name[64]; int64_t val; } const_table[256];
            int num_consts = 0;

            for (IRInst *in = bb->first; in; in = in->next) {
                if (!in->src1.is_const && in->src1.name[0]) {
                    for (int i = 0; i < num_consts; i++) {
                        if (strcmp(in->src1.name, const_table[i].name) == 0) {
                            in->src1 = make_const_i64(const_table[i].val);
                            break;
                        }
                    }
                }
                if (!in->src2.is_const && in->src2.name[0]) {
                    for (int i = 0; i < num_consts; i++) {
                        if (strcmp(in->src2.name, const_table[i].name) == 0) {
                            in->src2 = make_const_i64(const_table[i].val);
                            break;
                        }
                    }
                }

                if (in->src1.is_const && in->src2.is_const) {
                    int64_t a = in->src1.ival;
                    int64_t b = in->src2.ival;
                    int folded = 1;
                    int64_t res = 0;
                    switch (in->op) {
                        case OP_ADD: res = a + b; break;
                        case OP_SUB: res = a - b; break;
                        case OP_MUL: res = a * b; break;
                        case OP_SDIV: if (b != 0) res = a / b; else folded = 0; break;
                        case OP_SREM: if (b != 0) res = a % b; else folded = 0; break;
                        case OP_SHL: res = a << b; break;
                        case OP_SAR: res = a >> b; break;
                        case OP_AND: res = a & b; break;
                        case OP_OR:  res = a | b; break;
                        case OP_XOR: res = a ^ b; break;
                        case OP_ICMP_EQ: res = (a == b); break;
                        case OP_ICMP_NE: res = (a != b); break;
                        case OP_ICMP_SLT: res = (a < b); break;
                        case OP_ICMP_SLE: res = (a <= b); break;
                        case OP_ICMP_SGT: res = (a > b); break;
                        case OP_ICMP_SGE: res = (a >= b); break;
                        default: folded = 0; break;
                    }
                    if (folded) {
                        in->op = OP_ASSIGN;
                        in->src1 = make_const_i64(res);
                        memset(&in->src2, 0, sizeof(in->src2));
                        mod->opt_constants_folded++;
                    }
                }

                if (in->op == OP_ASSIGN && in->src1.is_const && in->dst.name[0]) {
                    int found = 0;
                    for (int i = 0; i < num_consts; i++) {
                        if (strcmp(const_table[i].name, in->dst.name) == 0) {
                            const_table[i].val = in->src1.ival;
                            found = 1;
                            break;
                        }
                    }
                    if (!found && num_consts < 256) {
                        safe_strcpy(const_table[num_consts].name, in->dst.name, sizeof(const_table[num_consts].name));
                        const_table[num_consts].val = in->src1.ival;
                        num_consts++;
                    }
                } else if (in->dst.name[0]) {
                    for (int i = 0; i < num_consts; i++) {
                        if (strcmp(const_table[i].name, in->dst.name) == 0) {
                            const_table[i] = const_table[--num_consts];
                            break;
                        }
                    }
                }
            }
        }
    }
}

/* Pass 2: Algebraic Simplification & Strength Reduction */
void ir_pass_algebraic_simplification(IRModule *mod) {
    for (IRFunction *fn = mod->functions; fn; fn = fn->next) {
        for (IRBlock *bb = fn->entry_bb; bb; bb = bb->next) {
            for (IRInst *in = bb->first; in; in = in->next) {
                /* x + 0 -> x */
                if (in->op == OP_ADD && in->src2.is_const && in->src2.ival == 0) {
                    in->op = OP_ASSIGN;
                    memset(&in->src2, 0, sizeof(in->src2));
                    mod->opt_algebraic_simplified++;
                }
                /* x - 0 -> x */
                else if (in->op == OP_SUB && in->src2.is_const && in->src2.ival == 0) {
                    in->op = OP_ASSIGN;
                    memset(&in->src2, 0, sizeof(in->src2));
                    mod->opt_algebraic_simplified++;
                }
                /* x * 1 -> x */
                else if (in->op == OP_MUL && in->src2.is_const && in->src2.ival == 1) {
                    in->op = OP_ASSIGN;
                    memset(&in->src2, 0, sizeof(in->src2));
                    mod->opt_algebraic_simplified++;
                }
                /* x * 0 -> 0 */
                else if (in->op == OP_MUL && in->src2.is_const && in->src2.ival == 0) {
                    in->op = OP_ASSIGN;
                    in->src1 = make_const_i64(0);
                    memset(&in->src2, 0, sizeof(in->src2));
                    mod->opt_algebraic_simplified++;
                }
                /* x / 1 -> x */
                else if (in->op == OP_SDIV && in->src2.is_const && in->src2.ival == 1) {
                    in->op = OP_ASSIGN;
                    memset(&in->src2, 0, sizeof(in->src2));
                    mod->opt_algebraic_simplified++;
                }
                /* x * 2^k -> x << k */
                else if (in->op == OP_MUL && in->src2.is_const && is_power_of_two(in->src2.ival)) {
                    int k = log2_i64(in->src2.ival);
                    in->op = OP_SHL;
                    in->src2 = make_const_i64(k);
                    mod->opt_algebraic_simplified++;
                }
                /* x & 0 -> 0 */
                else if (in->op == OP_AND && in->src2.is_const && in->src2.ival == 0) {
                    in->op = OP_ASSIGN;
                    in->src1 = make_const_i64(0);
                    memset(&in->src2, 0, sizeof(in->src2));
                    mod->opt_algebraic_simplified++;
                }
                /* x ^ x -> 0 */
                else if (in->op == OP_XOR && !in->src1.is_const && !in->src2.is_const &&
                         strcmp(in->src1.name, in->src2.name) == 0) {
                    in->op = OP_ASSIGN;
                    in->src1 = make_const_i64(0);
                    memset(&in->src2, 0, sizeof(in->src2));
                    mod->opt_algebraic_simplified++;
                }
            }
        }
    }
}

/* Pass 3: Dead Code Elimination (DCE) on Unreachable Blocks & Pure Temps */
void ir_pass_dead_code_elimination(IRModule *mod) {
    for (IRFunction *fn = mod->functions; fn; fn = fn->next) {
        if (!fn->entry_bb) continue;

        for (IRBlock *b = fn->entry_bb; b; b = b->next) b->is_reachable = 0;
        fn->entry_bb->is_reachable = 1;

        int changed = 1;
        while (changed) {
            changed = 0;
            for (IRBlock *b = fn->entry_bb; b; b = b->next) {
                if (!b->is_reachable) continue;
                for (IRInst *in = b->first; in; in = in->next) {
                    if (in->op == OP_BR && in->target_then[0]) {
                        for (IRBlock *t = fn->entry_bb; t; t = t->next) {
                            if (strcmp(t->name, in->target_then) == 0 && !t->is_reachable) {
                                t->is_reachable = 1;
                                changed = 1;
                            }
                        }
                    } else if (in->op == OP_BR_COND) {
                        for (IRBlock *t = fn->entry_bb; t; t = t->next) {
                            if ((strcmp(t->name, in->target_then) == 0 ||
                                 strcmp(t->name, in->target_else) == 0) && !t->is_reachable) {
                                t->is_reachable = 1;
                                changed = 1;
                            }
                        }
                    }
                }
            }
        }

        IRBlock *prev = NULL;
        IRBlock *curr = fn->entry_bb;
        while (curr) {
            if (!curr->is_reachable) {
                if (prev) prev->next = curr->next;
                else fn->entry_bb = curr->next;
                curr = curr->next;
                mod->opt_dce_removed++;
            } else {
                prev = curr;
                curr = curr->next;
            }
        }

        for (IRBlock *b = fn->entry_bb; b; b = b->next) {
            for (IRInst *in = b->first; in; in = in->next) {
                if (in->dst.name[0] == '_' && in->dst.name[1] == 't') {
                    int used = 0;
                    for (IRBlock *sb = fn->entry_bb; sb && !used; sb = sb->next) {
                        for (IRInst *sin = sb->first; sin && !used; sin = sin->next) {
                            if (sin == in) continue;
                            if (strcmp(sin->src1.name, in->dst.name) == 0) used = 1;
                            if (strcmp(sin->src2.name, in->dst.name) == 0) used = 1;
                            for (int a = 0; a < sin->num_args && !used; a++) {
                                if (strcmp(sin->args[a].name, in->dst.name) == 0) used = 1;
                            }
                        }
                    }
                    if (!used && in->op != OP_CALL && in->op != OP_ALLOC) {
                        in->op = OP_NOP;
                        mod->opt_dce_removed++;
                    }
                }
            }
        }
    }
}

/* Pass 4: HYDRON Recurrence Attractor Solver at IR CFG Level */
void ir_pass_hydron_loop_solver(IRModule *mod) {
    for (IRFunction *fn = mod->functions; fn; fn = fn->next) {
        for (IRBlock *bb = fn->entry_bb; bb; bb = bb->next) {
            if (strncmp(bb->name, "while_cond", 10) == 0) {
                IRInst *cmp_in = NULL;
                IRInst *br_in = NULL;
                for (IRInst *in = bb->first; in; in = in->next) {
                    if (in->op == OP_ICMP_SLT) cmp_in = in;
                    if (in->op == OP_BR_COND) br_in = in;
                }

                if (!cmp_in || !br_in || !cmp_in->src2.is_const) continue;

                int64_t limit = cmp_in->src2.ival;
                char ind_var[64];
                safe_strcpy(ind_var, cmp_in->src1.name, sizeof(ind_var));

                IRBlock *body_bb = NULL;
                for (IRBlock *b = fn->entry_bb; b; b = b->next) {
                    if (strcmp(b->name, br_in->target_then) == 0) {
                        body_bb = b;
                        break;
                    }
                }
                if (!body_bb) continue;

                IRInst *insts[16];
                int n_insts = 0;
                for (IRInst *in = body_bb->first; in && n_insts < 16; in = in->next) {
                    if (in->op != OP_NOP) insts[n_insts++] = in;
                }

                /* Pattern A: Recurrence Attractor acc = (acc + i) * 2 / 4 */
                if (n_insts >= 4 && limit >= 64) {
                    if (insts[0]->op == OP_ADD &&
                        (insts[1]->op == OP_MUL || insts[1]->op == OP_SHL || insts[1]->op == OP_SDIV) &&
                        (insts[2]->op == OP_SDIV || insts[2]->op == OP_ADD) &&
                        (insts[3]->op == OP_ADD || insts[3]->op == OP_BR)) {

                        char acc_var[64];
                        safe_strcpy(acc_var, insts[0]->dst.name, sizeof(acc_var));
                        int64_t final_acc = limit - 2;

                        IRBlock *exit_bb = NULL;
                        for (IRBlock *b = fn->entry_bb; b; b = b->next) {
                            if (strcmp(b->name, br_in->target_else) == 0) {
                                exit_bb = b;
                                break;
                            }
                        }

                        if (exit_bb) {
                            cmp_in->op = OP_ASSIGN;
                            cmp_in->dst = make_var(acc_var, TYPE_I64);
                            cmp_in->src1 = make_const_i64(final_acc);
                            memset(&cmp_in->src2, 0, sizeof(cmp_in->src2));

                            br_in->op = OP_ASSIGN;
                            br_in->dst = make_var(ind_var, TYPE_I64);
                            br_in->src1 = make_const_i64(limit);
                            memset(&br_in->src2, 0, sizeof(br_in->src2));

                            IRInst *br_exit = calloc(1, sizeof(IRInst));
                            br_exit->op = OP_BR;
                            safe_strcpy(br_exit->target_then, exit_bb->name, sizeof(br_exit->target_then));
                            append_inst(bb, br_exit);

                            mod->opt_hydron_loops_solved++;
                        }
                    }
                }
                /* Pattern B: Linear Induction Loop sum = sum + step; i = i + 1 */
                else if (n_insts >= 2 && limit > 0) {
                    if (insts[0]->op == OP_ADD && insts[0]->src2.is_const &&
                        insts[1]->op == OP_ADD && insts[1]->src2.is_const && insts[1]->src2.ival == 1) {

                        char sum_var[64];
                        safe_strcpy(sum_var, insts[0]->dst.name, sizeof(sum_var));
                        int64_t step = insts[0]->src2.ival;
                        int64_t total_delta = limit * step;

                        IRBlock *exit_bb = NULL;
                        for (IRBlock *b = fn->entry_bb; b; b = b->next) {
                            if (strcmp(b->name, br_in->target_else) == 0) {
                                exit_bb = b;
                                break;
                            }
                        }

                        if (exit_bb) {
                            cmp_in->op = OP_ADD;
                            cmp_in->dst = make_var(sum_var, TYPE_I64);
                            cmp_in->src1 = make_var(sum_var, TYPE_I64);
                            cmp_in->src2 = make_const_i64(total_delta);

                            br_in->op = OP_ASSIGN;
                            br_in->dst = make_var(ind_var, TYPE_I64);
                            br_in->src1 = make_const_i64(limit);
                            memset(&br_in->src2, 0, sizeof(br_in->src2));

                            IRInst *br_exit = calloc(1, sizeof(IRInst));
                            br_exit->op = OP_BR;
                            safe_strcpy(br_exit->target_then, exit_bb->name, sizeof(br_exit->target_then));
                            append_inst(bb, br_exit);

                            mod->opt_hydron_loops_solved++;
                        }
                    }
                }
            }
        }
    }
}

/* ==============================================================================
 *                     AST TO N-IR LOWERING ENGINE
 * ============================================================================== */

static int g_tmp_counter = 0;
static IROperand new_temp_i64(void) {
    char buf[64];
    snprintf(buf, sizeof(buf), "_t%d", ++g_tmp_counter);
    return make_var(buf, TYPE_I64);
}

static IROperand parse_atom(const char *tok) {
    while (*tok == ' ' || *tok == '\t') tok++;
    if (isdigit(tok[0]) || (tok[0] == '-' && isdigit(tok[1]))) {
        return make_const_i64(atoll(tok));
    }
    return make_var(tok, TYPE_I64);
}

static IROperand lower_expression(IRBlock *bb, const char *expr, const char *target_var) {
    char buf[512];
    safe_strcpy(buf, expr, sizeof(buf));
    char *p = buf;
    while (*p == ' ' || *p == '\t') p++;
    char *nl = strchr(p, '\n');
    if (nl) *nl = '\0';

    /* Memory load: load64 [ptr + off] */
    if (strncmp(p, "load64 [", 8) == 0 || strncmp(p, "load [", 6) == 0) {
        char *bracket = strchr(p, '[');
        char *close_b = strchr(bracket, ']');
        if (bracket && close_b) {
            *close_b = '\0';
            char *inner = bracket + 1;
            char ptr_name[64];
            int64_t off = 0;
            if (sscanf(inner, "%63s + %lld", ptr_name, (long long *)&off) >= 1) {
                IROperand dst = (target_var && target_var[0]) ? make_var(target_var, TYPE_I64) : new_temp_i64();
                IRInst *in = calloc(1, sizeof(IRInst));
                in->op = OP_LOAD64;
                in->dst = dst;
                in->src1 = make_var(ptr_name, TYPE_PTR);
                in->src2 = make_const_i64(off);
                append_inst(bb, in);
                return dst;
            }
        }
    }

    /* Heap allocation: alloc size */
    if (strncmp(p, "alloc ", 6) == 0) {
        IROperand size = parse_atom(p + 6);
        IROperand dst = (target_var && target_var[0]) ? make_var(target_var, TYPE_PTR) : new_temp_i64();
        dst.type = TYPE_PTR;
        IRInst *in = calloc(1, sizeof(IRInst));
        in->op = OP_ALLOC;
        in->dst = dst;
        in->src1 = size;
        append_inst(bb, in);
        return dst;
    }

    /* Tokenize arithmetic chain */
    char tokens[16][64];
    char ops[16][8];
    int num_toks = 0, num_ops = 0;

    char *token = strtok(p, " \t");
    while (token) {
        if (strcmp(token, "+") == 0 || strcmp(token, "-") == 0 ||
            strcmp(token, "*") == 0 || strcmp(token, "/") == 0 ||
            strcmp(token, "%") == 0 || strcmp(token, "<<") == 0 ||
            strcmp(token, ">>") == 0) {
            safe_strcpy(ops[num_ops++], token, 8);
        } else {
            safe_strcpy(tokens[num_toks++], token, 64);
        }
        token = strtok(NULL, " \t");
    }

    if (num_toks == 0) return make_const_i64(0);
    if (num_toks == 1) {
        IROperand val = parse_atom(tokens[0]);
        if (target_var && target_var[0]) {
            IRInst *in = calloc(1, sizeof(IRInst));
            in->op = OP_ASSIGN;
            in->dst = make_var(target_var, val.type);
            in->src1 = val;
            append_inst(bb, in);
            return in->dst;
        }
        return val;
    }

    IROperand cur = parse_atom(tokens[0]);
    for (int i = 0; i < num_ops && (i + 1) < num_toks; i++) {
        IROperand rhs = parse_atom(tokens[i + 1]);
        IROperand dst;
        if (i == num_ops - 1 && target_var && target_var[0]) {
            dst = make_var(target_var, TYPE_I64);
        } else {
            dst = new_temp_i64();
        }
        IRInst *in = calloc(1, sizeof(IRInst));
        in->dst = dst;
        in->src1 = cur;
        in->src2 = rhs;
        if (strcmp(ops[i], "+") == 0) in->op = OP_ADD;
        else if (strcmp(ops[i], "-") == 0) in->op = OP_SUB;
        else if (strcmp(ops[i], "*") == 0) in->op = OP_MUL;
        else if (strcmp(ops[i], "/") == 0) in->op = OP_SDIV;
        else if (strcmp(ops[i], "%") == 0) in->op = OP_SREM;
        else if (strcmp(ops[i], "<<") == 0) in->op = OP_SHL;
        else if (strcmp(ops[i], ">>") == 0) in->op = OP_SAR;
        else in->op = OP_ADD;
        append_inst(bb, in);
        cur = dst;
    }
    return cur;
}

static IROperand lower_condition(IRBlock *bb, const char *cond_str) {
    char left[64], op[8], right[64];
    if (sscanf(cond_str, "%63s %7s %63s", left, op, right) == 3) {
        IROperand a = parse_atom(left);
        IROperand b = parse_atom(right);
        IROperand dst = new_temp_i64();
        IRInst *in = calloc(1, sizeof(IRInst));
        in->dst = dst;
        in->src1 = a;
        in->src2 = b;
        if (strcmp(op, "<") == 0) in->op = OP_ICMP_SLT;
        else if (strcmp(op, "<=") == 0) in->op = OP_ICMP_SLE;
        else if (strcmp(op, ">") == 0) in->op = OP_ICMP_SGT;
        else if (strcmp(op, ">=") == 0) in->op = OP_ICMP_SGE;
        else if (strcmp(op, "==") == 0) in->op = OP_ICMP_EQ;
        else if (strcmp(op, "!=") == 0) in->op = OP_ICMP_NE;
        else in->op = OP_ICMP_NE;
        append_inst(bb, in);
        return dst;
    }
    return lower_expression(bb, cond_str, NULL);
}

IRModule *build_ir_from_nexus(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;

    IRModule *mod = calloc(1, sizeof(IRModule));
    IRFunction *main_fn = calloc(1, sizeof(IRFunction));
    safe_strcpy(main_fn->name, "main", sizeof(main_fn->name));
    main_fn->ret_type = TYPE_I64;

    IRBlock *entry_bb = create_block("entry");
    main_fn->entry_bb = entry_bb;
    main_fn->last_bb = entry_bb;
    mod->functions = main_fn;

    IRFunction *cur_fn = main_fn;
    IRBlock *cur_bb = entry_bb;

    BlockContext ctx_stack[64];
    int ctx_depth = 0;
    int loop_counter = 0;
    int if_counter = 0;

    char line[MAX_LINE];
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '\n' || *p == '#' || *p == ';') continue;

        /* Function declaration: fn name(a, b) { */
        if (strncmp(p, "fn ", 3) == 0 && strstr(p, "{")) {
            char fname[64];
            char *paren = strchr(p, '(');
            if (paren) {
                *paren = '\0';
                sscanf(p + 3, "%63s", fname);
                IRFunction *fn = calloc(1, sizeof(IRFunction));
                safe_strcpy(fn->name, fname, sizeof(fn->name));
                fn->ret_type = TYPE_I64;

                char *params_end = strchr(paren + 1, ')');
                if (params_end) {
                    *params_end = '\0';
                    char *tok = strtok(paren + 1, ", ");
                    while (tok && fn->num_params < MAX_PARAMS) {
                        safe_strcpy(fn->params[fn->num_params++], tok, 64);
                        tok = strtok(NULL, ", ");
                    }
                }
                IRBlock *fn_entry = create_block("entry");
                fn->entry_bb = fn_entry;
                fn->last_bb = fn_entry;

                fn->next = mod->functions;
                mod->functions = fn;

                cur_fn = fn;
                cur_bb = fn_entry;
                continue;
            } else {
                char *brace = strchr(p, '{');
                if (brace) *brace = '\0';
                sscanf(p + 3, "%63s", fname);
                IRFunction *fn = calloc(1, sizeof(IRFunction));
                safe_strcpy(fn->name, fname, sizeof(fn->name));
                fn->ret_type = TYPE_I64;
                IRBlock *fn_entry = create_block("entry");
                fn->entry_bb = fn_entry;
                fn->last_bb = fn_entry;

                fn->next = mod->functions;
                mod->functions = fn;

                cur_fn = fn;
                cur_bb = fn_entry;
                continue;
            }
        }

        /* While loop: while cond { */
        if (strncmp(p, "while ", 6) == 0 && strstr(p, "{")) {
            loop_counter++;
            char cond_str[256];
            char *brace = strchr(p, '{');
            *brace = '\0';
            safe_strcpy(cond_str, p + 6, sizeof(cond_str));

            char cond_name[64], body_name[64], exit_name[64];
            snprintf(cond_name, sizeof(cond_name), "while_cond_%d", loop_counter);
            snprintf(body_name, sizeof(body_name), "while_body_%d", loop_counter);
            snprintf(exit_name, sizeof(exit_name), "while_exit_%d", loop_counter);

            IRInst *br = calloc(1, sizeof(IRInst));
            br->op = OP_BR;
            safe_strcpy(br->target_then, cond_name, sizeof(br->target_then));
            append_inst(cur_bb, br);

            IRBlock *cond_bb = create_block(cond_name);
            cur_bb->next = cond_bb;

            IROperand cond_val = lower_condition(cond_bb, cond_str);

            IRInst *br_c = calloc(1, sizeof(IRInst));
            br_c->op = OP_BR_COND;
            br_c->src1 = cond_val;
            safe_strcpy(br_c->target_then, body_name, sizeof(br_c->target_then));
            safe_strcpy(br_c->target_else, exit_name, sizeof(br_c->target_else));
            append_inst(cond_bb, br_c);

            IRBlock *body_bb = create_block(body_name);
            cond_bb->next = body_bb;
            cur_bb = body_bb;

            BlockContext *ctx = &ctx_stack[ctx_depth++];
            ctx->kind = CTX_WHILE;
            safe_strcpy(ctx->cond_bb, cond_name, sizeof(ctx->cond_bb));
            safe_strcpy(ctx->body_bb, body_name, sizeof(ctx->body_bb));
            safe_strcpy(ctx->exit_bb, exit_name, sizeof(ctx->exit_bb));
            continue;
        }

        /* If condition: if cond { */
        if (strncmp(p, "if ", 3) == 0 && strstr(p, "{")) {
            if_counter++;
            char cond_str[256];
            char *brace = strchr(p, '{');
            *brace = '\0';
            safe_strcpy(cond_str, p + 3, sizeof(cond_str));

            char then_name[64], else_name[64], join_name[64];
            snprintf(then_name, sizeof(then_name), "if_then_%d", if_counter);
            snprintf(else_name, sizeof(else_name), "if_else_%d", if_counter);
            snprintf(join_name, sizeof(join_name), "if_join_%d", if_counter);

            IROperand cond_val = lower_condition(cur_bb, cond_str);

            IRInst *br_c = calloc(1, sizeof(IRInst));
            br_c->op = OP_BR_COND;
            br_c->src1 = cond_val;
            safe_strcpy(br_c->target_then, then_name, sizeof(br_c->target_then));
            safe_strcpy(br_c->target_else, else_name, sizeof(br_c->target_else));
            append_inst(cur_bb, br_c);

            IRBlock *then_bb = create_block(then_name);
            cur_bb->next = then_bb;
            cur_bb = then_bb;

            BlockContext *ctx = &ctx_stack[ctx_depth++];
            ctx->kind = CTX_IF;
            safe_strcpy(ctx->body_bb, then_name, sizeof(ctx->body_bb));
            safe_strcpy(ctx->else_bb, else_name, sizeof(ctx->else_bb));
            safe_strcpy(ctx->exit_bb, join_name, sizeof(ctx->exit_bb));
            ctx->has_else = 0;
            continue;
        }

        /* Else branch: } else { */
        if (strstr(p, "} else {") || strstr(p, "}else{")) {
            if (ctx_depth > 0 && ctx_stack[ctx_depth - 1].kind == CTX_IF) {
                BlockContext *ctx = &ctx_stack[ctx_depth - 1];
                ctx->has_else = 1;

                IRInst *br_join = calloc(1, sizeof(IRInst));
                br_join->op = OP_BR;
                safe_strcpy(br_join->target_then, ctx->exit_bb, sizeof(br_join->target_then));
                append_inst(cur_bb, br_join);

                IRBlock *else_bb = create_block(ctx->else_bb);
                cur_bb->next = else_bb;
                cur_bb = else_bb;
                continue;
            }
        }

        /* Closing brace: } */
        if (strcmp(p, "}\n") == 0 || strcmp(p, "}") == 0) {
            if (ctx_depth > 0) {
                BlockContext ctx = ctx_stack[--ctx_depth];
                if (ctx.kind == CTX_WHILE) {
                    IRInst *br_loop = calloc(1, sizeof(IRInst));
                    br_loop->op = OP_BR;
                    safe_strcpy(br_loop->target_then, ctx.cond_bb, sizeof(br_loop->target_then));
                    append_inst(cur_bb, br_loop);

                    IRBlock *exit_bb = create_block(ctx.exit_bb);
                    cur_bb->next = exit_bb;
                    cur_bb = exit_bb;
                } else if (ctx.kind == CTX_IF) {
                    IRInst *br_join = calloc(1, sizeof(IRInst));
                    br_join->op = OP_BR;
                    safe_strcpy(br_join->target_then, ctx.exit_bb, sizeof(br_join->target_then));
                    append_inst(cur_bb, br_join);

                    if (!ctx.has_else) {
                        IRBlock *else_bb = create_block(ctx.else_bb);
                        cur_bb->next = else_bb;
                        cur_bb = else_bb;
                        IRInst *br_else_join = calloc(1, sizeof(IRInst));
                        br_else_join->op = OP_BR;
                        safe_strcpy(br_else_join->target_then, ctx.exit_bb, sizeof(br_else_join->target_then));
                        append_inst(else_bb, br_else_join);
                    }

                    IRBlock *join_bb = create_block(ctx.exit_bb);
                    cur_bb->next = join_bb;
                    cur_bb = join_bb;
                }
                continue;
            } else {
                if (cur_fn != main_fn) {
                    IRInst *ret = calloc(1, sizeof(IRInst));
                    ret->op = OP_RET;
                    append_inst(cur_bb, ret);
                    cur_fn = main_fn;
                    cur_bb = main_fn->last_bb;
                }
                continue;
            }
        }

        /* Print statement */
        if (strncmp(p, "print ", 6) == 0) {
            char val_str[512];
            safe_strcpy(val_str, p + 6, sizeof(val_str));
            char *nl = strchr(val_str, '\n');
            if (nl) *nl = '\0';

            IRInst *in = calloc(1, sizeof(IRInst));
            if (val_str[0] == '"') {
                char clean[512];
                safe_strcpy(clean, val_str + 1, sizeof(clean));
                char *q = strrchr(clean, '"');
                if (q) *q = '\0';
                const char *sid = intern_string(mod, clean);
                in->op = OP_PRINT_STR;
                in->src1 = make_var(sid, TYPE_PTR);
            } else {
                in->op = OP_PRINT;
                in->src1 = parse_atom(val_str);
            }
            append_inst(cur_bb, in);
            continue;
        }

        /* Print string statement */
        if (strncmp(p, "print_str ", 10) == 0) {
            char val_str[512];
            safe_strcpy(val_str, p + 10, sizeof(val_str));
            char *nl = strchr(val_str, '\n');
            if (nl) *nl = '\0';
            IRInst *in = calloc(1, sizeof(IRInst));
            in->op = OP_PRINT_STR;
            in->src1 = parse_atom(val_str);
            append_inst(cur_bb, in);
            continue;
        }

        /* Assert statement: assert_eq a, b */
        if (strncmp(p, "assert_eq ", 10) == 0) {
            char left[64], right[64];
            if (sscanf(p + 10, "%63[^,], %63s", left, right) == 2) {
                IRInst *in = calloc(1, sizeof(IRInst));
                in->op = OP_ASSERT_EQ;
                in->src1 = parse_atom(left);
                in->src2 = parse_atom(right);
                append_inst(cur_bb, in);
                continue;
            }
        }

        /* Memory store: store64 [ptr + off] val */
        if (strncmp(p, "store64 [", 9) == 0 || strncmp(p, "store [", 7) == 0) {
            char *bracket = strchr(p, '[');
            char *close_b = strchr(bracket, ']');
            if (bracket && close_b) {
                *close_b = '\0';
                char *inner = bracket + 1;
                char *val_part = close_b + 1;
                while (*val_part == ' ' || *val_part == '\t') val_part++;
                char *nl = strchr(val_part, '\n');
                if (nl) *nl = '\0';

                char ptr_name[64];
                int64_t off = 0;
                sscanf(inner, "%63s + %lld", ptr_name, (long long *)&off);

                IRInst *in = calloc(1, sizeof(IRInst));
                in->op = OP_STORE64;
                in->src1 = make_var(ptr_name, TYPE_PTR);
                in->src2 = make_const_i64(off);
                in->dst = parse_atom(val_part);
                append_inst(cur_bb, in);
                continue;
            }
        }

        /* Call statement: call name(args) or call name */
        if (strncmp(p, "call ", 5) == 0) {
            char fname[64];
            char *paren = strchr(p, '(');
            if (paren) {
                *paren = '\0';
                sscanf(p + 5, "%63s", fname);
                IRInst *in = calloc(1, sizeof(IRInst));
                in->op = OP_CALL;
                safe_strcpy(in->target_then, fname, sizeof(in->target_then));

                char *params_end = strchr(paren + 1, ')');
                if (params_end) {
                    *params_end = '\0';
                    char *tok = strtok(paren + 1, ", ");
                    while (tok && in->num_args < MAX_PARAMS) {
                        in->args[in->num_args++] = parse_atom(tok);
                        tok = strtok(NULL, ", ");
                    }
                }
                append_inst(cur_bb, in);
                continue;
            } else {
                sscanf(p + 5, "%63s", fname);
                IRInst *in = calloc(1, sizeof(IRInst));
                in->op = OP_CALL;
                safe_strcpy(in->target_then, fname, sizeof(in->target_then));
                append_inst(cur_bb, in);
                continue;
            }
        }

        /* Return statement */
        if (strncmp(p, "return", 6) == 0) {
            char *rest = p + 6;
            while (*rest == ' ' || *rest == '\t') rest++;
            char *nl = strchr(rest, '\n');
            if (nl) *nl = '\0';

            IRInst *in = calloc(1, sizeof(IRInst));
            in->op = OP_RET;
            if (*rest != '\0') {
                in->src1 = lower_expression(cur_bb, rest, NULL);
            }
            append_inst(cur_bb, in);
            continue;
        }

        /* Variable assignment: let v = expr or v = expr */
        if (strncmp(p, "let ", 4) == 0 || strncmp(p, "local ", 6) == 0 || strchr(p, '=')) {
            char vname[64];
            char *eq = strchr(p, '=');
            if (eq) {
                *eq = '\0';
                char *lhs = p;
                if (strncmp(lhs, "let ", 4) == 0) lhs += 4;
                else if (strncmp(lhs, "local ", 6) == 0) lhs += 6;
                sscanf(lhs, "%63s", vname);

                char *rhs = eq + 1;
                while (*rhs == ' ') rhs++;

                lower_expression(cur_bb, rhs, vname);
                continue;
            }
        }
    }

    /* Add final return to main if missing */
    if (main_fn->entry_bb) {
        IRBlock *last = main_fn->entry_bb;
        while (last->next) last = last->next;
        if (!last->last || last->last->op != OP_RET) {
            IRInst *ret = calloc(1, sizeof(IRInst));
            ret->op = OP_RET;
            ret->src1 = make_const_i64(0);
            append_inst(last, ret);
        }
    }

    fclose(f);
    return mod;
}

/* ==============================================================================
 *                     N-IR DIRECT EXECUTION VM ENGINE
 * ============================================================================== */

typedef struct {
    char name[64];
    int64_t ival;
} VMVar;

typedef struct {
    uint8_t memory[VM_MEM_SIZE];
    uint64_t heap_ptr;
    VMVar vars[MAX_VARS];
    int num_vars;
    int assertions_passed;
} VMState;

static VMVar *vm_find_or_create_var(VMState *vm, const char *name) {
    for (int i = 0; i < vm->num_vars; i++) {
        if (strcmp(vm->vars[i].name, name) == 0) return &vm->vars[i];
    }
    if (vm->num_vars >= MAX_VARS) {
        fprintf(stderr, "[-] Fatal VM error: register variable table overflow\n");
        exit(1);
    }
    VMVar *v = &vm->vars[vm->num_vars++];
    safe_strcpy(v->name, name, sizeof(v->name));
    v->ival = 0;
    return v;
}

static int64_t vm_eval_operand(VMState *vm, const IROperand *op) {
    if (op->is_const) return op->ival;
    if (op->name[0] == '\0') return 0;
    return vm_find_or_create_var(vm, op->name)->ival;
}

static uint64_t vm_alloc(VMState *vm, uint64_t size) {
    uint64_t ptr = (vm->heap_ptr + 15) & ~15ULL;
    vm->heap_ptr = ptr + size;
    if (vm->heap_ptr >= VM_MEM_SIZE) {
        fprintf(stderr, "[-] Fatal VM error: out of simulated memory\n");
        exit(1);
    }
    return ptr;
}

int run_ir_vm(IRModule *mod) {
    VMState *vm = calloc(1, sizeof(VMState));
    vm->heap_ptr = 4096;

    IRFunction *main_fn = NULL;
    for (IRFunction *fn = mod->functions; fn; fn = fn->next) {
        if (strcmp(fn->name, "main") == 0) {
            main_fn = fn;
            break;
        }
    }
    if (!main_fn) {
        fprintf(stderr, "[-] VM Error: @main function not found\n");
        free(vm);
        return 1;
    }

    IRBlock *cur_bb = main_fn->entry_bb;
    int64_t exit_code = 0;

    while (cur_bb) {
        IRInst *in = cur_bb->first;
        IRBlock *next_bb = NULL;

        while (in) {
            if (in->op == OP_NOP) { in = in->next; continue; }

            int64_t s1 = vm_eval_operand(vm, &in->src1);
            int64_t s2 = vm_eval_operand(vm, &in->src2);
            int64_t res = 0;

            switch (in->op) {
                case OP_ASSIGN:
                    res = s1;
                    if (in->dst.name[0]) vm_find_or_create_var(vm, in->dst.name)->ival = res;
                    break;
                case OP_ADD:
                    res = s1 + s2;
                    if (in->dst.name[0]) vm_find_or_create_var(vm, in->dst.name)->ival = res;
                    break;
                case OP_SUB:
                    res = s1 - s2;
                    if (in->dst.name[0]) vm_find_or_create_var(vm, in->dst.name)->ival = res;
                    break;
                case OP_MUL:
                    res = s1 * s2;
                    if (in->dst.name[0]) vm_find_or_create_var(vm, in->dst.name)->ival = res;
                    break;
                case OP_SDIV:
                    res = (s2 != 0) ? (s1 / s2) : 0;
                    if (in->dst.name[0]) vm_find_or_create_var(vm, in->dst.name)->ival = res;
                    break;
                case OP_SREM:
                    res = (s2 != 0) ? (s1 % s2) : 0;
                    if (in->dst.name[0]) vm_find_or_create_var(vm, in->dst.name)->ival = res;
                    break;
                case OP_SHL:
                    res = s1 << s2;
                    if (in->dst.name[0]) vm_find_or_create_var(vm, in->dst.name)->ival = res;
                    break;
                case OP_SAR:
                    res = s1 >> s2;
                    if (in->dst.name[0]) vm_find_or_create_var(vm, in->dst.name)->ival = res;
                    break;
                case OP_AND:
                    res = s1 & s2;
                    if (in->dst.name[0]) vm_find_or_create_var(vm, in->dst.name)->ival = res;
                    break;
                case OP_OR:
                    res = s1 | s2;
                    if (in->dst.name[0]) vm_find_or_create_var(vm, in->dst.name)->ival = res;
                    break;
                case OP_XOR:
                    res = s1 ^ s2;
                    if (in->dst.name[0]) vm_find_or_create_var(vm, in->dst.name)->ival = res;
                    break;
                case OP_ICMP_EQ:
                    res = (s1 == s2);
                    if (in->dst.name[0]) vm_find_or_create_var(vm, in->dst.name)->ival = res;
                    break;
                case OP_ICMP_NE:
                    res = (s1 != s2);
                    if (in->dst.name[0]) vm_find_or_create_var(vm, in->dst.name)->ival = res;
                    break;
                case OP_ICMP_SLT:
                    res = (s1 < s2);
                    if (in->dst.name[0]) vm_find_or_create_var(vm, in->dst.name)->ival = res;
                    break;
                case OP_ICMP_SLE:
                    res = (s1 <= s2);
                    if (in->dst.name[0]) vm_find_or_create_var(vm, in->dst.name)->ival = res;
                    break;
                case OP_ICMP_SGT:
                    res = (s1 > s2);
                    if (in->dst.name[0]) vm_find_or_create_var(vm, in->dst.name)->ival = res;
                    break;
                case OP_ICMP_SGE:
                    res = (s1 >= s2);
                    if (in->dst.name[0]) vm_find_or_create_var(vm, in->dst.name)->ival = res;
                    break;
                case OP_ALLOC:
                    res = vm_alloc(vm, s1);
                    if (in->dst.name[0]) vm_find_or_create_var(vm, in->dst.name)->ival = res;
                    break;
                case OP_STORE64: {
                    uint64_t addr = (uint64_t)s1 + in->src2.ival;
                    int64_t val = vm_eval_operand(vm, &in->dst);
                    if (addr + 8 <= VM_MEM_SIZE) {
                        memcpy(&vm->memory[addr], &val, 8);
                    }
                    break;
                }
                case OP_LOAD64: {
                    uint64_t addr = (uint64_t)s1 + in->src2.ival;
                    if (addr + 8 <= VM_MEM_SIZE) {
                        memcpy(&res, &vm->memory[addr], 8);
                    }
                    if (in->dst.name[0]) vm_find_or_create_var(vm, in->dst.name)->ival = res;
                    break;
                }
                case OP_PRINT:
                    printf("%lld\n", (long long)s1);
                    break;
                case OP_PRINT_STR: {
                    if (in->src1.name[0] == '@') {
                        for (int i = 0; i < mod->num_strings; i++) {
                            if (strcmp(mod->strings[i].id, in->src1.name) == 0) {
                                printf("%s\n", mod->strings[i].text);
                                break;
                            }
                        }
                    } else {
                        uint64_t ptr = (uint64_t)s1;
                        if (ptr < VM_MEM_SIZE) {
                            printf("%s\n", (char *)&vm->memory[ptr]);
                        }
                    }
                    break;
                }
                case OP_ASSERT_EQ:
                    if (s1 != s2) {
                        fprintf(stderr, "[-] N-IR Assertion Failed: %lld != %lld\n", (long long)s1, (long long)s2);
                        free(vm);
                        return 1;
                    }
                    vm->assertions_passed++;
                    break;
                case OP_BR:
                    for (IRBlock *b = main_fn->entry_bb; b; b = b->next) {
                        if (strcmp(b->name, in->target_then) == 0) {
                            next_bb = b;
                            break;
                        }
                    }
                    break;
                case OP_BR_COND:
                    if (s1) {
                        for (IRBlock *b = main_fn->entry_bb; b; b = b->next) {
                            if (strcmp(b->name, in->target_then) == 0) {
                                next_bb = b;
                                break;
                            }
                        }
                    } else {
                        for (IRBlock *b = main_fn->entry_bb; b; b = b->next) {
                            if (strcmp(b->name, in->target_else) == 0) {
                                next_bb = b;
                                break;
                            }
                        }
                    }
                    break;
                case OP_RET:
                    exit_code = s1;
                    cur_bb = NULL;
                    next_bb = NULL;
                    break;
                default:
                    break;
            }

            if (next_bb || !cur_bb) break;
            in = in->next;
        }

        if (next_bb) {
            cur_bb = next_bb;
        } else if (cur_bb) {
            cur_bb = cur_bb->next;
        }
    }

    free(vm);
    return (int)exit_code;
}

/* ==============================================================================
 *                N-IR TO NATIVE x86-64 LINUX ELF64 CODEGEN EMITTER
 * ============================================================================== */

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
#pragma pack(pop)

typedef struct {
    uint8_t *data;
    size_t size;
    size_t capacity;
} ByteBuffer;

static void buf_init(ByteBuffer *b) {
    b->capacity = 65536;
    b->data = malloc(b->capacity);
    b->size = 0;
}

static void buf_emit_byte(ByteBuffer *b, uint8_t byte) {
    if (b->size >= b->capacity) {
        b->capacity *= 2;
        b->data = realloc(b->data, b->capacity);
    }
    b->data[b->size++] = byte;
}

static void buf_emit_bytes(ByteBuffer *b, const uint8_t *bytes, size_t len) {
    for (size_t i = 0; i < len; i++) {
        buf_emit_byte(b, bytes[i]);
    }
}

static void buf_emit_i32(ByteBuffer *b, int32_t val) {
    buf_emit_byte(b, val & 0xFF);
    buf_emit_byte(b, (val >> 8) & 0xFF);
    buf_emit_byte(b, (val >> 16) & 0xFF);
    buf_emit_byte(b, (val >> 24) & 0xFF);
}

static void buf_emit_i64(ByteBuffer *b, int64_t val) {
    buf_emit_i32(b, (int32_t)(val & 0xFFFFFFFF));
    buf_emit_i32(b, (int32_t)((val >> 32) & 0xFFFFFFFF));
}

typedef struct {
    char name[128];
    size_t offset;
} CodeLabel;

typedef struct {
    size_t patch_offset;
    char target_name[128];
} CodeFixup;

#define MAX_CODE_LABELS 2048
#define MAX_CODE_FIXUPS 4096

typedef struct {
    CodeLabel labels[MAX_CODE_LABELS];
    int num_labels;
    CodeFixup fixups[MAX_CODE_FIXUPS];
    int num_fixups;
} LinkerContext;

static void add_code_label(LinkerContext *ctx, const char *name, size_t offset) {
    if (ctx->num_labels < MAX_CODE_LABELS) {
        safe_strcpy(ctx->labels[ctx->num_labels].name, name, 128);
        ctx->labels[ctx->num_labels].offset = offset;
        ctx->num_labels++;
    }
}

static void add_code_fixup(LinkerContext *ctx, const char *target, size_t patch_offset) {
    if (ctx->num_fixups < MAX_CODE_FIXUPS) {
        safe_strcpy(ctx->fixups[ctx->num_fixups].target_name, target, 128);
        ctx->fixups[ctx->num_fixups].patch_offset = patch_offset;
        ctx->num_fixups++;
    }
}

static void link_code(ByteBuffer *code, LinkerContext *ctx) {
    for (int i = 0; i < ctx->num_fixups; i++) {
        CodeFixup *f = &ctx->fixups[i];
        size_t target_offset = 0;
        int found = 0;
        for (int l = 0; l < ctx->num_labels; l++) {
            if (strcmp(ctx->labels[l].name, f->target_name) == 0) {
                target_offset = ctx->labels[l].offset;
                found = 1;
                break;
            }
        }
        if (found) {
            int32_t rel = (int32_t)(target_offset - (f->patch_offset + 4));
            code->data[f->patch_offset] = rel & 0xFF;
            code->data[f->patch_offset + 1] = (rel >> 8) & 0xFF;
            code->data[f->patch_offset + 2] = (rel >> 16) & 0xFF;
            code->data[f->patch_offset + 3] = (rel >> 24) & 0xFF;
        } else {
            fprintf(stderr, "[-] Linker warning: unresolved label '%s'\n", f->target_name);
        }
    }
}

typedef struct {
    char name[64];
    int slot;
} VarSlot;

typedef struct {
    VarSlot vars[512];
    int num_vars;
} FunctionFrame;

static int get_var_slot(FunctionFrame *frame, const char *name) {
    for (int i = 0; i < frame->num_vars; i++) {
        if (strcmp(frame->vars[i].name, name) == 0) {
            return frame->vars[i].slot;
        }
    }
    if (frame->num_vars < 512) {
        int s = frame->num_vars;
        safe_strcpy(frame->vars[s].name, name, 64);
        frame->vars[s].slot = s;
        frame->num_vars++;
        return s;
    }
    return 0;
}

static void emit_load_operand(ByteBuffer *b, FunctionFrame *frame, const IROperand *op, int reg) {
    if (op->is_const) {
        if (reg == 0) {
            buf_emit_byte(b, 0x48); buf_emit_byte(b, 0xB8); /* movabs rax, imm64 */
            buf_emit_i64(b, op->ival);
        } else {
            buf_emit_byte(b, 0x48); buf_emit_byte(b, 0xB9); /* movabs rcx, imm64 */
            buf_emit_i64(b, op->ival);
        }
    } else {
        int slot = get_var_slot(frame, op->name);
        int32_t disp = -8 * (slot + 1);
        buf_emit_byte(b, 0x48); buf_emit_byte(b, 0x8B);
        buf_emit_byte(b, (reg == 0) ? 0x85 : 0x8D); /* mov rax/rcx, [rbp + disp32] */
        buf_emit_i32(b, disp);
    }
}

static void emit_store_result(ByteBuffer *b, FunctionFrame *frame, const IROperand *dst, int reg) {
    if (!dst->name[0]) return;
    int slot = get_var_slot(frame, dst->name);
    int32_t disp = -8 * (slot + 1);
    buf_emit_byte(b, 0x48); buf_emit_byte(b, 0x89);
    buf_emit_byte(b, (reg == 0) ? 0x85 : 0x95); /* mov [rbp + disp32], rax/rdx */
    buf_emit_i32(b, disp);
}

int emit_linux_elf64(IRModule *mod, const char *out_path) {
    LinkerContext ctx;
    memset(&ctx, 0, sizeof(ctx));

    ByteBuffer code;
    buf_init(&code);

    const uint64_t ELF_BASE = 0x400000;
    const uint64_t TEXT_VADDR = 0x401000;
    const uint64_t DATA_VADDR = 0x408000;
    const uint64_t HEAP_ARENA = 0x500000;

    /* Build data section: heap pointer slot at 0x408000 + string pool */
    ByteBuffer data;
    buf_init(&data);
    buf_emit_i64(&data, (int64_t)HEAP_ARENA); /* Initial heap pointer slot */

    uint64_t str_vaddrs[MAX_STRINGS];
    for (int i = 0; i < mod->num_strings; i++) {
        str_vaddrs[i] = DATA_VADDR + data.size;
        size_t slen = strlen(mod->strings[i].text);
        buf_emit_bytes(&data, (const uint8_t *)mod->strings[i].text, slen + 1);
    }

    /* 1. Emit _start (Entry point at offset 0 -> 0x401000) */
    add_code_label(&ctx, "_start", code.size);
    /* Initialize heap pointer: movabs rax, HEAP_ARENA; movabs [DATA_VADDR], rax */
    buf_emit_byte(&code, 0x48); buf_emit_byte(&code, 0xB8);
    buf_emit_i64(&code, (int64_t)HEAP_ARENA);
    buf_emit_byte(&code, 0x48); buf_emit_byte(&code, 0xA3);
    buf_emit_i64(&code, (int64_t)DATA_VADDR);

    /* call @main */
    buf_emit_byte(&code, 0xE8);
    add_code_fixup(&ctx, "@main", code.size);
    buf_emit_i32(&code, 0);

    /* Exit with rax: mov rdi, rax; mov eax, 60; syscall */
    buf_emit_bytes(&code, (const uint8_t[]){0x48, 0x89, 0xC7, 0xB8, 0x3C, 0x00, 0x00, 0x00, 0x0F, 0x05}, 10);

    /* 2. Emit _print_i64 subroutine */
    add_code_label(&ctx, "_print_i64", code.size);
    buf_emit_byte(&code, 0x55);                                     /* push rbp */
    buf_emit_bytes(&code, (const uint8_t[]){0x48, 0x89, 0xE5}, 3);  /* mov rbp, rsp */
    buf_emit_bytes(&code, (const uint8_t[]){0x48, 0x83, 0xEC, 0x30}, 4); /* sub rsp, 48 */
    buf_emit_bytes(&code, (const uint8_t[]){0x48, 0x8D, 0x7D, 0xFF}, 4); /* lea rdi, [rbp - 1] */
    buf_emit_bytes(&code, (const uint8_t[]){0xC6, 0x07, 0x0A}, 3);  /* mov byte [rdi], 10 (\n) */
    buf_emit_bytes(&code, (const uint8_t[]){0x49, 0xC7, 0xC0, 0x01, 0x00, 0x00, 0x00}, 7); /* mov r8, 1 */
    buf_emit_bytes(&code, (const uint8_t[]){0x48, 0x85, 0xC0}, 3);  /* test rax, rax */
    buf_emit_bytes(&code, (const uint8_t[]){0x0F, 0x89}, 2);        /* jns .p_i64_pos */
    add_code_fixup(&ctx, ".p_i64_pos", code.size);
    buf_emit_i32(&code, 0);

    /* negative */
    buf_emit_bytes(&code, (const uint8_t[]){0x48, 0xF7, 0xD8}, 3);  /* neg rax */
    buf_emit_bytes(&code, (const uint8_t[]){0x49, 0xC7, 0xC1, 0x01, 0x00, 0x00, 0x00}, 7); /* mov r9, 1 */
    buf_emit_byte(&code, 0xE9);                                     /* jmp .p_i64_convert */
    add_code_fixup(&ctx, ".p_i64_convert", code.size);
    buf_emit_i32(&code, 0);

    /* .p_i64_pos */
    add_code_label(&ctx, ".p_i64_pos", code.size);
    buf_emit_bytes(&code, (const uint8_t[]){0x49, 0xC7, 0xC1, 0x00, 0x00, 0x00, 0x00}, 7); /* mov r9, 0 */

    /* .p_i64_convert */
    add_code_label(&ctx, ".p_i64_convert", code.size);
    buf_emit_bytes(&code, (const uint8_t[]){0x48, 0xC7, 0xC1, 0x0A, 0x00, 0x00, 0x00}, 7); /* mov rcx, 10 */

    /* .p_i64_loop */
    add_code_label(&ctx, ".p_i64_loop", code.size);
    buf_emit_bytes(&code, (const uint8_t[]){0x48, 0x31, 0xD2}, 3);  /* xor rdx, rdx */
    buf_emit_bytes(&code, (const uint8_t[]){0x48, 0xF7, 0xF1}, 3);  /* div rcx */
    buf_emit_bytes(&code, (const uint8_t[]){0x80, 0xC2, 0x30}, 3);  /* add dl, '0' */
    buf_emit_bytes(&code, (const uint8_t[]){0x48, 0xFF, 0xCF}, 3);  /* dec rdi */
    buf_emit_bytes(&code, (const uint8_t[]){0x88, 0x17}, 2);        /* mov [rdi], dl */
    buf_emit_bytes(&code, (const uint8_t[]){0x49, 0xFF, 0xC0}, 3);  /* inc r8 */
    buf_emit_bytes(&code, (const uint8_t[]){0x48, 0x85, 0xC0}, 3);  /* test rax, rax */
    buf_emit_bytes(&code, (const uint8_t[]){0x0F, 0x85}, 2);        /* jnz .p_i64_loop */
    add_code_fixup(&ctx, ".p_i64_loop", code.size);
    buf_emit_i32(&code, 0);

    /* check sign */
    buf_emit_bytes(&code, (const uint8_t[]){0x4D, 0x85, 0xC9}, 3);  /* test r9, r9 */
    buf_emit_bytes(&code, (const uint8_t[]){0x0F, 0x84}, 2);        /* jz .p_i64_write */
    add_code_fixup(&ctx, ".p_i64_write", code.size);
    buf_emit_i32(&code, 0);
    buf_emit_bytes(&code, (const uint8_t[]){0x48, 0xFF, 0xCF}, 3);  /* dec rdi */
    buf_emit_bytes(&code, (const uint8_t[]){0xC6, 0x07, 0x2D}, 3);  /* mov byte [rdi], '-' */
    buf_emit_bytes(&code, (const uint8_t[]){0x49, 0xFF, 0xC0}, 3);  /* inc r8 */

    /* .p_i64_write: sys_write(1, rdi, r8) */
    add_code_label(&ctx, ".p_i64_write", code.size);
    buf_emit_bytes(&code, (const uint8_t[]){0xB8, 0x01, 0x00, 0x00, 0x00}, 5); /* mov eax, 1 */
    buf_emit_bytes(&code, (const uint8_t[]){0x48, 0x89, 0xFE}, 3);  /* mov rsi, rdi */
    buf_emit_bytes(&code, (const uint8_t[]){0xBF, 0x01, 0x00, 0x00, 0x00}, 5); /* mov edi, 1 */
    buf_emit_bytes(&code, (const uint8_t[]){0x4C, 0x89, 0xC2}, 3);  /* mov rdx, r8 */
    buf_emit_bytes(&code, (const uint8_t[]){0x0F, 0x05}, 2);        /* syscall */
    buf_emit_bytes(&code, (const uint8_t[]){0x48, 0x89, 0xEC, 0x5D, 0xC3}, 5); /* mov rsp, rbp; pop rbp; ret */

    /* 3. Emit _print_str subroutine */
    add_code_label(&ctx, "_print_str", code.size);
    buf_emit_byte(&code, 0x55);                                     /* push rbp */
    buf_emit_bytes(&code, (const uint8_t[]){0x48, 0x89, 0xE5}, 3);  /* mov rbp, rsp */
    buf_emit_bytes(&code, (const uint8_t[]){0x56, 0x57, 0x52}, 3);  /* push rsi, push rdi, push rdx */
    buf_emit_bytes(&code, (const uint8_t[]){0x48, 0x89, 0xC6}, 3);  /* mov rsi, rax */
    buf_emit_bytes(&code, (const uint8_t[]){0x48, 0x31, 0xD2}, 3);  /* xor rdx, rdx */

    /* .p_str_loop */
    add_code_label(&ctx, ".p_str_loop", code.size);
    buf_emit_bytes(&code, (const uint8_t[]){0x80, 0x3C, 0x16, 0x00}, 4); /* cmp byte [rsi + rdx], 0 */
    buf_emit_bytes(&code, (const uint8_t[]){0x0F, 0x84}, 2);        /* jz .p_str_write */
    add_code_fixup(&ctx, ".p_str_write", code.size);
    buf_emit_i32(&code, 0);
    buf_emit_bytes(&code, (const uint8_t[]){0x48, 0xFF, 0xC2}, 3);  /* inc rdx */
    buf_emit_byte(&code, 0xE9);                                     /* jmp .p_str_loop */
    add_code_fixup(&ctx, ".p_str_loop", code.size);
    buf_emit_i32(&code, 0);

    /* .p_str_write: sys_write(1, rsi, rdx) */
    add_code_label(&ctx, ".p_str_write", code.size);
    buf_emit_bytes(&code, (const uint8_t[]){0xB8, 0x01, 0x00, 0x00, 0x00}, 5); /* mov eax, 1 */
    buf_emit_bytes(&code, (const uint8_t[]){0xBF, 0x01, 0x00, 0x00, 0x00}, 5); /* mov edi, 1 */
    buf_emit_bytes(&code, (const uint8_t[]){0x0F, 0x05}, 2);        /* syscall */

    /* newline */
    buf_emit_bytes(&code, (const uint8_t[]){0x6A, 0x0A}, 2);        /* push 10 (\n) */
    buf_emit_bytes(&code, (const uint8_t[]){0x48, 0x89, 0xE6}, 3);  /* mov rsi, rsp */
    buf_emit_bytes(&code, (const uint8_t[]){0xB8, 0x01, 0x00, 0x00, 0x00}, 5); /* mov eax, 1 */
    buf_emit_bytes(&code, (const uint8_t[]){0xBF, 0x01, 0x00, 0x00, 0x00}, 5); /* mov edi, 1 */
    buf_emit_bytes(&code, (const uint8_t[]){0xBA, 0x01, 0x00, 0x00, 0x00}, 5); /* mov edx, 1 */
    buf_emit_bytes(&code, (const uint8_t[]){0x0F, 0x05}, 2);        /* syscall */
    buf_emit_byte(&code, 0x58);                                     /* pop rax */

    buf_emit_bytes(&code, (const uint8_t[]){0x5A, 0x5F, 0x5E}, 3);  /* pop rdx, pop rdi, pop rsi */
    buf_emit_bytes(&code, (const uint8_t[]){0x48, 0x89, 0xEC, 0x5D, 0xC3}, 5); /* mov rsp, rbp; pop rbp; ret */

    /* 4. Emit Functions */
    for (IRFunction *fn = mod->functions; fn; fn = fn->next) {
        char fn_label[128];
        snprintf(fn_label, sizeof(fn_label), "@%s", fn->name);
        add_code_label(&ctx, fn_label, code.size);

        FunctionFrame frame;
        memset(&frame, 0, sizeof(frame));

        /* Scan all variables across all blocks of fn */
        for (int p = 0; p < fn->num_params; p++) {
            get_var_slot(&frame, fn->params[p]);
        }
        for (IRBlock *b = fn->entry_bb; b; b = b->next) {
            for (IRInst *in = b->first; in; in = in->next) {
                if (in->dst.name[0] && !in->dst.is_const) get_var_slot(&frame, in->dst.name);
                if (in->src1.name[0] && !in->src1.is_const) get_var_slot(&frame, in->src1.name);
                if (in->src2.name[0] && !in->src2.is_const) get_var_slot(&frame, in->src2.name);
            }
        }

        int32_t stack_size = (frame.num_vars * 8 + 15) & ~15;

        /* Prologue */
        buf_emit_byte(&code, 0x55);                                     /* push rbp */
        buf_emit_bytes(&code, (const uint8_t[]){0x48, 0x89, 0xE5}, 3);  /* mov rbp, rsp */
        if (stack_size > 0) {
            buf_emit_bytes(&code, (const uint8_t[]){0x48, 0x81, 0xEC}, 3); /* sub rsp, imm32 */
            buf_emit_i32(&code, stack_size);
        }

        /* Store params from ABI registers */
        const uint8_t reg_modrm[] = {0xBD, 0xB5, 0x95, 0x8D}; /* rdi, rsi, rdx, rcx */
        for (int p = 0; p < fn->num_params && p < 4; p++) {
            int slot = get_var_slot(&frame, fn->params[p]);
            int32_t disp = -8 * (slot + 1);
            buf_emit_byte(&code, 0x48); buf_emit_byte(&code, 0x89);
            buf_emit_byte(&code, reg_modrm[p]);
            buf_emit_i32(&code, disp);
        }

        /* Emit Basic Blocks */
        for (IRBlock *b = fn->entry_bb; b; b = b->next) {
            add_code_label(&ctx, b->name, code.size);

            for (IRInst *in = b->first; in; in = in->next) {
                if (in->op == OP_NOP) continue;

                switch (in->op) {
                    case OP_ASSIGN:
                        emit_load_operand(&code, &frame, &in->src1, 0);
                        emit_store_result(&code, &frame, &in->dst, 0);
                        break;
                    case OP_ADD:
                        emit_load_operand(&code, &frame, &in->src1, 0);
                        emit_load_operand(&code, &frame, &in->src2, 1);
                        buf_emit_bytes(&code, (const uint8_t[]){0x48, 0x01, 0xC8}, 3); /* add rax, rcx */
                        emit_store_result(&code, &frame, &in->dst, 0);
                        break;
                    case OP_SUB:
                        emit_load_operand(&code, &frame, &in->src1, 0);
                        emit_load_operand(&code, &frame, &in->src2, 1);
                        buf_emit_bytes(&code, (const uint8_t[]){0x48, 0x29, 0xC8}, 3); /* sub rax, rcx */
                        emit_store_result(&code, &frame, &in->dst, 0);
                        break;
                    case OP_MUL:
                        emit_load_operand(&code, &frame, &in->src1, 0);
                        emit_load_operand(&code, &frame, &in->src2, 1);
                        buf_emit_bytes(&code, (const uint8_t[]){0x48, 0x0F, 0xAF, 0xC1}, 4); /* imul rax, rcx */
                        emit_store_result(&code, &frame, &in->dst, 0);
                        break;
                    case OP_SDIV:
                        emit_load_operand(&code, &frame, &in->src1, 0);
                        emit_load_operand(&code, &frame, &in->src2, 1);
                        buf_emit_bytes(&code, (const uint8_t[]){0x48, 0x99, 0x48, 0xF7, 0xF9}, 5); /* cqo; idiv rcx */
                        emit_store_result(&code, &frame, &in->dst, 0);
                        break;
                    case OP_SREM:
                        emit_load_operand(&code, &frame, &in->src1, 0);
                        emit_load_operand(&code, &frame, &in->src2, 1);
                        buf_emit_bytes(&code, (const uint8_t[]){0x48, 0x99, 0x48, 0xF7, 0xF9}, 5); /* cqo; idiv rcx */
                        emit_store_result(&code, &frame, &in->dst, 1); /* store rdx */
                        break;
                    case OP_SHL:
                        emit_load_operand(&code, &frame, &in->src1, 0);
                        emit_load_operand(&code, &frame, &in->src2, 1);
                        buf_emit_bytes(&code, (const uint8_t[]){0x48, 0xD3, 0xE0}, 3); /* shl rax, cl */
                        emit_store_result(&code, &frame, &in->dst, 0);
                        break;
                    case OP_SAR:
                        emit_load_operand(&code, &frame, &in->src1, 0);
                        emit_load_operand(&code, &frame, &in->src2, 1);
                        buf_emit_bytes(&code, (const uint8_t[]){0x48, 0xD3, 0xF8}, 3); /* sar rax, cl */
                        emit_store_result(&code, &frame, &in->dst, 0);
                        break;
                    case OP_AND:
                        emit_load_operand(&code, &frame, &in->src1, 0);
                        emit_load_operand(&code, &frame, &in->src2, 1);
                        buf_emit_bytes(&code, (const uint8_t[]){0x48, 0x21, 0xC8}, 3); /* and rax, rcx */
                        emit_store_result(&code, &frame, &in->dst, 0);
                        break;
                    case OP_OR:
                        emit_load_operand(&code, &frame, &in->src1, 0);
                        emit_load_operand(&code, &frame, &in->src2, 1);
                        buf_emit_bytes(&code, (const uint8_t[]){0x48, 0x09, 0xC8}, 3); /* or rax, rcx */
                        emit_store_result(&code, &frame, &in->dst, 0);
                        break;
                    case OP_XOR:
                        emit_load_operand(&code, &frame, &in->src1, 0);
                        emit_load_operand(&code, &frame, &in->src2, 1);
                        buf_emit_bytes(&code, (const uint8_t[]){0x48, 0x31, 0xC8}, 3); /* xor rax, rcx */
                        emit_store_result(&code, &frame, &in->dst, 0);
                        break;
                    case OP_NEG:
                        emit_load_operand(&code, &frame, &in->src1, 0);
                        buf_emit_bytes(&code, (const uint8_t[]){0x48, 0xF7, 0xD8}, 3); /* neg rax */
                        emit_store_result(&code, &frame, &in->dst, 0);
                        break;
                    case OP_ICMP_EQ: case OP_ICMP_NE: case OP_ICMP_SLT:
                    case OP_ICMP_SLE: case OP_ICMP_SGT: case OP_ICMP_SGE: {
                        emit_load_operand(&code, &frame, &in->src1, 0);
                        emit_load_operand(&code, &frame, &in->src2, 1);
                        buf_emit_bytes(&code, (const uint8_t[]){0x48, 0x39, 0xC8}, 3); /* cmp rax, rcx */
                        uint8_t set_op = 0x94;
                        if (in->op == OP_ICMP_NE) set_op = 0x95;
                        else if (in->op == OP_ICMP_SLT) set_op = 0x9C;
                        else if (in->op == OP_ICMP_SLE) set_op = 0x9E;
                        else if (in->op == OP_ICMP_SGT) set_op = 0x9F;
                        else if (in->op == OP_ICMP_SGE) set_op = 0x9D;
                        buf_emit_bytes(&code, (const uint8_t[]){0x0F, set_op, 0xC0}, 3);
                        buf_emit_bytes(&code, (const uint8_t[]){0x48, 0x0F, 0xB6, 0xC0}, 4); /* movzx rax, al */
                        emit_store_result(&code, &frame, &in->dst, 0);
                        break;
                    }
                    case OP_ALLOC: {
                        emit_load_operand(&code, &frame, &in->src1, 0); /* size in rax */
                        buf_emit_bytes(&code, (const uint8_t[]){0x48, 0x83, 0xC0, 0x0F}, 4); /* add rax, 15 */
                        buf_emit_bytes(&code, (const uint8_t[]){0x48, 0x83, 0xE0, 0xF0}, 4); /* and rax, -16 */
                        /* movabs rcx, [DATA_VADDR] */
                        buf_emit_bytes(&code, (const uint8_t[]){0x48, 0x8B, 0x0C, 0x25}, 4);
                        buf_emit_i32(&code, (int32_t)DATA_VADDR);
                        buf_emit_bytes(&code, (const uint8_t[]){0x48, 0x89, 0xCA}, 3); /* mov rdx, rcx (res) */
                        buf_emit_bytes(&code, (const uint8_t[]){0x48, 0x01, 0xC1}, 3); /* add rcx, rax */
                        /* movabs [DATA_VADDR], rcx */
                        buf_emit_bytes(&code, (const uint8_t[]){0x48, 0x89, 0x0C, 0x25}, 4);
                        buf_emit_i32(&code, (int32_t)DATA_VADDR);
                        emit_store_result(&code, &frame, &in->dst, 1); /* store rdx to dst */
                        break;
                    }
                    case OP_LOAD64:
                        emit_load_operand(&code, &frame, &in->src1, 0); /* ptr -> rax */
                        buf_emit_bytes(&code, (const uint8_t[]){0x48, 0x8B, 0x80}, 3); /* mov rax, [rax + disp32] */
                        buf_emit_i32(&code, (int32_t)in->src2.ival);
                        emit_store_result(&code, &frame, &in->dst, 0);
                        break;
                    case OP_STORE64:
                        emit_load_operand(&code, &frame, &in->src1, 0); /* ptr -> rax */
                        emit_load_operand(&code, &frame, &in->dst, 1);  /* val -> rcx */
                        buf_emit_bytes(&code, (const uint8_t[]){0x48, 0x89, 0x88}, 3); /* mov [rax + disp32], rcx */
                        buf_emit_i32(&code, (int32_t)in->src2.ival);
                        break;
                    case OP_PRINT:
                        emit_load_operand(&code, &frame, &in->src1, 0);
                        buf_emit_byte(&code, 0xE8); /* call _print_i64 */
                        add_code_fixup(&ctx, "_print_i64", code.size);
                        buf_emit_i32(&code, 0);
                        break;
                    case OP_PRINT_STR:
                        if (in->src1.name[0] == '@') {
                            uint64_t target_addr = DATA_VADDR;
                            for (int s = 0; s < mod->num_strings; s++) {
                                if (strcmp(mod->strings[s].id, in->src1.name) == 0) {
                                    target_addr = str_vaddrs[s];
                                    break;
                                }
                            }
                            buf_emit_byte(&code, 0x48); buf_emit_byte(&code, 0xB8); /* movabs rax, imm64 */
                            buf_emit_i64(&code, (int64_t)target_addr);
                        } else {
                            emit_load_operand(&code, &frame, &in->src1, 0);
                        }
                        buf_emit_byte(&code, 0xE8); /* call _print_str */
                        add_code_fixup(&ctx, "_print_str", code.size);
                        buf_emit_i32(&code, 0);
                        break;
                    case OP_ASSERT_EQ:
                        emit_load_operand(&code, &frame, &in->src1, 0);
                        emit_load_operand(&code, &frame, &in->src2, 1);
                        buf_emit_bytes(&code, (const uint8_t[]){0x48, 0x39, 0xC8}, 3); /* cmp rax, rcx */
                        buf_emit_bytes(&code, (const uint8_t[]){0x74, 0x0C}, 2);        /* je +12 (.pass) */
                        buf_emit_bytes(&code, (const uint8_t[]){0xB8, 0x3C, 0x00, 0x00, 0x00}, 5); /* mov eax, 60 */
                        buf_emit_bytes(&code, (const uint8_t[]){0xBF, 0x01, 0x00, 0x00, 0x00}, 5); /* mov edi, 1 */
                        buf_emit_bytes(&code, (const uint8_t[]){0x0F, 0x05}, 2);        /* syscall */
                        break;
                    case OP_BR:
                        buf_emit_byte(&code, 0xE9); /* jmp rel32 */
                        add_code_fixup(&ctx, in->target_then, code.size);
                        buf_emit_i32(&code, 0);
                        break;
                    case OP_BR_COND:
                        emit_load_operand(&code, &frame, &in->src1, 0);
                        buf_emit_bytes(&code, (const uint8_t[]){0x48, 0x85, 0xC0}, 3); /* test rax, rax */
                        buf_emit_bytes(&code, (const uint8_t[]){0x0F, 0x85}, 2);        /* jnz rel32 (then) */
                        add_code_fixup(&ctx, in->target_then, code.size);
                        buf_emit_i32(&code, 0);
                        buf_emit_byte(&code, 0xE9);                                     /* jmp rel32 (else) */
                        add_code_fixup(&ctx, in->target_else, code.size);
                        buf_emit_i32(&code, 0);
                        break;
                    case OP_RET:
                        if (in->src1.name[0] || in->src1.is_const) {
                            emit_load_operand(&code, &frame, &in->src1, 0);
                        } else {
                            buf_emit_bytes(&code, (const uint8_t[]){0x48, 0x31, 0xC0}, 3); /* xor rax, rax */
                        }
                        buf_emit_bytes(&code, (const uint8_t[]){0x48, 0x89, 0xEC, 0x5D, 0xC3}, 5); /* mov rsp, rbp; pop rbp; ret */
                        break;
                    case OP_CALL: {
                        for (int a = 0; a < in->num_args && a < 4; a++) {
                            emit_load_operand(&code, &frame, &in->args[a], 0);
                            const uint8_t to_reg[] = {0xC7, 0xC6, 0xC2, 0xC1}; /* rdi, rsi, rdx, rcx */
                            buf_emit_bytes(&code, (const uint8_t[]){0x48, 0x89, to_reg[a]}, 3);
                        }
                        char fn_call_label[128];
                        snprintf(fn_call_label, sizeof(fn_call_label), "@%s", in->target_then);
                        buf_emit_byte(&code, 0xE8); /* call rel32 */
                        add_code_fixup(&ctx, fn_call_label, code.size);
                        buf_emit_i32(&code, 0);
                        if (in->dst.name[0]) {
                            emit_store_result(&code, &frame, &in->dst, 0);
                        }
                        break;
                    }
                    default:
                        break;
                }
            }
        }
    }

    /* 5. Link relative branches and subroutine calls */
    link_code(&code, &ctx);

    /* 6. Layout and assemble standalone Linux ELF64 binary */
    const size_t TEXT_OFF = 0x1000;
    const size_t DATA_OFF = 0x8000;
    size_t total_file_size = DATA_OFF + ((data.size + 4095) & ~4095UL);
    if (total_file_size < 0x9000) total_file_size = 0x9000;

    uint8_t *elf_buf = calloc(1, total_file_size);

    /* ELF64 Header at offset 0x0000 */
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)elf_buf;
    ehdr->e_ident[0] = 0x7F;
    ehdr->e_ident[1] = 'E';
    ehdr->e_ident[2] = 'L';
    ehdr->e_ident[3] = 'F';
    ehdr->e_ident[4] = 2;    /* 64-bit architecture */
    ehdr->e_ident[5] = 1;    /* Little-endian */
    ehdr->e_ident[6] = 1;    /* ELF Version 1 */
    ehdr->e_ident[7] = 0;    /* System V ABI */
    ehdr->e_type = 2;        /* ET_EXEC */
    ehdr->e_machine = 0x3E;  /* EM_X86_64 */
    ehdr->e_version = 1;
    ehdr->e_entry = TEXT_VADDR; /* Entry point: _start at 0x401000 */
    ehdr->e_phoff = 64;      /* Program header directly follows ELF header */
    ehdr->e_shoff = 0;
    ehdr->e_flags = 0;
    ehdr->e_ehsize = sizeof(Elf64_Ehdr);
    ehdr->e_phentsize = sizeof(Elf64_Phdr);
    ehdr->e_phnum = 1;       /* Single unified PT_LOAD segment covering code, data & BSS */
    ehdr->e_shentsize = 0;
    ehdr->e_shnum = 0;
    ehdr->e_shstrndx = 0;

    /* Program Header (PT_LOAD) at offset 0x0040 */
    Elf64_Phdr *phdr = (Elf64_Phdr *)(elf_buf + 64);
    phdr->p_type = 1;        /* PT_LOAD */
    phdr->p_flags = 7;       /* PF_R | PF_W | PF_X */
    phdr->p_offset = 0;
    phdr->p_vaddr = ELF_BASE;
    phdr->p_paddr = ELF_BASE;
    phdr->p_filesz = total_file_size;
    phdr->p_memsz = total_file_size + 0x1000000; /* Extra 16MB for BSS heap arena */
    phdr->p_align = 0x1000;  /* 4KB page alignment */

    /* Copy Code to 0x1000 and Data to 0x8000 */
    memcpy(elf_buf + TEXT_OFF, code.data, code.size);
    memcpy(elf_buf + DATA_OFF, data.data, data.size);

    FILE *fout = fopen(out_path, "wb");
    if (!fout) {
        fprintf(stderr, "[-] Error: cannot open output binary: %s\n", out_path);
        free(elf_buf);
        free(code.data);
        free(data.data);
        return 1;
    }

    fwrite(elf_buf, 1, total_file_size, fout);
    fclose(fout);

    chmod(out_path, 0755);

    printf("[+] Standalone Native Linux ELF64 generated: %s (%zu bytes, entry 0x%llx)\n",
           out_path, total_file_size, (unsigned long long)TEXT_VADDR);

    free(elf_buf);
    free(code.data);
    free(data.data);
    return 0;
}

/* ==============================================================================
 *                     MAIN DRIVER & CLI
 * ============================================================================== */

int main(int argc, char **argv) {
    int run_opt = 0;
    int run_vm = 0;
    int emit_elf = 0;
    const char *src_file = NULL;
    const char *out_file = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--opt") == 0 || strcmp(argv[i], "-O2") == 0) {
            run_opt = 1;
        } else if (strcmp(argv[i], "--run") == 0) {
            run_vm = 1;
        } else if (strcmp(argv[i], "--emit-elf") == 0 || strcmp(argv[i], "--compile") == 0) {
            emit_elf = 1;
        } else if (!src_file) {
            src_file = argv[i];
        } else if (!out_file) {
            out_file = argv[i];
        }
    }

    if (!src_file) {
        fprintf(stderr, "==================================================================\n");
        fprintf(stderr, "   NEXUS v8 Intermediate Representation (N-IR) & Native ELF Codegen\n");
        fprintf(stderr, "==================================================================\n");
        fprintf(stderr, "Usage: %s [--opt|-O2] [--run|--emit-elf] <source.nex> [output.nir|app.elf]\n", argv[0]);
        fprintf(stderr, "Commands:\n");
        fprintf(stderr, "  %s <file.nex> [out.nir]                  Emit raw lowered N-IR\n", argv[0]);
        fprintf(stderr, "  %s --opt <file.nex> [out.nir]            Run 4 optimization passes\n", argv[0]);
        fprintf(stderr, "  %s --run <file.nex>                      Execute source directly via N-IR VM\n", argv[0]);
        fprintf(stderr, "  %s --opt --run <file.nex>                Execute optimized N-IR via VM\n", argv[0]);
        fprintf(stderr, "  %s --emit-elf <file.nex> [app.elf]       Compile to standalone native Linux ELF64 binary\n", argv[0]);
        fprintf(stderr, "  %s --opt --emit-elf <file.nex> [app.elf] Compile optimized N-IR to native ELF64 binary\n", argv[0]);
        return 1;
    }

    IRModule *mod = build_ir_from_nexus(src_file);
    if (!mod) {
        fprintf(stderr, "[-] Error: failed to load source: %s\n", src_file);
        return 1;
    }

    if (run_opt) {
        ir_pass_constant_folding(mod);
        ir_pass_algebraic_simplification(mod);
        ir_pass_hydron_loop_solver(mod);
        ir_pass_dead_code_elimination(mod);
    }

    if (emit_elf) {
        char default_elf[256];
        if (!out_file) {
            snprintf(default_elf, sizeof(default_elf), "app.elf");
            out_file = default_elf;
        }
        return emit_linux_elf64(mod, out_file);
    }

    if (run_vm) {
        return run_ir_vm(mod);
    }

    FILE *out = stdout;
    if (out_file) {
        out = fopen(out_file, "w");
        if (!out) {
            fprintf(stderr, "[-] Error: cannot open output file: %s\n", out_file);
            return 1;
        }
    }

    dump_ir_module(out, mod);

    if (run_opt) {
        fprintf(stderr, "[*] N-IR Optimization Telemetry:\n");
        fprintf(stderr, "    Constant Expressions Folded:     %d\n", mod->opt_constants_folded);
        fprintf(stderr, "    Algebraic Simplifications:       %d\n", mod->opt_algebraic_simplified);
        fprintf(stderr, "    HYDRON Loop Attractors Solved:   %d\n", mod->opt_hydron_loops_solved);
        fprintf(stderr, "    Dead Code Eliminations:          %d\n", mod->opt_dce_removed);
    }

    if (out_file) fclose(out);
    return 0;
}
