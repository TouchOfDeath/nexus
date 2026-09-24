/*
 * nexprep.c - NEXUS source preprocessor (v3)
 * ===========================================================================
 * Handles, line-by-line:
 *   1. include "path" / import "path"   - recursive source inclusion
 *        Resolution order: directory of the INCLUDING file first,
 *        then the current working directory. Circular includes are
 *        detected and skipped.
 *   2. struct declarations
 *        struct Name { field1 field2 ... }   (8 bytes per field)
 *        - declarations are commented out in the output
 *        - Name.size  -> total byte size        (constant substitution)
 *        - Name.field -> byte offset of field   (constant substitution)
 *        - obj.field  -> member access transform:
 *             read  in let/print/if/while/etc: hoisted temp loads
 *               let v = p.x        ->  let v = load [p + 0]
 *               let z = p.x + 10   ->  let _s_p_x = load [p + 0]
 *                                       let z = _s_p_x + 10
 *             write p.f = RHS:
 *               number   -> store [p + 0] <number>
 *               variable -> store [p + 0] <variable>
 *               member   -> hoisted load of source member, then store
 *               other    -> let _s_rhs_N = <expr>; store [p + 0] _s_rhs_N
 *        - object -> struct type is tracked from 'let o = alloc Name.size'
 *          so identically-named fields in different structs resolve
 *          correctly (a global first-declaration-wins map is the fallback).
 *   3. assertion macros (expand to counting compare-and-report blocks):
 *        assert_eq <exprA>, <exprB>
 *        assert_ne <exprA>, <exprB>
 *   4. NOTHING ELSE IS TOUCHED. Byte sequences such as "[m" or "[1;31m"
 *      inside strings are preserved verbatim.
 *
 *   --list-deps <file>: print the canonical paths of the file and every
 *      resolved include (one per line, no preprocessing) - used by the
 *      incremental build system for dependency tracking.
 *
 * Build:  gcc -O2 -o bin/nexprep tools/nexprep.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>

#define MAXLINE    8192
#define MAXSTRUCTS 128
#define MAXFIELDS  64
#define MAXOBJS    4096
#define MAXGLOBALS 4096
#define MAXSEEN    256
#define MAXHOIST   256

static struct {
    char name[128];
    int nfields;
    char fields[MAXFIELDS][128];
    int offs[MAXFIELDS];
} S[MAXSTRUCTS];
static int ns = 0;

/* global fallback: first declaration wins */
static struct { char field[128]; int off; } GF[MAXGLOBALS];
static int ngf = 0;

/* tracked objects: variable name -> struct index */
static struct { char obj[128]; int st; } OB[MAXOBJS];
static int nob = 0;

static char *seen[MAXSEEN]; /* canonical paths, cycle guard */
static int nseen = 0;
static int rhs_counter = 0;
static int assert_counter = 0;
static int cur_st = -1;
static int list_deps_mode = 0;
static FILE *deps_out = NULL;

static void die(const char *msg, const char *arg) {
    fprintf(stderr, "[-] Error: %s%s%s\n", msg, arg ? ": " : "", arg ? arg : "");
    exit(1);
}

static int is_ident0(int c) { return isalpha((unsigned char)c) || c == '_'; }
static int is_ident1(int c) { return isalnum((unsigned char)c) || c == '_'; }

static int is_nexus_keyword(const char *tok) {
    static const char *kw[] = {
        "if", "else", "while", "for", "fn", "call", "return", "print",
        "print_str", "print_float", "read", "struct", "include", "import",
        "break", "continue", "alloc", "store", "store64", "store32", "store16",
        "load", "load64", "load32", "load16", "fload", "fstore",
        "file_open", "file_create", "file_read", "file_write", "file_close",
        "assert_eq", "assert_ne", NULL
    };
    for (int i = 0; kw[i]; i++) {
        if (!strcmp(tok, kw[i])) return 1;
    }
    return 0;
}

static int is_simple_val(const char *s) {
    if (!*s) return 0;
    int all_digits = 1;
    for (const char *p = s; *p; p++) {
        if (!isdigit((unsigned char)*p)) { all_digits = 0; break; }
    }
    if (all_digits) return 1;
    if (!is_ident0((unsigned char)*s)) return 0;
    for (const char *p = s + 1; *p; p++) {
        if (!is_ident1((unsigned char)*p)) return 0;
    }
    return 1;
}

static int find_struct(const char *nm) {
    for (int i = 0; i < ns; i++) if (!strcmp(S[i].name, nm)) return i;
    return -1;
}
static int find_obj(const char *nm) {
    for (int i = 0; i < nob; i++) if (!strcmp(OB[i].obj, nm)) return OB[i].st;
    return -1;
}
static void track_obj(const char *nm, int st) {
    if (nm[0] == '\0') return;
    for (int i = 0; i < nob; i++)
        if (!strcmp(OB[i].obj, nm)) { OB[i].st = st; return; }
    if (nob >= MAXOBJS) return;
    snprintf(OB[nob].obj, 128, "%s", nm);
    OB[nob].st = st;
    nob++;
}
static int global_field_off(const char *f, int *found) {
    for (int i = 0; i < ngf; i++)
        if (!strcmp(GF[i].field, f)) { *found = 1; return GF[i].off; }
    *found = 0;
    return -1;
}

/* offset of field f for object o (typed if known, else global map) */
static int member_off(const char *o, const char *f, int *resolved) {
    int st = find_obj(o);
    if (st >= 0) {
        for (int i = 0; i < S[st].nfields; i++)
            if (!strcmp(S[st].fields[i], f)) { *resolved = 1; return S[st].offs[i]; }
    }
    return global_field_off(f, resolved);
}

static int already_seen(const char *canon) {
    for (int i = 0; i < nseen; i++) if (!strcmp(seen[i], canon)) return 1;
    return 0;
}

/* hoisted-load buffer for the line currently being emitted */
static char hoist[MAXHOIST][MAXLINE];
static int nhoist = 0;

static void reset_hoist(void) { nhoist = 0; }

static void add_hoist(const char *name, const char *obj, int off) {
    if (nhoist >= MAXHOIST) die("too many hoisted loads on one line", NULL);
    for (int q = 0; q < nhoist; q++)
        if (strstr(hoist[q], name) == hoist[q] + 4) return; /* already hoisted */
    snprintf(hoist[nhoist++], MAXLINE, "let %s = load [%s + %d]", name, obj, off);
}

static void add_hoist_line(const char *name, const char *line) {
    if (nhoist >= MAXHOIST) die("too many hoisted loads on one line", NULL);
    for (int q = 0; q < nhoist; q++)
        if (strstr(hoist[q], name) == hoist[q] + 4) return; /* already hoisted */
    snprintf(hoist[nhoist++], MAXLINE, "%s", line);
}

/* --------------------------------------------------------------------------
 * Token transform of a code segment (outside string literals).
 *  - substitutes struct constants Name.size / Name.field
 *  - with mode M_INLINE, a lone obj.field token becomes load [obj + off];
 *    with M_NONE it becomes the hoisted temp name _s_obj_field
 * -------------------------------------------------------------------------- */
#define M_NONE   0
#define M_INLINE 1

