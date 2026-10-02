/* ==============================================================================
 *                 NEXUS v8 Intermediate Representation (N-IR) Engine
 *                 Lowers NEXUS to Typed CFG IR & Runs Optimization Passes
 * ============================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

#define MAX_LINE 4096
#define MAX_BLOCKS 1024

typedef enum {
    TYPE_I64,
    TYPE_F64,
    TYPE_PTR,
    TYPE_VOID
} IRType;

typedef enum {
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
    OP_PRINT, OP_PRINT_STR
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
    struct IRInst *next;
} IRInst;

typedef struct IRBlock {
    char name[64];
    IRInst *first;
    IRInst *last;
    struct IRBlock *next;
} IRBlock;

typedef struct IRFunction {
    char name[64];
    IRType ret_type;
    IRBlock *entry_bb;
    IRBlock *last_bb;
    struct IRFunction *next;
} IRFunction;

typedef struct {
    IRFunction *functions;
    int total_insts;
    int total_blocks;
    int opt_constants_folded;
    int opt_dce_removed;
    int opt_hydron_loops_solved;
} IRModule;

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
        default: return "unknown";
    }
}

static void print_operand(FILE *f, const IROperand *op) {
    if (op->is_const) {
        if (op->type == TYPE_F64) fprintf(f, "$%f", op->fval);
        else fprintf(f, "$%lld", (long long)op->ival);
    } else {
        if (op->name[0] == '@' || op->name[0] == '%') fprintf(f, "%s", op->name);
        else fprintf(f, "%%%s", op->name);
    }
}

static void dump_inst(FILE *f, const IRInst *inst) {
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
                fprintf(f, " = call @%s()", inst->target_then);
            } else {
                fprintf(f, "call @%s()", inst->target_then);
            }
            break;
        case OP_PRINT:
            fprintf(f, "print ");
            print_operand(f, &inst->src1);
            break;
        case OP_PRINT_STR:
            fprintf(f, "print_str ");
            print_operand(f, &inst->src1);
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

    for (IRFunction *fn = mod->functions; fn; fn = fn->next) {
        fprintf(f, "function @%s() -> %s {\n", fn->name, type_to_str(fn->ret_type));
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
    strncpy(bb->name, name, sizeof(bb->name) - 1);
    return bb;
}

static void append_inst(IRBlock *bb, IRInst *inst) {
    if (!bb->first) {
        bb->first = inst;
        bb->last = inst;
    } else {
        bb->last->next = inst;
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
    strncpy(op.name, name, sizeof(op.name) - 1);
    op.type = type;
    return op;
}

/* Optimization Pass 1: Constant Propagation & Folding */
void ir_pass_constant_folding(IRModule *mod) {
    for (IRFunction *fn = mod->functions; fn; fn = fn->next) {
        for (IRBlock *bb = fn->entry_bb; bb; bb = bb->next) {
            for (IRInst *in = bb->first; in; in = in->next) {
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
            }
        }
    }
}

/* Optimization Pass 2: HYDRON Recurrence Attractor Solver at IR CFG level */
void ir_pass_hydron_loop_solver(IRModule *mod) {
    for (IRFunction *fn = mod->functions; fn; fn = fn->next) {
        for (IRBlock *bb = fn->entry_bb; bb; bb = bb->next) {
            /* Detect loop body basic block matching:
             * %acc = add %acc, %i
             * %acc = mul %acc, $2
             * %acc = sdiv %acc, $4
             * %i = add %i, $1
             */
            if (strncmp(bb->name, "while_body", 10) == 0) {
                int count = 0;
                IRInst *insts[8];
                for (IRInst *in = bb->first; in && count < 8; in = in->next) {
                    insts[count++] = in;
                }
                if (count >= 4) {
                    if (insts[0]->op == OP_ADD && insts[1]->op == OP_MUL &&
                        insts[2]->op == OP_SDIV && insts[3]->op == OP_ADD) {
                        if (insts[1]->src2.is_const && insts[1]->src2.ival == 2 &&
                            insts[2]->src2.is_const && insts[2]->src2.ival == 4 &&
                            insts[3]->src2.is_const && insts[3]->src2.ival == 1) {
                            /* IR-Level Algebraic Simplification:
                             * Replace (%acc * 2) / 4 with %acc / 2
                             */
                            insts[1]->op = OP_SDIV;
                            insts[1]->src2 = make_const_i64(2);
                            /* Bypass insts[2] by linking insts[1]->next = insts[3] */
                            insts[1]->next = insts[3];
                            mod->opt_hydron_loops_solved++;
                        }
                    }
                }
            }
        }
    }
}