static int transform_seg(char *out, const char *in, int mode) {
    int changed = 0, i = 0, o = 0, len = (int)strlen(in);
    while (i < len) {
        if (in[i] == '"') {                      /* copy string literal */
            out[o++] = in[i++];
            while (i < len && in[i] != '"') out[o++] = in[i++];
            if (i < len) out[o++] = in[i++];
            continue;
        }
        if (in[i] == '#' || in[i] == ';') {      /* copy comment verbatim */
            while (i < len) out[o++] = in[i++];
            break;
        }
        if (is_ident0((unsigned char)in[i])) {
            char tok[128]; int t = 0;
            while (i < len && is_ident1((unsigned char)in[i])) tok[t++] = in[i++];
            tok[t] = '\0';
            int j = i;
            while (j < len && (in[j] == ' ' || in[j] == '\t')) j++;
            if (j < len && in[j] == '.' && j + 1 < len &&
                is_ident0((unsigned char)in[j + 1])) {
                char fld[128]; int f = 0, k = j + 1;
                while (k < len && is_ident1((unsigned char)in[k])) fld[f++] = in[k++];
                fld[f] = '\0';
                int st = find_struct(tok);
                if (st >= 0) {                   /* struct constant */
                    if (!strcmp(fld, "size")) {
                        int total = S[st].nfields * 8;
                        o += sprintf(out + o, "%d", total);
                    } else {
                        int off = -1, found = 0;
                        for (int q = 0; q < S[st].nfields; q++)
                            if (!strcmp(S[st].fields[q], fld)) { off = S[st].offs[q]; found = 1; }
                        if (found) o += sprintf(out + o, "%d", off);
                        else o += sprintf(out + o, "%s.%s", tok, fld);
                    }
                    changed = 1;
                    i = k;
                    continue;
                }
                int resolved = 0;
                int off = member_off(tok, fld, &resolved);
                if (resolved) {
                    if (mode == M_INLINE)
                        o += sprintf(out + o, "load [%s + %d]", tok, off);
                    else {
                        char hn[160];
                        snprintf(hn, 160, "_s_%s_%s", tok, fld);
                        add_hoist(hn, tok, off);
                        o += sprintf(out + o, "%s", hn);
                    }
                    changed = 1;
                    i = k;
                    continue;
                }
                o += sprintf(out + o, "%s.%s", tok, fld);  /* unknown: verbatim */
                i = k;
                continue;
            }
            /* Array indexing read: tok[idx] */
            if (j < len && in[j] == '[' && !is_nexus_keyword(tok)) {
                int k = j + 1;
                char idx[128]; int ilen = 0;
                while (k < len && in[k] != ']') {
                    if (ilen < 127) idx[ilen++] = in[k];
                    k++;
                }
                idx[ilen] = '\0';
                if (k < len && in[k] == ']') {
                    k++; /* past ']' */
                    char *s = idx;
                    while (*s == ' ' || *s == '\t') s++;
                    int l = (int)strlen(s);
                    while (l > 0 && (s[l-1] == ' ' || s[l-1] == '\t')) s[--l] = '\0';
                    if (l > 0) {
                        int all_digits = 1;
                        for (char *c = s; *c; c++) {
                            if (!isdigit((unsigned char)*c)) { all_digits = 0; break; }
                        }
                        char hn[160];
                        char hline[MAXLINE];
                        if (all_digits) {
                            int off = atoi(s) * 8;
                            snprintf(hn, sizeof hn, "_a_%s_%s", tok, s);
                            snprintf(hline, sizeof hline, "let %s = load64 [%s + %d]", hn, tok, off);
                        } else if (is_simple_val(s)) {
                            snprintf(hn, sizeof hn, "_a_%s_%s", tok, s);
                            snprintf(hline, sizeof hline, "let %s = load64 [%s + %s * 8]", hn, tok, s);
                        } else {
                            static int h_idx_cnt = 0;
                            int hid = h_idx_cnt++;
                            char id_line[MAXLINE];
                            snprintf(id_line, sizeof id_line, "let _arr_hid_%d = %s", hid, s);
                            add_hoist_line("_arr_hid", id_line);
                            snprintf(hn, sizeof hn, "_a_%s_%d", tok, hid);
                            snprintf(hline, sizeof hline, "let %s = load64 [%s + _arr_hid_%d * 8]", hn, tok, hid);
                        }
                        if (mode == M_INLINE) {
                            if (all_digits) o += sprintf(out + o, "load64 [%s + %d]", tok, atoi(s) * 8);
                            else o += sprintf(out + o, "load64 [%s + %s * 8]", tok, s);
                        } else {
                            add_hoist_line(hn, hline);
                            o += sprintf(out + o, "%s", hn);
                        }
                        changed = 1;
                        i = k;
                        continue;
                    }
                }
            }

            o += sprintf(out + o, "%s", tok);
            continue;
        }
        out[o++] = in[i++];
    }
    out[o] = '\0';
    return changed;
}

/* --------------------------------------------------------------------------
 * assertion macro expansion
 *   assert_eq <A>, <B>  -> counting compare-and-report block
 * -------------------------------------------------------------------------- */
static int assert_line(const char *raw, FILE *out) {
    char t[MAXLINE];
    snprintf(t, MAXLINE, "%s", raw);
    char *p = t;
    while (*p == ' ' || *p == '\t') p++;
    int is_eq = !strncmp(p, "assert_eq", 9) && (p[9] == ' ' || p[9] == '\t');
    int is_ne = !strncmp(p, "assert_ne", 9) && (p[9] == ' ' || p[9] == '\t');
    if (!is_eq && !is_ne) return 0;

    p += 9;
    while (*p == ' ' || *p == '\t') p++;
    /* split on the first top-level comma */
    char a[MAXLINE], b[MAXLINE];
    int na = 0, in_str = 0;
    while (*p && *p != '\n' && *p != '\r') {
        if (*p == '"') in_str = !in_str;
        if (*p == ',' && !in_str) break;
        a[na++] = *p++;
    }
    a[na] = '\0';
    if (*p != ',') die("assert_eq/assert_ne requires two arguments separated by a comma", a);
    p++;
    while (*p == ' ' || *p == '\t') p++;
    int nb = 0;
    while (*p && *p != '\n' && *p != '\r') b[nb++] = *p++;
    while (nb > 0 && (b[nb-1] == ' ' || b[nb-1] == '\t')) nb--;
    b[nb] = '\0';
    /* trim trailing spaces on a */
    na = (int)strlen(a);
    while (na > 0 && (a[na-1] == ' ' || a[na-1] == '\t')) na--;
    a[na] = '\0';
    if (!na || !nb) die("assert_eq/assert_ne arguments cannot be empty", NULL);

    char lhs[MAXLINE], rhs[MAXLINE];
    transform_seg(lhs, a, M_INLINE);
    transform_seg(rhs, b, M_INLINE);

    int id = assert_counter++;
    fprintf(out, "# assert %d: %s %s %s\n", id, a, is_eq ? "==" : "!=", b);
    fprintf(out, "let _as_l_%d = %s\n", id, lhs);
    fprintf(out, "let _as_r_%d = %s\n", id, rhs);
    fprintf(out, "let _as_ok_%d = 0\n", id);
    fprintf(out, "if _as_l_%d %s _as_r_%d {\n", id, is_eq ? "==" : "!=", id);
    fprintf(out, "    let _as_ok_%d = 1\n", id);
    fprintf(out, "}\n");
    fprintf(out, "if _as_ok_%d == 1 {\n", id);
    fprintf(out, "    let nx_assert_passes = nx_assert_passes + 1\n");
    fprintf(out, "} else {\n");
    fprintf(out, "    let nx_assert_fails = nx_assert_fails + 1\n");
    fprintf(out, "    print \"[FAIL] assert %s: got\"\n", is_eq ? "eq" : "ne");
    fprintf(out, "    print _as_l_%d\n", id);
    fprintf(out, "    print \"[FAIL] expected %s\"\n", is_eq ? "equal" : "different");
    fprintf(out, "    print _as_r_%d\n", id);
    fprintf(out, "}\n");
    return 1;
}

/* --------------------------------------------------------------------------
 * struct declaration collection; returns 1 when the line was consumed
 * (declarations are commented out in the output)
 * -------------------------------------------------------------------------- */
static int struct_line(const char *raw) {
    char t[MAXLINE];
    snprintf(t, MAXLINE, "%s", raw);
    char *p = t;
    while (*p == ' ' || *p == '\t') p++;
    if (cur_st < 0) {
        if (!strncmp(p, "struct", 6) && (p[6] == ' ' || p[6] == '\t')) {
            char nm[128]; int n = 0, k = 7;
            while (p[k] == ' ' || p[k] == '\t') k++;
            while (p[k] && is_ident1((unsigned char)p[k])) nm[n++] = p[k++];
            nm[n] = '\0';
            while (p[k] == ' ' || p[k] == '\t') k++;
            if (nm[0] && p[k] == '{') {
                if (ns >= MAXSTRUCTS) die("too many structs", nm);
                snprintf(S[ns].name, 128, "%s", nm);
                S[ns].nfields = 0;
                cur_st = ns;
                ns++;
                return 1;
            }
        }
        return 0;
    }
    if (*p == '}') { cur_st = -1; return 1; }
    if (*p == '#' || *p == '\0' || *p == '\n') return 1;
    char *save = NULL;
    char *fld = strtok_r(p, " \t\r\n", &save);
    while (fld) {
        if (is_ident0((unsigned char)fld[0]) && S[cur_st].nfields < MAXFIELDS) {
            int nf = S[cur_st].nfields;
            snprintf(S[cur_st].fields[nf], 128, "%s", fld);
            S[cur_st].offs[nf] = nf * 8;
            S[cur_st].nfields++;
            int found = 0;
            global_field_off(fld, &found);
            if (!found && ngf < MAXGLOBALS) {
                snprintf(GF[ngf].field, 128, "%s", fld);
                GF[ngf].off = nf * 8;
                ngf++;
            }
        }
        fld = strtok_r(NULL, " \t\r\n", &save);
    }
    return 1;
}

/* --------------------------------------------------------------------------
 * include / import processing
 * -------------------------------------------------------------------------- */
static void process_file(const char *path, FILE *out);

static int try_include(char *line, const char *curdir, FILE *out) {
    char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    int kwlen = 0;
    if (!strncmp(p, "include", 7) && (p[7] == ' ' || p[7] == '\t')) kwlen = 7;
    if (!strncmp(p, "import", 6) && (p[6] == ' ' || p[6] == '\t')) kwlen = 6;
    if (!kwlen) return 0;
    p += kwlen;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '"') return 0;
    char rel[MAXLINE]; int r = 0;
    p++;
    while (*p && *p != '"' && r < MAXLINE - 1) rel[r++] = *p++;
    rel[r] = '\0';
    if (*p != '"') return 0;

    /* resolution order:
       1. relative to the INCLUDING file's directory
       2. relative to the current working directory
       3. relative to each ancestor directory of the including file
          (so "stdlib/nstdlib.nex" works from any project subdirectory) */
    const char *use = NULL;
    char cand[MAXLINE * 3];
    snprintf(cand, sizeof cand, "%s/%s", curdir, rel);
    if (access(cand, R_OK) == 0) use = cand;
    if (!use) {
        snprintf(cand, sizeof cand, "%s", rel);
        if (access(cand, R_OK) == 0) use = cand;
    }
    if (!use) {
        char walk[MAXLINE * 2];
        snprintf(walk, sizeof walk, "%s", curdir);
        while (use == NULL && strcmp(walk, "/") != 0 && walk[0] != '\0') {
            char *slash = strrchr(walk, '/');
            if (slash == walk) {
                walk[1] = '\0';
                snprintf(cand, sizeof cand, "%s%s", walk, rel);
            } else if (slash) {
                *slash = '\0';
                snprintf(cand, sizeof cand, "%s/%s", walk, rel);
            } else break;
            if (access(cand, R_OK) == 0) use = cand;
            else if (!strcmp(walk, "/")) break;
        }
    }
    if (!use) die("Cannot resolve file path", rel);

    char canon[MAXLINE * 2];
    if (!realpath(use, canon)) die("Cannot canonicalize", use);
    if (list_deps_mode) {
        fprintf(deps_out ? deps_out : stdout, "%s\n", canon);
        /* recurse so nested includes are recorded too; emit_line is a no-op
           in list-deps mode (skipped in process_file) */
        process_file(canon, out);
        return 1;
    }
    if (already_seen(canon)) {
        fprintf(out, "# --- SKIP INCLUDE (already included): %s ---\n", rel);
        return 1;
    }
    if (nseen < MAXSEEN) seen[nseen++] = strdup(canon);
    fprintf(out, "# --- BEGIN INCLUDE: %s ---\n", rel);
    process_file(canon, out);
    fprintf(out, "# --- END INCLUDE: %s ---\n", rel);
    return 1;
}

/* object type tracking: let o = alloc Name.size */
static void track_alloc_type(const char *line) {
    const char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (strncmp(p, "let", 3) || (p[3] != ' ' && p[3] != '\t')) return;
    p += 3;
    while (*p == ' ' || *p == '\t') p++;
    char obj[128]; int n = 0;
    while (*p && is_ident1((unsigned char)*p)) obj[n++] = *p++;
    obj[n] = '\0';
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '=') return;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    if (strncmp(p, "alloc", 5) || (p[5] != ' ' && p[5] != '\t')) return;
    p += 5;
    while (*p == ' ' || *p == '\t') p++;
    char sn[128]; int m = 0;
    while (*p && is_ident1((unsigned char)*p)) sn[m++] = *p++;
    sn[m] = '\0';
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '.' && !strcmp(p + 1, "size\n") && !strchr(p + 1, '\n') == 0) {
        /* trailing newline tolerated */
    }
    if (*p == '.' && !strncmp(p + 1, "size", 4)) {
        int st = find_struct(sn);
        if (st >= 0) track_obj(obj, st);
    }
}

/* --------------------------------------------------------------------------
 * floating-point syntax sugar
 * -------------------------------------------------------------------------- */
static int ftmp_counter = 0;

static const char *get_fop(const char *op) {
    if (!strcmp(op, "+.")) return "fadd";
    if (!strcmp(op, "-.")) return "fsub";
    if (!strcmp(op, "*.")) return "fmul";
    if (!strcmp(op, "/.")) return "fdiv";
    return NULL;
}

static int is_relop_f(const char *s, int *len, const char **clean_op) {
    if (!strncmp(s, "==.", 3)) { *len = 3; *clean_op = "=="; return 1; }
    if (!strncmp(s, "!=.", 3)) { *len = 3; *clean_op = "!="; return 1; }
    if (!strncmp(s, "<=.", 3)) { *len = 3; *clean_op = "<="; return 1; }
    if (!strncmp(s, ">=.", 3)) { *len = 3; *clean_op = ">="; return 1; }
    if (!strncmp(s, "<.", 2))  { *len = 2; *clean_op = "<"; return 1; }
    if (!strncmp(s, ">.", 2))  { *len = 2; *clean_op = ">"; return 1; }
    return 0;
}

static int desugar_float(const char *in, char *out, size_t cap) {
    char clean[MAXLINE];
    snprintf(clean, sizeof clean, "%s", in);
    char *p = clean;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '#' || *p == ';' || *p == '\0') return 0;

    int indent_len = (int)(p - clean);
    char indent[128] = "";
    if (indent_len > 0) {
        if (indent_len >= (int)sizeof(indent)) indent_len = (int)sizeof(indent) - 1;
        snprintf(indent, sizeof(indent), "%.*s", indent_len, clean);
    }

    /* 1. Conditions: if, while, else if */
    int is_cond = 0;
    const char *cond_type = NULL;
    char prefix[256] = "";

    if (!strncmp(p, "if ", 3) || !strncmp(p, "if\t", 3)) {
        is_cond = 1; cond_type = "if_f "; p += 3;
    } else if (!strncmp(p, "if_f ", 5) || !strncmp(p, "if_f\t", 5)) {
        is_cond = 1; cond_type = "if_f "; p += 5;
    } else if (!strncmp(p, "while ", 6) || !strncmp(p, "while\t", 6)) {
        is_cond = 1; cond_type = "while_f "; p += 6;
    } else if (!strncmp(p, "while_f ", 8) || !strncmp(p, "while_f\t", 8)) {
        is_cond = 1; cond_type = "while_f "; p += 8;
    } else {
        char *eif = strstr(p, "else if ");
        if (!eif) eif = strstr(p, "else if\t");
        if (eif) {
            int pre_len = (int)(eif - p);
            snprintf(prefix, sizeof prefix, "%.*selse if_f ", pre_len, p);
            p = eif + 8;
            is_cond = 1;
            cond_type = prefix;
        } else {
            char *eiff = strstr(p, "else if_f ");
            if (!eiff) eiff = strstr(p, "else if_f\t");
            if (eiff) {
                int pre_len = (int)(eiff - p);
                snprintf(prefix, sizeof prefix, "%.*selse if_f ", pre_len, p);
                p = eiff + 10;
                is_cond = 1;
                cond_type = prefix;
            }
        }
    }

    if (is_cond) {
        int in_quote = 0;
        for (char *c = p; *c; c++) {
            if (*c == '"') in_quote = !in_quote;
            if (in_quote) continue;
            int rlen = 0;
            const char *clean_op = NULL;
            if (is_relop_f(c, &rlen, &clean_op)) {
                int left_len = (int)(c - p);
                const char *right = c + rlen;
                snprintf(out, cap, "%s%s%.*s%s%s\n", indent, cond_type, left_len, p, clean_op, right);
                return 1;
            }
        }
        return 0;
    }

    /* 2. Let assignments: let <dest> = <op1> <op.> <op2> ... */
    if (!strncmp(p, "let ", 4) || !strncmp(p, "let\t", 4)) {
        char *eq = strchr(p, '=');
        if (eq) {
            char *rhs = eq + 1;
            while (*rhs == ' ' || *rhs == '\t') rhs++;
            if (*rhs == '"') return 0;

            char rhs_copy[MAXLINE];
            snprintf(rhs_copy, sizeof rhs_copy, "%s", rhs);
            char *nl = strchr(rhs_copy, '\n');
            if (nl) *nl = '\0';
            char *cr = strchr(rhs_copy, '\r');
            if (cr) *cr = '\0';
            char *cm = strchr(rhs_copy, '#');
            if (cm) *cm = '\0';
            char *sc = strchr(rhs_copy, ';');
            if (sc) *sc = '\0';

            char tokens[32][128];
            int ntokens = 0;
            char *tok = strtok(rhs_copy, " \t");
            while (tok && ntokens < 32) {
                snprintf(tokens[ntokens++], 128, "%s", tok);
                tok = strtok(NULL, " \t");
            }

            int has_fop = 0;
            for (int i = 0; i < ntokens; i++) {
                if (get_fop(tokens[i])) { has_fop = 1; break; }
            }

            if (has_fop && ntokens >= 3) {
                char dest[128];
                int dlen = (int)(eq - (p + 4));
                snprintf(dest, sizeof dest, "%.*s", dlen, p + 4);
                char *dstart = dest;
                while (*dstart == ' ' || *dstart == '\t') dstart++;
                char *dend = dstart + strlen(dstart);
                while (dend > dstart && (dend[-1] == ' ' || dend[-1] == '\t')) *--dend = '\0';

                char cur_res[128];
                out[0] = '\0';
                snprintf(cur_res, sizeof cur_res, "%s", tokens[0]);
                int cur_tok = 1;

                while (cur_tok + 1 < ntokens) {
                    const char *fop = get_fop(tokens[cur_tok]);
                    if (!fop) break;
                    const char *next_op = tokens[cur_tok + 1];
                    int is_last = (cur_tok + 2 >= ntokens);
                    char target_dest[128];
                    if (is_last) {
                        snprintf(target_dest, sizeof target_dest, "%s", dstart);
                    } else {
                        snprintf(target_dest, sizeof target_dest, "_f_tmp_%d", ftmp_counter++);
                    }
                    char line_buf[512];
                    snprintf(line_buf, sizeof line_buf, "%slet %s = %s %s %s\n",
                             indent, target_dest, fop, cur_res, next_op);
                    strncat(out, line_buf, cap - strlen(out) - 1);
                    snprintf(cur_res, sizeof cur_res, "%s", target_dest);
                    cur_tok += 2;
                }
                return 1;
            }
        }
    }

    return 0;
}

/* --------------------------------------------------------------------------
 * function parameter, call expression, and return value syntax desugaring
 * -------------------------------------------------------------------------- */
static char cur_fn_name[128] = "";
static int fn_block_depth = 0;
static int call_tmp_counter = 0;

static void update_fn_depth(const char *line) {
    int in_str = 0;
    for (int i = 0; line[i]; i++) {
        if (line[i] == '"') in_str = !in_str;
        if (in_str) continue;
        if (line[i] == '#' || line[i] == ';') break;
        if (line[i] == '{') {
            if (cur_fn_name[0] != '\0') {
                fn_block_depth++;
            }
        } else if (line[i] == '}') {
            if (cur_fn_name[0] != '\0') {
                fn_block_depth--;
                if (fn_block_depth <= 0) {
                    cur_fn_name[0] = '\0';
                    fn_block_depth = 0;
                }
            }
        }
    }
}