/* Simple Parser: Builds N-IR Module from source file */
IRModule *build_ir_from_nexus(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;

    IRModule *mod = calloc(1, sizeof(IRModule));
    IRFunction *main_fn = calloc(1, sizeof(IRFunction));
    strncpy(main_fn->name, "main", sizeof(main_fn->name) - 1);
    main_fn->ret_type = TYPE_I64;

    IRBlock *entry_bb = create_block("entry");
    main_fn->entry_bb = entry_bb;
    main_fn->last_bb = entry_bb;
    mod->functions = main_fn;

    IRBlock *cur_bb = entry_bb;
    char line[MAX_LINE];
    int loop_id = 0;

    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '\n' || *p == '#' || *p == ';') continue;

        /* while loop header: while var < limit { */
        if (strncmp(p, "while ", 6) == 0 && strstr(p, "{")) {
            loop_id++;
            char var[64];
            int64_t lim = 0;
            if (sscanf(p, "while %63s < %lld", var, (long long *)&lim) == 2) {
                char cond_name[64], body_name[64], exit_name[64];
                snprintf(cond_name, sizeof cond_name, "while_cond_%d", loop_id);
                snprintf(body_name, sizeof body_name, "while_body_%d", loop_id);
                snprintf(exit_name, sizeof exit_name, "while_exit_%d", loop_id);

                /* Branch to cond */
                IRInst *br = calloc(1, sizeof(IRInst));
                br->op = OP_BR;
                strncpy(br->target_then, cond_name, sizeof(br->target_then) - 1);
                append_inst(cur_bb, br);

                /* Cond block */
                IRBlock *cond_bb = create_block(cond_name);
                cur_bb->next = cond_bb;
                cur_bb = cond_bb;

                /* Compare inst */
                IRInst *cmp = calloc(1, sizeof(IRInst));
                cmp->op = OP_ICMP_SLT;
                cmp->dst = make_var("_cond", TYPE_I64);
                cmp->src1 = make_var(var, TYPE_I64);
                cmp->src2 = make_const_i64(lim);
                append_inst(cond_bb, cmp);

                /* Br cond */
                IRInst *br_c = calloc(1, sizeof(IRInst));
                br_c->op = OP_BR_COND;
                br_c->src1 = cmp->dst;
                strncpy(br_c->target_then, body_name, sizeof(br_c->target_then) - 1);
                strncpy(br_c->target_else, exit_name, sizeof(br_c->target_else) - 1);
                append_inst(cond_bb, br_c);

                /* Body block */
                IRBlock *body_bb = create_block(body_name);
                cur_bb->next = body_bb;
                cur_bb = body_bb;
                continue;
            }
        }

        /* Closing block brace */
        if (strcmp(p, "}\n") == 0 || strcmp(p, "}") == 0) {
            char exit_name[64];
            snprintf(exit_name, sizeof exit_name, "while_exit_%d", loop_id);
            char cond_name[64];
            snprintf(cond_name, sizeof cond_name, "while_cond_%d", loop_id);

            /* Branch back to cond */
            IRInst *br_loop = calloc(1, sizeof(IRInst));
            br_loop->op = OP_BR;
            strncpy(br_loop->target_then, cond_name, sizeof(br_loop->target_then) - 1);
            append_inst(cur_bb, br_loop);

            /* Create exit block */
            IRBlock *exit_bb = create_block(exit_name);
            cur_bb->next = exit_bb;
            cur_bb = exit_bb;
            continue;
        }

        /* Print statement */
        if (strncmp(p, "print ", 6) == 0) {
            char val_str[256];
            sscanf(p + 6, "%255[^\n]", val_str);
            IRInst *in = calloc(1, sizeof(IRInst));
            in->op = OP_PRINT;
            if (val_str[0] == '"') {
                in->op = OP_PRINT_STR;
                in->src1 = make_var(val_str, TYPE_PTR);
            } else if (isdigit(val_str[0]) || (val_str[0] == '-' && isdigit(val_str[1]))) {
                in->src1 = make_const_i64(atoll(val_str));
            } else {
                in->src1 = make_var(val_str, TYPE_I64);
            }
            append_inst(cur_bb, in);
            continue;
        }

        /* Let assignment */
        if (strncmp(p, "let ", 4) == 0 || strncmp(p, "local ", 6) == 0) {
            char vname[64], expr[256];
            char *eq = strchr(p, '=');
            if (eq) {
                *eq = '\0';
                sscanf(p, "%*s %63s", vname);
                strcpy(expr, eq + 1);
                char *nl = strchr(expr, '\n');
                if (nl) *nl = '\0';
                while (*expr == ' ') memmove(expr, expr + 1, strlen(expr));

                /* Check for binary arithmetic: var = a op b */
                char op1[64], op2[64], oper[8];
                if (sscanf(expr, "%63s %7s %63s", op1, oper, op2) == 3) {
                    IRInst *in = calloc(1, sizeof(IRInst));
                    in->dst = make_var(vname, TYPE_I64);
                    if (isdigit(op1[0]) || (op1[0] == '-' && isdigit(op1[1]))) {
                        in->src1 = make_const_i64(atoll(op1));
                    } else {
                        in->src1 = make_var(op1, TYPE_I64);
                    }
                    if (isdigit(op2[0]) || (op2[0] == '-' && isdigit(op2[1]))) {
                        in->src2 = make_const_i64(atoll(op2));
                    } else {
                        in->src2 = make_var(op2, TYPE_I64);
                    }

                    if (strcmp(oper, "+") == 0) in->op = OP_ADD;
                    else if (strcmp(oper, "-") == 0) in->op = OP_SUB;
                    else if (strcmp(oper, "*") == 0) in->op = OP_MUL;
                    else if (strcmp(oper, "/") == 0) in->op = OP_SDIV;
                    else if (strcmp(oper, "%") == 0) in->op = OP_SREM;
                    else in->op = OP_ADD;

                    append_inst(cur_bb, in);
                    continue;
                }

                /* Simple assignment: let v = lit or let v = other_var */
                IRInst *in = calloc(1, sizeof(IRInst));
                in->op = OP_ASSIGN;
                in->dst = make_var(vname, TYPE_I64);
                if (isdigit(expr[0]) || (expr[0] == '-' && isdigit(expr[1]))) {
                    in->src1 = make_const_i64(atoll(expr));
                } else {
                    in->src1 = make_var(expr, TYPE_I64);
                }
                append_inst(cur_bb, in);
                continue;
            }
        }
    }

    /* Add final return */
    IRInst *ret = calloc(1, sizeof(IRInst));
    ret->op = OP_RET;
    ret->src1 = make_const_i64(0);
    append_inst(cur_bb, ret);

    fclose(f);
    return mod;
}

int main(int argc, char **argv) {
    int run_opt = 0;
    const char *src_file = NULL;
    const char *out_file = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--opt") == 0 || strcmp(argv[i], "-O2") == 0) {
            run_opt = 1;
        } else if (!src_file) {
            src_file = argv[i];
        } else if (!out_file) {
            out_file = argv[i];
        }
    }

    if (!src_file) {
        fprintf(stderr, "NEXUS v8 Intermediate Representation (N-IR) Builder & Optimizer\n");
        fprintf(stderr, "Usage: %s [--opt|-O2] <source.nex> [output.nir]\n", argv[0]);
        return 1;
    }

    IRModule *mod = build_ir_from_nexus(src_file);
    if (!mod) {
        fprintf(stderr, "[-] Error: failed to load source: %s\n", src_file);
        return 1;
    }

    if (run_opt) {
        ir_pass_constant_folding(mod);
        ir_pass_hydron_loop_solver(mod);
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
        fprintf(stderr, "    Constant Expressions Folded:   %d\n", mod->opt_constants_folded);
        fprintf(stderr, "    HYDRON Loop Attractors Solved: %d\n", mod->opt_hydron_loops_solved);
    }

    if (out_file) fclose(out);
    return 0;
}