static int split_args(const char *arg_str, char args[][512], int max_args) {
    int count = 0;
    int len = (int)strlen(arg_str);
    int start = 0, in_str = 0, paren_depth = 0;

    for (int i = 0; i <= len; i++) {
        char c = arg_str[i];
        if (c == '"') in_str = !in_str;
        if (!in_str) {
            if (c == '(') paren_depth++;
            else if (c == ')') paren_depth--;
        }

        if ((c == ',' && !in_str && paren_depth == 0) || c == '\0') {
            if (count >= max_args) break;
            int s = start, e = i - 1;
            while (s <= e && (arg_str[s] == ' ' || arg_str[s] == '\t')) s++;
            while (e >= s && (arg_str[e] == ' ' || arg_str[e] == '\t' || arg_str[e] == '\r' || arg_str[e] == '\n')) e--;
            if (e >= s) {
                int n = e - s + 1;
                if (n >= 512) n = 511;
                snprintf(args[count], 512, "%.*s", n, arg_str + s);
                count++;
            }
            start = i + 1;
        }
    }
    return count;
}

static int desugar_fn(const char *in, char *out, size_t cap) {
    char clean[MAXLINE];
    snprintf(clean, sizeof clean, "%s", in);
    char *p = clean;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '#' || *p == ';' || *p == '\0') return 0;

    int indent_len = (int)(p - clean);
    char indent[128] = "";
    if (indent_len > 0) {
        if (indent_len >= (int)sizeof(indent)) indent_len = (int)sizeof(indent) - 1;
        snprintf(indent, sizeof(indent), "%.*s", indent_len, clean);
    }

    /* 1. Check for 'fn <name>(<params>) {' OR 'fn <name> {' */
    if (!strncmp(p, "fn ", 3) || !strncmp(p, "fn\t", 3)) {
        char *q = p + 3;
        while (*q == ' ' || *q == '\t') q++;
        char fn_name[128] = "";
        int fn_len = 0;
        while (*q && is_ident1((unsigned char)*q)) fn_name[fn_len++] = *q++;
        fn_name[fn_len] = '\0';

        if (fn_len > 0) {
            while (*q == ' ' || *q == '\t') q++;
            if (*q == '(') {
                /* Parameterized function: fn name(p1, p2, ...) { */
                char *paren_open = q;
                char *paren_close = strrchr(q, ')');
                if (paren_close) {
                    char *brace = strchr(paren_close, '{');
                    char param_str[MAXLINE] = "";
                    int plen = (int)(paren_close - (paren_open + 1));
                    if (plen > 0) {
                        snprintf(param_str, sizeof param_str, "%.*s", plen, paren_open + 1);
                    }
                    char params[32][512];
                    int nparams = split_args(param_str, params, 32);

                    snprintf(cur_fn_name, sizeof cur_fn_name, "%s", fn_name);
                    fn_block_depth = 0;

                    out[0] = '\0';
                    char line_buf[MAXLINE];
                    char *comment = brace ? strchr(brace + 1, '#') : NULL;
                    if (!comment && brace) comment = strchr(brace + 1, ';');
                    if (comment) {
                        snprintf(line_buf, sizeof line_buf, "%sfn %s { %s\n", indent, fn_name, comment);
                    } else {
                        snprintf(line_buf, sizeof line_buf, "%sfn %s {\n", indent, fn_name);
                    }
                    strncat(out, line_buf, cap - strlen(out) - 1);

                    for (int i = 0; i < nparams; i++) {
                        snprintf(line_buf, sizeof line_buf, "%s    let %s = _arg_%s_%d\n",
                                 indent, params[i], fn_name, i);
                        strncat(out, line_buf, cap - strlen(out) - 1);
                    }
                    return 1;
                }
            } else if (*q == '{' || *q == '\0' || *q == '#' || *q == ';') {
                /* Parameterless function: fn name { */
                snprintf(cur_fn_name, sizeof cur_fn_name, "%s", fn_name);
                fn_block_depth = 0;
                return 0; /* unchanged, emitted verbatim */
            }
        }
    }

    /* 2. Check for 'return <expr>' */
    if (!strncmp(p, "return", 6)) {
        if (p[6] == ' ' || p[6] == '\t') {
            char *expr = p + 6;
            while (*expr == ' ' || *expr == '\t') expr++;
            if (*expr != '\0' && *expr != '#' && *expr != ';' && *expr != '\n' && *expr != '\r') {
                char ret_expr[MAXLINE];
                snprintf(ret_expr, sizeof ret_expr, "%s", expr);
                int elen = (int)strlen(ret_expr);
                while (elen > 0 && (ret_expr[elen-1] == ' ' || ret_expr[elen-1] == '\t' ||
                                    ret_expr[elen-1] == '\n' || ret_expr[elen-1] == '\r')) {
                    ret_expr[--elen] = '\0';
                }

                const char *target_fn = cur_fn_name[0] ? cur_fn_name : "_main";
                snprintf(out, cap, "%slet _ret_%s = %s\n%sreturn\n",
                         indent, target_fn, ret_expr, indent);
                return 1;
            }
        }
    }

    /* 3. Check for 'let <dest> = call <name>(<args>)' or 'let <dest> = call <name>' */
    if (!strncmp(p, "let ", 4) || !strncmp(p, "let\t", 4)) {
        char *eq = strchr(p, '=');
        if (eq) {
            char *rhs = eq + 1;
            while (*rhs == ' ' || *rhs == '\t') rhs++;
            if (!strncmp(rhs, "call ", 5) || !strncmp(rhs, "call\t", 5)) {
                char dest[128] = "";
                int dlen = (int)(eq - (p + 4));
                snprintf(dest, sizeof dest, "%.*s", dlen, p + 4);
                char *dstart = dest;
                while (*dstart == ' ' || *dstart == '\t') dstart++;
                char *dend = dstart + strlen(dstart);
                while (dend > dstart && (dend[-1] == ' ' || dend[-1] == '\t')) *--dend = '\0';

                char *call_target = rhs + 5;
                while (*call_target == ' ' || *call_target == '\t') call_target++;
                char fn_name[128] = "";
                int fn_len = 0;
                while (*call_target && is_ident1((unsigned char)*call_target)) {
                    fn_name[fn_len++] = *call_target++;
                }
                fn_name[fn_len] = '\0';

                if (fn_len > 0) {
                    while (*call_target == ' ' || *call_target == '\t') call_target++;
                    if (*call_target == '(') {
                        char *paren_open = call_target;
                        char *paren_close = strrchr(call_target, ')');
                        if (paren_close) {
                            char arg_str[MAXLINE] = "";
                            int alen = (int)(paren_close - (paren_open + 1));
                            if (alen > 0) {
                                snprintf(arg_str, sizeof arg_str, "%.*s", alen, paren_open + 1);
                            }
                            char args[32][512];
                            int nargs = split_args(arg_str, args, 32);

                            out[0] = '\0';
                            char line_buf[MAXLINE];

                            int has_nested_call = 0;
                            for (int i = 0; i < nargs; i++) {
                                if (strstr(args[i], "call ") || strstr(args[i], "call\t")) {
                                    has_nested_call = 1;
                                    break;
                                }
                            }

                            if (has_nested_call) {
                                int tmp_indices[32];
                                for (int i = 0; i < nargs; i++) {
                                    tmp_indices[i] = call_tmp_counter++;
                                    snprintf(line_buf, sizeof line_buf, "%slet _c_tmp_%d = %s\n",
                                             indent, tmp_indices[i], args[i]);
                                    strncat(out, line_buf, cap - strlen(out) - 1);
                                }
                                for (int i = 0; i < nargs; i++) {
                                    snprintf(line_buf, sizeof line_buf, "%slet _arg_%s_%d = _c_tmp_%d\n",
                                             indent, fn_name, i, tmp_indices[i]);
                                    strncat(out, line_buf, cap - strlen(out) - 1);
                                }
                            } else {
                                for (int i = 0; i < nargs; i++) {
                                    snprintf(line_buf, sizeof line_buf, "%slet _arg_%s_%d = %s\n",
                                             indent, fn_name, i, args[i]);
                                    strncat(out, line_buf, cap - strlen(out) - 1);
                                }
                            }

                            snprintf(line_buf, sizeof line_buf, "%scall %s\n", indent, fn_name);
                            strncat(out, line_buf, cap - strlen(out) - 1);
                            snprintf(line_buf, sizeof line_buf, "%slet %s = _ret_%s\n", indent, dstart, fn_name);
                            strncat(out, line_buf, cap - strlen(out) - 1);
                            return 1;
                        }
                    } else {
                        /* let dest = call name (zero args, no parens) */
                        snprintf(out, cap, "%scall %s\n%slet %s = _ret_%s\n",
                                 indent, fn_name, indent, dstart, fn_name);
                        return 1;
                    }
                }
            }
        }
    }

    /* 4. Check for statement 'call <name>(<args>)' */
    if (!strncmp(p, "call ", 5) || !strncmp(p, "call\t", 5)) {
        char *call_target = p + 5;
        while (*call_target == ' ' || *call_target == '\t') call_target++;
        char fn_name[128] = "";
        int fn_len = 0;
        while (*call_target && is_ident1((unsigned char)*call_target)) {
            fn_name[fn_len++] = *call_target++;
        }
        fn_name[fn_len] = '\0';

        if (fn_len > 0) {
            while (*call_target == ' ' || *call_target == '\t') call_target++;
            if (*call_target == '(') {
                char *paren_open = call_target;
                char *paren_close = strrchr(call_target, ')');
                if (paren_close) {
                    char arg_str[MAXLINE] = "";
                    int alen = (int)(paren_close - (paren_open + 1));
                    if (alen > 0) {
                        snprintf(arg_str, sizeof arg_str, "%.*s", alen, paren_open + 1);
                    }
                    char args[32][512];
                    int nargs = split_args(arg_str, args, 32);

                    out[0] = '\0';
                    char line_buf[MAXLINE];

                    int has_nested_call = 0;
                    for (int i = 0; i < nargs; i++) {
                        if (strstr(args[i], "call ") || strstr(args[i], "call\t")) {
                            has_nested_call = 1;
                            break;
                        }
                    }

                    if (has_nested_call) {
                        int tmp_indices[32];
                        for (int i = 0; i < nargs; i++) {
                            tmp_indices[i] = call_tmp_counter++;
                            snprintf(line_buf, sizeof line_buf, "%slet _c_tmp_%d = %s\n",
                                     indent, tmp_indices[i], args[i]);
                            strncat(out, line_buf, cap - strlen(out) - 1);
                        }
                        for (int i = 0; i < nargs; i++) {
                            snprintf(line_buf, sizeof line_buf, "%slet _arg_%s_%d = _c_tmp_%d\n",
                                     indent, fn_name, i, tmp_indices[i]);
                            strncat(out, line_buf, cap - strlen(out) - 1);
                        }
                    } else {
                        for (int i = 0; i < nargs; i++) {
                            snprintf(line_buf, sizeof line_buf, "%slet _arg_%s_%d = %s\n",
                                     indent, fn_name, i, args[i]);
                            strncat(out, line_buf, cap - strlen(out) - 1);
                        }
                    }

                    snprintf(line_buf, sizeof line_buf, "%scall %s\n", indent, fn_name);
                    strncat(out, line_buf, cap - strlen(out) - 1);
                    return 1;
                }
            }
        }
    }

    return 0;
}

/* --------------------------------------------------------------------------
 * compound assignment operators (+=, -=, *=, /=, %= and float +=., -=., etc.)
 * -------------------------------------------------------------------------- */
static const char *check_compound_op(const char *s, int *op_len, const char **base_op) {
    if (!strncmp(s, "**=", 3)) { *op_len = 3; *base_op = "**"; return "**="; }
    if (!strncmp(s, "+=.", 3)) { *op_len = 3; *base_op = "+."; return "+=."; }
    if (!strncmp(s, "-=.", 3)) { *op_len = 3; *base_op = "-."; return "-=."; }
    if (!strncmp(s, "*=.", 3)) { *op_len = 3; *base_op = "*."; return "*=."; }
    if (!strncmp(s, "/=.", 3)) { *op_len = 3; *base_op = "/."; return "/=."; }

    if (!strncmp(s, "+=", 2))  { *op_len = 2; *base_op = "+"; return "+="; }
    if (!strncmp(s, "-=", 2))  { *op_len = 2; *base_op = "-"; return "-="; }
    if (!strncmp(s, "*=", 2))  { *op_len = 2; *base_op = "*"; return "*="; }
    if (!strncmp(s, "/=", 2))  { *op_len = 2; *base_op = "/"; return "/="; }
    if (!strncmp(s, "%=", 2))  { *op_len = 2; *base_op = "%"; return "%="; }

    return NULL;
}

static int desugar_compound(const char *in, char *out, size_t cap) {
    char clean[MAXLINE];
    snprintf(clean, sizeof clean, "%s", in);
    char *p = clean;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '#' || *p == ';' || *p == '\0') return 0;

    int indent_len = (int)(p - clean);
    char indent[128] = "";
    if (indent_len > 0) {
        if (indent_len >= (int)sizeof(indent)) indent_len = (int)sizeof(indent) - 1;
        snprintf(indent, sizeof(indent), "%.*s", indent_len, clean);
    }

    int has_let = 0;
    if (!strncmp(p, "let ", 4) || !strncmp(p, "let\t", 4)) {
        has_let = 1;
        p += 4;
        while (*p == ' ' || *p == '\t') p++;
    }

    /* Target variable or member: ident or ident.ident */
    if (!is_ident0((unsigned char)*p)) return 0;

    char target[128] = "";
    int tlen = 0;
    while (*p && is_ident1((unsigned char)*p)) {
        if (tlen < 127) target[tlen++] = *p;
        p++;
    }
    if (*p == '.') {
        if (tlen < 127) target[tlen++] = *p;
        p++;
        while (*p && is_ident1((unsigned char)*p)) {
            if (tlen < 127) target[tlen++] = *p;
            p++;
        }
    }
    target[tlen] = '\0';

    if (!has_let && is_nexus_keyword(target)) return 0;

    while (*p == ' ' || *p == '\t') p++;

    int op_len = 0;
    const char *base_op = NULL;
    const char *cop = check_compound_op(p, &op_len, &base_op);
    if (!cop) return 0;

    p += op_len;
    while (*p == ' ' || *p == '\t') p++;

    /* Rest of the line is the rhs */
    char rhs[MAXLINE];
    snprintf(rhs, sizeof rhs, "%s", p);
    int rlen = (int)strlen(rhs);
    while (rlen > 0 && (rhs[rlen-1] == ' ' || rhs[rlen-1] == '\t' ||
                        rhs[rlen-1] == '\n' || rhs[rlen-1] == '\r')) {
        rhs[--rlen] = '\0';
    }

    if (strchr(target, '.')) {
        /* struct member write: p.x = p.x + <rhs> */
        snprintf(out, cap, "%s%s = %s %s %s\n", indent, target, target, base_op, rhs);
    } else {
        /* standard let assignment */
        snprintf(out, cap, "%slet %s = %s %s %s\n", indent, target, target, base_op, rhs);
    }
    return 1;
}

static int desugar_for_compound(const char *in, char *out, size_t cap) {
    char clean[MAXLINE];
    snprintf(clean, sizeof clean, "%s", in);
    char *p = clean;
    while (*p == ' ' || *p == '\t') p++;
    if (strncmp(p, "for ", 4) && strncmp(p, "for\t", 4)) return 0;

    /* find first comma */
    char *c1 = strchr(p, ',');
    if (!c1) return 0;
    /* find second comma */
    char *c2 = strchr(c1 + 1, ',');
    if (!c2) return 0;

    char *upd = c2 + 1;
    while (*upd == ' ' || *upd == '\t') upd++;

    char var[128] = "";
    int vlen = 0;
    while (*upd && is_ident1((unsigned char)*upd)) {
        if (vlen < 127) var[vlen++] = *upd;
        upd++;
    }
    var[vlen] = '\0';
    if (!vlen) return 0;

    while (*upd == ' ' || *upd == '\t') upd++;

    int op_len = 0;
    const char *base_op = NULL;
    const char *cop = check_compound_op(upd, &op_len, &base_op);
    if (!cop) return 0;
    if (strcmp(base_op, "+") && strcmp(base_op, "-")) return 0;

    upd += op_len;
    while (*upd == ' ' || *upd == '\t') upd++;

    /* find '{' */
    char *brace = strchr(upd, '{');
    if (!brace) return 0;

    int rlen = (int)(brace - upd);
    while (rlen > 0 && (upd[rlen-1] == ' ' || upd[rlen-1] == '\t')) rlen--;

    char rhs[128];
    snprintf(rhs, sizeof rhs, "%.*s", rlen, upd);

    int prefix_len = (int)(c2 - clean) + 1;
    snprintf(out, cap, "%.*s %s = %s %s %s %s\n",
             prefix_len, clean, var, var, base_op, rhs, brace);
    return 1;
}

/* --------------------------------------------------------------------------
 * array literal and indexed access desugaring
 * -------------------------------------------------------------------------- */
static int is_unclosed_array(const char *s) {
    const char *p = s;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '#' || *p == ';' || *p == '\0') return 0;
    int has_let = 0;
    if (!strncmp(p, "let ", 4) || !strncmp(p, "let\t", 4)) {
        has_let = 1; p += 4;
        while (*p == ' ' || *p == '\t') p++;
    }
    if (!is_ident0((unsigned char)*p)) return 0;
    char var[128]; int vlen = 0;
    while (*p && is_ident1((unsigned char)*p)) {
        if (vlen < 127) var[vlen++] = *p;
        p++;
    }
    var[vlen] = '\0';
    if (!has_let && is_nexus_keyword(var)) return 0;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '=') return 0;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '[') return 0;

    int depth = 0;
    int in_str = 0;
    for (const char *c = p; *c; c++) {
        if (*c == '"') in_str = !in_str;
        else if (!in_str) {
            if (*c == '#' || *c == ';') {
                while (*c && *c != '\n' && *c != '\r') c++;
                if (!*c) break;
                continue;
            }
            if (*c == '[') depth++;
            else if (*c == ']') depth--;
        }
    }
    return depth > 0;
}

static int desugar_array_literal(const char *in, char *out, size_t cap) {
    char clean[MAXLINE];
    snprintf(clean, sizeof clean, "%s", in);
    char *p = clean;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '#' || *p == ';' || *p == '\0') return 0;

    int indent_len = (int)(p - clean);
    char indent[128] = "";
    if (indent_len > 0) {
        if (indent_len >= (int)sizeof(indent)) indent_len = (int)sizeof(indent) - 1;
        snprintf(indent, sizeof(indent), "%.*s", indent_len, clean);
    }

    int has_let = 0;
    if (!strncmp(p, "let ", 4) || !strncmp(p, "let\t", 4)) {
        has_let = 1;
        p += 4;
        while (*p == ' ' || *p == '\t') p++;
    }

    if (!is_ident0((unsigned char)*p)) return 0;
    char var[128];
    int vlen = 0;
    while (*p && is_ident1((unsigned char)*p)) {
        if (vlen < 127) var[vlen++] = *p;
        p++;
    }
    var[vlen] = '\0';
    if (!has_let && is_nexus_keyword(var)) return 0;

    while (*p == ' ' || *p == '\t') p++;
    if (*p != '=') return 0;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '[') return 0;
    p++; /* past '[' */

    char elems[128][512];
    int nelems = 0;
    char cur_elem[512];
    int cur_len = 0;
    int depth = 1;
    int in_str = 0;

    while (*p) {
        if (*p == '"') {
            in_str = !in_str;
            if (cur_len < 511) cur_elem[cur_len++] = *p;
            p++;
            continue;
        }
        if (!in_str) {
            if (*p == '#' || *p == ';') {
                while (*p && *p != '\n' && *p != '\r') p++;
                continue;
            }
            if (*p == '[') depth++;
            else if (*p == ']') {
                depth--;
                if (depth == 0) {
                    p++;
                    break;
                }
            } else if (*p == ',' && depth == 1) {
                cur_elem[cur_len] = '\0';
                char *s = cur_elem;
                while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
                int l = (int)strlen(s);
                while (l > 0 && (s[l-1] == ' ' || s[l-1] == '\t' || s[l-1] == '\n' || s[l-1] == '\r')) s[--l] = '\0';
                if (l > 0 && nelems < 128) {
                    snprintf(elems[nelems++], sizeof(elems[0]), "%s", s);
                }
                cur_len = 0;
                p++;
                continue;
            }
        }
        if (cur_len < 511) cur_elem[cur_len++] = *p;
        p++;
    }
    if (depth != 0) return 0;

    while (*p == ' ' || *p == '\t') p++;
    if (*p != '\0' && *p != '\n' && *p != '\r' && *p != '#' && *p != ';') return 0;

    cur_elem[cur_len] = '\0';
    char *s = cur_elem;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    int l = (int)strlen(s);
    while (l > 0 && (s[l-1] == ' ' || s[l-1] == '\t' || s[l-1] == '\n' || s[l-1] == '\r')) s[--l] = '\0';
    if (l > 0 && nelems < 128) {
        snprintf(elems[nelems++], sizeof(elems[0]), "%s", s);
    }

    out[0] = '\0';
    static int arr_el_counter = 0;
    char line[MAXLINE];
    if (nelems == 0) {
        snprintf(line, sizeof line, "%slet %s = alloc 8\n", indent, var);
        strncat(out, line, cap - strlen(out) - 1);
    } else {
        snprintf(line, sizeof line, "%slet %s = alloc %d\n", indent, var, nelems * 8);
        strncat(out, line, cap - strlen(out) - 1);
        for (int k = 0; k < nelems; k++) {
            int off = k * 8;
            if (is_simple_val(elems[k])) {
                snprintf(line, sizeof line, "%sstore64 [%s + %d] %s\n", indent, var, off, elems[k]);
                strncat(out, line, cap - strlen(out) - 1);
            } else {
                int cid = arr_el_counter++;
                snprintf(line, sizeof line, "%slet _arr_el_%d = %s\n", indent, cid, elems[k]);
                strncat(out, line, cap - strlen(out) - 1);
                snprintf(line, sizeof line, "%sstore64 [%s + %d] _arr_el_%d\n", indent, var, off, cid);
                strncat(out, line, cap - strlen(out) - 1);
            }
        }
    }
    return 1;
}

static int desugar_array_read(const char *in, char *out, size_t cap) {
    char clean[MAXLINE];
    snprintf(clean, sizeof clean, "%s", in);
    char *p = clean;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '#' || *p == ';' || *p == '\0') return 0;

    int indent_len = (int)(p - clean);
    char indent[128] = "";
    if (indent_len > 0) {
        if (indent_len >= (int)sizeof(indent)) indent_len = (int)sizeof(indent) - 1;
        snprintf(indent, sizeof(indent), "%.*s", indent_len, clean);
    }

    int has_let = 0;
    if (!strncmp(p, "let ", 4) || !strncmp(p, "let\t", 4)) {
        has_let = 1; p += 4;
        while (*p == ' ' || *p == '\t') p++;
    }

    if (!is_ident0((unsigned char)*p)) return 0;
    char dest[128]; int dlen = 0;
    while (*p && is_ident1((unsigned char)*p)) {
        if (dlen < 127) dest[dlen++] = *p;
        p++;
    }
    dest[dlen] = '\0';
    if (!has_let && is_nexus_keyword(dest)) return 0;

    while (*p == ' ' || *p == '\t') p++;
    if (*p != '=') return 0;
    p++;
    while (*p == ' ' || *p == '\t') p++;

    if (!is_ident0((unsigned char)*p)) return 0;
    char arr[128]; int alen = 0;
    while (*p && is_ident1((unsigned char)*p)) {
        if (alen < 127) arr[alen++] = *p;
        p++;
    }
    arr[alen] = '\0';
    if (is_nexus_keyword(arr)) return 0;

    while (*p == ' ' || *p == '\t') p++;
    if (*p != '[') return 0;
    p++;
    char idx[128]; int ilen = 0;
    while (*p && *p != ']') {
        if (ilen < 127) idx[ilen++] = *p;
        p++;
    }
    idx[ilen] = '\0';
    if (*p != ']') return 0;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '\0' && *p != '\n' && *p != '\r' && *p != '#' && *p != ';') return 0;

    char *s = idx;
    while (*s == ' ' || *s == '\t') s++;
    int l = (int)strlen(s);
    while (l > 0 && (s[l-1] == ' ' || s[l-1] == '\t')) s[--l] = '\0';
    if (!l) return 0;

    int all_digits = 1;
    for (char *c = s; *c; c++) {
        if (!isdigit((unsigned char)*c)) { all_digits = 0; break; }
    }
    if (all_digits) {
        int off = atoi(s) * 8;
        snprintf(out, cap, "%slet %s = load64 [%s + %d]\n", indent, dest, arr, off);
    } else if (is_simple_val(s)) {
        snprintf(out, cap, "%slet %s = load64 [%s + %s * 8]\n", indent, dest, arr, s);
    } else {
        static int arr_ridx_counter = 0;
        int cid = arr_ridx_counter++;
        snprintf(out, cap, "%slet _arr_idx_%d = %s\n%slet %s = load64 [%s + _arr_idx_%d * 8]\n",
                 indent, cid, s, indent, dest, arr, cid);
    }
    return 1;
}

static int desugar_array_compound(const char *in, char *out, size_t cap) {
    char clean[MAXLINE];
    snprintf(clean, sizeof clean, "%s", in);
    char *p = clean;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '#' || *p == ';' || *p == '\0') return 0;

    int indent_len = (int)(p - clean);
    char indent[128] = "";
    if (indent_len > 0) {
        if (indent_len >= (int)sizeof(indent)) indent_len = (int)sizeof(indent) - 1;
        snprintf(indent, sizeof(indent), "%.*s", indent_len, clean);
    }

    if (!is_ident0((unsigned char)*p)) return 0;
    char arr[128]; int alen = 0;
    while (*p && is_ident1((unsigned char)*p)) {
        if (alen < 127) arr[alen++] = *p;
        p++;
    }
    arr[alen] = '\0';
    if (is_nexus_keyword(arr)) return 0;

    while (*p == ' ' || *p == '\t') p++;
    if (*p != '[') return 0;
    p++;
    char idx[128]; int ilen = 0;
    while (*p && *p != ']') {
        if (ilen < 127) idx[ilen++] = *p;
        p++;
    }
    idx[ilen] = '\0';
    if (*p != ']') return 0;
    p++;

    while (*p == ' ' || *p == '\t') p++;
    int op_len = 0;
    const char *base_op = NULL;
    const char *cop = check_compound_op(p, &op_len, &base_op);
    if (!cop) return 0;

    p += op_len;
    while (*p == ' ' || *p == '\t') p++;

    char rhs[MAXLINE];
    snprintf(rhs, sizeof rhs, "%s", p);
    int rlen = (int)strlen(rhs);
    while (rlen > 0 && (rhs[rlen-1] == ' ' || rhs[rlen-1] == '\t' ||
                        rhs[rlen-1] == '\n' || rhs[rlen-1] == '\r')) {
        rhs[--rlen] = '\0';
    }

    static int arr_c_counter = 0;
    int cid = arr_c_counter++;
    snprintf(out, cap, "%slet _arr_cval_%d = %s[%s]\n%s_arr_cval_%d %s %s\n%s%s[%s] = _arr_cval_%d\n",
             indent, cid, arr, idx,
             indent, cid, cop, rhs,
             indent, arr, idx, cid);
    return 1;
}

static int desugar_array_assign(const char *in, char *out, size_t cap) {
    char clean[MAXLINE];
    snprintf(clean, sizeof clean, "%s", in);
    char *p = clean;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '#' || *p == ';' || *p == '\0') return 0;

    int indent_len = (int)(p - clean);
    char indent[128] = "";
    if (indent_len > 0) {
        if (indent_len >= (int)sizeof(indent)) indent_len = (int)sizeof(indent) - 1;
        snprintf(indent, sizeof(indent), "%.*s", indent_len, clean);
    }

    if (!is_ident0((unsigned char)*p)) return 0;
    char arr[128]; int alen = 0;
    while (*p && is_ident1((unsigned char)*p)) {
        if (alen < 127) arr[alen++] = *p;
        p++;
    }
    arr[alen] = '\0';
    if (is_nexus_keyword(arr)) return 0;

    while (*p == ' ' || *p == '\t') p++;
    if (*p != '[') return 0;
    p++;
    char idx[128]; int ilen = 0;
    while (*p && *p != ']') {
        if (ilen < 127) idx[ilen++] = *p;
        p++;
    }
    idx[ilen] = '\0';
    if (*p != ']') return 0;
    p++;

    while (*p == ' ' || *p == '\t') p++;
    if (*p != '=') return 0;
    p++;
    while (*p == ' ' || *p == '\t') p++;

    char rhs[MAXLINE];
    snprintf(rhs, sizeof rhs, "%s", p);
    int rlen = (int)strlen(rhs);
    while (rlen > 0 && (rhs[rlen-1] == ' ' || rhs[rlen-1] == '\t' ||
                        rhs[rlen-1] == '\n' || rhs[rlen-1] == '\r')) {
        rhs[--rlen] = '\0';
    }

    char *s = idx;
    while (*s == ' ' || *s == '\t') s++;
    int l = (int)strlen(s);
    while (l > 0 && (s[l-1] == ' ' || s[l-1] == '\t')) s[--l] = '\0';
    if (!l) return 0;

    static int arr_w_counter = 0;
    int all_digits = 1;
    for (char *c = s; *c; c++) {
        if (!isdigit((unsigned char)*c)) { all_digits = 0; break; }
    }

    if (all_digits) {
        int off = atoi(s) * 8;
        if (is_simple_val(rhs)) {
            snprintf(out, cap, "%sstore64 [%s + %d] %s\n", indent, arr, off, rhs);
        } else {
            int cid = arr_w_counter++;
            snprintf(out, cap, "%slet _arr_wval_%d = %s\n%sstore64 [%s + %d] _arr_wval_%d\n",
                     indent, cid, rhs, indent, arr, off, cid);
        }
    } else if (is_simple_val(s)) {
        if (is_simple_val(rhs)) {
            snprintf(out, cap, "%sstore64 [%s + %s * 8] %s\n", indent, arr, s, rhs);
        } else {
            int cid = arr_w_counter++;
            snprintf(out, cap, "%slet _arr_wval_%d = %s\n%sstore64 [%s + %s * 8] _arr_wval_%d\n",
                     indent, cid, rhs, indent, arr, s, cid);
        }
    } else {
        int cid_idx = arr_w_counter++;
        int cid_val = arr_w_counter++;
        snprintf(out, cap, "%slet _arr_widx_%d = %s\n%slet _arr_wval_%d = %s\n%sstore64 [%s + _arr_widx_%d * 8] _arr_wval_%d\n",
                 indent, cid_idx, s, indent, cid_val, rhs, indent, arr, cid_idx, cid_val);
    }
    return 1;
}

static int desugar_power(const char *in, char *out, size_t cap) {
    char clean[MAXLINE];
    snprintf(clean, sizeof clean, "%s", in);
    char *p = clean;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '#' || *p == ';' || *p == '\0') return 0;

    int indent_len = (int)(p - clean);
    char indent[128] = "";
    if (indent_len > 0) {
        if (indent_len >= (int)sizeof(indent)) indent_len = (int)sizeof(indent) - 1;
        snprintf(indent, sizeof(indent), "%.*s", indent_len, clean);
    }

    int has_let = 0;
    if (!strncmp(p, "let ", 4) || !strncmp(p, "let\t", 4)) {
        has_let = 1;
        p += 4;
        while (*p == ' ' || *p == '\t') p++;
    }

    if (!is_ident0((unsigned char)*p)) return 0;
    char target[128] = "";
    int tlen = 0;
    while (*p && (is_ident1((unsigned char)*p) || *p == '.')) {
        if (tlen < 127) target[tlen++] = *p;
        p++;
    }
    target[tlen] = '\0';
    if (!has_let && is_nexus_keyword(target)) return 0;

    while (*p == ' ' || *p == '\t') p++;
    if (*p != '=') return 0;
    p++;
    while (*p == ' ' || *p == '\t') p++;

    char *pow_pos = NULL;
    char *r = p;
    while (*r) {
        if (*r == '"') {
            r++;
            while (*r && *r != '"') r++;
            if (*r) r++;
            continue;
        }
        if (*r == '#' || *r == ';') break;
        if (r[0] == '*' && r[1] == '*') {
            pow_pos = r;
            break;
        }
        r++;
    }
    if (!pow_pos) return 0;

    int blen = (int)(pow_pos - p);
    while (blen > 0 && (p[blen-1] == ' ' || p[blen-1] == '\t')) blen--;
    if (blen <= 0) return 0;
    char base[128];
    snprintf(base, sizeof base, "%.*s", blen, p);

    char *e = pow_pos + 2;
    while (*e == ' ' || *e == '\t') e++;
    int elen = (int)strlen(e);
    while (elen > 0 && (e[elen-1] == ' ' || e[elen-1] == '\t' ||
                        e[elen-1] == '\n' || e[elen-1] == '\r')) elen--;
    if (elen <= 0) return 0;
    char exp[128];
    snprintf(exp, sizeof exp, "%.*s", elen, e);

    static int pow_counter = 0;
    int cid = pow_counter++;

    snprintf(out, cap,
        "%slet _pow_b_%d = %s\n"
        "%slet _pow_e_%d = %s\n"
        "%slet _pow_r_%d = 1\n"
        "%sif _pow_e_%d < 0 {\n"
        "%s    let _pow_r_%d = 0\n"
        "%s} else {\n"
        "%s    while _pow_e_%d > 0 {\n"
        "%s        let _pow_m_%d = _pow_e_%d %% 2\n"
        "%s        if _pow_m_%d == 1 {\n"
        "%s            let _pow_r_%d = _pow_r_%d * _pow_b_%d\n"
        "%s        }\n"
        "%s        let _pow_b_%d = _pow_b_%d * _pow_b_%d\n"
        "%s        let _pow_e_%d = _pow_e_%d / 2\n"
        "%s    }\n"
        "%s}\n"
        "%slet %s = _pow_r_%d\n",
        indent, cid, base,
        indent, cid, exp,
        indent, cid,
        indent, cid,
        indent, cid,
        indent,
        indent, cid,
        indent, cid, cid,
        indent, cid,
        indent, cid, cid, cid,
        indent,
        indent, cid, cid, cid,
        indent, cid, cid,
        indent,
        indent,
        indent, target, cid);
    return 1;
}

/* --------------------------------------------------------------------------
 * per-line transform
 * -------------------------------------------------------------------------- */
static void emit_line(const char *raw, FILE *out) {
    reset_hoist();

    if (assert_line(raw, out)) return;

    char pow_desugared[MAXLINE * 4];
    if (desugar_power(raw, pow_desugared, sizeof pow_desugared)) {
        char *p = pow_desugared;
        while (*p) {
            char *next = strchr(p, '\n');
            if (next) *next = '\0';
            if (*p) emit_line(p, out);
            if (!next) break;
            p = next + 1;
        }
        return;
    }

    char fn_desugared[MAXLINE * 4];
    if (desugar_fn(raw, fn_desugared, sizeof fn_desugared)) {
        char *p = fn_desugared;
        while (*p) {
            char *next = strchr(p, '\n');
            if (next) *next = '\0';
            if (*p) emit_line(p, out);
            if (!next) break;
            p = next + 1;
        }
        return;
    }

    char arr_desugared[MAXLINE * 4];
    if (desugar_array_literal(raw, arr_desugared, sizeof arr_desugared)) {
        char *p = arr_desugared;
        while (*p) {
            char *next = strchr(p, '\n');
            if (next) *next = '\0';
            if (*p) emit_line(p, out);
            if (!next) break;
            p = next + 1;
        }
        return;
    }

    char arr_comp[MAXLINE];
    if (desugar_array_compound(raw, arr_comp, sizeof arr_comp)) {
        char *p = arr_comp;
        while (*p) {
            char *next = strchr(p, '\n');
            if (next) *next = '\0';
            if (*p) emit_line(p, out);
            if (!next) break;
            p = next + 1;
        }
        return;
    }

    char arr_asgn[MAXLINE];
    if (desugar_array_assign(raw, arr_asgn, sizeof arr_asgn)) {
        char *p = arr_asgn;
        while (*p) {
            char *next = strchr(p, '\n');
            if (next) *next = '\0';
            if (*p) emit_line(p, out);
            if (!next) break;
            p = next + 1;
        }
        return;
    }

    char arr_r[MAXLINE];
    if (desugar_array_read(raw, arr_r, sizeof arr_r)) {
        char *p = arr_r;
        while (*p) {
            char *next = strchr(p, '\n');
            if (next) *next = '\0';
            if (*p) emit_line(p, out);
            if (!next) break;
            p = next + 1;
        }
        return;
    }

    char c_desugared[MAXLINE];
    if (desugar_compound(raw, c_desugared, sizeof c_desugared)) {
        char *p = c_desugared;
        while (*p) {
            char *next = strchr(p, '\n');
            if (next) *next = '\0';
            if (*p) emit_line(p, out);
            if (!next) break;
            p = next + 1;
        }
        return;
    }

    char for_desugared[MAXLINE];
    if (desugar_for_compound(raw, for_desugared, sizeof for_desugared)) {
        emit_line(for_desugared, out);
        return;
    }

    char f_desugared[MAXLINE];
    if (desugar_float(raw, f_desugared, sizeof f_desugared)) {
        char *p = f_desugared;
        while (*p) {
            char *next = strchr(p, '\n');
            if (next) *next = '\0';
            if (*p) emit_line(p, out);
            if (!next) break;
            p = next + 1;
        }
        return;
    }

    /* work on a newline-stripped copy; exactly one '\n' is printed per line */
    char clean[MAXLINE];
    snprintf(clean, MAXLINE, "%s", raw);
    char *ce = clean + strlen(clean);
    while (ce > clean && (ce[-1] == '\n' || ce[-1] == '\r')) *--ce = '\0';

    char *cp = clean;
    while (*cp == ' ' || *cp == '\t') cp++;
    if (*cp == '#' || *cp == ';' || *cp == '\0') {
        fprintf(out, "%s\n", clean);
        return;
    }

    update_fn_depth(clean);
    track_alloc_type(clean);

    /* classify: member store  obj.f = RHS */
    char obj[128], fld[128];
    obj[0] = fld[0] = '\0';
    const char *rhs = NULL;
    {
        const char *p = clean;
        while (*p == ' ' || *p == '\t') p++;
        if (is_ident0((unsigned char)*p)) {
            char t1[128]; int n = 0;
            while (*p && is_ident1((unsigned char)*p)) t1[n++] = *p++;
            t1[n] = '\0';
            if (*p == '.' && find_struct(t1) < 0) {
                const char *q = p + 1;
                char t2[128]; int m = 0;
                while (*q && is_ident1((unsigned char)*q)) t2[m++] = *q++;
                t2[m] = '\0';
                const char *r = q;
                while (*r == ' ' || *r == '\t') r++;
                if (m && *r == '=') {
                    int resolved = 0;
                    member_off(t1, t2, &resolved);
                    if (resolved) {
                        snprintf(obj, 128, "%s", t1);
                        snprintf(fld, 128, "%s", t2);
                        r++;
                        while (*r == ' ' || *r == '\t') r++;
                        rhs = r;
                    }
                }
            }
        }
    }

    if (obj[0]) {
        int resolved = 0;
        int off = member_off(obj, fld, &resolved);
        char rhsbuf[MAXLINE];
        snprintf(rhsbuf, MAXLINE, "%s", rhs);
        char *e = rhsbuf + strlen(rhsbuf);
        while (e > rhsbuf && (e[-1] == '\n' || e[-1] == '\r' ||
                              e[-1] == ' ' || e[-1] == '\t')) *--e = '\0';

        char *p = rhsbuf;
        while (*p == ' ' || *p == '\t') p++;
        char *end = p + strlen(p);
        while (end > p && (end[-1] == ' ' || end[-1] == '\t')) *--end = '\0';

        char cls = 'x';                    /* x=expr, n=number, v=var, m=member */
        if (*p) {
            int dots = 0, spaces = 0;
            for (char *s = p; *s; s++) {
                if (*s == '.') dots++;
                if (*s == ' ' || *s == '\t') spaces++;
            }
            if (spaces == 0 && dots == 0 &&
                (isdigit((unsigned char)*p) || *p == '-'))
                cls = 'n';
            else if (spaces == 0 && dots == 0 && is_ident0((unsigned char)*p))
                cls = 'v';
            else if (spaces == 0 && dots == 1 && is_ident0((unsigned char)*p))
                cls = 'm';
        }

        if (cls == 'n' || cls == 'v') {
            fprintf(out, "store [%s + %d] %s\n", obj, off, p);
        } else if (cls == 'm') {
            char t1[128] = "", t2[128] = "";
            sscanf(p, "%127[^.].%127s", t1, t2);
            int r2 = 0, off2 = member_off(t1, t2, &r2);
            if (r2) {
                fprintf(out, "let _s_%s_%s = load [%s + %d]\n", t1, t2, t1, off2);
                fprintf(out, "store [%s + %d] _s_%s_%s\n", obj, off, t1, t2);
            } else {
                fprintf(out, "store [%s + %d] %s\n", obj, off, p);
            }
        } else {
            /* general expression: hoist members, rewrite, compute, store */
            char rewritten[MAXLINE];
            transform_seg(rewritten, p, M_NONE);
            for (int q = 0; q < nhoist; q++) fprintf(out, "%s\n", hoist[q]);
            fprintf(out, "let _s_rhs_%d = %s\n", rhs_counter++, rewritten);
            fprintf(out, "store [%s + %d] _s_rhs_%d\n", obj, off, rhs_counter - 1);
        }
        return;
    }

    /* generic line: maybe inline single-member let RHS, else hoist+substitute */
    {
        char trimmed[MAXLINE];
        snprintf(trimmed, MAXLINE, "%s", raw);
        char *tp = trimmed;
        while (*tp == ' ' || *tp == '\t') tp++;
        if (!strncmp(tp, "let", 3) && (tp[3] == ' ' || tp[3] == '\t')) {
            const char *eq = strchr(tp, '=');
            if (eq) {
                const char *v0 = eq + 1;
                while (*v0 == ' ' || *v0 == '\t') v0++;
                const char *v1 = v0 + strlen(v0);
                while (v1 > v0 && (v1[-1] == '\n' || v1[-1] == '\r' ||
                                   v1[-1] == ' ' || v1[-1] == '\t')) v1--;
                int dots = 0, spaces = 0;
                for (const char *c = v0; c < v1; c++) {
                    if (*c == '.') dots++;
                    if (*c == ' ' || *c == '\t') spaces++;
                }
                if (spaces == 0 && dots == 1 && v1 > v0 &&
                    is_ident0((unsigned char)*v0)) {
                    /* check it is obj.field with resolvable offset */
                    char t1[128] = "", t2[128] = "";
                    sscanf(v0, "%127[^.].%127s", t1, t2);
                    char *nl = strchr(t2, '\n');
                    if (nl) *nl = '\0';
                    int r2 = 0, off2 = member_off(t1, t2, &r2);
                    if (r2 && find_struct(t1) < 0) {
                        char head[MAXLINE];
                        int hlen = (int)(eq - tp);
                        snprintf(head, MAXLINE, "%.*s= ", hlen, tp);
                        fprintf(out, "%sload [%s + %d]\n", head, t1, off2);
                        return;
                    }
                }
            }
        }

        char rewritten[MAXLINE];
        transform_seg(rewritten, clean, M_NONE);
        for (int q = 0; q < nhoist; q++) fprintf(out, "%s\n", hoist[q]);
        fprintf(out, "%s\n", rewritten);
    }
}

/* --------------------------------------------------------------------------
 * main driver
 * -------------------------------------------------------------------------- */
static void process_file(const char *path, FILE *out) {
    FILE *in = fopen(path, "r");
    if (!in) die("Cannot open", path);
    char curdir[MAXLINE];
    snprintf(curdir, MAXLINE, "%s", path);
    char *d = strrchr(curdir, '/');
    if (d) *d = '\0'; else snprintf(curdir, MAXLINE, ".");

    char *line = NULL;
    size_t cap = 0;
    while (getline(&line, &cap, in) != -1) {
        if (try_include(line, curdir, out)) continue;
        if (list_deps_mode) continue;
        if (struct_line(line)) {
            fprintf(out, "# %s", line);     /* comment out declaration */
            continue;
        }
        if (is_unclosed_array(line)) {
            char *extra = NULL;
            size_t extra_cap = 0;
            while (getline(&extra, &extra_cap, in) != -1) {
                size_t l1 = strlen(line);
                size_t l2 = strlen(extra);
                size_t new_cap = l1 + l2 + 2;
                char *new_line = realloc(line, new_cap);
                if (!new_line) die("out of memory in array literal", NULL);
                line = new_line;
                cap = new_cap;
                strcat(line, " ");
                strcat(line, extra);
                if (!is_unclosed_array(line)) break;
            }
            if (extra) free(extra);
        }
        emit_line(line, out);
    }
    free(line);
    fclose(in);
}

int main(int argc, char **argv) {
    const char *out_path = NULL;
    int argi = 1;
    if (argi < argc && !strcmp(argv[argi], "--list-deps")) {
        list_deps_mode = 1;
        argi++;
        if (argi + 1 < argc) {
            deps_out = fopen(argv[argi + 1], "w");
            if (!deps_out) die("Cannot write", argv[argi + 1]);
        }
    }
    if (argi >= argc || (argc - argi) > (list_deps_mode ? 2 : 2)) {
        fprintf(stderr, "NEXUS Source Preprocessor v3 (string-safe)\n");
        fprintf(stderr, "Usage: %s [--list-deps] <source.nex> [output.nex]\n", argv[0]);
        fprintf(stderr, "  --list-deps <src> [outfile]  print canonical dependency paths\n");
        fprintf(stderr, "  Assertion macros: assert_eq <A>, <B> | assert_ne <A>, <B>\n");
        return 1;
    }
    const char *in_path = argv[argi];
    if (argi + 1 < argc && !(list_deps_mode && deps_out)) out_path = argv[argi + 1];
    char canon[MAXLINE * 2];
    if (!realpath(in_path, canon)) die("Cannot canonicalize", in_path);
    if (nseen < MAXSEEN) seen[nseen++] = strdup(canon);

    if (list_deps_mode) {
        printf("%s\n", canon);
        process_file(canon, stdout);
        if (deps_out) fclose(deps_out);
        return 0;
    }
    if (out_path) {
        FILE *out = fopen(out_path, "w");
        if (!out) die("Cannot write", out_path);
        process_file(canon, out);
        fclose(out);
    } else {
        process_file(canon, stdout);
    }
    return 0;
}
