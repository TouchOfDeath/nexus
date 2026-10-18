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
#ifdef _WIN32
#include <io.h>
#include <windows.h>
#define R_OK 4
#define access _access
#define strdup _strdup
static char *my_strtok_r(char *str, const char *delim, char **saveptr) {
    if (!str) str = *saveptr;
    if (!str) return NULL;
    while (*str && strchr(delim, *str)) str++;
    if (!*str) { *saveptr = NULL; return NULL; }
    char *token = str;
    while (*str && !strchr(delim, *str)) str++;
    if (*str) { *str = '\0'; *saveptr = str + 1; }
    else { *saveptr = NULL; }
    return token;
}
#define strtok_r my_strtok_r
#define strtoll _strtoi64
#define strtoull _strtoui64

static char *realpath(const char *path, char *resolved) {
    char buf[MAX_PATH];
    DWORD len = GetFullPathNameA(path, MAX_PATH, buf, NULL);
    if (len == 0 || len >= MAX_PATH) return NULL;
    for (DWORD i = 0; i < len; i++) {
        if (buf[i] == '\\') buf[i] = '/';
    }
    if (!resolved) {
        resolved = (char *)malloc(len + 1);
        if (!resolved) return NULL;
    }
    strcpy(resolved, buf);
    return resolved;
}

static long long getline(char **lineptr, size_t *n, FILE *stream) {
    if (!lineptr || !n || !stream) return -1;
    if (!*lineptr || *n == 0) {
        *n = 256;
        *lineptr = (char *)malloc(*n);
        if (!*lineptr) return -1;
    }
    size_t pos = 0;
    int c;
    while ((c = fgetc(stream)) != EOF) {
        if (pos + 2 >= *n) {
            size_t new_n = *n * 2;
            char *new_ptr = (char *)realloc(*lineptr, new_n);
            if (!new_ptr) return -1;
            *lineptr = new_ptr;
            *n = new_n;
        }
        (*lineptr)[pos++] = (char)c;
        if (c == '\n') break;
    }
    if (pos == 0 && c == EOF) return -1;
    (*lineptr)[pos] = '\0';
    return (long long)pos;
}

static FILE *create_body_tmp(void) {
    char temp_dir[MAX_PATH];
    char temp_file[MAX_PATH];
    if (GetTempPathA(MAX_PATH, temp_dir) == 0) strcpy(temp_dir, ".");
    if (GetTempFileNameA(temp_dir, "nxp", 0, temp_file) == 0) return tmpfile();
    return fopen(temp_file, "w+b");
}

static char *find_last_path_separator(char *p) {
    char *s1 = strrchr(p, '/');
    char *s2 = strrchr(p, '\\');
    if (!s1) return s2;
    if (!s2) return s1;
    return (s1 > s2) ? s1 : s2;
}
#else
#include <unistd.h>
static FILE *create_body_tmp(void) {
    return tmpfile();
}
static char *find_last_path_separator(char *p) {
    char *s1 = strrchr(p, '/');
    char *s2 = strrchr(p, '\\');
    if (!s1) return s2;
    if (!s2) return s1;
    return (s1 > s2) ? s1 : s2;
}
#endif
#include <stdint.h>
#include <inttypes.h>

static void emit_line(const char *raw, FILE *out);

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
        "if", "else", "while", "for", "fn", "let", "call", "return", "print",
        "print_str", "print_float", "read", "struct", "include", "import",
        "break", "continue", "alloc", "store", "store64", "store32", "store16",
        "load", "load64", "load32", "load16", "fload", "fstore",
        "file_open", "file_create", "file_read", "file_write", "file_close",
        "assert_eq", "assert_ne", "os_argc", "os_argv",
        "syscall", "abs", "len", "itof", "ftoi", "fadd", "fsub", "fmul",
        "fdiv", "fsqrt", "fneg", "int", "float", "var", "local",
        "frame_pointer", "stack_pointer", "frame_parent", NULL
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
            while (i < len && in[i] != '"') {
                if (in[i] == '\\' && i + 1 < len) {
                    out[o++] = in[i++];
                }
                out[o++] = in[i++];
            }
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
        if (in_str && *p == '\\' && *(p + 1)) {
            a[na++] = *p++;
            a[na++] = *p++;
            continue;
        }
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
    char linebuf[MAXLINE * 2];
    snprintf(linebuf, sizeof linebuf, "# assert %d: %s %s %s", id, a, is_eq ? "==" : "!=", b);
    emit_line(linebuf, out);
    snprintf(linebuf, sizeof linebuf, "let _as_l_%d = %s", id, lhs);
    emit_line(linebuf, out);
    snprintf(linebuf, sizeof linebuf, "let _as_r_%d = %s", id, rhs);
    emit_line(linebuf, out);
    snprintf(linebuf, sizeof linebuf, "let _as_ok_%d = 0", id);
    emit_line(linebuf, out);
    snprintf(linebuf, sizeof linebuf, "if _as_l_%d %s _as_r_%d {", id, is_eq ? "==" : "!=", id);
    emit_line(linebuf, out);
    snprintf(linebuf, sizeof linebuf, "    let _as_ok_%d = 1", id);
    emit_line(linebuf, out);
    snprintf(linebuf, sizeof linebuf, "}");
    emit_line(linebuf, out);
    snprintf(linebuf, sizeof linebuf, "if _as_ok_%d == 1 {", id);
    emit_line(linebuf, out);
    snprintf(linebuf, sizeof linebuf, "    let nx_assert_passes = nx_assert_passes + 1");
    emit_line(linebuf, out);
    snprintf(linebuf, sizeof linebuf, "} else {");
    emit_line(linebuf, out);
    snprintf(linebuf, sizeof linebuf, "    let nx_assert_fails = nx_assert_fails + 1");
    emit_line(linebuf, out);
    snprintf(linebuf, sizeof linebuf, "    print \"[FAIL] assert %s: got\"", is_eq ? "eq" : "ne");
    emit_line(linebuf, out);
    snprintf(linebuf, sizeof linebuf, "    print _as_l_%d", id);
    emit_line(linebuf, out);
    snprintf(linebuf, sizeof linebuf, "    print \"[FAIL] expected %s\"", is_eq ? "equal" : "different");
    emit_line(linebuf, out);
    snprintf(linebuf, sizeof linebuf, "    print _as_r_%d", id);
    emit_line(linebuf, out);
    snprintf(linebuf, sizeof linebuf, "}");
    emit_line(linebuf, out);
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
            if (isalpha((unsigned char)walk[0]) && walk[1] == ':' && (walk[2] == '\0' || (walk[2] == '/' && walk[3] == '\0') || (walk[2] == '\\' && walk[3] == '\0'))) break;
            char *slash = find_last_path_separator(walk);
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
 * function context and type inference tracking
 * -------------------------------------------------------------------------- */
static char cur_fn_name[128] = "";
static int fn_block_depth = 0;

/* --------------------------------------------------------------------------
 * function signature registry - maps function name to ordered param names
 * populated when we process 'fn name(p1, p2, ...) {' declarations
 * used for: named argument reordering, multi-return destructuring
 * -------------------------------------------------------------------------- */
#define MAXFNS      256
#define MAXFNPARAMS  32

static int split_args(const char *arg_str, char args[][512], int max_args);

typedef struct {
    char name[128];
    int  nparams;
    char params[MAXFNPARAMS][128];
    int  param_has_default[MAXFNPARAMS];
    char param_defaults[MAXFNPARAMS][256];
} FnRegistryEntry;

static FnRegistryEntry fn_registry[MAXFNS];
static int fn_registry_count = 0;

static void fn_registry_add_full(const char *name, char raw_params[][512], int nparams) {
    int idx = -1;
    for (int i = 0; i < fn_registry_count; i++) {
        if (!strcmp(fn_registry[i].name, name)) {
            idx = i;
            break;
        }
    }
    if (idx >= 0 && fn_registry[idx].nparams > 0 && nparams == 0) {
        return;
    }
    if (idx < 0) {
        if (fn_registry_count >= MAXFNS) return;
        idx = fn_registry_count++;
        snprintf(fn_registry[idx].name, 128, "%s", name);
    }

    fn_registry[idx].nparams = nparams < MAXFNPARAMS ? nparams : MAXFNPARAMS;
    for (int j = 0; j < fn_registry[idx].nparams; j++) {
        char raw[512];
        snprintf(raw, sizeof raw, "%s", raw_params[j]);

        /* Check for '=' (default parameter value) */
        char *eq = strchr(raw, '=');
        char pname[128] = "";
        char pdef[256] = "";

        if (eq) {
            int nlen = (int)(eq - raw);
            if (nlen >= 128) nlen = 127;
            snprintf(pname, sizeof pname, "%.*s", nlen, raw);
            char *def_start = eq + 1;
            while (*def_start == ' ' || *def_start == '\t') def_start++;
            int dlen = (int)strlen(def_start);
            while (dlen > 0 && (def_start[dlen-1] == ' ' || def_start[dlen-1] == '\t' ||
                                def_start[dlen-1] == '\r' || def_start[dlen-1] == '\n')) {
                dlen--;
            }
            snprintf(pdef, sizeof pdef, "%.*s", dlen, def_start);
            fn_registry[idx].param_has_default[j] = 1;
            snprintf(fn_registry[idx].param_defaults[j], 256, "%s", pdef);
        } else {
            snprintf(pname, sizeof pname, "%s", raw);
            fn_registry[idx].param_has_default[j] = 0;
            fn_registry[idx].param_defaults[j][0] = '\0';
        }

        /* Check for ':' (type annotation, e.g. 'path: string') */
        char *colon = strchr(pname, ':');
        if (colon) *colon = '\0';

        /* Trim pname */
        char *ps = pname;
        while (*ps == ' ' || *ps == '\t') ps++;
        int plen = (int)strlen(ps);
        while (plen > 0 && (ps[plen-1] == ' ' || ps[plen-1] == '\t')) plen--;
        ps[plen] = '\0';

        snprintf(fn_registry[idx].params[j], 128, "%s", ps);
    }
}

static void fn_registry_add(const char *name, char param_names[][512], int nparams) {
    fn_registry_add_full(name, param_names, nparams);
}

static int fn_registry_find(const char *name) {
    for (int i = 0; i < fn_registry_count; i++) {
        if (!strcmp(fn_registry[i].name, name)) return i;
    }
    return -1;
}

static int g_dispatch_needed[8] = {0};
static int g_lambda_count = 0;

static void init_closure_dispatch_sigs(void) {
    char p0[1][512] = {"_nx_cl"};
    fn_registry_add("_nx_dispatch_closure_0", p0, 1);

    char p1[2][512] = {"_nx_cl", "_nx_a0"};
    fn_registry_add("_nx_dispatch_closure_1", p1, 2);

    char p2[3][512] = {"_nx_cl", "_nx_a0", "_nx_a1"};
    fn_registry_add("_nx_dispatch_closure_2", p2, 3);

    char p3[4][512] = {"_nx_cl", "_nx_a0", "_nx_a1", "_nx_a2"};
    fn_registry_add("_nx_dispatch_closure_3", p3, 4);

    char p4[5][512] = {"_nx_cl", "_nx_a0", "_nx_a1", "_nx_a2", "_nx_a3"};
    fn_registry_add("_nx_dispatch_closure_4", p4, 5);
}

static void scan_signatures_in_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return;
    char curdir[MAXLINE];
    snprintf(curdir, sizeof curdir, "%s", path);
    char *d = find_last_path_separator(curdir);
    if (d) *d = '\0'; else snprintf(curdir, sizeof curdir, ".");

    char *line = NULL;
    size_t cap = 0;
    while (getline(&line, &cap, f) != -1) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (!strncmp(p, "include ", 8) || !strncmp(p, "include\t", 8) ||
            !strncmp(p, "import ", 7) || !strncmp(p, "import\t", 7)) {
            char *quote1 = strchr(p, '"');
            if (quote1) {
                char *quote2 = strchr(quote1 + 1, '"');
                if (quote2) {
                    int ilen = (int)(quote2 - quote1 - 1);
                    char sub[MAXLINE];
                    snprintf(sub, sizeof sub, "%.*s", ilen, quote1 + 1);
                    char full[MAXLINE * 2];
                    if (sub[0] == '/') snprintf(full, sizeof full, "%s", sub);
                    else snprintf(full, sizeof full, "%s/%s", curdir, sub);
                    char canon_inc[MAXLINE * 2];
                    if (realpath(full, canon_inc)) {
                        scan_signatures_in_file(canon_inc);
                    }
                }
            }
        } else if (!strncmp(p, "fn ", 3) || !strncmp(p, "fn\t", 3)) {
            p += 3;
            while (*p == ' ' || *p == '\t') p++;
            char fn_name[128] = "";
            int fn_len = 0;
            while (*p && is_ident1((unsigned char)*p)) {
                if (fn_len < 127) fn_name[fn_len++] = *p;
                p++;
            }
            fn_name[fn_len] = '\0';
            while (*p == ' ' || *p == '\t') p++;
            if (*p == '(') {
                char *paren_close = strrchr(p, ')');
                if (paren_close) {
                    char param_str[MAXLINE] = "";
                    int plen = (int)(paren_close - (p + 1));
                    if (plen > 0) snprintf(param_str, sizeof param_str, "%.*s", plen, p + 1);
                    char raw_params[32][512];
                    int nparams = split_args(param_str, raw_params, 32);
                    fn_registry_add_full(fn_name, raw_params, nparams);
                }
            } else if (*p == '{' || *p == '\0' || *p == '#' || *p == ';' || *p == '\n' || *p == '\r') {
                if (fn_len > 0) {
                    char raw_params[1][512];
                    fn_registry_add_full(fn_name, raw_params, 0);
                }
            }
        }
    }
    if (line) free(line);
    fclose(f);
}

/* ==========================================================================
 * Lexical Local Scoping Engine (v2)
 * ==========================================================================
 * Implements block-scoped local variables strictly bounded by { ... } blocks.
 *
 * Scopes:
 *   depth 1  = file/global level
 *   depth 2  = function body (fn name { ... })
 *   depth 3+ = nested block inside function (if/while/for/{})
 *   or depth 2+ at file level = nested block at file level
 *
 * Rules:
 *   1. 'local x = expr', 'var x = expr', 'int x = expr', 'float x = expr'
 *      Explicit local declaration: ALWAYS creates a new local variable for
 *      the current block, shadowing any variable in outer scopes.
 *   2. 'let x = expr'
 *      - If 'x' is already declared in an enclosing outer scope, it mutates
 *        that outer variable (100% backward-compatible with loops like let i = i + 1).
 *      - If 'x' is NOT declared in any outer scope, it declares a new local
 *        variable strictly bounded to the current block.
 *   3. 'x = expr' (bare assignment without let)
 *      Assigns to the existing variable in scope (or declares if none).
 *   4. All references in expressions resolve innermost-to-outermost.
 *   5. When '}' exits a block, all variables declared in that block go out
 *      of scope, and shadowed outer variables are unshadowed.
 * ========================================================================== */

#define LS_MAX_DEPTH     64
#define LS_MAX_VARS      128
#define LS_NAME_LEN      128
#define LS_SNAME_LEN     192

typedef struct {
    char orig[LS_NAME_LEN];
    char scoped[LS_SNAME_LEN];
    int is_explicit;
} LSScopeVar;

typedef struct {
    LSScopeVar vars[LS_MAX_VARS];
    int nvars;
    int scope_id;
} LSScopeFrame;

static LSScopeFrame ls_stack[LS_MAX_DEPTH];
static int ls_depth = 1;           /* 1 = global scope */
static int ls_scope_counter = 1;
static int ls_in_fn = 0;
static int ls_fn_depth = 0;

static void ls_init(void) {
    ls_depth = 1;
    ls_stack[0].nvars = 0;
    ls_stack[0].scope_id = 0;
    ls_scope_counter = 1;
    ls_in_fn = 0;
    ls_fn_depth = 0;
}

static const char *ls_lookup(const char *name) {
    for (int d = ls_depth - 1; d >= 0; d--) {
        for (int i = 0; i < ls_stack[d].nvars; i++) {
            if (!strcmp(ls_stack[d].vars[i].orig, name)) {
                return ls_stack[d].vars[i].scoped;
            }
        }
    }
    return NULL;
}

static int ls_exists_in_enclosing(const char *name) {
    if (ls_depth <= 1) return 0;
    for (int d = ls_depth - 2; d >= 0; d--) {
        for (int i = 0; i < ls_stack[d].nvars; i++) {
            if (!strcmp(ls_stack[d].vars[i].orig, name)) {
                return 1;
            }
        }
    }
    return 0;
}

static int ls_exists_in_current(const char *name) {
    if (ls_depth < 1) return 0;
    int d = ls_depth - 1;
    for (int i = 0; i < ls_stack[d].nvars; i++) {
        if (!strcmp(ls_stack[d].vars[i].orig, name)) return 1;
    }
    return 0;
}

static const char *ls_declare(const char *name, int is_explicit_local) {
    if (ls_depth < 1) return name;
    if (name[0] == '_' || !strncmp(name, "nx_", 3)) return name;

    int should_rename = 0;
    if (is_explicit_local) {
        should_rename = 1;
    } else if (ls_in_fn && ls_depth >= 3) {
        /* Nested block inside a function */
        should_rename = 1;
    }

    LSScopeFrame *f = should_rename ? &ls_stack[ls_depth - 1] : &ls_stack[0];

    /* Check if already in this exact frame */
    for (int i = 0; i < f->nvars; i++) {
        if (!strcmp(f->vars[i].orig, name)) {
            return f->vars[i].scoped;
        }
    }

    if (f->nvars >= LS_MAX_VARS) return name;
    LSScopeVar *v = &f->vars[f->nvars++];
    snprintf(v->orig, LS_NAME_LEN, "%s", name);
    v->is_explicit = is_explicit_local;

    if (should_rename) {
        snprintf(v->scoped, LS_SNAME_LEN, "_ls_%d_%s", f->scope_id, name);
    } else {
        snprintf(v->scoped, LS_SNAME_LEN, "%s", name);
    }
    return v->scoped;
}

static void ls_push(void) {
    if (ls_depth >= LS_MAX_DEPTH) return;
    LSScopeFrame *f = &ls_stack[ls_depth];
    f->nvars = 0;
    f->scope_id = ls_scope_counter++;
    ls_depth++;
}

static void ls_pop(void) {
    if (ls_depth > 1) {
        ls_depth--;
        if (ls_in_fn && ls_depth <= ls_fn_depth) {
            ls_in_fn = 0;
            ls_fn_depth = 0;
        }
    }
}

static int is_bare_assignment(const char *p, char *dest, size_t dest_cap, const char **rhs_out) {
    while (*p == ' ' || *p == '\t') p++;
    if (!is_ident0((unsigned char)*p)) return 0;

    int n = 0;
    while (is_ident1((unsigned char)*p)) {
        if (n < (int)dest_cap - 1) dest[n++] = *p;
        p++;
    }
    dest[n] = '\0';
    if (!n) return 0;
    if (is_nexus_keyword(dest)) return 0;

    while (*p == ' ' || *p == '\t') p++;
    if (*p == '=' && *(p + 1) != '=') {
        *rhs_out = p + 1;
        return 1;
    }
    return 0;
}

static int ls_substitute(const char *src, char *dst, size_t cap) {
    int len = (int)strlen(src);
    int changed = 0;
    int in_str = 0;
    size_t out_pos = 0;

    for (int i = 0; i < len; ) {
        char c = src[i];

        if (in_str && c == '\\' && i + 1 < len) {
            if (out_pos < cap - 1) dst[out_pos++] = c;
            i++;
            if (out_pos < cap - 1) dst[out_pos++] = src[i];
            i++;
            continue;
        }
        if (c == '"') {
            in_str = !in_str;
            if (out_pos < cap - 1) dst[out_pos++] = c;
            i++;
            continue;
        }
        if (in_str) {
            if (out_pos < cap - 1) dst[out_pos++] = c;
            i++;
            continue;
        }

        if (c == '#' || c == ';') {
            while (i < len && out_pos < cap - 1) dst[out_pos++] = src[i++];
            break;
        }

        if (is_ident0((unsigned char)c) && (i == 0 || !is_ident1((unsigned char)src[i-1]))) {
            int prev = i - 1;
            while (prev >= 0 && (src[prev] == ' ' || src[prev] == '\t')) prev--;
            if (prev >= 0 && src[prev] == '.') {
                while (i < len && is_ident1((unsigned char)src[i])) {
                    if (out_pos < cap - 1) dst[out_pos++] = src[i++];
                }
                continue;
            }

            int id_start = i;
            while (i < len && is_ident1((unsigned char)src[i])) i++;
            int id_len = i - id_start;
            char id[LS_NAME_LEN];
            if (id_len >= LS_NAME_LEN) id_len = LS_NAME_LEN - 1;
            memcpy(id, src + id_start, id_len);
            id[id_len] = '\0';

            if (!is_nexus_keyword(id) && id[0] != '_' && strncmp(id, "nx_", 3) != 0) {
                int is_fn_target = 0;
                int pkw = id_start - 1;
                while (pkw >= 0 && (src[pkw] == ' ' || src[pkw] == '\t')) pkw--;
                if (pkw >= 3 && !strncmp(src + pkw - 3, "call", 4) && (pkw - 4 < 0 || !is_ident1((unsigned char)src[pkw - 4]))) {
                    is_fn_target = 1;
                }
                if (pkw >= 1 && !strncmp(src + pkw - 1, "fn", 2) && (pkw - 2 < 0 || !is_ident1((unsigned char)src[pkw - 2]))) {
                    is_fn_target = 1;
                }

                int next_c = i;
                while (next_c < len && (src[next_c] == ' ' || src[next_c] == '\t')) next_c++;
                if (next_c < len && src[next_c] == '(' && fn_registry_find(id) >= 0) {
                    is_fn_target = 1;
                }

                if (!is_fn_target) {
                    const char *sub = ls_lookup(id);
                    if (sub && strcmp(sub, id) != 0) {
                        size_t slen = strlen(sub);
                        if (out_pos + slen < cap) {
                            memcpy(dst + out_pos, sub, slen);
                            out_pos += slen;
                            changed = 1;
                            continue;
                        }
                    }
                }
            }

            if (out_pos + id_len < cap) {
                memcpy(dst + out_pos, src + id_start, id_len);
                out_pos += id_len;
            }
            continue;
        }

        if (out_pos < cap - 1) dst[out_pos++] = c;
        i++;
    }
    dst[out_pos] = '\0';
    return changed;
}

static int ls_apply(const char *raw, char *dst, size_t cap) {
    char clean[MAXLINE];
    snprintf(clean, sizeof clean, "%s", raw);
    char *p = clean;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '#' || *p == ';' || *p == '\0') {
        return 0;
    }

    if (*p == '_' && (strncmp(p, "_ls_", 4) == 0 || strncmp(p, "_fcall_", 7) == 0 ||
                      strncmp(p, "_c_arg_", 7) == 0 || strncmp(p, "_ret_", 5) == 0 ||
                      strncmp(p, "_nx_", 4) == 0 || strncmp(p, "_as_", 4) == 0)) {
        return 0;
    }

    int indent_len = (int)(p - clean);
    char indent[128] = "";
    if (indent_len > 0) {
        int il = indent_len < 127 ? indent_len : 127;
        memcpy(indent, clean, il);
        indent[il] = '\0';
    }

    char body[MAXLINE];
    snprintf(body, sizeof body, "%s", p);
    int blen = (int)strlen(body);
    while (blen > 0 && (body[blen - 1] == '\n' || body[blen - 1] == '\r')) body[--blen] = '\0';

    /* 1. Track any '}' that appears before any '{' */
    int in_str = 0;
    for (int i = 0; body[i]; i++) {
        if (in_str && body[i] == '\\' && body[i + 1]) { i++; continue; }
        if (body[i] == '"') in_str = !in_str;
        if (in_str) continue;
        if (body[i] == '#' || body[i] == ';') break;
        if (body[i] == '}') {
            ls_pop();
        } else if (body[i] == '{') {
            break;
        }
    }

    /* 2. Check for function header */
    if (!strncmp(body, "fn ", 3) || !strncmp(body, "fn\t", 3)) {
        if (strchr(body, '(')) {
            return 0;
        } else if (strchr(body, '{')) {
            ls_push();
            ls_in_fn = 1;
            ls_fn_depth = ls_depth - 1;
            return 0;
        }
    }

    /* 3. Check for explicit local declaration: local x = ... */
    if (!strncmp(body, "local ", 6) || !strncmp(body, "local\t", 6)) {
        char *after_kw = body + 6;
        while (*after_kw == ' ' || *after_kw == '\t') after_kw++;
        char vname[LS_NAME_LEN] = "";
        int vn = 0;
        char *vp = after_kw;
        while (*vp && is_ident1((unsigned char)*vp)) {
            if (vn < LS_NAME_LEN - 1) vname[vn++] = *vp;
            vp++;
        }
        vname[vn] = '\0';
        while (*vp == ' ' || *vp == '\t') vp++;
        if (*vp == '=' && *(vp + 1) != '=') {
            const char *scoped = ls_declare(vname, 1);
            char rhs_sub[MAXLINE];
            ls_substitute(vp + 1, rhs_sub, sizeof rhs_sub);
            snprintf(dst, cap, "%slet %s =%s\n", indent, scoped, rhs_sub);
            return 1;
        }
    }

    /* 4. Check for type-alias declarations: var x = ..., int x = ..., float x = ... */
    if (!strncmp(body, "var ", 4) || !strncmp(body, "var\t", 4) ||
        !strncmp(body, "int ", 4) || !strncmp(body, "int\t", 4) ||
        !strncmp(body, "float ", 6) || !strncmp(body, "float\t", 6)) {
        const char *after_kw = strchr(body, ' ');
        if (!after_kw) after_kw = strchr(body, '\t');
        while (after_kw && (*after_kw == ' ' || *after_kw == '\t')) after_kw++;
        if (after_kw) {
            char vname[LS_NAME_LEN] = "";
            int vn = 0;
            const char *vp = after_kw;
            while (*vp && is_ident1((unsigned char)*vp)) {
                if (vn < LS_NAME_LEN - 1) vname[vn++] = *vp;
                vp++;
            }
            vname[vn] = '\0';
            while (*vp == ' ' || *vp == '\t') vp++;
            if (*vp == '=' && *(vp + 1) != '=') {
                const char *scoped = ls_declare(vname, 1);
                char rhs_sub[MAXLINE];
                ls_substitute(vp + 1, rhs_sub, sizeof rhs_sub);
                snprintf(dst, cap, "%slet %s =%s\n", indent, scoped, rhs_sub);
                return 1;
            }
        }
    }

    /* 5. Check for 'let dest = rhs' */
    if (!strncmp(body, "let ", 4) || !strncmp(body, "let\t", 4)) {
        char *after_let = body + 4;
        while (*after_let == ' ' || *after_let == '\t') after_let++;
        char vname[LS_NAME_LEN] = "";
        int vn = 0;
        char *vp = after_let;
        while (*vp && is_ident1((unsigned char)*vp)) {
            if (vn < LS_NAME_LEN - 1) vname[vn++] = *vp;
            vp++;
        }
        vname[vn] = '\0';
        while (*vp == ' ' || *vp == '\t') vp++;

        if (*vp == ',') {
            char *eq = strchr(vp, '=');
            if (eq && *(eq + 1) != '=') {
                char rhs_sub[MAXLINE];
                if (ls_substitute(eq + 1, rhs_sub, sizeof rhs_sub)) {
                    int dlen = (int)(eq - clean) + 1;
                    snprintf(dst, cap, "%.*s%s\n", dlen, clean, rhs_sub);
                    return 1;
                }
            }
            return 0;
        }

        if (*vp == '=' && *(vp + 1) != '=') {
            const char *scoped = NULL;
            if (ls_exists_in_enclosing(vname) && !ls_exists_in_current(vname)) {
                scoped = ls_lookup(vname);
            } else {
                scoped = ls_declare(vname, 0);
            }
            char rhs_sub[MAXLINE];
            ls_substitute(vp + 1, rhs_sub, sizeof rhs_sub);
            snprintf(dst, cap, "%slet %s =%s\n", indent, scoped ? scoped : vname, rhs_sub);
            return 1;
        }
    }

    /* 6. Check for bare assignment: <ident> = <rhs> */
    char bare_dest[LS_NAME_LEN];
    const char *bare_rhs = NULL;
    if (is_bare_assignment(body, bare_dest, sizeof bare_dest, &bare_rhs)) {
        const char *scoped = ls_lookup(bare_dest);
        char rhs_sub[MAXLINE];
        ls_substitute(bare_rhs, rhs_sub, sizeof rhs_sub);
        snprintf(dst, cap, "%slet %s =%s\n", indent, scoped ? scoped : bare_dest, rhs_sub);
        return 1;
    }

    /* 7. General identifier substitution & '{' tracking */
    char subbed[MAXLINE];
    int changed = ls_substitute(raw, subbed, sizeof subbed);

    in_str = 0;
    for (int i = 0; body[i]; i++) {
        if (in_str && body[i] == '\\' && body[i + 1]) { i++; continue; }
        if (body[i] == '"') in_str = !in_str;
        if (in_str) continue;
        if (body[i] == '#' || body[i] == ';') break;
        if (body[i] == '{') {
            ls_push();
        }
    }

    if (changed) {
        snprintf(dst, cap, "%s", subbed);
        return 1;
    }
    return 0;
}


typedef enum {
    TY_UNKNOWN = 0,
    TY_INT,
    TY_FLOAT,
    TY_PTR
} NexType;

typedef struct {
    char scope[64];
    char name[64];
    NexType type;
} VarTypeEntry;

#define MAX_VAR_TYPES 4096
static VarTypeEntry VTYPES[MAX_VAR_TYPES];
static int num_vtypes = 0;

typedef struct {
    char name[64];
    NexType ret_type;
    NexType param_types[32];
    int nparams;
} FnSigEntry;

#define MAX_FN_SIGS 512
static FnSigEntry FNSIGS[MAX_FN_SIGS];
static int num_fnsigs = 0;

static void set_var_type(const char *scope, const char *name, NexType type) {
    if (!name || !*name) return;
    const char *sc = scope ? scope : "";
    for (int i = 0; i < num_vtypes; i++) {
        if (!strcmp(VTYPES[i].scope, sc) && !strcmp(VTYPES[i].name, name)) {
            if (type != TY_UNKNOWN) VTYPES[i].type = type;
            return;
        }
    }
    if (num_vtypes < MAX_VAR_TYPES) {
        snprintf(VTYPES[num_vtypes].scope, sizeof(VTYPES[num_vtypes].scope), "%s", sc);
        snprintf(VTYPES[num_vtypes].name, sizeof(VTYPES[num_vtypes].name), "%s", name);
        VTYPES[num_vtypes].type = type;
        num_vtypes++;
    }
}

static NexType get_var_type(const char *scope, const char *name) {
    if (!name || !*name) return TY_UNKNOWN;
    const char *sc = scope ? scope : "";
    if (sc[0] != '\0') {
        for (int i = 0; i < num_vtypes; i++) {
            if (!strcmp(VTYPES[i].scope, sc) && !strcmp(VTYPES[i].name, name)) {
                return VTYPES[i].type;
            }
        }
    }
    for (int i = 0; i < num_vtypes; i++) {
        if (VTYPES[i].scope[0] == '\0' && !strcmp(VTYPES[i].name, name)) {
            return VTYPES[i].type;
        }
    }
    return TY_UNKNOWN;
}

static FnSigEntry *find_or_create_fn_sig(const char *name) {
    if (!name || !*name) return NULL;
    for (int i = 0; i < num_fnsigs; i++) {
        if (!strcmp(FNSIGS[i].name, name)) return &FNSIGS[i];
    }
    if (num_fnsigs < MAX_FN_SIGS) {
        FnSigEntry *entry = &FNSIGS[num_fnsigs++];
        memset(entry, 0, sizeof(*entry));
        snprintf(entry->name, sizeof(entry->name), "%s", name);
        return entry;
    }
    return NULL;
}

static void set_fn_ret_type(const char *name, NexType type) {
    FnSigEntry *sig = find_or_create_fn_sig(name);
    if (sig && type != TY_UNKNOWN) sig->ret_type = type;
}

static NexType get_fn_ret_type(const char *name) {
    if (!name || !*name) return TY_UNKNOWN;
    for (int i = 0; i < num_fnsigs; i++) {
        if (!strcmp(FNSIGS[i].name, name)) return FNSIGS[i].ret_type;
    }
    return TY_UNKNOWN;
}

static int is_float_literal(const char *s) {
    if (!*s) return 0;
    const char *p = s;
    if (*p == '+' || *p == '-') p++;
    if (!isdigit((unsigned char)*p)) return 0;
    while (isdigit((unsigned char)*p)) p++;
    if (*p != '.') return 0;
    p++;
    if (!isdigit((unsigned char)*p)) return 0;
    while (isdigit((unsigned char)*p)) p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    return *p == '\0';
}

static int is_int_literal(const char *s) {
    if (!*s) return 0;
    const char *p = s;
    if (*p == '+' || *p == '-') p++;
    if (!isdigit((unsigned char)*p)) return 0;
    while (isdigit((unsigned char)*p)) p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    return *p == '\0';
}

static NexType get_operand_type(const char *scope, const char *tok) {
    if (!tok || !*tok) return TY_UNKNOWN;
    if (is_float_literal(tok)) return TY_FLOAT;
    if (is_int_literal(tok)) return TY_INT;
    if (tok[0] == '"') return TY_PTR;
    if (tok[0] == '[') return TY_PTR;

    if (!strncmp(tok, "_ret_", 5)) {
        NexType rt = get_fn_ret_type(tok + 5);
        if (rt != TY_UNKNOWN) return rt;
    }
    NexType frt = get_fn_ret_type(tok);
    if (frt != TY_UNKNOWN) return frt;

    NexType vt = get_var_type(scope, tok);
    if (vt != TY_UNKNOWN) return vt;

    return TY_UNKNOWN;
}

static int is_operand_float(const char *scope, const char *tok) {
    return get_operand_type(scope, tok) == TY_FLOAT;
}

/* --------------------------------------------------------------------------
 * floating-point syntax sugar
 * -------------------------------------------------------------------------- */
static int ftmp_counter = 0;

static const char *get_fop(const char *op) {
    if (!strcmp(op, "+.") || !strcmp(op, "+")) return "fadd";
    if (!strcmp(op, "-.") || !strcmp(op, "-")) return "fsub";
    if (!strcmp(op, "*.") || !strcmp(op, "*")) return "fmul";
    if (!strcmp(op, "/.") || !strcmp(op, "/")) return "fdiv";
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

static int is_relop_std(const char *s, int *len, const char **clean_op) {
    if (!strncmp(s, "==", 2)) { *len = 2; *clean_op = "=="; return 1; }
    if (!strncmp(s, "!=", 2)) { *len = 2; *clean_op = "!="; return 1; }
    if (!strncmp(s, "<=", 2)) { *len = 2; *clean_op = "<="; return 1; }
    if (!strncmp(s, ">=", 2)) { *len = 2; *clean_op = ">="; return 1; }
    if (*s == '<')             { *len = 1; *clean_op = "<"; return 1; }
    if (*s == '>')             { *len = 1; *clean_op = ">"; return 1; }
    return 0;
}

static void trim_bounds(const char *start, const char *end, char *out, size_t cap) {
    while (start < end && (*start == ' ' || *start == '\t')) start++;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) end--;
    size_t len = (size_t)(end - start);
    if (len >= cap) len = cap - 1;
    if (len > 0) memcpy(out, start, len);
    out[len] = '\0';
}

static int desugar_store_float(const char *in, char *out, size_t cap) {
    char clean[MAXLINE];
    snprintf(clean, sizeof clean, "%s", in);
    char *p = clean;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '#' || *p == ';' || *p == '\0') return 0;

    int is_store64 = 0;
    if (!strncmp(p, "store64 ", 8) || !strncmp(p, "store64\t", 8)) {
        is_store64 = 1;
        p += 8;
    } else if (!strncmp(p, "store ", 6) || !strncmp(p, "store\t", 6)) {
        is_store64 = 0;
        p += 6;
    } else {
        return 0;
    }

    int indent_len = (int)(p - clean - (is_store64 ? 8 : 6));
    char indent[128] = "";
    if (indent_len > 0) {
        if (indent_len >= (int)sizeof(indent)) indent_len = (int)sizeof(indent) - 1;
        snprintf(indent, sizeof indent, "%.*s", indent_len, clean);
    }

    while (*p == ' ' || *p == '\t') p++;
    if (*p != '[') return 0;
    char *close_bracket = strchr(p, ']');
    if (!close_bracket) return 0;

    char addr[128];
    int addr_len = (int)(close_bracket - p + 1);
    snprintf(addr, sizeof addr, "%.*s", addr_len, p);

    char *val = close_bracket + 1;
    while (*val == ' ' || *val == '\t') val++;
    char val_buf[128];
    snprintf(val_buf, sizeof val_buf, "%s", val);
    char *cm = strchr(val_buf, '#');
    if (cm) *cm = '\0';
    char *sc = strchr(val_buf, ';');
    if (sc) *sc = '\0';
    int vlen = (int)strlen(val_buf);
    while (vlen > 0 && (val_buf[vlen-1] == ' ' || val_buf[vlen-1] == '\t' ||
                        val_buf[vlen-1] == '\n' || val_buf[vlen-1] == '\r')) {
        val_buf[--vlen] = '\0';
    }

    if (is_float_literal(val_buf)) {
        static int fstore_counter = 0;
        int cid = fstore_counter++;
        snprintf(out, cap, "%slet _f_sval_%d = %s\n%sstore64 %s _f_sval_%d\n",
                 indent, cid, val_buf, indent, addr, cid);
        return 1;
    }
    return 0;
}

static int op_prec(const char *op) {
    if (!strcmp(op, "+.") || !strcmp(op, "-.") || !strcmp(op, "+") || !strcmp(op, "-")) return 1;
    if (!strcmp(op, "*.") || !strcmp(op, "/.") || !strcmp(op, "*") || !strcmp(op, "/")) return 2;
    return 0;
}

typedef enum {
    TOK_F_END,
    TOK_F_OPERAND,
    TOK_F_OP,
    TOK_F_LPAREN,
    TOK_F_RPAREN
} FTokType;

typedef struct {
    FTokType type;
    char text[128];
} FToken;

static int tokenize_fexpr(const char *s, FToken *tokens, int max_tokens) {
    int ntok = 0;
    const char *p = s;
    while (*p && ntok < max_tokens) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p || *p == '#' || *p == ';' || *p == '\n' || *p == '\r') break;

        if (*p == '(') {
            tokens[ntok].type = TOK_F_LPAREN;
            strcpy(tokens[ntok].text, "(");
            ntok++;
            p++;
            continue;
        }
        if (*p == ')') {
            tokens[ntok].type = TOK_F_RPAREN;
            strcpy(tokens[ntok].text, ")");
            ntok++;
            p++;
            continue;
        }

        /* Check dotted float operators: +., -., *., /. */
        if ((*p == '+' || *p == '-' || *p == '*' || *p == '/') && *(p + 1) == '.') {
            tokens[ntok].type = TOK_F_OP;
            tokens[ntok].text[0] = *p;
            tokens[ntok].text[1] = '.';
            tokens[ntok].text[2] = '\0';
            ntok++;
            p += 2;
            continue;
        }

        /* Binary operators: * and / are always binary */
        if (*p == '*' || *p == '/') {
            tokens[ntok].type = TOK_F_OP;
            tokens[ntok].text[0] = *p;
            tokens[ntok].text[1] = '\0';
            ntok++;
            p++;
            continue;
        }

        /* Binary vs unary + and - */
        if (*p == '+' || *p == '-') {
            if (ntok > 0 && (tokens[ntok - 1].type == TOK_F_OPERAND || tokens[ntok - 1].type == TOK_F_RPAREN)) {
                tokens[ntok].type = TOK_F_OP;
                tokens[ntok].text[0] = *p;
                tokens[ntok].text[1] = '\0';
                ntok++;
                p++;
                continue;
            }
        }

        /* General operand */
        int len = 0;
        char buf[128];
        while (*p && *p != ' ' && *p != '\t' && *p != '(' && *p != ')' &&
               *p != '#' && *p != ';' && *p != '\n' && *p != '\r') {
            if ((*p == '+' || *p == '-' || *p == '*' || *p == '/') && *(p + 1) == '.') {
                break;
            }
            if (*p == '*' || *p == '/') {
                break;
            }
            if (len > 0 && (*p == '+' || *p == '-')) {
                break;
            }
            if (len < 127) buf[len++] = *p;
            p++;
        }
        buf[len] = '\0';
        if (len > 0) {
            tokens[ntok].type = TOK_F_OPERAND;
            strcpy(tokens[ntok].text, buf);
            ntok++;
        }
    }
    return ntok;
}

static int is_fexpr_tokens(const char *scope, const FToken *tokens, int ntok) {
    for (int i = 0; i < ntok; i++) {
        if (tokens[i].type == TOK_F_OP && strchr(tokens[i].text, '.')) return 1;
    }
    int has_op = 0;
    int has_float = 0;
    for (int i = 0; i < ntok; i++) {
        if (tokens[i].type == TOK_F_OP) has_op = 1;
        if (tokens[i].type == TOK_F_OPERAND) {
            if (is_operand_float(scope, tokens[i].text)) has_float = 1;
        }
    }
    return (has_op && has_float);
}

static int desugar_float_expr(const char *indent, const char *dest, const char *rhs, char *out, size_t cap) {
    FToken tokens[128];
    int ntok = tokenize_fexpr(rhs, tokens, 128);
    if (ntok == 0) return 0;

    if (!is_fexpr_tokens(cur_fn_name, tokens, ntok)) return 0;

    out[0] = '\0';

    for (int i = 0; i < ntok; i++) {
        if (tokens[i].type == TOK_F_OPERAND) {
            if (is_int_literal(tokens[i].text)) {
                strncat(tokens[i].text, ".0", sizeof(tokens[i].text) - strlen(tokens[i].text) - 1);
            } else if (get_operand_type(cur_fn_name, tokens[i].text) == TY_INT) {
                char prom[128];
                snprintf(prom, sizeof prom, "_f_prom_%d", ftmp_counter++);
                char line[MAXLINE];
                snprintf(line, sizeof line, "%slet %s = itof %s\n", indent, prom, tokens[i].text);
                strncat(out, line, cap - strlen(out) - 1);
                set_var_type(cur_fn_name, prom, TY_FLOAT);
                snprintf(tokens[i].text, sizeof(tokens[i].text), "%s", prom);
            }
        }
    }

    char op_stack[128][16];
    int op_top = 0;

    char val_stack[128][128];
    int val_top = 0;

    for (int i = 0; i < ntok; i++) {
        if (tokens[i].type == TOK_F_OPERAND) {
            if (val_top >= 128) return 0;
            strcpy(val_stack[val_top++], tokens[i].text);
        } else if (tokens[i].type == TOK_F_LPAREN) {
            if (op_top >= 128) return 0;
            strcpy(op_stack[op_top++], "(");
        } else if (tokens[i].type == TOK_F_RPAREN) {
            while (op_top > 0 && strcmp(op_stack[op_top - 1], "(") != 0) {
                if (val_top < 2) return 0;
                char op[16];
                strcpy(op, op_stack[--op_top]);
                char r[128], l[128];
                strcpy(r, val_stack[--val_top]);
                strcpy(l, val_stack[--val_top]);

                char tmp[128];
                snprintf(tmp, sizeof tmp, "_f_tmp_%d", ftmp_counter++);
                char line[MAXLINE];
                snprintf(line, sizeof line, "%slet %s = %s %s %s\n", indent, tmp, get_fop(op), l, r);
                strncat(out, line, cap - strlen(out) - 1);
                set_var_type(cur_fn_name, tmp, TY_FLOAT);
                strcpy(val_stack[val_top++], tmp);
            }
            if (op_top > 0 && strcmp(op_stack[op_top - 1], "(") == 0) {
                op_top--;
            } else {
                return 0;
            }
        } else if (tokens[i].type == TOK_F_OP) {
            int prec = op_prec(tokens[i].text);
            while (op_top > 0 && strcmp(op_stack[op_top - 1], "(") != 0 &&
                   op_prec(op_stack[op_top - 1]) >= prec) {
                if (val_top < 2) return 0;
                char op[16];
                strcpy(op, op_stack[--op_top]);
                char r[128], l[128];
                strcpy(r, val_stack[--val_top]);
                strcpy(l, val_stack[--val_top]);

                char tmp[128];
                snprintf(tmp, sizeof tmp, "_f_tmp_%d", ftmp_counter++);
                char line[MAXLINE];
                snprintf(line, sizeof line, "%slet %s = %s %s %s\n", indent, tmp, get_fop(op), l, r);
                strncat(out, line, cap - strlen(out) - 1);
                set_var_type(cur_fn_name, tmp, TY_FLOAT);
                strcpy(val_stack[val_top++], tmp);
            }
            strcpy(op_stack[op_top++], tokens[i].text);
        }
    }

    while (op_top > 0) {
        if (strcmp(op_stack[op_top - 1], "(") == 0) return 0;
        if (val_top < 2) return 0;
        char op[16];
        strcpy(op, op_stack[--op_top]);
        char r[128], l[128];
        strcpy(r, val_stack[--val_top]);
        strcpy(l, val_stack[--val_top]);

        int is_final = (op_top == 0 && val_top == 0);
        char target[128];
        if (is_final) {
            strcpy(target, dest);
        } else {
            snprintf(target, sizeof target, "_f_tmp_%d", ftmp_counter++);
        }

        char line[MAXLINE];
        snprintf(line, sizeof line, "%slet %s = %s %s %s\n", indent, target, get_fop(op), l, r);
        strncat(out, line, cap - strlen(out) - 1);
        set_var_type(cur_fn_name, target, TY_FLOAT);
        strcpy(val_stack[val_top++], target);
    }

    if (val_top == 1 && strcmp(val_stack[0], dest) != 0) {
        char line[MAXLINE];
        snprintf(line, sizeof line, "%slet %s = %s\n", indent, dest, val_stack[0]);
        strncat(out, line, cap - strlen(out) - 1);
    }

    set_var_type(cur_fn_name, dest, TY_FLOAT);
    return 1;
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
    int was_explicit_f = 0;
    const char *cond_type = NULL;
    char prefix[256] = "";

    if (!strncmp(p, "if ", 3) || !strncmp(p, "if\t", 3)) {
        is_cond = 1; cond_type = "if_f "; p += 3;
    } else if (!strncmp(p, "if_f ", 5) || !strncmp(p, "if_f\t", 5)) {
        is_cond = 1; was_explicit_f = 1; cond_type = "if_f "; p += 5;
    } else if (!strncmp(p, "while ", 6) || !strncmp(p, "while\t", 6)) {
        is_cond = 1; cond_type = "while_f "; p += 6;
    } else if (!strncmp(p, "while_f ", 8) || !strncmp(p, "while_f\t", 8)) {
        is_cond = 1; was_explicit_f = 1; cond_type = "while_f "; p += 8;
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
                was_explicit_f = 1;
                cond_type = prefix;
            }
        }
    }

    if (is_cond) {
        int in_quote = 0;
        /* First check dotted float operators: ==., <., etc. */
        for (char *c = p; *c; c++) {
            if (in_quote && *c == '\\' && *(c + 1)) { c++; continue; }
            if (*c == '"') { in_quote = !in_quote; continue; }
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

        if (was_explicit_f) {
            /* Already clean if_f / while_f / else if_f, do not re-desugar */
            return 0;
        }

        /* Check standard operators: ==, !=, <, >, <=, >= */
        in_quote = 0;
        for (char *c = p; *c; c++) {
            if (in_quote && *c == '\\' && *(c + 1)) { c++; continue; }
            if (*c == '"') { in_quote = !in_quote; continue; }
            if (in_quote) continue;
            int rlen = 0;
            const char *std_op = NULL;
            if (is_relop_std(c, &rlen, &std_op)) {
                char left[128], right[128];
                trim_bounds(p, c, left, sizeof left);

                /* find '{' string-aware */
                char *brace = NULL;
                int b_in_str = 0;
                for (char *bc = c + rlen; *bc; bc++) {
                    if (b_in_str && *bc == '\\' && *(bc + 1)) { bc++; continue; }
                    if (*bc == '"') { b_in_str = !b_in_str; continue; }
                    if (!b_in_str && *bc == '{') { brace = bc; break; }
                }
                char rest[256] = "";
                if (brace) {
                    trim_bounds(c + rlen, brace, right, sizeof right);
                    snprintf(rest, sizeof rest, "%s", brace);
                } else {
                    trim_bounds(c + rlen, c + strlen(c), right, sizeof right);
                }

                int left_f = is_operand_float(cur_fn_name, left);
                int right_f = is_operand_float(cur_fn_name, right);

                if (left_f || right_f || was_explicit_f) {
                    out[0] = '\0';
                    /* Promote left if int variable */
                    if (!left_f && get_operand_type(cur_fn_name, left) == TY_INT) {
                        char prom[128];
                        snprintf(prom, sizeof prom, "_f_prom_%d", ftmp_counter++);
                        char line[MAXLINE];
                        snprintf(line, sizeof line, "%slet %s = itof %s\n", indent, prom, left);
                        strncat(out, line, cap - strlen(out) - 1);
                        set_var_type(cur_fn_name, prom, TY_FLOAT);
                        snprintf(left, sizeof left, "%s", prom);
                    } else if (is_int_literal(left)) {
                        strncat(left, ".0", sizeof(left) - strlen(left) - 1);
                    }

                    /* Promote right if int variable */
                    if (!right_f && get_operand_type(cur_fn_name, right) == TY_INT) {
                        char prom[128];
                        snprintf(prom, sizeof prom, "_f_prom_%d", ftmp_counter++);
                        char line[MAXLINE];
                        snprintf(line, sizeof line, "%slet %s = itof %s\n", indent, prom, right);
                        strncat(out, line, cap - strlen(out) - 1);
                        set_var_type(cur_fn_name, prom, TY_FLOAT);
                        snprintf(right, sizeof right, "%s", prom);
                    } else if (is_int_literal(right)) {
                        strncat(right, ".0", sizeof(right) - strlen(right) - 1);
                    }

                    char line[MAXLINE];
                    if (rest[0]) {
                        snprintf(line, sizeof line, "%s%s%s %s %s %s\n", indent, cond_type, left, std_op, right, rest);
                    } else {
                        snprintf(line, sizeof line, "%s%s%s %s %s\n", indent, cond_type, left, std_op, right);
                    }
                    strncat(out, line, cap - strlen(out) - 1);
                    return 1;
                }
                break;
            }
        }
        return 0;
    }

    /* 2. Let assignments: let <dest> = <rhs> */
    if (!strncmp(p, "let ", 4) || !strncmp(p, "let\t", 4)) {
        char *eq = strchr(p, '=');
        if (eq) {
            char *rhs = eq + 1;
            while (*rhs == ' ' || *rhs == '\t') rhs++;
            if (*rhs == '"') return 0;

            char dest[128];
            int dlen = (int)(eq - (p + 4));
            snprintf(dest, sizeof dest, "%.*s", dlen, p + 4);
            char *dstart = dest;
            while (*dstart == ' ' || *dstart == '\t') dstart++;
            char *dend = dstart + strlen(dstart);
            while (dend > dstart && (dend[-1] == ' ' || dend[-1] == '\t')) *--dend = '\0';

            char rhs_clean[MAXLINE];
            snprintf(rhs_clean, sizeof rhs_clean, "%s", rhs);
            char *cm = strchr(rhs_clean, '#');
            if (cm) *cm = '\0';
            char *sc = strchr(rhs_clean, ';');
            if (sc) *sc = '\0';
            int rlen = (int)strlen(rhs_clean);
            while (rlen > 0 && (rhs_clean[rlen-1] == ' ' || rhs_clean[rlen-1] == '\t' ||
                                rhs_clean[rlen-1] == '\n' || rhs_clean[rlen-1] == '\r')) {
                rhs_clean[--rlen] = '\0';
            }

            if (desugar_float_expr(indent, dstart, rhs_clean, out, cap)) {
                return 1;
            }

            /* Record type if single value/literal/builtin */
            if (is_float_literal(rhs_clean)) {
                set_var_type(cur_fn_name, dstart, TY_FLOAT);
            } else if (is_int_literal(rhs_clean)) {
                set_var_type(cur_fn_name, dstart, TY_INT);
            } else if (!strncmp(rhs_clean, "fadd ", 5) || !strncmp(rhs_clean, "fsub ", 5) ||
                       !strncmp(rhs_clean, "fmul ", 5) || !strncmp(rhs_clean, "fdiv ", 5) ||
                       !strncmp(rhs_clean, "fsqrt ", 6) || !strncmp(rhs_clean, "fneg ", 5) ||
                       !strncmp(rhs_clean, "itof ", 5)) {
                set_var_type(cur_fn_name, dstart, TY_FLOAT);
            } else if (!strncmp(rhs_clean, "ftoi ", 5)) {
                set_var_type(cur_fn_name, dstart, TY_INT);
            } else {
                NexType src_t = get_operand_type(cur_fn_name, rhs_clean);
                if (src_t != TY_UNKNOWN) set_var_type(cur_fn_name, dstart, src_t);
            }
        }
    }

    return 0;
}

static int desugar_print(const char *in, char *out, size_t cap) {
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

    if (!strncmp(p, "print ", 6) || !strncmp(p, "print\t", 6)) {
        char *arg = p + 6;
        while (*arg == ' ' || *arg == '\t') arg++;
        if (*arg == '"' || *arg == '\0' || *arg == '#' || *arg == ';') return 0;

        char var[128];
        int vn = 0;
        while (*arg && *arg != ' ' && *arg != '\t' && *arg != '#' && *arg != ';' && *arg != '\n' && *arg != '\r') {
            if (vn < 127) var[vn++] = *arg;
            arg++;
        }
        var[vn] = '\0';
        while (*arg == ' ' || *arg == '\t') arg++;

        if (is_float_literal(var) || get_operand_type(cur_fn_name, var) == TY_FLOAT) {
            snprintf(out, cap, "%sprint_float %s%s\n", indent, var, *arg ? arg : "");
            return 1;
        }
    }
    return 0;
}

/* --------------------------------------------------------------------------
 * function parameter, call expression, and return value syntax desugaring
 * -------------------------------------------------------------------------- */
static int call_tmp_counter = 0;
static int cur_fn_is_scoped = 0;
static char cur_fn_locals[256][64];
static int cur_fn_nlocals = 0;

static void fn_add_local(const char *var) {
    if (!var || !*var) return;
    if (var[0] == '_' && strncmp(var, "_fcall_", 7) != 0 && strncmp(var, "_ls_", 4) != 0 &&
        strncmp(var, "_tern_", 6) != 0 && strncmp(var, "_nc_", 4) != 0 &&
        strncmp(var, "_paren_", 7) != 0 && strncmp(var, "_prec_", 6) != 0 &&
        strncmp(var, "_nxu_", 5) != 0) return;
    for (int i = 0; i < cur_fn_nlocals; i++) {
        if (!strcmp(cur_fn_locals[i], var)) return;
    }
    if (cur_fn_nlocals < 256) {
        snprintf(cur_fn_locals[cur_fn_nlocals++], 64, "%s", var);
    }
}

static void update_fn_depth(const char *line) {
    int in_str = 0;
    for (int i = 0; line[i]; i++) {
        if (in_str && line[i] == '\\' && line[i + 1]) { i++; continue; }
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
                    cur_fn_is_scoped = 0;
                    cur_fn_nlocals = 0;
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
        if (in_str && c == '\\' && arg_str[i + 1]) { i++; continue; }
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

static int find_matching_paren(const char *s, int open_idx) {
    int depth = 0;
    int in_str = 0;
    int len = (int)strlen(s);
    for (int i = open_idx; i < len; i++) {
        if (in_str && s[i] == '\\' && s[i + 1]) { i++; continue; }
        if (s[i] == '"') in_str = !in_str;
        if (in_str) continue;
        if (s[i] == '(') depth++;
        else if (s[i] == ')') {
            depth--;
            if (depth == 0) return i;
        }
    }
    return -1;
}

static int contains_fn_call(const char *s) {
    int len = (int)strlen(s);
    int in_str = 0;
    for (int i = 0; i < len; i++) {
        if (in_str && s[i] == '\\' && s[i + 1]) { i++; continue; }
        if (s[i] == '"') { in_str = !in_str; continue; }
        if (in_str) continue;
        if (s[i] == '#' || s[i] == ';') break;
        if (is_ident0((unsigned char)s[i]) && (i == 0 || !is_ident1((unsigned char)s[i-1]))) {
            int start = i;
            while (i < len && is_ident1((unsigned char)s[i])) i++;
            int id_len = i - start;
            if (id_len >= 128) continue;
            char id[128];
            memcpy(id, s + start, id_len);
            id[id_len] = '\0';
            if (is_nexus_keyword(id)) continue;
            int j = i;
            while (j < len && (s[j] == ' ' || s[j] == '\t')) j++;
            if (j < len && s[j] == '(') return 1;
        }
    }
    return 0;
}

static void emit_scoped_call(const char *indent, const char *fn_name, char args[][512], int nargs, const char *dest, char *out, size_t cap) {
    char line_buf[MAXLINE];
    int arg_tmp_indices[32];

    /* 0. Handle any nested calls in arguments first */
    for (int i = 0; i < nargs; i++) {
        if (strstr(args[i], "call ") || strstr(args[i], "call\t") || contains_fn_call(args[i])) {
            int tmp_id = call_tmp_counter++;
            snprintf(line_buf, sizeof line_buf, "%slet _c_tmp_%d = %s\n",
                     indent, tmp_id, args[i]);
            strncat(out, line_buf, cap - strlen(out) - 1);
            snprintf(args[i], 512, "_c_tmp_%d", tmp_id);
        }
    }

    /* 1. Evaluate arguments to temporaries before saving caller frame */
    for (int i = 0; i < nargs; i++) {
        arg_tmp_indices[i] = call_tmp_counter++;
        snprintf(line_buf, sizeof line_buf, "%slet _c_arg_%d = %s\n",
                 indent, arg_tmp_indices[i], args[i]);
        strncat(out, line_buf, cap - strlen(out) - 1);
    }

    /* 2. Save stack frame activation record:
     *    [_nx_sp + 0] = caller _nx_fp (previous frame pointer link)
     *    [_nx_sp + 8] = frame depth / metadata
     *    [_nx_sp + 16 + i * 8] = caller local variables (cur_fn_locals[i])
     */
    int frame_size = 16 + cur_fn_nlocals * 8;
    if (cur_fn_is_scoped) {
        snprintf(line_buf, sizeof line_buf, "%sstore64 [_nx_sp + 0] _nx_fp\n", indent);
        strncat(out, line_buf, cap - strlen(out) - 1);
        for (int i = 0; i < cur_fn_nlocals; i++) {
            snprintf(line_buf, sizeof line_buf, "%sstore64 [_nx_sp + %d] %s\n",
                     indent, 16 + i * 8, cur_fn_locals[i]);
            strncat(out, line_buf, cap - strlen(out) - 1);
        }
        snprintf(line_buf, sizeof line_buf, "%slet _nx_fp = _nx_sp\n", indent);
        strncat(out, line_buf, cap - strlen(out) - 1);
        snprintf(line_buf, sizeof line_buf, "%slet _nx_sp = _nx_sp + %d\n",
                 indent, frame_size);
        strncat(out, line_buf, cap - strlen(out) - 1);
    } else {
        snprintf(line_buf, sizeof line_buf, "%sstore64 [_nx_sp + 0] _nx_fp\n", indent);
        strncat(out, line_buf, cap - strlen(out) - 1);
        snprintf(line_buf, sizeof line_buf, "%slet _nx_fp = _nx_sp\n", indent);
        strncat(out, line_buf, cap - strlen(out) - 1);
        snprintf(line_buf, sizeof line_buf, "%slet _nx_sp = _nx_sp + 16\n", indent);
        strncat(out, line_buf, cap - strlen(out) - 1);
    }

    /* 3. Pass evaluated arguments to callee argument slots */
    for (int i = 0; i < nargs; i++) {
        snprintf(line_buf, sizeof line_buf, "%slet _arg_%s_%d = _c_arg_%d\n",
                 indent, fn_name, i, arg_tmp_indices[i]);
        strncat(out, line_buf, cap - strlen(out) - 1);
    }

    /* 4. Emit the actual function call */
    snprintf(line_buf, sizeof line_buf, "%scall %s\n", indent, fn_name);
    strncat(out, line_buf, cap - strlen(out) - 1);

    /* 5. Capture return value if caller expects a destination */
    int res_id = -1;
    if (dest && dest[0]) {
        res_id = call_tmp_counter++;
        snprintf(line_buf, sizeof line_buf, "%slet _c_res_%d = _ret_%s\n",
                 indent, res_id, fn_name);
        strncat(out, line_buf, cap - strlen(out) - 1);
    }

    /* 6. Restore caller frame and caller locals from _nx_fp */
    if (cur_fn_is_scoped) {
        snprintf(line_buf, sizeof line_buf, "%slet _nx_sp = _nx_fp\n", indent);
        strncat(out, line_buf, cap - strlen(out) - 1);
        for (int i = 0; i < cur_fn_nlocals; i++) {
            snprintf(line_buf, sizeof line_buf, "%slet %s = load64 [_nx_fp + %d]\n",
                     indent, cur_fn_locals[i], 16 + i * 8);
            strncat(out, line_buf, cap - strlen(out) - 1);
        }
        snprintf(line_buf, sizeof line_buf, "%slet _nx_fp = load64 [_nx_fp + 0]\n", indent);
        strncat(out, line_buf, cap - strlen(out) - 1);
    } else {
        snprintf(line_buf, sizeof line_buf, "%slet _nx_sp = _nx_fp\n", indent);
        strncat(out, line_buf, cap - strlen(out) - 1);
        snprintf(line_buf, sizeof line_buf, "%slet _nx_fp = load64 [_nx_fp + 0]\n", indent);
        strncat(out, line_buf, cap - strlen(out) - 1);
    }

    /* 7. Assign destination variable */
    if (dest && dest[0]) {
        snprintf(line_buf, sizeof line_buf, "%slet %s = _c_res_%d\n",
                 indent, dest, res_id);
        strncat(out, line_buf, cap - strlen(out) - 1);
        if (cur_fn_is_scoped) {
            fn_add_local(dest);
        }
    }
}

static int desugar_var_decl(const char *in, char *out, size_t cap) {
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

    if (!strncmp(p, "int ", 4) || !strncmp(p, "int\t", 4) ||
        !strncmp(p, "float ", 6) || !strncmp(p, "float\t", 6) ||
        !strncmp(p, "var ", 4) || !strncmp(p, "var\t", 4)) {
        const char *after_kw = strchr(p, ' ');
        if (!after_kw) after_kw = strchr(p, '\t');
        while (after_kw && (*after_kw == ' ' || *after_kw == '\t')) after_kw++;
        if (after_kw) {
            snprintf(out, cap, "%slet %s\n", indent, after_kw);
            return 1;
        }
    }
    return 0;
}

static int desugar_print_parens(const char *in, char *out, size_t cap) {
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

    if (!strncmp(p, "print(", 6) || !strncmp(p, "print (", 7)) {
        char *open_p = strchr(p, '(');
        int match_off = open_p ? find_matching_paren(open_p, 0) : -1;
        char *close_p = (match_off > 0) ? (open_p + match_off) : NULL;
        if (close_p) {
            char *after_close = close_p + 1;
            while (*after_close == ' ' || *after_close == '	') after_close++;
            if (*after_close != '\0' && *after_close != '#' && *after_close != ';' &&
                *after_close != '\n' && *after_close != '\r') {
                /* e.g. print (1 + 2) * 3 -> hoist entire expression after print */
                char *expr_start = open_p;
                char expr_buf[MAXLINE];
                snprintf(expr_buf, sizeof expr_buf, "%s", expr_start);
                char *cm = strchr(expr_buf, '#');
                if (!cm) cm = strchr(expr_buf, ';');
                char comment_saved[MAXLINE] = "";
                if (cm) {
                    snprintf(comment_saved, sizeof comment_saved, "%s", cm);
                    *cm = '\0';
                }
                int elen = (int)strlen(expr_buf);
                while (elen > 0 && (expr_buf[elen-1] == ' ' || expr_buf[elen-1] == '\t' ||
                                    expr_buf[elen-1] == '\n' || expr_buf[elen-1] == '\r')) {
                    expr_buf[--elen] = '\0';
                }
                int tmp_id = call_tmp_counter++;
                if (comment_saved[0]) {
                    snprintf(out, cap, "%slet _p_tmp_%d = %s\n%sprint _p_tmp_%d %s\n",
                             indent, tmp_id, expr_buf, indent, tmp_id, comment_saved);
                } else {
                    snprintf(out, cap, "%slet _p_tmp_%d = %s\n%sprint _p_tmp_%d\n",
                             indent, tmp_id, expr_buf, indent, tmp_id);
                }
                return 1;
            }
        }
        if (open_p && close_p && close_p > open_p) {
            char inner[MAXLINE];
            int ilen = (int)(close_p - (open_p + 1));
            snprintf(inner, sizeof inner, "%.*s", ilen, open_p + 1);
            char *i_start = inner;
            while (*i_start == ' ' || *i_start == '\t') i_start++;
            char *i_end = i_start + strlen(i_start);
            while (i_end > i_start && (i_end[-1] == ' ' || i_end[-1] == '\t')) *--i_end = '\0';

            char *comment = strchr(close_p + 1, '#');
            if (!comment) comment = strchr(close_p + 1, ';');

            int has_op = 0;
            int in_str = 0;
            for (int i = 0; i_start[i]; i++) {
                if (in_str && i_start[i] == '\\' && i_start[i + 1]) { i++; continue; }
                if (i_start[i] == '"') in_str = !in_str;
                if (!in_str && (i_start[i] == '+' || i_start[i] == '-' || i_start[i] == '*' ||
                    i_start[i] == '/' || i_start[i] == '%')) {
                    has_op = 1; break;
                }
            }
            if (has_op) {
                int tmp_id = call_tmp_counter++;
                if (comment) {
                    snprintf(out, cap, "%slet _p_tmp_%d = %s\n%sprint _p_tmp_%d %s\n",
                             indent, tmp_id, i_start, indent, tmp_id, comment);
                } else {
                    snprintf(out, cap, "%slet _p_tmp_%d = %s\n%sprint _p_tmp_%d\n",
                             indent, tmp_id, i_start, indent, tmp_id);
                }
                return 1;
            } else {
                if (comment) {
                    snprintf(out, cap, "%sprint %s %s\n", indent, i_start, comment);
                } else {
                    snprintf(out, cap, "%sprint %s\n", indent, i_start);
                }
                return 1;
            }
        }
    }
    return 0;
}

static int hoist_embedded_calls(const char *in, char *out, size_t cap) {
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

    if (!strncmp(p, "fn ", 3) || !strncmp(p, "fn\t", 3) ||
        !strncmp(p, "include ", 8) || !strncmp(p, "import ", 7) ||
        !strncmp(p, "struct ", 7)) {
        return 0;
    }

    /* Check pure statement call: name(args) or call name(args) */
    char *cp = p;
    if (!strncmp(cp, "call ", 5) || !strncmp(cp, "call\t", 5)) {
        cp += 5;
        while (*cp == ' ' || *cp == '\t') cp++;
    }
    char first_ident[128] = "";
    int fi_len = 0;
    while (*cp && is_ident1((unsigned char)*cp)) {
        if (fi_len < 127) first_ident[fi_len++] = *cp;
        cp++;
    }
    first_ident[fi_len] = '\0';
    while (*cp == ' ' || *cp == '\t') cp++;
    if (*cp == '(') {
        int close_p = find_matching_paren(cp, 0);
        if (close_p >= 0) {
            char *t = cp + close_p + 1;
            while (*t == ' ' || *t == '\t') t++;
            if (*t == '\0' || *t == '#' || *t == ';' || *t == '\n' || *t == '\r') {
                return 0;
            }
        }
    }

    /* Check pure let assignment call: let dest = name(args) or let dest = call name(args) */
    if (!strncmp(p, "let ", 4) || !strncmp(p, "let\t", 4)) {
        char *eq = strchr(p, '=');
        if (eq) {
            char *rhs = eq + 1;
            while (*rhs == ' ' || *rhs == '\t') rhs++;
            if (!strncmp(rhs, "call ", 5) || !strncmp(rhs, "call\t", 5)) {
                rhs += 5;
                while (*rhs == ' ' || *rhs == '\t') rhs++;
            }
            char r_ident[128] = "";
            int ri_len = 0;
            while (*rhs && is_ident1((unsigned char)*rhs)) {
                if (ri_len < 127) r_ident[ri_len++] = *rhs;
                rhs++;
            }
            r_ident[ri_len] = '\0';
            while (*rhs == ' ' || *rhs == '\t') rhs++;
            if (*rhs == '(') {
                int close_p = find_matching_paren(rhs, 0);
                if (close_p >= 0) {
                    char *t = rhs + close_p + 1;
                    while (*t == ' ' || *t == '\t') t++;
                    if (*t == '\0' || *t == '#' || *t == ';' || *t == '\n' || *t == '\r') {
                        return 0;
                    }
                }
            }
        }
    }

    int len = (int)strlen(p);
    int in_str = 0;
    for (int i = 0; i < len; i++) {
        if (in_str && p[i] == '\\' && p[i + 1]) { i++; continue; }
        if (p[i] == '"') { in_str = !in_str; continue; }
        if (in_str) continue;
        if (p[i] == '#' || p[i] == ';') break;

        if (is_ident0((unsigned char)p[i]) && (i == 0 || !is_ident1((unsigned char)p[i-1]))) {
            int id_start = i;
            while (i < len && is_ident1((unsigned char)p[i])) i++;
            int id_len = i - id_start;
            if (id_len >= 128) continue;
            char id[128];
            memcpy(id, p + id_start, id_len);
            id[id_len] = '\0';

            if (is_nexus_keyword(id)) continue;

            int j = i;
            while (j < len && (p[j] == ' ' || p[j] == '\t')) j++;
            if (j < len && p[j] == '(') {
                int open_paren = j;
                int close_paren = -1;
                int has_nested = 0;
                int k = open_paren + 1;
                int k_in_str = 0;
                while (k < len) {
                    if (k_in_str && p[k] == '\\' && p[k + 1]) { k += 2; continue; }
                    if (p[k] == '"') { k_in_str = !k_in_str; k++; continue; }
                    if (k_in_str) { k++; continue; }
                    if (p[k] == '(') { has_nested = 1; break; }
                    if (p[k] == ')') { close_paren = k; break; }
                    k++;
                }
                if (has_nested) {
                    i = open_paren;
                    continue;
                }
                if (close_paren > open_paren) {
                    char call_expr[MAXLINE];
                    int clen = close_paren - id_start + 1;
                    snprintf(call_expr, sizeof call_expr, "%.*s", clen, p + id_start);

                    int tmp_id = call_tmp_counter++;
                    char tmp_var[64];
                    snprintf(tmp_var, sizeof tmp_var, "_fcall_%d", tmp_id);

                    char mod_line[MAXLINE];
                    snprintf(mod_line, sizeof mod_line, "%.*s%s%s",
                             id_start, p, tmp_var, p + close_paren + 1);

                    snprintf(out, cap, "%slet %s = %s\n%s%s",
                             indent, tmp_var, call_expr, indent, mod_line);
                    return 1;
                }
            }
        }
    }
    return 0;
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

                    /* Register function signature for named-arg, default-args, and multi-return support */
                    fn_registry_add_full(fn_name, params, nparams);
                    int reg_idx = fn_registry_find(fn_name);

                    snprintf(cur_fn_name, sizeof cur_fn_name, "%s", fn_name);
                    fn_block_depth = 0;
                    cur_fn_is_scoped = 1;
                    cur_fn_nlocals = 0;
                    for (int i = 0; i < nparams; i++) {
                        fn_add_local(fn_registry[reg_idx].params[i]);
                    }

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
                                 indent, fn_registry[reg_idx].params[i], fn_name, i);
                        strncat(out, line_buf, cap - strlen(out) - 1);
                    }
                    return 1;
                }
            } else if (*q == '{' || *q == '\0' || *q == '#' || *q == ';') {
                /* Parameterless function: fn name { */
                char raw_params[1][512];
                fn_registry_add_full(fn_name, raw_params, 0);
                if (cur_fn_is_scoped && !strcmp(cur_fn_name, fn_name)) {
                    /* Re-entrant emission of desugared fn header, keep scoped state */
                    return 0;
                }
                snprintf(cur_fn_name, sizeof cur_fn_name, "%s", fn_name);
                fn_block_depth = 0;
                cur_fn_is_scoped = 0;
                cur_fn_nlocals = 0;
                return 0; /* unchanged, emitted verbatim */
            }
        }
    }

    /* 2. Check for 'return <expr>' or 'return a, b, c' (multi-value) */
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

                /* Remove trailing comment */
                char *comment_pos = NULL;
                {
                    int in_s = 0, paren_d = 0;
                    for (int ci = 0; ret_expr[ci]; ci++) {
                        if (in_s && ret_expr[ci] == '\\' && ret_expr[ci + 1]) { ci++; continue; }
                        if (ret_expr[ci] == '"') { in_s = !in_s; continue; }
                        if (in_s) continue;
                        if (ret_expr[ci] == '(') paren_d++;
                        else if (ret_expr[ci] == ')') paren_d--;
                        else if ((ret_expr[ci] == '#' || ret_expr[ci] == ';') && paren_d == 0) {
                            comment_pos = ret_expr + ci;
                            break;
                        }
                    }
                }
                if (comment_pos) *comment_pos = '\0';
                elen = (int)strlen(ret_expr);
                while (elen > 0 && (ret_expr[elen-1] == ' ' || ret_expr[elen-1] == '\t')) ret_expr[--elen] = '\0';

                const char *target_fn = cur_fn_name[0] ? cur_fn_name : "_main";

                /* Check for comma-separated multiple return values */
                char ret_vals[MAXFNPARAMS][512];
                int nret = split_args(ret_expr, ret_vals, MAXFNPARAMS);

                if (nret > 1) {
                    /* Multi-value return: return a, b, c */
                    out[0] = '\0';
                    char line_buf[MAXLINE];
                    for (int i = 0; i < nret; i++) {
                        snprintf(line_buf, sizeof line_buf, "%slet _ret_%s_%d = %s\n",
                                 indent, target_fn, i, ret_vals[i]);
                        strncat(out, line_buf, cap - strlen(out) - 1);
                    }
                    snprintf(line_buf, sizeof line_buf, "%sreturn\n", indent);
                    strncat(out, line_buf, cap - strlen(out) - 1);
                } else {
                    /* Single return value: backward-compatible _ret_fn */
                    snprintf(out, cap, "%slet _ret_%s = %s\n%sreturn\n",
                             indent, target_fn, ret_expr, indent);
                }
                return 1;
            }
        }
    }

    /* 3. Check for 'let <dest> = call <name>(<args>)' OR 'let <dest> = <name>(<args>)'
     *    Including multi-return: 'let x, y = name(args)'
     *    Including named args:   'let x = name(b=10, a=5)' */
    if (!strncmp(p, "let ", 4) || !strncmp(p, "let\t", 4)) {
        /* Find the '=' that separates dest from rhs.
         * We must skip '==' (comparison) and commas in dest list. */
        char *eq = NULL;
        {
            int in_s = 0;
            for (char *scan = p + 4; *scan; scan++) {
                if (in_s && *scan == '\\' && *(scan + 1)) { scan++; continue; }
                if (*scan == '"') { in_s = !in_s; continue; }
                if (in_s) continue;
                if (*scan == '=' && *(scan+1) != '=') { eq = scan; break; }
            }
        }
        if (eq) {
            /* Extract dest portion: everything between 'let ' and '=' */
            char dest_full[MAXLINE] = "";
            int dlen = (int)(eq - (p + 4));
            snprintf(dest_full, sizeof dest_full, "%.*s", dlen, p + 4);
            char *df = dest_full;
            while (*df == ' ' || *df == '\t') df++;
            char *df_end = df + strlen(df);
            while (df_end > df && (df_end[-1]==' ' || df_end[-1]=='\t')) *--df_end = '\0';

            char *rhs = eq + 1;
            while (*rhs == ' ' || *rhs == '\t') rhs++;
            char *call_target = rhs;
            int had_call = 0;
            if (!strncmp(call_target, "call ", 5) || !strncmp(call_target, "call\t", 5)) {
                had_call = 1;
                call_target += 5;
                while (*call_target == ' ' || *call_target == '\t') call_target++;
            }
            char fn_name[128] = "";
            int fn_len = 0;
            while (*call_target && is_ident1((unsigned char)*call_target)) {
                if (fn_len < 127) fn_name[fn_len++] = *call_target;
                call_target++;
            }
            fn_name[fn_len] = '\0';

            if (fn_len > 0 && !is_nexus_keyword(fn_name)) {
                while (*call_target == ' ' || *call_target == '\t') call_target++;
                if (*call_target == '(') {
                    int close_idx = find_matching_paren(call_target, 0);
                    if (close_idx >= 0) {
                        char *tail = call_target + close_idx + 1;
                        while (*tail == ' ' || *tail == '\t') tail++;
                        if (*tail == '\0' || *tail == '#' || *tail == ';' || *tail == '\n' || *tail == '\r') {
                            char arg_str[MAXLINE] = "";
                            int alen = close_idx - 1;
                            if (alen > 0) {
                                snprintf(arg_str, sizeof arg_str, "%.*s", alen, call_target + 1);
                            }
                            char args[32][512];
                            int nargs = split_args(arg_str, args, 32);

                            /* --- Named argument reordering ---
                             * Detect if any arg has the form 'name=value'.
                             * If so, reorder them to match the registered signature. */
                            int reg_idx = fn_registry_find(fn_name);
                            if (reg_idx >= 0) {
                                int reg_nparams = fn_registry[reg_idx].nparams;
                                char ordered[32][512];
                                for (int i = 0; i < reg_nparams; i++) ordered[i][0] = '\0';

                                /* 1. Place named arguments */
                                for (int i = 0; i < nargs; i++) {
                                    char *eq2 = strchr(args[i], '=');
                                    if (eq2 && eq2 > args[i] && is_ident0((unsigned char)args[i][0])) {
                                        char kname[128] = "";
                                        int klen = (int)(eq2 - args[i]);
                                        if (klen < 128) {
                                            memcpy(kname, args[i], klen);
                                            kname[klen] = '\0';
                                            char *kp = kname + strlen(kname);
                                            while (kp > kname && (kp[-1] == ' ' || kp[-1] == '\t')) *--kp = '\0';
                                            char *kv = eq2 + 1;
                                            while (*kv == ' ' || *kv == '\t') kv++;
                                            for (int j = 0; j < reg_nparams; j++) {
                                                if (!strcmp(fn_registry[reg_idx].params[j], kname)) {
                                                    snprintf(ordered[j], 512, "%s", kv);
                                                    break;
                                                }
                                            }
                                        }
                                    }
                                }

                                /* 2. Place positional arguments into remaining empty slots */
                                int pos_slot = 0;
                                for (int i = 0; i < nargs; i++) {
                                    char *eq2 = strchr(args[i], '=');
                                    int is_named = 0;
                                    if (eq2 && eq2 > args[i] && is_ident0((unsigned char)args[i][0])) {
                                        is_named = 1;
                                        for (char *cp = args[i]; cp < eq2; cp++) {
                                            if (!is_ident1((unsigned char)*cp) && *cp != ' ' && *cp != '\t') { is_named = 0; break; }
                                        }
                                    }
                                    if (!is_named) {
                                        while (pos_slot < reg_nparams && ordered[pos_slot][0] != '\0') pos_slot++;
                                        if (pos_slot < reg_nparams) {
                                            snprintf(ordered[pos_slot], 512, "%s", args[i]);
                                            pos_slot++;
                                        }
                                    }
                                }

                                /* 3. Fill in defaults for remaining empty slots */
                                for (int j = 0; j < reg_nparams; j++) {
                                    if (ordered[j][0] == '\0' && fn_registry[reg_idx].param_has_default[j]) {
                                        snprintf(ordered[j], 512, "%s", fn_registry[reg_idx].param_defaults[j]);
                                    }
                                }

                                for (int i = 0; i < reg_nparams; i++) {
                                    snprintf(args[i], 512, "%s", ordered[i]);
                                }
                                nargs = reg_nparams;
                            } else if (g_lambda_count > 0 && nargs <= 4) {
                                /* Closure call via _nx_dispatch_closure_N */
                                g_dispatch_needed[nargs] = 1;
                                char disp_fn[64];
                                snprintf(disp_fn, sizeof disp_fn, "_nx_dispatch_closure_%d", nargs);
                                char c_args[5][512];
                                snprintf(c_args[0], 512, "%s", fn_name);
                                for (int i = 0; i < nargs; i++) {
                                    snprintf(c_args[i+1], 512, "%s", args[i]);
                                }
                                out[0] = '\0';
                                emit_scoped_call(indent, disp_fn, c_args, nargs + 1, df, out, cap);
                                return 1;
                            } else {
                                /* Unregistered or forward-declared normal function */
                                out[0] = '\0';
                                emit_scoped_call(indent, fn_name, args, nargs, df, out, cap);
                                return 1;
                            }

                            /* --- Multi-return destructuring ---
                             * 'let x, y = fn(args)' -> split dest by comma */
                            char dest_parts[MAXFNPARAMS][128];
                            int ndest = 0;
                            {
                                /* Count commas in df (the dest) */
                                char tmp_df[MAXLINE];
                                snprintf(tmp_df, sizeof tmp_df, "%s", df);
                                char *tp = tmp_df;
                                while (*tp) {
                                    char *comma = strchr(tp, ',');
                                    char part[128] = "";
                                    if (comma) {
                                        int plen2 = (int)(comma - tp);
                                        if (plen2 < 128) {
                                            memcpy(part, tp, plen2);
                                            part[plen2] = '\0';
                                        }
                                        tp = comma + 1;
                                    } else {
                                        snprintf(part, sizeof part, "%s", tp);
                                        tp += strlen(tp);
                                    }
                                    /* trim */
                                    char *ps = part;
                                    while (*ps==' '||*ps=='\t') ps++;
                                    char *pe = ps + strlen(ps);
                                    while (pe > ps && (pe[-1]==' '||pe[-1]=='\t')) *--pe = '\0';
                                    if (*ps && ndest < MAXFNPARAMS) {
                                        snprintf(dest_parts[ndest++], 128, "%s", ps);
                                    }
                                }
                            }

                            if (ndest > 1) {
                                /* Multi-destination: call without dest, then destructure */
                                out[0] = '\0';
                                emit_scoped_call(indent, fn_name, args, nargs, NULL, out, cap);
                                char line_buf[MAXLINE];
                                for (int i = 0; i < ndest; i++) {
                                    snprintf(line_buf, sizeof line_buf, "%slet %s = _ret_%s_%d\n",
                                             indent, dest_parts[i], fn_name, i);
                                    strncat(out, line_buf, cap - strlen(out) - 1);
                                    if (cur_fn_is_scoped) fn_add_local(dest_parts[i]);
                                }
                                return 1;
                            } else {
                                /* Single destination */
                                out[0] = '\0';
                                emit_scoped_call(indent, fn_name, args, nargs, df, out, cap);
                                return 1;
                            }
                        }
                    }
                } else if (had_call) {
                    /* let dest = call name (zero args, no parens) */
                    char args[1][512];
                    out[0] = '\0';
                    emit_scoped_call(indent, fn_name, args, 0, df, out, cap);
                    return 1;
                }
            }
        }
    }


    /* 4. Check for statement 'call <name>(<args>)' OR '<name>(<args>)' */
    char *call_target = p;
    int had_call = 0;
    if (!strncmp(p, "call ", 5) || !strncmp(p, "call\t", 5)) {
        had_call = 1;
        call_target = p + 5;
        while (*call_target == ' ' || *call_target == '\t') call_target++;
    }
    char fn_name[128] = "";
    int fn_len = 0;
    while (*call_target && is_ident1((unsigned char)*call_target)) {
        if (fn_len < 127) fn_name[fn_len++] = *call_target;
        call_target++;
    }
    fn_name[fn_len] = '\0';

    if (fn_len > 0 && !is_nexus_keyword(fn_name)) {
        while (*call_target == ' ' || *call_target == '\t') call_target++;
        if (*call_target == '(') {
            int close_idx = find_matching_paren(call_target, 0);
            if (close_idx >= 0) {
                char *tail = call_target + close_idx + 1;
                while (*tail == ' ' || *tail == '\t') tail++;
                if (*tail == '\0' || *tail == '#' || *tail == ';' || *tail == '\n' || *tail == '\r') {
                    char arg_str[MAXLINE] = "";
                    int alen = close_idx - 1;
                    if (alen > 0) {
                        snprintf(arg_str, sizeof arg_str, "%.*s", alen, call_target + 1);
                    }
                    char args[32][512];
                    int nargs = split_args(arg_str, args, 32);

                    int reg_idx = fn_registry_find(fn_name);
                    if (reg_idx >= 0) {
                        int reg_nparams = fn_registry[reg_idx].nparams;
                        char ordered[32][512];
                        for (int i = 0; i < reg_nparams; i++) ordered[i][0] = '\0';

                        /* 1. Place named arguments */
                        for (int i = 0; i < nargs; i++) {
                            char *eq2 = strchr(args[i], '=');
                            if (eq2 && eq2 > args[i] && is_ident0((unsigned char)args[i][0])) {
                                char kname[128] = "";
                                int klen = (int)(eq2 - args[i]);
                                if (klen < 128) {
                                    memcpy(kname, args[i], klen);
                                    kname[klen] = '\0';
                                    char *kp = kname + strlen(kname);
                                    while (kp > kname && (kp[-1] == ' ' || kp[-1] == '\t')) *--kp = '\0';
                                    char *kv = eq2 + 1;
                                    while (*kv == ' ' || *kv == '\t') kv++;
                                    for (int j = 0; j < reg_nparams; j++) {
                                        if (!strcmp(fn_registry[reg_idx].params[j], kname)) {
                                            snprintf(ordered[j], 512, "%s", kv);
                                            break;
                                        }
                                    }
                                }
                            }
                        }

                        /* 2. Place positional arguments into remaining empty slots */
                        int pos_slot = 0;
                        for (int i = 0; i < nargs; i++) {
                            char *eq2 = strchr(args[i], '=');
                            int is_named = 0;
                            if (eq2 && eq2 > args[i] && is_ident0((unsigned char)args[i][0])) {
                                is_named = 1;
                                for (char *cp = args[i]; cp < eq2; cp++) {
                                    if (!is_ident1((unsigned char)*cp) && *cp != ' ' && *cp != '\t') { is_named = 0; break; }
                                }
                            }
                            if (!is_named) {
                                while (pos_slot < reg_nparams && ordered[pos_slot][0] != '\0') pos_slot++;
                                if (pos_slot < reg_nparams) {
                                    snprintf(ordered[pos_slot], 512, "%s", args[i]);
                                    pos_slot++;
                                }
                            }
                        }

                        /* 3. Fill in defaults for remaining empty slots */
                        for (int j = 0; j < reg_nparams; j++) {
                            if (ordered[j][0] == '\0' && fn_registry[reg_idx].param_has_default[j]) {
                                snprintf(ordered[j], 512, "%s", fn_registry[reg_idx].param_defaults[j]);
                            }
                        }

                        for (int i = 0; i < reg_nparams; i++) {
                            snprintf(args[i], 512, "%s", ordered[i]);
                        }
                        nargs = reg_nparams;

                        out[0] = '\0';
                        emit_scoped_call(indent, fn_name, args, nargs, NULL, out, cap);
                        return 1;
                    } else if (g_lambda_count > 0 && nargs <= 4) {
                        /* Closure statement call via _nx_dispatch_closure_N */
                        g_dispatch_needed[nargs] = 1;
                        char disp_fn[64];
                        snprintf(disp_fn, sizeof disp_fn, "_nx_dispatch_closure_%d", nargs);
                        char c_args[5][512];
                        snprintf(c_args[0], 512, "%s", fn_name);
                        for (int i = 0; i < nargs; i++) {
                            snprintf(c_args[i+1], 512, "%s", args[i]);
                        }
                        out[0] = '\0';
                        emit_scoped_call(indent, disp_fn, c_args, nargs + 1, NULL, out, cap);
                        return 1;
                    } else {
                        /* Unregistered normal statement call */
                        out[0] = '\0';
                        emit_scoped_call(indent, fn_name, args, nargs, NULL, out, cap);
                        return 1;
                    }
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

    if (!strncmp(s, "<<=", 3)) { *op_len = 3; *base_op = "<<"; return "<<="; }
    if (!strncmp(s, ">>=", 3)) { *op_len = 3; *base_op = ">>"; return ">>="; }

    if (!strncmp(s, "+=", 2))  { *op_len = 2; *base_op = "+"; return "+="; }
    if (!strncmp(s, "-=", 2))  { *op_len = 2; *base_op = "-"; return "-="; }
    if (!strncmp(s, "*=", 2))  { *op_len = 2; *base_op = "*"; return "*="; }
    if (!strncmp(s, "/=", 2))  { *op_len = 2; *base_op = "/"; return "/="; }
    if (!strncmp(s, "%=", 2))  { *op_len = 2; *base_op = "%"; return "%="; }
    if (!strncmp(s, "&=", 2))  { *op_len = 2; *base_op = "&"; return "&="; }
    if (!strncmp(s, "|=", 2))  { *op_len = 2; *base_op = "|"; return "|="; }
    if (!strncmp(s, "^=", 2))  { *op_len = 2; *base_op = "^"; return "^="; }

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
        /* For bitwise compound ops, desugar to function call */
        if (strcmp(base_op, "&") == 0)
            snprintf(out, cap, "%s%s = nx_band(%s, %s)\n", indent, target, target, rhs);
        else if (strcmp(base_op, "|") == 0)
            snprintf(out, cap, "%s%s = nx_bor(%s, %s)\n", indent, target, target, rhs);
        else if (strcmp(base_op, "^") == 0)
            snprintf(out, cap, "%s%s = nx_bxor(%s, %s)\n", indent, target, target, rhs);
        else if (strcmp(base_op, "<<") == 0)
            snprintf(out, cap, "%s%s = nx_shl(%s, %s)\n", indent, target, target, rhs);
        else if (strcmp(base_op, ">>") == 0)
            snprintf(out, cap, "%s%s = nx_shr(%s, %s)\n", indent, target, target, rhs);
        else
            snprintf(out, cap, "%s%s = %s %s %s\n", indent, target, target, base_op, rhs);
    } else {
        /* standard let assignment */
        if (strcmp(base_op, "&") == 0)
            snprintf(out, cap, "%slet %s = nx_band(%s, %s)\n", indent, target, target, rhs);
        else if (strcmp(base_op, "|") == 0)
            snprintf(out, cap, "%slet %s = nx_bor(%s, %s)\n", indent, target, target, rhs);
        else if (strcmp(base_op, "^") == 0)
            snprintf(out, cap, "%slet %s = nx_bxor(%s, %s)\n", indent, target, target, rhs);
        else if (strcmp(base_op, "<<") == 0)
            snprintf(out, cap, "%slet %s = nx_shl(%s, %s)\n", indent, target, target, rhs);
        else if (strcmp(base_op, ">>") == 0)
            snprintf(out, cap, "%slet %s = nx_shr(%s, %s)\n", indent, target, target, rhs);
        else
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

    int indent_len = (int)(p - clean);
    char indent[128] = "";
    if (indent_len > 0) {
        if (indent_len >= (int)sizeof(indent)) indent_len = (int)sizeof(indent) - 1;
        snprintf(indent, sizeof(indent), "%.*s", indent_len, clean);
    }

    /* find first comma */
    char *c1 = strchr(p, ',');
    if (!c1) return 0;
    /* find second comma */
    char *c2 = strchr(c1 + 1, ',');
    if (!c2) return 0;
    /* optional third comma: for i, 0, 10, i++ { */
    char *c3 = strchr(c2 + 1, ',');

    char *upd = c3 ? (c3 + 1) : (c2 + 1);
    while (*upd == ' ' || *upd == '\t') upd++;

    /* Check prefix ++ / -- */
    int prefix_inc = 0;
    if (upd[0] == '+' && upd[1] == '+') { prefix_inc = 1; upd += 2; }
    else if (upd[0] == '-' && upd[1] == '-') { prefix_inc = -1; upd += 2; }
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

    const char *base_op = NULL;
    char rhs[128] = "1";

    if (prefix_inc != 0) {
        base_op = (prefix_inc == 1) ? "+" : "-";
    } else if (upd[0] == '+' && upd[1] == '+') {
        base_op = "+";
        upd += 2;
    } else if (upd[0] == '-' && upd[1] == '-') {
        base_op = "-";
        upd += 2;
    } else {
        int op_len = 0;
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
        snprintf(rhs, sizeof rhs, "%.*s", rlen, upd);
        upd = brace;
    }

    while (*upd == ' ' || *upd == '\t') upd++;
    char *brace = strchr(upd, '{');
    if (!brace) return 0;

    if (c3) {
        /* 3-comma form: for var, start, end, upd { */
        char v_init[128] = "", v_start[128] = "", v_end[128] = "";
        trim_bounds(p + 4, c1, v_init, sizeof v_init);
        trim_bounds(c1 + 1, c2, v_start, sizeof v_start);
        trim_bounds(c2 + 1, c3, v_end, sizeof v_end);
        const char *cmp_op = (strcmp(base_op, "+") == 0) ? "<" : ">";
        snprintf(out, cap, "%sfor %s = %s, %s %s %s, %s = %s %s %s %s\n",
                 indent, v_init, v_start, v_init, cmp_op, v_end, var, var, base_op, rhs, brace);
    } else {
        /* standard 2-comma form: for init, cond, upd { */
        int prefix_len = (int)(c2 - clean) + 1;
        snprintf(out, cap, "%.*s %s = %s %s %s %s\n",
                 prefix_len, clean, var, var, base_op, rhs, brace);
    }
    return 1;
}

/* --------------------------------------------------------------------------
 * uppercase hex literal normalization: 0xABCD -> 0xabcd
 * The compiler's parse_uint only accepts lowercase a-f after 0x prefix.
 * -------------------------------------------------------------------------- */
static void normalize_hex_literals(char *line) {
    char *p = line;
    while (*p) {
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
            p += 2;
            while (*p && ((*p >= '0' && *p <= '9') ||
                          (*p >= 'a' && *p <= 'f') ||
                          (*p >= 'A' && *p <= 'F'))) {
                if (*p >= 'A' && *p <= 'F')
                    *p = *p - 'A' + 'a';
                p++;
            }
        } else {
            p++;
        }
    }
}

/* --------------------------------------------------------------------------
 * octal literal normalization: 0o755 / 0O755 -> decimal string
 * Converts octal literals to decimal representation so they work across
 * all bootstrap compiler stages and target architectures.
 * -------------------------------------------------------------------------- */
static void normalize_octal_literals(char *line) {
    char tmp[MAXLINE];
    int ti = 0;
    int in_str = 0;
    int len = (int)strlen(line);
    for (int i = 0; i < len; ) {
        if (in_str) {
            if (line[i] == '\\' && i + 1 < len) {
                if (ti < MAXLINE - 2) {
                    tmp[ti++] = line[i++];
                    tmp[ti++] = line[i++];
                } else {
                    i += 2;
                }
                continue;
            }
            if (line[i] == '"') in_str = 0;
            if (ti < MAXLINE - 1) tmp[ti++] = line[i++];
            else i++;
            continue;
        }
        if (line[i] == '"') {
            in_str = 1;
            if (ti < MAXLINE - 1) tmp[ti++] = line[i++];
            else i++;
            continue;
        }
        if (line[i] == '#' || line[i] == ';') {
            while (i < len && ti < MAXLINE - 1) tmp[ti++] = line[i++];
            break;
        }

        /* Check for 0o or 0O token: must not be part of an identifier */
        if (line[i] == '0' && (line[i+1] == 'o' || line[i+1] == 'O') &&
            (i == 0 || !is_ident1((unsigned char)line[i-1]))) {
            int j = i + 2;
            if (j < len && line[j] >= '0' && line[j] <= '7') {
                unsigned long long val = 0;
                while (j < len && (line[j] >= '0' && line[j] <= '7')) {
                    val = (val << 3) + (line[j] - '0');
                    j++;
                }
                /* Ensure it doesn't end with identifier char (e.g. 0o755xyz) */
                if (j >= len || !is_ident1((unsigned char)line[j])) {
                    ti += snprintf(tmp + ti, sizeof(tmp) - ti, "%llu", val);
                    i = j;
                    continue;
                }
            }
        }
        if (ti < MAXLINE - 1) {
            tmp[ti++] = line[i++];
        } else {
            i++;
        }
    }
    tmp[ti] = '\0';
    snprintf(line, MAXLINE, "%s", tmp);
}

/* --------------------------------------------------------------------------
 * increment / decrement operator desugaring:
 *   target++ / ++target -> let target = target + 1
 *   target-- / --target -> let target = target - 1
 *   obj.field++ / ++obj.field -> obj.field = obj.field + 1
 *   obj.field-- / --obj.field -> obj.field = obj.field - 1
 * -------------------------------------------------------------------------- */
static int desugar_inc_dec(const char *in, char *out, size_t cap) {
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

    /* Optional 'let ' in front */
    if (!strncmp(p, "let ", 4) || !strncmp(p, "let\t", 4)) {
        p += 4;
        while (*p == ' ' || *p == '\t') p++;
    }

    const char *op = NULL;
    char target[128] = "";
    int tlen = 0;

    /* Check prefix ++ / -- */
    if (p[0] == '+' && p[1] == '+') {
        op = "+";
        p += 2;
        while (*p == ' ' || *p == '\t') p++;
        if (!is_ident0((unsigned char)*p)) return 0;
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
    } else if (p[0] == '-' && p[1] == '-') {
        op = "-";
        p += 2;
        while (*p == ' ' || *p == '\t') p++;
        if (!is_ident0((unsigned char)*p)) return 0;
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
    } else if (is_ident0((unsigned char)*p)) {
        /* Check postfix target++ / target-- */
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

        while (*p == ' ' || *p == '\t') p++;
        if (p[0] == '+' && p[1] == '+') {
            op = "+";
            p += 2;
        } else if (p[0] == '-' && p[1] == '-') {
            op = "-";
            p += 2;
        } else {
            return 0;
        }
    } else {
        return 0;
    }

    if (!op || tlen == 0) return 0;
    if (is_nexus_keyword(target)) return 0;

    /* Remainder of the line must be trailing whitespace and optional comment */
    while (*p == ' ' || *p == '\t') p++;
    char trailing[MAXLINE] = "";
    if (*p == '#' || *p == ';') {
        snprintf(trailing, sizeof trailing, " %s", p);
        int trlen = (int)strlen(trailing);
        while (trlen > 0 && (trailing[trlen-1] == '\n' || trailing[trlen-1] == '\r')) {
            trailing[--trlen] = '\0';
        }
    } else if (*p != '\0' && *p != '\n' && *p != '\r') {
        return 0;
    }

    if (strchr(target, '.')) {
        snprintf(out, cap, "%s%s = %s %s 1%s\n", indent, target, target, op, trailing);
    } else {
        snprintf(out, cap, "%slet %s = %s %s 1%s\n", indent, target, target, op, trailing);
    }
    return 1;
}

/* --------------------------------------------------------------------------
 * ternary / conditional expression desugaring:
 *   let col = is_err ? col_err : col_ok
 *   ->
 *   let _tern_N = col_ok
 *   if is_err != 0 {
 *       let _tern_N = col_err
 *   }
 *   let col = _tern_N
 * -------------------------------------------------------------------------- */
static int tern_tmp_counter = 0;

static int contains_relop(const char *s) {
    int in_s = 0;
    int len = (int)strlen(s);
    for (int i = 0; i < len; i++) {
        if (in_s && s[i] == '\\' && i + 1 < len) { i++; continue; }
        if (s[i] == '"') { in_s = !in_s; continue; }
        if (in_s) continue;
        int rlen = 0;
        const char *cop = NULL;
        if (is_relop_std(s + i, &rlen, &cop) || is_relop_f(s + i, &rlen, &cop)) return 1;
    }
    return 0;
}

static void trim_outer_parens(char *s) {
    char *p = s;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '(') return;
    int match = find_matching_paren(p, 0);
    if (match < 0) return;
    char *end = p + match + 1;
    while (*end == ' ' || *end == '\t') end++;
    if (*end == '\0') {
        p[match] = '\0';
        memmove(s, p + 1, strlen(p + 1) + 1);
        trim_outer_parens(s);
    }
}

static int desugar_ternary(const char *in, char *out, size_t cap) {
    char clean[MAXLINE];
    snprintf(clean, sizeof clean, "%s", in);
    int clen = (int)strlen(clean);
    while (clen > 0 && (clean[clen-1] == '\n' || clean[clen-1] == '\r')) {
        clean[--clen] = '\0';
    }

    char *p = clean;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '#' || *p == ';' || *p == '\0') return 0;

    /* Skip declarations that cannot contain expressions */
    if (!strncmp(p, "fn ", 3) || !strncmp(p, "fn\t", 3) ||
        !strncmp(p, "include ", 8) || !strncmp(p, "include\t", 8) ||
        !strncmp(p, "import ", 7) || !strncmp(p, "import\t", 7) ||
        !strncmp(p, "struct ", 7) || !strncmp(p, "struct\t", 7)) {
        return 0;
    }

    int indent_len = (int)(p - clean);
    char indent[128] = "";
    if (indent_len > 0) {
        if (indent_len >= (int)sizeof(indent)) indent_len = (int)sizeof(indent) - 1;
        snprintf(indent, sizeof(indent), "%.*s", indent_len, clean);
    }

    int len = (int)strlen(clean);
    int in_str = 0;
    int max_depth = -1;
    int best_q = -1;
    int depth = 0;

    for (int i = 0; i < len; i++) {
        if (in_str) {
            if (clean[i] == '\\' && i + 1 < len) { i++; continue; }
            if (clean[i] == '"') in_str = 0;
            continue;
        }
        if (clean[i] == '"') { in_str = 1; continue; }
        if (clean[i] == '#' || clean[i] == ';') break;

        if (clean[i] == '(' || clean[i] == '[' || clean[i] == '{') {
            depth++;
        } else if (clean[i] == ')' || clean[i] == ']' || clean[i] == '}') {
            if (depth > 0) depth--;
        } else if (clean[i] == '?') {
            if (i + 1 < len && clean[i+1] == '?') {
                i++; /* Skip ?? */
                continue;
            }
            if (depth >= max_depth) {
                max_depth = depth;
                best_q = i;
            }
        }
    }

    if (best_q < 0) return 0;

    /* Find matching ':' at max_depth */
    int colon_pos = -1;
    depth = max_depth;
    in_str = 0;
    for (int i = best_q + 1; i < len; i++) {
        if (in_str) {
            if (clean[i] == '\\' && i + 1 < len) { i++; continue; }
            if (clean[i] == '"') in_str = 0;
            continue;
        }
        if (clean[i] == '"') { in_str = 1; continue; }
        if (clean[i] == '#' || clean[i] == ';') break;

        if (clean[i] == '(' || clean[i] == '[' || clean[i] == '{') {
            depth++;
        } else if (clean[i] == ')' || clean[i] == ']' || clean[i] == '}') {
            if (depth == max_depth) break;
            if (depth > 0) depth--;
        } else if (clean[i] == ':') {
            if (depth == max_depth) {
                colon_pos = i;
                break;
            }
        }
    }

    if (colon_pos < 0) return 0;

    /* Find end_pos of val_false */
    int end_pos = -1;
    depth = max_depth;
    in_str = 0;
    for (int i = colon_pos + 1; i < len; i++) {
        if (in_str) {
            if (clean[i] == '\\' && i + 1 < len) { i++; continue; }
            if (clean[i] == '"') in_str = 0;
            continue;
        }
        if (clean[i] == '"') { in_str = 1; continue; }
        if (clean[i] == '#' || clean[i] == ';' || clean[i] == '\n' || clean[i] == '\r') {
            end_pos = i;
            break;
        }
        if (clean[i] == '(' || clean[i] == '[' || clean[i] == '{') {
            depth++;
        } else if (clean[i] == ')' || clean[i] == ']' || clean[i] == '}') {
            if (depth == max_depth) {
                end_pos = i;
                break;
            }
            if (depth > 0) depth--;
        } else if (clean[i] == ',') {
            if (depth == max_depth) {
                end_pos = i;
                break;
            }
        }
    }
    if (end_pos < 0) end_pos = len;

    /* Find start_pos of cond */
    int last_boundary = indent_len;
    depth = 0;
    in_str = 0;
    for (int i = 0; i < best_q; i++) {
        if (in_str) {
            if (clean[i] == '\\' && i + 1 < len) { i++; continue; }
            if (clean[i] == '"') in_str = 0;
            continue;
        }
        if (clean[i] == '"') { in_str = 1; continue; }
        if (clean[i] == '#' || clean[i] == ';') break;

        if (clean[i] == '(' || clean[i] == '[' || clean[i] == '{') {
            if (depth == max_depth - 1) {
                last_boundary = i + 1;
            }
            depth++;
        } else if (clean[i] == ')' || clean[i] == ']' || clean[i] == '}') {
            if (depth > 0) depth--;
        } else if (depth == max_depth) {
            if (clean[i] == ',' || clean[i] == ':') {
                last_boundary = i + 1;
            } else if (clean[i] == '=') {
                int is_asgn = 1;
                if (i > 0 && (clean[i-1] == '=' || clean[i-1] == '!' || clean[i-1] == '<' ||
                              clean[i-1] == '>' || clean[i-1] == '+' || clean[i-1] == '-' ||
                              clean[i-1] == '*' || clean[i-1] == '/' || clean[i-1] == '%' ||
                              clean[i-1] == '&' || clean[i-1] == '|' || clean[i-1] == '^')) {
                    is_asgn = 0;
                }
                if (i + 1 < len && clean[i+1] == '=') is_asgn = 0;
                if (is_asgn) last_boundary = i + 1;
            } else if (!strncmp(clean + i, "return ", 7) || !strncmp(clean + i, "return\t", 7)) {
                last_boundary = i + 7;
            } else if (!strncmp(clean + i, "call ", 5) || !strncmp(clean + i, "call\t", 5)) {
                last_boundary = i + 5;
            } else if (!strncmp(clean + i, "print ", 6) || !strncmp(clean + i, "print\t", 6)) {
                last_boundary = i + 6;
            } else if (!strncmp(clean + i, "print_str ", 10) || !strncmp(clean + i, "print_str\t", 10)) {
                last_boundary = i + 10;
            }
        }
    }

    int start_pos = last_boundary;
    while (start_pos < best_q && (clean[start_pos] == ' ' || clean[start_pos] == '\t')) {
        start_pos++;
    }

    /* Extract cond, val_true, val_false */
    char cond[MAXLINE];
    int cond_len = best_q - start_pos;
    while (cond_len > 0 && (clean[start_pos + cond_len - 1] == ' ' || clean[start_pos + cond_len - 1] == '\t')) {
        cond_len--;
    }
    snprintf(cond, sizeof cond, "%.*s", cond_len, clean + start_pos);

    char val_true[MAXLINE];
    int vt_start = best_q + 1;
    while (vt_start < colon_pos && (clean[vt_start] == ' ' || clean[vt_start] == '\t')) vt_start++;
    int vt_len = colon_pos - vt_start;
    while (vt_len > 0 && (clean[vt_start + vt_len - 1] == ' ' || clean[vt_start + vt_len - 1] == '\t')) vt_len--;
    snprintf(val_true, sizeof val_true, "%.*s", vt_len, clean + vt_start);

    char val_false[MAXLINE];
    int vf_start = colon_pos + 1;
    while (vf_start < end_pos && (clean[vf_start] == ' ' || clean[vf_start] == '\t')) vf_start++;
    int vf_len = end_pos - vf_start;
    while (vf_len > 0 && (clean[vf_start + vf_len - 1] == ' ' || clean[vf_start + vf_len - 1] == '\t')) vf_len--;
    snprintf(val_false, sizeof val_false, "%.*s", vf_len, clean + vf_start);

    if (cond_len <= 0 || vt_len <= 0 || vf_len <= 0) return 0;

    /* Check if the ternary is wrapped in parentheses: (cond ? a : b) */
    int repl_start = start_pos;
    int repl_end = end_pos;
    if (start_pos > 0 && clean[start_pos - 1] == '(' && end_pos < len && clean[end_pos] == ')') {
        int bi = start_pos - 2;
        while (bi >= 0 && (clean[bi] == ' ' || clean[bi] == '\t')) bi--;
        if (bi < 0 || clean[bi] == '=' || clean[bi] == ',' || clean[bi] == '(' ||
            (bi >= 5 && (!strncmp(clean + bi - 5, "return", 6)))) {
            repl_start = start_pos - 1;
            repl_end = end_pos + 1;
        }
    }

    trim_outer_parens(cond);
    trim_outer_parens(val_true);
    trim_outer_parens(val_false);

    /* Format condition */
    char cond_fmt[MAXLINE];
    if (cond[0] == '!') {
        char *ci = cond + 1;
        while (*ci == ' ' || *ci == '\t') ci++;
        snprintf(cond_fmt, sizeof cond_fmt, "%s == 0", ci);
    } else if (contains_relop(cond)) {
        snprintf(cond_fmt, sizeof cond_fmt, "%s", cond);
    } else {
        snprintf(cond_fmt, sizeof cond_fmt, "%s != 0", cond);
    }

    int tmp_id = tern_tmp_counter++;
    char tmp_var[64];
    snprintf(tmp_var, sizeof tmp_var, "_tern_%d", tmp_id);

    char mod_line[MAXLINE];
    snprintf(mod_line, sizeof mod_line, "%.*s%s%s",
             repl_start, clean, tmp_var, clean + repl_end);

    int is_fl = is_operand_float(cur_fn_name, val_true) || is_operand_float(cur_fn_name, val_false);
    const char *init_val = is_fl ? "0.0" : "0";

    snprintf(out, cap, "%slet %s = %s\n%sif %s {\n%s    let %s = %s\n%s} else {\n%s    let %s = %s\n%s}\n%s\n",
             indent, tmp_var, init_val,
             indent, cond_fmt,
             indent, tmp_var, val_true,
             indent,
             indent, tmp_var, val_false,
             indent,
             mod_line);
    return 1;
}

/* --------------------------------------------------------------------------
 * bitwise binary operator desugaring:
 *   Converts simple "LHS op RHS" patterns to function calls.
 *   Handles: & -> nx_band, | -> nx_bor, ^ -> nx_bxor, << -> nx_shl, >> -> nx_shr
 *   Only processes the FIRST occurrence in the RHS of an assignment.
 *   For complex expressions, users should call nx_band() etc. directly.
 * -------------------------------------------------------------------------- */
static int desugar_bitwise_binop(const char *in, char *out, size_t cap) {
    char clean[MAXLINE];
    snprintf(clean, sizeof clean, "%s", in);
    char *p = clean;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '#' || *p == ';' || *p == '\0') return 0;

    /* Find '=' (but not '==', '!=', '<=', '>=', '+=', etc.) outside strings */
    char *eq = NULL;
    char *q = p;
    int in_str_scan = 0;
    while (*q) {
        if (in_str_scan) {
            if (*q == '\\' && *(q+1)) { q += 2; continue; }
            if (*q == '"') in_str_scan = 0;
            q++;
            continue;
        }
        if (*q == '"') { in_str_scan = 1; q++; continue; }
        if (*q == '#' || *q == '\n') break;
        if (*q == '=' && (q == p || (q[-1] != '=' && q[-1] != '!' &&
                                     q[-1] != '<' && q[-1] != '>' &&
                                     q[-1] != '+' && q[-1] != '-' &&
                                     q[-1] != '*' && q[-1] != '/' &&
                                     q[-1] != '%' && q[-1] != '&' &&
                                     q[-1] != '|' && q[-1] != '^'))) {
            eq = q;
            break;
        }
        q++;
    }
    if (!eq) return 0;

    /* RHS starts after '=' */
    char *rhs = eq + 1;
    while (*rhs == ' ' || *rhs == '\t') rhs++;

    /* Find first bitwise operator in RHS (outside strings) */
    char *op_pos = NULL;
    int op_type = 0;  /* 1=&, 2=|, 3=^, 4=<<, 5=>> */
    int in_string = 0;
    char *r = rhs;
    while (*r && *r != '#' && *r != '\n') {
        if (in_string && *r == '\\' && *(r + 1)) { r += 2; continue; }
        if (*r == '"') in_string = !in_string;
        if (!in_string) {
            if (r[0] == '<' && r[1] == '<') { op_pos = r; op_type = 4; break; }
            if (r[0] == '>' && r[1] == '>') { op_pos = r; op_type = 5; break; }
            if (r[0] == '&' && r[1] != '&') { op_pos = r; op_type = 1; break; }
            if (r[0] == '|' && r[1] != '|') { op_pos = r; op_type = 2; break; }
            if (r[0] == '^') { op_pos = r; op_type = 3; break; }
        }
        r++;
    }
    if (!op_pos) return 0;

    /* Find LHS end (trim trailing spaces before op) */
    char *lhs_end = op_pos;
    while (lhs_end > rhs && (lhs_end[-1] == ' ' || lhs_end[-1] == '\t'))
        lhs_end--;

    /* Find RHS end: next arithmetic operator (+, -, *, /, %) or end of line */
    int op_len = (op_type >= 4) ? 2 : 1;
    char *rhs_start = op_pos + op_len;
    while (*rhs_start == ' ' || *rhs_start == '\t') rhs_start++;

    char *rhs_end = rhs_start;
    in_string = 0;
    while (*rhs_end && *rhs_end != '#' && *rhs_end != '\n') {
        if (in_string && *rhs_end == '\\' && *(rhs_end + 1)) { rhs_end += 2; continue; }
        if (*rhs_end == '"') in_string = !in_string;
        if (!in_string) {
            if (*rhs_end == '+' || *rhs_end == '-' ||
                *rhs_end == '*' || *rhs_end == '/' || *rhs_end == '%')
                break;
        }
        rhs_end++;
    }
    /* Trim trailing spaces from rhs_end */
    while (rhs_end > rhs_start && (rhs_end[-1] == ' ' || rhs_end[-1] == '\t' ||
                                    rhs_end[-1] == '\r'))
        rhs_end--;

    /* Build the function call */
    const char *fn_name = NULL;
    switch (op_type) {
        case 1: fn_name = "nx_band"; break;
        case 2: fn_name = "nx_bor"; break;
        case 3: fn_name = "nx_bxor"; break;
        case 4: fn_name = "nx_shl"; break;
        case 5: fn_name = "nx_shr"; break;
    }

    /* Extract LHS and RHS of the bitwise op */
    char lhs_expr[MAXLINE];
    int lhs_len = (int)(lhs_end - rhs);
    if (lhs_len <= 0) return 0;  /* no LHS */
    snprintf(lhs_expr, sizeof lhs_expr, "%.*s", lhs_len, rhs);

    char bit_rhs[MAXLINE];
    int bit_rhs_len = (int)(rhs_end - rhs_start);
    if (bit_rhs_len <= 0) return 0;  /* no RHS */
    snprintf(bit_rhs, sizeof bit_rhs, "%.*s", bit_rhs_len, rhs_start);

    /* Remainder after the bitwise RHS */
    char remainder[MAXLINE];
    snprintf(remainder, sizeof remainder, "%s", rhs_end);

    /* Reconstruct: prefix + = nx_fn(lhs, rhs) + remainder */
    int prefix_len = (int)(rhs - clean);
    snprintf(out, cap, "%.*s= %s(%s, %s)%s\n",
             prefix_len, clean, fn_name, lhs_expr, bit_rhs, remainder);
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
        if (in_str && *c == '\\' && *(c + 1)) { c++; continue; }
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
        if (in_str && *p == '\\' && *(p + 1)) {
            if (cur_len < 510) {
                cur_elem[cur_len++] = *p++;
                cur_elem[cur_len++] = *p++;
            } else p += 2;
            continue;
        }
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
            while (*r && *r != '"') {
                if (*r == '\\' && *(r + 1)) r++;
                r++;
            }
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
 * Compiler Optimizer & Truthiness Desugaring
 * -------------------------------------------------------------------------- */
static void trim_ws(char *s) {
    char *p = s;
    while (*p == ' ' || *p == '\t') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\r' || s[len - 1] == '\n')) {
        s[--len] = '\0';
    }
}

static void decompose_int64(int64_t v, char *out, size_t cap) {
    if (v >= -2147483647LL && v <= 2147483647LL) {
        if (v < 0) {
            snprintf(out, cap, "0 - %" PRId64, -v);
        } else {
            snprintf(out, cap, "%" PRId64, v);
        }
        return;
    }
    uint64_t uv = (uint64_t)v;
    uint32_t p0 = uv & 0xFFFF;
    uint32_t p1 = (uv >> 16) & 0xFFFF;
    uint32_t p2 = (uv >> 32) & 0xFFFF;
    uint32_t p3 = (uv >> 48) & 0xFFFF;
    if (p3 != 0) {
        snprintf(out, cap, "%u * 65536 + %u * 65536 + %u * 65536 + %u", p3, p2, p1, p0);
    } else if (p2 != 0) {
        snprintf(out, cap, "%u * 65536 + %u * 65536 + %u", p2, p1, p0);
    } else {
        snprintf(out, cap, "%u * 65536 + %u", p1, p0);
    }
}

/* Parse a single int64 literal (optional leading negative sign) */
static int parse_int64_tok(const char *s, const char **endptr, int64_t *val) {
    while (*s == ' ' || *s == '\t') s++;
    if (!*s) return 0;
    int is_neg = 0;
    if (*s == '-' && isdigit((unsigned char)s[1])) {
        is_neg = 1;
        s++;
    }
    if (!isdigit((unsigned char)*s)) return 0;
    char *ep = NULL;
    uint64_t uv = strtoull(s, &ep, 10);
    if (ep == s) return 0;
    if (*ep == '.') return 0; /* Float literal */
    int64_t v = is_neg ? -(int64_t)uv : (int64_t)uv;
    if (val) *val = v;
    if (endptr) *endptr = ep;
    return 1;
}

/* Recursive-descent constant integer expression evaluator with precedence & parentheses */
static int eval_int_expr_rd(const char **pp, int64_t *val, int *op_count);

static int eval_int_factor_rd(const char **pp, int64_t *val, int *op_count) {
    const char *p = *pp;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '(') {
        p++;
        int64_t inner = 0;
        if (!eval_int_expr_rd(&p, &inner, op_count)) return 0;
        while (*p == ' ' || *p == '\t') p++;
        if (*p != ')') return 0;
        p++;
        (*op_count)++;
        *pp = p;
        *val = inner;
        return 1;
    }
    int64_t v = 0;
    if (!parse_int64_tok(p, &p, &v)) return 0;
    *pp = p;
    *val = v;
    return 1;
}

static int eval_int_term_rd(const char **pp, int64_t *val, int *op_count) {
    const char *p = *pp;
    int64_t acc = 0;
    if (!eval_int_factor_rd(&p, &acc, op_count)) return 0;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p != '*' && *p != '/' && *p != '%') break;
        if (p[0] == '*' && p[1] == '*') break; /* power operator handled separately */
        if (p[1] == '=') break;
        char op = *p;
        const char *next_p = p + 1;
        int64_t rhs = 0;
        if (!eval_int_factor_rd(&next_p, &rhs, op_count)) return 0;
        if ((op == '/' || op == '%') && rhs == 0) return 0;
        if (op == '*') acc = (int64_t)((uint64_t)acc * (uint64_t)rhs);
        else if (op == '/') {
            if (acc == INT64_MIN && rhs == -1) return 0;
            acc = acc / rhs;
        } else if (op == '%') {
            if (acc == INT64_MIN && rhs == -1) acc = 0;
            else acc = acc % rhs;
        }
        p = next_p;
        (*op_count)++;
    }
    *pp = p;
    *val = acc;
    return 1;
}

static int eval_int_expr_rd(const char **pp, int64_t *val, int *op_count) {
    const char *p = *pp;
    int64_t acc = 0;
    if (!eval_int_term_rd(&p, &acc, op_count)) return 0;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p != '+' && *p != '-') break;
        if (p[1] == '=' || p[1] == p[0] || p[1] == '.') break;
        char op = *p;
        const char *next_p = p + 1;
        int64_t rhs = 0;
        if (!eval_int_term_rd(&next_p, &rhs, op_count)) return 0;
        if (op == '+') acc = (int64_t)((uint64_t)acc + (uint64_t)rhs);
        else if (op == '-') acc = (int64_t)((uint64_t)acc - (uint64_t)rhs);
        p = next_p;
        (*op_count)++;
    }
    *pp = p;
    *val = acc;
    return 1;
}

static int eval_int_chain(const char *s, const char **endptr, int64_t *result) {
    if (strstr(s, "* 65536")) return 0;
    const char *p = s;
    int64_t acc = 0;
    int op_count = 0;
    if (!eval_int_expr_rd(&p, &acc, &op_count)) return 0;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '\0') return 0;
    if (op_count == 0) {
        /* Single literal: check if it needs 64-bit decomposition */
        if (acc > 2147483647LL || acc < -2147483647LL) {
            if (result) *result = acc;
            if (endptr) *endptr = p;
            return 1;
        }
        return 0;
    }
    if (result) *result = acc;
    if (endptr) *endptr = p;
    return 1;
}

/* Fold unary builtins: abs <lit> and len "<lit>" */
static void fold_unary_builtins(char *line) {
    char buf[MAXLINE];
    char *p = line;
    buf[0] = '\0';
    int changed = 0;

    while (*p) {
        if (*p == '"') {
            size_t blen = strlen(buf);
            if (blen < MAXLINE - 1) buf[blen++] = *p++;
            while (*p && *p != '"' && blen < MAXLINE - 1) {
                if (*p == '\\' && *(p + 1) && blen < MAXLINE - 2) {
                    buf[blen++] = *p++;
                }
                buf[blen++] = *p++;
            }
            if (*p == '"' && blen < MAXLINE - 1) buf[blen++] = *p++;
            buf[blen] = '\0';
            continue;
        }
        if (*p == '#' || *p == ';') {
            strncat(buf, p, MAXLINE - strlen(buf) - 1);
            break;
        }

        /* Check for "abs " */
        if ((p == line || !is_ident1((unsigned char)p[-1])) &&
            !strncmp(p, "abs", 3) && (p[3] == ' ' || p[3] == '\t' || p[3] == '(')) {
            const char *arg = p + 3;
            while (*arg == ' ' || *arg == '\t') arg++;
            int has_paren = (*arg == '(');
            if (has_paren) {
                arg++;
                while (*arg == ' ' || *arg == '\t') arg++;
            }
            int64_t val = 0;
            const char *end = NULL;
            if (parse_int64_tok(arg, &end, &val)) {
                if (has_paren) {
                    while (*end == ' ' || *end == '\t') end++;
                    if (*end == ')') end++;
                }
                int64_t abs_val = (val < 0) ? -val : val;
                char val_str[64];
                decompose_int64(abs_val, val_str, sizeof val_str);
                strncat(buf, val_str, MAXLINE - strlen(buf) - 1);
                p = (char *)end;
                changed = 1;
                continue;
            }
        }

        /* Check for "len " */
        if ((p == line || !is_ident1((unsigned char)p[-1])) &&
            !strncmp(p, "len", 3) && (p[3] == ' ' || p[3] == '\t' || p[3] == '(')) {
            const char *arg = p + 3;
            while (*arg == ' ' || *arg == '\t') arg++;
            int has_paren = (*arg == '(');
            if (has_paren) {
                arg++;
                while (*arg == ' ' || *arg == '\t') arg++;
            }
            if (*arg == '"') {
                const char *s = arg + 1;
                size_t slen = 0;
                while (*s && *s != '"') {
                    if (*s == '\\' && *(s + 1)) {
                        slen++;
                        if (*(s + 1) == 'x' && *(s + 2) && *(s + 3) &&
                            isxdigit((unsigned char)*(s + 2)) && isxdigit((unsigned char)*(s + 3))) {
                            s += 4;
                        } else {
                            s += 2;
                        }
                        continue;
                    }
                    slen++;
                    s++;
                }
                if (*s == '"') {
                    s++;
                    if (has_paren) {
                        while (*s == ' ' || *s == '\t') s++;
                        if (*s == ')') s++;
                    }
                    char val_str[32];
                    snprintf(val_str, sizeof val_str, "%zu", slen);
                    strncat(buf, val_str, MAXLINE - strlen(buf) - 1);
                    p = (char *)s;
                    changed = 1;
                    continue;
                }
            }
        }

        size_t blen = strlen(buf);
        if (blen < MAXLINE - 1) {
            buf[blen++] = *p++;
            buf[blen] = '\0';
        } else {
            break;
        }
    }
    if (changed) snprintf(line, MAXLINE, "%s", buf);
}

/* Fold expressions inside memory brackets: [ptr + 0 * 8] -> [ptr + 0] */
static void fold_mem_offsets(char *line) {
    char buf[MAXLINE];
    char *p = line;
    buf[0] = '\0';
    int changed = 0;

    while (*p) {
        if (*p == '"') {
            size_t blen = strlen(buf);
            if (blen < MAXLINE - 1) buf[blen++] = *p++;
            while (*p && *p != '"' && blen < MAXLINE - 1) {
                if (*p == '\\' && *(p + 1) && blen < MAXLINE - 2) {
                    buf[blen++] = *p++;
                }
                buf[blen++] = *p++;
            }
            if (*p == '"' && blen < MAXLINE - 1) buf[blen++] = *p++;
            buf[blen] = '\0';
            continue;
        }
        if (*p == '#' || *p == ';') {
            strncat(buf, p, MAXLINE - strlen(buf) - 1);
            break;
        }
        if (*p == '[') {
            char *close = strchr(p, ']');
            if (close) {
                char inner[MAXLINE];
                size_t n = close - (p + 1);
                if (n < sizeof(inner) - 1) {
                    strncpy(inner, p + 1, n);
                    inner[n] = '\0';
                    char *plus = strchr(inner, '+');
                    if (plus) {
                        char base[128];
                        size_t b_len = plus - inner;
                        if (b_len < sizeof(base) - 1) {
                            strncpy(base, inner, b_len);
                            base[b_len] = '\0';
                            trim_ws(base);
                            const char *expr = plus + 1;
                            while (*expr == ' ' || *expr == '\t') expr++;
                            int64_t val = 0;
                            const char *end = NULL;
                            if (eval_int_chain(expr, &end, &val) && (*end == '\0' || *end == ' ' || *end == '\t')) {
                                char folded[MAXLINE];
                                snprintf(folded, sizeof folded, "[%s + %" PRId64 "]", base, val);
                                strncat(buf, folded, MAXLINE - strlen(buf) - 1);
                                p = close + 1;
                                changed = 1;
                                continue;
                            }
                        }
                    }
                }
            }
        }
        size_t blen = strlen(buf);
        if (blen < MAXLINE - 1) {
            buf[blen++] = *p++;
            buf[blen] = '\0';
        } else {
            break;
        }
    }
    if (changed) snprintf(line, MAXLINE, "%s", buf);
}

/* Desugar truthiness and constant conditions */
static void transform_cond_str(const char *in_cond, char *out_cond, size_t cap) {
    char cond[MAXLINE];
    snprintf(cond, sizeof cond, "%s", in_cond);
    trim_ws(cond);

    /* Strip outer parentheses if present */
    if (cond[0] == '(' && cond[strlen(cond) - 1] == ')') {
        cond[strlen(cond) - 1] = '\0';
        memmove(cond, cond + 1, strlen(cond));
        trim_ws(cond);
    }

    /* Check for relational operators: ==, !=, <=, >=, <, > */
    char *op = NULL;
    int op_len = 0;
    char *p = cond;
    int in_str = 0;
    while (*p) {
        if (in_str && *p == '\\' && *(p + 1)) {
            p += 2;
            continue;
        }
        if (*p == '"') {
            in_str = !in_str;
            p++;
            continue;
        }
        if (!in_str) {
            if (!strncmp(p, "==", 2) || !strncmp(p, "!=", 2) ||
                !strncmp(p, "<=", 2) || !strncmp(p, ">=", 2)) {
                op = p;
                op_len = 2;
                break;
            }
            if (*p == '<' || *p == '>') {
                op = p;
                op_len = 1;
                break;
            }
        }
        p++;
    }

    if (op) {
        char lhs[MAXLINE], rhs[MAXLINE];
        size_t nlhs = op - cond;
        strncpy(lhs, cond, nlhs);
        lhs[nlhs] = '\0';
        trim_ws(lhs);
        snprintf(rhs, sizeof rhs, "%s", op + op_len);
        trim_ws(rhs);

        int64_t v1 = 0, v2 = 0;
        const char *ep1 = NULL, *ep2 = NULL;
        int is_c1 = eval_int_chain(lhs, &ep1, &v1) || parse_int64_tok(lhs, &ep1, &v1);
        int is_c2 = eval_int_chain(rhs, &ep2, &v2) || parse_int64_tok(rhs, &ep2, &v2);
        if (is_c1 && ep1 && *ep1 == '\0' && is_c2 && ep2 && *ep2 == '\0') {
            int true_val = 0;
            if (op_len == 2) {
                if (!strncmp(op, "==", 2)) true_val = (v1 == v2);
                else if (!strncmp(op, "!=", 2)) true_val = (v1 != v2);
                else if (!strncmp(op, "<=", 2)) true_val = (v1 <= v2);
                else if (!strncmp(op, ">=", 2)) true_val = (v1 >= v2);
            } else {
                if (*op == '<') true_val = (v1 < v2);
                else if (*op == '>') true_val = (v1 > v2);
            }
            if (true_val) snprintf(out_cond, cap, "_TRUE_ != 0");
            else snprintf(out_cond, cap, "_FALSE_ != 0");
            return;
        }
        snprintf(out_cond, cap, "%s", cond);
        return;
    }

    /* No relational operator: truthiness */
    int64_t val = 0;
    const char *ep = NULL;
    if ((eval_int_chain(cond, &ep, &val) || parse_int64_tok(cond, &ep, &val)) && ep && *ep == '\0') {
        if (val != 0) snprintf(out_cond, cap, "_TRUE_ != 0");
        else snprintf(out_cond, cap, "_FALSE_ != 0");
        return;
    }

    /* Variable truthiness */
    snprintf(out_cond, cap, "%s != 0", cond);
}

static int desugar_truthiness_and_conditions(const char *in, char *out, size_t cap) {
    char clean[MAXLINE];
    snprintf(clean, sizeof clean, "%s", in);
    char *p = clean;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '#' || *p == ';' || *p == '\0') return 0;

    /* Skip float conditions */
    if (!strncmp(p, "if_f ", 5) || !strncmp(p, "if_f\t", 5) ||
        !strncmp(p, "while_f ", 8) || !strncmp(p, "while_f\t", 8) ||
        strstr(p, "else if_f ") || strstr(p, "else if_f\t")) {
        return 0;
    }

    int indent_len = (int)(p - clean);
    char indent[128] = "";
    if (indent_len > 0) {
        if (indent_len >= (int)sizeof(indent)) indent_len = (int)sizeof(indent) - 1;
        snprintf(indent, sizeof(indent), "%.*s", indent_len, clean);
    }

    /* 1. for loop header: for <init>, <cond>, <update> { */
    if (!strncmp(p, "for ", 4) || !strncmp(p, "for\t", 4)) {
        char *c1 = strchr(p, ',');
        if (c1) {
            char *c2 = strchr(c1 + 1, ',');
            if (c2) {
                char cond_part[MAXLINE];
                size_t n = c2 - (c1 + 1);
                if (n < sizeof(cond_part) - 1) {
                    strncpy(cond_part, c1 + 1, n);
                    cond_part[n] = '\0';
                    char new_cond[MAXLINE];
                    transform_cond_str(cond_part, new_cond, sizeof new_cond);
                    if (strcmp(cond_part, new_cond) != 0) {
                        snprintf(out, cap, "%s%.*s, %s,%s", indent, (int)(c1 - p), p, new_cond, c2 + 1);
                        return 1;
                    }
                }
            }
        }
        return 0;
    }

    /* 2. if, while, else if */
    const char *kw = NULL;
    char *cond_start = NULL;
    char prefix[256] = "";

    if (!strncmp(p, "if ", 3) || !strncmp(p, "if\t", 3)) {
        kw = "if ";
        cond_start = p + 3;
    } else if (!strncmp(p, "while ", 6) || !strncmp(p, "while\t", 6)) {
        kw = "while ";
        cond_start = p + 6;
    } else {
        char *eif = strstr(p, "else if ");
        if (!eif) eif = strstr(p, "else if\t");
        if (eif) {
            int pre_len = (int)(eif - p);
            snprintf(prefix, sizeof prefix, "%.*selse if ", pre_len, p);
            kw = prefix;
            cond_start = eif + 8;
        }
    }

    if (!kw || !cond_start) return 0;

    char *brace = strchr(cond_start, '{');
    if (!brace) return 0;

    char cond[MAXLINE];
    size_t clen = brace - cond_start;
    if (clen >= sizeof(cond)) clen = sizeof(cond) - 1;
    strncpy(cond, cond_start, clen);
    cond[clen] = '\0';

    char new_cond[MAXLINE];
    transform_cond_str(cond, new_cond, sizeof new_cond);
    if (!strcmp(cond, new_cond)) return 0;
    snprintf(out, cap, "%s%s%s %s", indent, kw, new_cond, brace);
    return 1;
}


/* ==========================================================================
 * Integer operator precedence (Feature #34 / correctness fix)
 *
 * The nexc core evaluates `<expr>` strictly left-to-right, so `1 + 2 * 3`
 * yields 9 instead of 7.  This pass rewrites `let D = <expr>` and
 * `D = <expr>` into a precedence-correct sequence of single-operator
 * statements using compiler temporaries, which nexc already handles
 * correctly:
 *
 *   let c = 1 + 2 * 3
 *     ->
 *   let _prec_1 = 2 * 3
 *   let c = 1 + _prec_1
 *
 * Two precedence levels are handled (additive over multiplicative), which
 * covers the whole operator set the language has: + - * / %.
 *
 * Safety rules -- the pass bails out (returns 0) unless the RHS is a pure
 * infix chain of identifiers / decimal literals / bracketed loads joined by
 * + - * / %.  Anything with parentheses, strings, floats, function calls,
 * comparisons or unary operators is left untouched for other passes.
 * ========================================================================== */

static int prec_tmp_counter = 0;

static int prec_is_ident0(unsigned char c) {
    return isalpha(c) || c == '_';
}
static int prec_is_ident1(unsigned char c) {
    return isalnum(c) || c == '_';
}

/* Tokenize a pure infix chain.  Returns token count or -1 if unsafe. */
#define PREC_MAXTOK 64
typedef struct {
    char tok[128];      /* operand text (ident / literal / [..]) or operator */
    int  is_op;         /* 1 = operator, 0 = operand                        */
} prec_tok;

static int prec_tokenize(const char *s, prec_tok *tk, int max) {
    int n = 0;
    const char *p = s;
    int want_operand = 1;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        if (n >= max) return -1;
        if (want_operand) {
            if (*p == '[') {
                /* bracketed memory load: copy to matching ']' */
                int depth = 0;
                int o = 0;
                while (*p) {
                    if (*p == '[') depth++;
                    else if (*p == ']') { depth--; }
                    if (o + 1 < (int)sizeof tk[n].tok) tk[n].tok[o++] = *p;
                    p++;
                    if (depth == 0) break;
                }
                tk[n].tok[o] = '\0';
                if (depth != 0) return -1;
                if (!o) return -1;
                tk[n].is_op = 0;
                n++;
                want_operand = 0;
                continue;
            }
            int o = 0;
            if (prec_is_ident0((unsigned char)*p)) {
                while (prec_is_ident1((unsigned char)*p)) {
                    if (o + 1 < (int)sizeof tk[n].tok) tk[n].tok[o++] = *p;
                    p++;
                }
                /* an identifier followed immediately by '(' is a call: unsafe */
                {
                    const char *q = p;
                    while (*q == ' ' || *q == '\t') q++;
                    if (*q == '(') return -1;
                }
            } else if (isdigit((unsigned char)*p)) {
                while (isdigit((unsigned char)*p)) {
                    if (o + 1 < (int)sizeof tk[n].tok) tk[n].tok[o++] = *p;
                    p++;
                }
                if (*p == '.') return -1;       /* float literal -> other pass */
                if (*p == 'e' || *p == 'E') return -1;
            } else {
                return -1;
            }
            tk[n].tok[o] = '\0';
            if (!o) return -1;
            tk[n].is_op = 0;
            n++;
            want_operand = 0;
        } else {
            char c = *p;
            if (c != '+' && c != '-' && c != '*' && c != '/' && c != '%') return -1;
            p++;
            /* reject ** (power), //, += style, and anything else doubled */
            if (*p == c) return -1;
            if (*p == '=') return -1;
            tk[n].tok[0] = c;
            tk[n].tok[1] = '\0';
            tk[n].is_op = 1;
            n++;
            want_operand = 1;
        }
    }
    if (want_operand) return -1;    /* trailing operator */
    if (n == 0) return -1;
    /* need at least one operator and at least one * / % for a rewrite to matter */
    return n;
}


/* Precedence-sensitivity probe shared by the folding and precedence passes.
 * Returns 1 when the raw infix chain mixes an additive operator with a
 * multiplicative one, i.e. when left-to-right evaluation would give a
 * different answer from precedence-correct evaluation.  Returns 0 for
 * flat chains, for anything unparseable, and for empty input.            */
static int prec_mixed_chain(const char *rhs) {
    prec_tok tk[PREC_MAXTOK];
    int n = prec_tokenize(rhs, tk, PREC_MAXTOK);
    if (n < 3) return 0;
    int has_add = 0, has_mul = 0;
    for (int i = 1; i < n; i += 2) {
        char o = tk[i].tok[0];
        if (o == '+' || o == '-') has_add = 1;
        else if (o == '*' || o == '/' || o == '%') has_mul = 1;
    }
    return has_add && has_mul;
}

/* Does the token list contain a multiplicative operator? */
static int prec_has_mul(const prec_tok *tk, int n) {
    for (int i = 1; i < n; i += 2) {
        if (tk[i].tok[0] == '*' || tk[i].tok[0] == '/' || tk[i].tok[0] == '%') return 1;
    }
    return 0;
}

/* Is the list free of multiplicative operators (single flat additive chain)? */
static int prec_no_mul(const prec_tok *tk, int n) {
    return !prec_has_mul(tk, n);
}

/* Emit the precedence-corrected form of `let D = <chain>` (or `D = <chain>`).
 * `indent` is the leading whitespace, `is_let` selects the statement form. */
static int prec_rewrite(const char *indent, const char *dest, int is_let,
                        const char *rhs, char *out, size_t cap) {
    prec_tok tk[PREC_MAXTOK];
    int n = prec_tokenize(rhs, tk, PREC_MAXTOK);
    if (n < 0) return 0;
    if (n < 3) return 0;                       /* nothing to reorder */
    if (!prec_mixed_chain(rhs)) return 0;      /* only rewrite if mixed additive & multiplicative */

    char buf[MAXLINE * 8];
    buf[0] = '\0';

    /* An additive expression is a chain of terms separated by + or -:
     * Term 0 (+|-) Term 1 (+|-) Term 2 ...
     * Each term is an operand or a chain of operands joined by *, /, % */
    char add_expr[MAXLINE * 4];
    add_expr[0] = '\0';

    int t_start = 0;
    while (t_start < n) {
        /* Find the end of this multiplicative term */
        int t_end = t_start; /* token index of last operand in this term */
        while (t_end + 2 < n && (tk[t_end + 1].tok[0] == '*' || 
                                 tk[t_end + 1].tok[0] == '/' || 
                                 tk[t_end + 1].tok[0] == '%')) {
            t_end += 2;
        }

        char term_repr[256];
        if (t_start == t_end) {
            /* Single operand term */
            snprintf(term_repr, sizeof term_repr, "%s", tk[t_start].tok);
        } else {
            /* Multiplicative term: build the sub-expression */
            char term_sub[512];
            term_sub[0] = '\0';
            for (int k = t_start; k <= t_end; k++) {
                if (k > t_start) strcat(term_sub, " ");
                strcat(term_sub, tk[k].tok);
            }
            prec_tmp_counter++;
            int id = prec_tmp_counter;
            char line[768];
            snprintf(line, sizeof line, "%slet _prec_%d = %s\n", indent, id, term_sub);
            if (strlen(buf) + strlen(line) + 1 >= sizeof buf) return 0;
            strcat(buf, line);
            snprintf(term_repr, sizeof term_repr, "_prec_%d", id);
        }

        /* Append term_repr to add_expr */
        if (t_start == 0) {
            snprintf(add_expr, sizeof add_expr, "%s", term_repr);
        } else {
            /* There is an additive operator before this term: tk[t_start - 1] */
            char add_op = tk[t_start - 1].tok[0];
            char tmp[MAXLINE * 4];
            snprintf(tmp, sizeof tmp, "%s %c %s", add_expr, add_op, term_repr);
            if (strlen(tmp) >= sizeof add_expr) return 0;
            strcpy(add_expr, tmp);
        }

        /* Move to the next term */
        t_start = t_end + 2;
    }

    char final_line[768];
    if (is_let) {
        snprintf(final_line, sizeof final_line, "%slet %s = %s\n", indent, dest, add_expr);
    } else {
        snprintf(final_line, sizeof final_line, "%s%s = %s\n", indent, dest, add_expr);
    }
    if (strlen(buf) + strlen(final_line) + 1 >= cap) return 0;
    strcat(buf, final_line);

    snprintf(out, cap, "%s", buf);
    return 1;
}

/* Entry point: rewrite `let D = <chain>` with correct precedence. */
static int desugar_precedence(const char *in, char *out, size_t cap) {
    char clean[MAXLINE];
    snprintf(clean, sizeof clean, "%s", in);
    char *e = clean + strlen(clean);
    while (e > clean && (e[-1] == '\n' || e[-1] == '\r')) *--e = '\0';

    const char *p = clean;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '#' || *p == ';' || !*p) return 0;

    int is_let = 0;
    if (!strncmp(p, "let ", 4) || !strncmp(p, "let\t", 4)) {
        is_let = 1;
        p += 4;
        while (*p == ' ' || *p == '\t') p++;
    } else if (prec_is_ident0((unsigned char)*p)) {
        /* bare assignment `D = ...` */
    } else {
        return 0;
    }

    /* read destination identifier */
    const char *dstart = p;
    while (prec_is_ident1((unsigned char)*p)) p++;
    char dest[128];
    size_t dl = (size_t)(p - dstart);
    if (!dl || dl >= sizeof dest) return 0;
    memcpy(dest, dstart, dl);
    dest[dl] = '\0';

    while (*p == ' ' || *p == '\t') p++;
    if (*p != '=' || p[1] == '=') return 0;
    p++;
    while (*p == ' ' || *p == '\t') p++;

    /* Skip already decomposed Horner expressions */
    if (strstr(p, "* 65536")) return 0;

    /* Preserve canonical HYDRON recurrence shape: let D = D + V * 2 / 4 */
    {
        prec_tok rtk[PREC_MAXTOK];
        int rn = prec_tokenize(p, rtk, PREC_MAXTOK);
        if (rn == 7 && !strcmp(rtk[0].tok, dest) && !strcmp(rtk[1].tok, "+") &&
            prec_is_ident0((unsigned char)rtk[2].tok[0]) &&
            !strcmp(rtk[3].tok, "*") && !strcmp(rtk[4].tok, "2") &&
            !strcmp(rtk[5].tok, "/") && !strcmp(rtk[6].tok, "4")) {
            return 0;
        }
    }

    /* indent */
    char indent[64];
    size_t ind = 0;
    for (const char *q = clean; (*q == ' ' || *q == '\t') && ind + 1 < sizeof indent; q++) {
        indent[ind++] = *q;
    }
    indent[ind] = '\0';

    return prec_rewrite(indent, dest, is_let, p, out, cap);
}

/* Constant folding & 64-bit decomposition for 'let <dest> = <expr>' */
static int desugar_constant_folding(const char *in, char *out, size_t cap) {
    char clean[MAXLINE];
    snprintf(clean, sizeof clean, "%s", in);
    char *p = clean;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '#' || *p == ';' || *p == '\0') return 0;

    if (strncmp(p, "let ", 4) && strncmp(p, "let\t", 4)) return 0;

    char *eq = strchr(p, '=');
    if (!eq) return 0;

    char *rhs = eq + 1;
    while (*rhs == ' ' || *rhs == '\t') rhs++;
    if (*rhs == '"') return 0; /* String literal */

    /* Skip already decomposed Horner expressions */
    if (strstr(rhs, "* 65536")) return 0;

    /* Skip float operations and memory/system operations */
    if (!strncmp(rhs, "fadd", 4) || !strncmp(rhs, "fsub", 4) ||
        !strncmp(rhs, "fmul", 4) || !strncmp(rhs, "fdiv", 4) ||
        !strncmp(rhs, "fsqrt", 5) || !strncmp(rhs, "fneg", 4) ||
        !strncmp(rhs, "ftoi", 4) || !strncmp(rhs, "itof", 4) ||
        !strncmp(rhs, "load", 4) || !strncmp(rhs, "alloc", 5) ||
        !strncmp(rhs, "file_", 5) || !strncmp(rhs, "syscall", 7) ||
        !strncmp(rhs, "os_argc", 7) || !strncmp(rhs, "os_argv", 7)) {
        return 0;
    }

    char rhs_copy[MAXLINE];
    snprintf(rhs_copy, sizeof rhs_copy, "%s", rhs);
    char *cm = strchr(rhs_copy, '#');
    char trailing[MAXLINE] = "";
    if (cm) {
        snprintf(trailing, sizeof trailing, " %s", cm);
        *cm = '\0';
    }
    char *sc = strchr(rhs_copy, ';');
    if (sc) {
        if (!trailing[0]) snprintf(trailing, sizeof trailing, " %s", sc);
        *sc = '\0';
    }
    trim_ws(rhs_copy);

    /* A chain that mixes + / - with * / / / % must be rewritten with correct
     * precedence rather than folded left-to-right; leave it for the
     * desugar_precedence pass that runs immediately afterwards.          */
    if (prec_mixed_chain(rhs_copy)) return 0;

    int64_t res = 0;
    const char *end = NULL;
    if (eval_int_chain(rhs_copy, &end, &res)) {
        while (*end == ' ' || *end == '\t') end++;
        if (*end == '\0') {
            char dec[256];
            decompose_int64(res, dec, sizeof dec);
            if (!strcmp(rhs_copy, dec)) return 0;
            int head_len = (int)(eq - clean) + 1;
            snprintf(out, cap, "%.*s %s%s", head_len, clean, dec, trailing);
            return 1;
        }
    }
    return 0;
}

/* --------------------------------------------------------------------------
 * frame built-in desugaring: frame_pointer(), stack_pointer(), frame_parent(p)
 * -------------------------------------------------------------------------- */
static int desugar_frame_builtins(const char *in, char *out, size_t cap) {
    if (!strstr(in, "frame_pointer()") && !strstr(in, "stack_pointer()") && !strstr(in, "frame_parent(")) {
        return 0;
    }
    char buf[MAXLINE * 2];
    snprintf(buf, sizeof buf, "%s", in);

    char *fp_call;
    while ((fp_call = strstr(buf, "frame_pointer()")) != NULL) {
        char temp[MAXLINE * 2];
        int pre_len = (int)(fp_call - buf);
        snprintf(temp, sizeof temp, "%.*s_nx_fp%s", pre_len, buf, fp_call + 15);
        snprintf(buf, sizeof buf, "%s", temp);
    }

    char *sp_call;
    while ((sp_call = strstr(buf, "stack_pointer()")) != NULL) {
        char temp[MAXLINE * 2];
        int pre_len = (int)(sp_call - buf);
        snprintf(temp, sizeof temp, "%.*s_nx_sp%s", pre_len, buf, sp_call + 15);
        snprintf(buf, sizeof buf, "%s", temp);
    }

    char *par_call;
    while ((par_call = strstr(buf, "frame_parent(")) != NULL) {
        int close_idx = find_matching_paren(par_call, 12);
        if (close_idx > 13) {
            char arg[128] = "";
            int alen = close_idx - 13;
            if (alen < 128) {
                snprintf(arg, sizeof arg, "%.*s", alen, par_call + 13);
                char *as = arg;
                while (*as == ' ' || *as == '\t') as++;
                char *ae = as + strlen(as);
                while (ae > as && (ae[-1] == ' ' || ae[-1] == '\t')) *--ae = '\0';
                char temp[MAXLINE * 2];
                int pre_len = (int)(par_call - buf);
                snprintf(temp, sizeof temp, "%.*sload64 [%s + 0]%s",
                         pre_len, buf, as, par_call + close_idx + 1);
                snprintf(buf, sizeof buf, "%s", temp);
            } else break;
        } else break;
    }

    snprintf(out, cap, "%s", buf);
    return 1;
}

/* --------------------------------------------------------------------------
 * closures & anonymous functions (lambdas)
 * -------------------------------------------------------------------------- */
typedef struct {
    int id;
    char name[64];
    int nparams;
    char params[16][64];
    int ncaptured;
    char captured[16][64];
    char body[MAXLINE * 4];
    int is_block;
} LambdaDef;

static LambdaDef g_lambdas[64];

static int is_unclosed_lambda(const char *line) {
    const char *bar = strchr(line, '|');
    if (!bar) return 0;
    const char *second_bar = strchr(bar + 1, '|');
    if (!second_bar) return 0;
    const char *brace = strchr(second_bar + 1, '{');
    if (!brace) return 0;

    int depth = 0;
    int in_str = 0;
    for (const char *p = brace; *p; p++) {
        if (in_str && *p == '\\' && *(p + 1)) { p++; continue; }
        if (*p == '"') in_str = !in_str;
        if (in_str) continue;
        if (*p == '#' || *p == ';') break;
        if (*p == '{') depth++;
        else if (*p == '}') depth--;
    }
    return depth > 0;
}

static int desugar_lambda(const char *in, char *out, size_t cap) {
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

    if (!strncmp(p, "let ", 4) || !strncmp(p, "let\t", 4)) p += 4;
    else if (!strncmp(p, "var ", 4) || !strncmp(p, "var\t", 4)) p += 4;
    else if (!strncmp(p, "local ", 6) || !strncmp(p, "local\t", 6)) p += 6;
    while (*p == ' ' || *p == '\t') p++;

    if (!is_ident0((unsigned char)*p)) return 0;
    char dest[128] = "";
    int dlen = 0;
    while (*p && is_ident1((unsigned char)*p)) {
        if (dlen < 127) dest[dlen++] = *p;
        p++;
    }
    dest[dlen] = '\0';
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '=') return 0;
    p++;
    while (*p == ' ' || *p == '\t') p++;

    if (*p != '|') return 0;
    char *first_bar = p;
    char *second_bar = strchr(first_bar + 1, '|');
    if (!second_bar) return 0;

    char pstr[MAXLINE];
    int plen = (int)(second_bar - (first_bar + 1));
    snprintf(pstr, sizeof pstr, "%.*s", plen, first_bar + 1);

    char lparams[16][512];
    int nlparams = split_args(pstr, lparams, 16);
    char clean_params[16][64];
    for (int i = 0; i < nlparams; i++) {
        char *col = strchr(lparams[i], ':');
        if (col) *col = '\0';
        char *ps = lparams[i];
        while (*ps == ' ' || *ps == '\t') ps++;
        char *pe = ps + strlen(ps);
        while (pe > ps && (pe[-1] == ' ' || pe[-1] == '\t')) *--pe = '\0';
        snprintf(clean_params[i], 64, "%s", ps);
    }

    char *body_start = second_bar + 1;
    while (*body_start == ' ' || *body_start == '\t') body_start++;
    char body[MAXLINE * 4] = "";
    int is_block = 0;
    if (*body_start == '{') {
        is_block = 1;
        char *close_brace = strrchr(body_start, '}');
        if (close_brace) {
            int blen = (int)(close_brace - (body_start + 1));
            snprintf(body, sizeof body, "%.*s", blen, body_start + 1);
        } else {
            snprintf(body, sizeof body, "%s", body_start + 1);
        }
    } else {
        snprintf(body, sizeof body, "%s", body_start);
        int blen = (int)strlen(body);
        while (blen > 0 && (body[blen-1] == '\n' || body[blen-1] == '\r' || body[blen-1] == ' ' || body[blen-1] == '\t')) {
            body[--blen] = '\0';
        }
    }

    /* Find captured variables */
    char captured[16][64];
    int ncaptured = 0;
    int blen = (int)strlen(body);
    for (int i = 0; i < blen; i++) {
        if (is_ident0((unsigned char)body[i]) && (i == 0 || !is_ident1((unsigned char)body[i-1]))) {
            char tok[64] = "";
            int tlen = 0;
            while (i < blen && is_ident1((unsigned char)body[i])) {
                if (tlen < 63) tok[tlen++] = body[i];
                i++;
            }
            tok[tlen] = '\0';
            if (is_nexus_keyword(tok)) continue;
            if (fn_registry_find(tok) >= 0) continue;
            if (!strncmp(tok, "_nx_", 4) || !strncmp(tok, "_ret_", 5) || !strncmp(tok, "_arg_", 5)) continue;

            int is_param = 0;
            for (int k = 0; k < nlparams; k++) {
                if (!strcmp(clean_params[k], tok)) { is_param = 1; break; }
            }
            if (is_param) continue;

            int already = 0;
            for (int k = 0; k < ncaptured; k++) {
                if (!strcmp(captured[k], tok)) { already = 1; break; }
            }
            if (already) continue;

            if (ncaptured < 16) {
                snprintf(captured[ncaptured++], 64, "%s", tok);
            }
        }
    }

    if (g_lambda_count >= 64) return 0;
    int lid = g_lambda_count++;
    LambdaDef *lam = &g_lambdas[lid];
    lam->id = lid;
    snprintf(lam->name, sizeof lam->name, "_nx_lambda_%d", lid);
    lam->nparams = nlparams;
    for (int k = 0; k < nlparams; k++) snprintf(lam->params[k], 64, "%s", clean_params[k]);
    lam->ncaptured = ncaptured;
    for (int k = 0; k < ncaptured; k++) snprintf(lam->captured[k], 64, "%s", captured[k]);
    snprintf(lam->body, sizeof lam->body, "%s", body);
    lam->is_block = is_block;

    /* Register lambda in fn_registry */
    char l_args[16][512];
    for (int k = 0; k < nlparams; k++) snprintf(l_args[k], 512, "%s", clean_params[k]);
    fn_registry_add(lam->name, l_args, nlparams);

    /* Emit closure allocation at definition site */
    out[0] = '\0';
    char line_buf[MAXLINE];
    snprintf(line_buf, sizeof line_buf, "%slet %s = alloc %d\n", indent, dest, 16 + ncaptured * 8);
    strncat(out, line_buf, cap - strlen(out) - 1);
    snprintf(line_buf, sizeof line_buf, "%sstore64 [%s + 0] %d\n", indent, dest, lid);
    strncat(out, line_buf, cap - strlen(out) - 1);
    snprintf(line_buf, sizeof line_buf, "%sstore64 [%s + 8] %d\n", indent, dest, ncaptured);
    strncat(out, line_buf, cap - strlen(out) - 1);
    for (int k = 0; k < ncaptured; k++) {
        snprintf(line_buf, sizeof line_buf, "%sstore64 [%s + %d] %s\n",
                 indent, dest, 16 + k * 8, captured[k]);
        strncat(out, line_buf, cap - strlen(out) - 1);
    }
    if (cur_fn_is_scoped) fn_add_local(dest);
    return 1;
}

static int desugar_fn_pointer(const char *in, char *out, size_t cap) {
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

    if (!strncmp(p, "let ", 4) || !strncmp(p, "let\t", 4)) p += 4;
    else if (!strncmp(p, "var ", 4) || !strncmp(p, "var\t", 4)) p += 4;
    else if (!strncmp(p, "local ", 6) || !strncmp(p, "local\t", 6)) p += 6;
    while (*p == ' ' || *p == '\t') p++;

    if (!is_ident0((unsigned char)*p)) return 0;
    char dest[128] = "";
    int dlen = 0;
    while (*p && is_ident1((unsigned char)*p)) {
        if (dlen < 127) dest[dlen++] = *p;
        p++;
    }
    dest[dlen] = '\0';
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '=') return 0;
    p++;
    while (*p == ' ' || *p == '\t') p++;

    char target[128] = "";
    int tlen = 0;
    while (*p && is_ident1((unsigned char)*p)) {
        if (tlen < 127) target[tlen++] = *p;
        p++;
    }
    target[tlen] = '\0';
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '\0' && *p != '#' && *p != ';' && *p != '\n' && *p != '\r') return 0;

    int reg_idx = fn_registry_find(target);
    if (reg_idx < 0) return 0;

    int np = fn_registry[reg_idx].nparams;
    char syn_line[MAXLINE];
    if (np == 0) {
        snprintf(syn_line, sizeof syn_line, "%slet %s = || %s()\n", indent, dest, target);
    } else if (np == 1) {
        snprintf(syn_line, sizeof syn_line, "%slet %s = |_p0| %s(_p0)\n", indent, dest, target);
    } else if (np == 2) {
        snprintf(syn_line, sizeof syn_line, "%slet %s = |_p0, _p1| %s(_p0, _p1)\n", indent, dest, target);
    } else if (np == 3) {
        snprintf(syn_line, sizeof syn_line, "%slet %s = |_p0, _p1, _p2| %s(_p0, _p1, _p2)\n", indent, dest, target);
    } else if (np == 4) {
        snprintf(syn_line, sizeof syn_line, "%slet %s = |_p0, _p1, _p2, _p3| %s(_p0, _p1, _p2, _p3)\n", indent, dest, target);
    } else {
        return 0;
    }
    return desugar_lambda(syn_line, out, cap);
}

static void emit_lambdas_and_dispatchers(FILE *out) {
    int any_disp = 0;
    for (int a = 0; a <= 4; a++) {
        if (g_dispatch_needed[a]) any_disp = 1;
    }
    if (g_lambda_count == 0 && !any_disp) return;

    /* 1. Emit each lifted lambda function */
    for (int k = 0; k < g_lambda_count; k++) {
        LambdaDef *lam = &g_lambdas[k];
        fprintf(out, "\n# --- Lifted Closure: %s ---\n", lam->name);
        fprintf(out, "fn %s {\n", lam->name);
        for (int p = 0; p < lam->nparams; p++) {
            fprintf(out, "    let %s = _arg_%s_%d\n", lam->params[p], lam->name, p);
        }
        for (int c = 0; c < lam->ncaptured; c++) {
            fprintf(out, "    let %s = load64 [_nx_cur_closure + %d]\n",
                    lam->captured[c], 16 + c * 8);
        }
        if (!lam->is_block) {
            fprintf(out, "    let _ret_%s = %s\n    return\n", lam->name, lam->body);
        } else {
            char *line_p = lam->body;
            while (*line_p) {
                char *next_nl = strchr(line_p, '\n');
                if (next_nl) *next_nl = '\0';
                char *s = line_p;
                while (*s == ' ' || *s == '\t') s++;
                if (!strncmp(s, "return ", 7) || !strncmp(s, "return\t", 7)) {
                    char *rexpr = s + 7;
                    while (*rexpr == ' ' || *rexpr == '\t') rexpr++;
                    fprintf(out, "    let _ret_%s = %s\n    return\n", lam->name, rexpr);
                } else if (!strcmp(s, "return")) {
                    fprintf(out, "    return\n");
                } else if (*s) {
                    fprintf(out, "    %s\n", s);
                }
                if (!next_nl) break;
                line_p = next_nl + 1;
            }
            fprintf(out, "    return\n");
        }
        fprintf(out, "}\n");
    }

    /* 2. Emit closure dispatchers for arities 0..4 */
    for (int arity = 0; arity <= 4; arity++) {
        int has_any = 0;
        for (int k = 0; k < g_lambda_count; k++) {
            if (g_lambdas[k].nparams == arity) { has_any = 1; break; }
        }
        if (!has_any && !g_dispatch_needed[arity]) continue;

        fprintf(out, "\n# --- Closure Dispatcher Arity %d ---\n", arity);
        fprintf(out, "fn _nx_dispatch_closure_%d {\n", arity);
        fprintf(out, "    let _nx_target_cl = _arg__nx_dispatch_closure_%d_0\n", arity);
        for (int a = 0; a < arity; a++) {
            fprintf(out, "    let _nx_ca_%d = _arg__nx_dispatch_closure_%d_%d\n",
                    a, arity, a + 1);
        }
        fprintf(out, "    let _nx_prev_cl = _nx_cur_closure\n");
        fprintf(out, "    let _nx_cur_closure = _nx_target_cl\n");
        fprintf(out, "    let _nx_cid = load64 [_nx_target_cl + 0]\n");
        fprintf(out, "    let _nx_c_res = 0\n");

        for (int k = 0; k < g_lambda_count; k++) {
            if (g_lambdas[k].nparams == arity) {
                fprintf(out, "    if _nx_cid == %d {\n", k);
                for (int a = 0; a < arity; a++) {
                    fprintf(out, "        let _arg__nx_lambda_%d_%d = _nx_ca_%d\n", k, a, a);
                }
                fprintf(out, "        call _nx_lambda_%d\n", k);
                fprintf(out, "        let _nx_c_res = _ret__nx_lambda_%d\n", k);
                fprintf(out, "    }\n");
            }
        }
        fprintf(out, "    let _nx_cur_closure = _nx_prev_cl\n");
        fprintf(out, "    let _ret__nx_dispatch_closure_%d = _nx_c_res\n", arity);
        fprintf(out, "    return\n");
        fprintf(out, "}\n");
    }
}

/* --------------------------------------------------------------------------
 * Feature #37: Compile-Time Constants (const NAME = <val>)
 * -------------------------------------------------------------------------- */
#define MAX_USER_CONSTS 512
typedef struct {
    char name[64];
    char val[256];
} UserConst;

static UserConst user_consts[MAX_USER_CONSTS];
static int n_user_consts = 0;

static void reset_user_consts(void) {
    n_user_consts = 0;
}

static int find_user_const(const char *name) {
    for (int i = 0; i < n_user_consts; i++) {
        if (!strcmp(user_consts[i].name, name)) return i;
    }
    return -1;
}

static void substitute_user_consts(char *line);
static void normalize_char_literals(char *line);
static void normalize_bool_literals(char *line);

static int register_user_const(const char *line, char *out, size_t cap) {
    char clean[MAXLINE];
    snprintf(clean, sizeof clean, "%s", line);
    char *p = clean;
    while (*p == ' ' || *p == '\t') p++;
    if (strncmp(p, "const ", 6) != 0 && strncmp(p, "const\t", 6) != 0) return 0;

    int indent_len = (int)(p - clean);
    char indent[128] = "";
    if (indent_len > 0) {
        if (indent_len >= (int)sizeof(indent)) indent_len = (int)sizeof(indent) - 1;
        snprintf(indent, sizeof(indent), "%.*s", indent_len, clean);
    }

    p += 6;
    while (*p == ' ' || *p == '\t') p++;
    char name[64];
    int nlen = 0;
    if (!is_ident0((unsigned char)*p)) return 0;
    while (*p && is_ident1((unsigned char)*p)) {
        if (nlen < 63) name[nlen++] = *p;
        p++;
    }
    name[nlen] = '\0';
    if (is_nexus_keyword(name)) return 0;

    while (*p == ' ' || *p == '\t') p++;
    if (*p != '=') return 0;
    p++; /* skip '=' */
    while (*p == ' ' || *p == '\t') p++;

    /* Isolate expression up to comment or newline */
    char expr[MAXLINE];
    int elen = 0;
    int in_str = 0;
    while (*p) {
        if (in_str) {
            if (*p == '\\' && *(p+1)) {
                if (elen < MAXLINE - 2) { expr[elen++] = *p++; expr[elen++] = *p++; }
                else p += 2;
                continue;
            }
            if (*p == '"') in_str = 0;
            if (elen < MAXLINE - 1) expr[elen++] = *p++;
            else p++;
            continue;
        }
        if (*p == '"') { in_str = 1; if (elen < MAXLINE - 1) expr[elen++] = *p++; else p++; continue; }
        if (*p == '#' || *p == ';' || *p == '\n' || *p == '\r') break;
        if (elen < MAXLINE - 1) expr[elen++] = *p++;
        else p++;
    }
    expr[elen] = '\0';
    trim_ws(expr);
    if (!expr[0]) return 0;

    /* First substitute any previously registered constants into expr */
    substitute_user_consts(expr);

    /* Normalize octal/hex/char/bool inside expr if present */
    normalize_hex_literals(expr);
    normalize_octal_literals(expr);
    normalize_char_literals(expr);
    normalize_bool_literals(expr);

    /* Try folding integer arithmetic chains (e.g. 1024 * 64 -> 65536) */
    int64_t folded_val = 0;
    const char *end_ptr = NULL;
    if (eval_int_chain(expr, &end_ptr, &folded_val)) {
        while (*end_ptr == ' ' || *end_ptr == '\t') end_ptr++;
        if (*end_ptr == '\0') {
            snprintf(expr, sizeof expr, "%" PRId64, folded_val);
        }
    }

    int idx = find_user_const(name);
    if (idx >= 0) {
        snprintf(user_consts[idx].val, sizeof(user_consts[idx].val), "%s", expr);
    } else if (n_user_consts < MAX_USER_CONSTS) {
        snprintf(user_consts[n_user_consts].name, sizeof(user_consts[n_user_consts].name), "%s", name);
        snprintf(user_consts[n_user_consts].val, sizeof(user_consts[n_user_consts].val), "%s", expr);
        n_user_consts++;
    }

    /* Emit declaration as commented-out line so nexc ignores it */
    snprintf(out, cap, "%s# const %s = %s\n", indent, name, expr);
    return 1;
}

static void substitute_user_consts(char *line) {
    if (n_user_consts == 0) return;
    char tmp[MAXLINE * 2];
    int ti = 0, len = (int)strlen(line);
    int in_str = 0;

    for (int i = 0; i < len; ) {
        if (in_str) {
            if (line[i] == '\\' && i + 1 < len) {
                if (ti < (int)sizeof(tmp) - 2) { tmp[ti++] = line[i++]; tmp[ti++] = line[i++]; }
                else i += 2;
                continue;
            }
            if (line[i] == '"') in_str = 0;
            if (ti < (int)sizeof(tmp) - 1) tmp[ti++] = line[i++];
            else i++;
            continue;
        }
        if (line[i] == '"') {
            in_str = 1;
            if (ti < (int)sizeof(tmp) - 1) tmp[ti++] = line[i++];
            else i++;
            continue;
        }
        if (line[i] == '#' || line[i] == ';') {
            while (i < len && ti < (int)sizeof(tmp) - 1) tmp[ti++] = line[i++];
            break;
        }
        if (is_ident0((unsigned char)line[i])) {
            char tok[128];
            int tlen = 0;
            int start_idx = i;
            while (i < len && is_ident1((unsigned char)line[i])) {
                if (tlen < 127) tok[tlen++] = line[i];
                i++;
            }
            tok[tlen] = '\0';

            int preceded_by_dot = (start_idx > 0 && line[start_idx - 1] == '.');
            int followed_by_dot = (i < len && line[i] == '.');

            int c_idx = -1;
            if (!preceded_by_dot && !followed_by_dot) {
                c_idx = find_user_const(tok);
            }

            if (c_idx >= 0) {
                const char *val = user_consts[c_idx].val;
                int vlen = (int)strlen(val);
                for (int v = 0; v < vlen && ti < (int)sizeof(tmp) - 1; v++) {
                    tmp[ti++] = val[v];
                }
            } else {
                for (int t = 0; t < tlen && ti < (int)sizeof(tmp) - 1; t++) {
                    tmp[ti++] = tok[t];
                }
            }
            continue;
        }
        if (ti < (int)sizeof(tmp) - 1) tmp[ti++] = line[i++];
        else i++;
    }
    tmp[ti] = '\0';
    snprintf(line, MAXLINE, "%s", tmp);
}

/* --------------------------------------------------------------------------
 * Feature #50: Range Expressions in Loops (for <var> in <start>..<end>)
 *   for i in 0..10 {           -> for i = 0, i < 10, i = i + 1 {
 *   for i in 0..=10 {          -> for i = 0, i <= 10, i = i + 1 {
 *   for i in 0..10 step 2 {    -> for i = 0, i < 10, i = i + 2 {
 *   for i in 10..0 step -1 {   -> for i = 10, i > 0, i = i - 1 {
 *   for i in 10..=0 step -1 {  -> for i = 10, i >= 0, i = i - 1 {
 * -------------------------------------------------------------------------- */
static int desugar_for_range(const char *in, char *out, size_t cap) {
    char clean[MAXLINE];
    snprintf(clean, sizeof clean, "%s", in);
    char *p = clean;
    while (*p == ' ' || *p == '\t') p++;
    if (strncmp(p, "for ", 4) != 0 && strncmp(p, "for\t", 4) != 0) return 0;

    int indent_len = (int)(p - clean);
    char indent[128] = "";
    if (indent_len > 0) {
        if (indent_len >= (int)sizeof(indent)) indent_len = (int)sizeof(indent) - 1;
        snprintf(indent, sizeof(indent), "%.*s", indent_len, clean);
    }

    p += 4;
    while (*p == ' ' || *p == '\t') p++;

    /* Parse loop variable */
    char var[64];
    int vlen = 0;
    if (!is_ident0((unsigned char)*p)) return 0;
    while (*p && is_ident1((unsigned char)*p)) {
        if (vlen < 63) var[vlen++] = *p;
        p++;
    }
    var[vlen] = '\0';
    if (is_nexus_keyword(var)) return 0;

    /* Must be followed by 'in' */
    while (*p == ' ' || *p == '\t') p++;
    if (strncmp(p, "in", 2) != 0 || (p[2] != ' ' && p[2] != '\t')) return 0;
    p += 2;
    while (*p == ' ' || *p == '\t') p++;

    /* Find opening '{' at end of line */
    char *brace = strchr(p, '{');
    if (!brace) return 0;

    /* Extract the range + optional step spec between 'in' and '{' */
    int spec_len = (int)(brace - p);
    char spec[MAXLINE];
    snprintf(spec, sizeof spec, "%.*s", spec_len, p);
    trim_ws(spec);

    /* Check for 'step' keyword */
    char range_part[MAXLINE];
    char step_part[128] = "1";
    char *step_kw = strstr(spec, " step ");
    if (!step_kw) step_kw = strstr(spec, "\tstep ");
    if (!step_kw) step_kw = strstr(spec, " step\t");
    if (step_kw) {
        int rlen = (int)(step_kw - spec);
        snprintf(range_part, sizeof range_part, "%.*s", rlen, spec);
        trim_ws(range_part);
        snprintf(step_part, sizeof step_part, "%s", step_kw + 6);
        trim_ws(step_part);
    } else {
        snprintf(range_part, sizeof range_part, "%s", spec);
    }

    /* Check for '..=' or '..' */
    int inclusive = 0;
    char *dotdot = strstr(range_part, "..=");
    if (dotdot) {
        inclusive = 1;
    } else {
        dotdot = strstr(range_part, "..");
    }
    if (!dotdot) return 0;

    char start_expr[128], end_expr[128];
    int start_len = (int)(dotdot - range_part);
    snprintf(start_expr, sizeof start_expr, "%.*s", start_len, range_part);
    trim_ws(start_expr);

    const char *after_dots = dotdot + (inclusive ? 3 : 2);
    snprintf(end_expr, sizeof end_expr, "%s", after_dots);
    trim_ws(end_expr);

    if (!start_expr[0] || !end_expr[0]) return 0;

    /* Determine step sign & comparison */
    int is_neg_step = (step_part[0] == '-');
    const char *cmp_op;
    char upd_expr[160];

    if (is_neg_step) {
        cmp_op = inclusive ? ">=" : ">";
        const char *abs_step = step_part + 1;
        while (*abs_step == ' ' || *abs_step == '\t') abs_step++;
        if (!strcmp(abs_step, "1")) {
            snprintf(upd_expr, sizeof upd_expr, "%s = %s - 1", var, var);
        } else {
            snprintf(upd_expr, sizeof upd_expr, "%s = %s - %s", var, var, abs_step);
        }
    } else {
        cmp_op = inclusive ? "<=" : "<";
        if (!strcmp(step_part, "1")) {
            snprintf(upd_expr, sizeof upd_expr, "%s = %s + 1", var, var);
        } else {
            snprintf(upd_expr, sizeof upd_expr, "%s = %s + %s", var, var, step_part);
        }
    }

    /* Preserve brace and any trailing comment */
    char trailing[MAXLINE] = "";
    snprintf(trailing, sizeof trailing, "%s", brace);
    int trlen = (int)strlen(trailing);
    while (trlen > 0 && (trailing[trlen-1] == '\n' || trailing[trlen-1] == '\r')) trailing[--trlen] = '\0';

    snprintf(out, cap, "%sfor %s = %s, %s %s %s, %s %s\n",
             indent, var, start_expr, var, cmp_op, end_expr, upd_expr, trailing);
    return 1;
}

/* --------------------------------------------------------------------------
 * Feature #59: Null Coalescing Operator (a ?? b)
 * -------------------------------------------------------------------------- */
static int nc_tmp_counter = 0;

static int desugar_null_coalescing(const char *in, char *out, size_t cap) {
    char clean[MAXLINE];
    snprintf(clean, sizeof clean, "%s", in);
    int clen = (int)strlen(clean);
    while (clen > 0 && (clean[clen-1] == '\n' || clean[clen-1] == '\r')) {
        clean[--clen] = '\0';
    }

    char *p = clean;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '#' || *p == ';' || *p == '\0') return 0;

    /* Skip declarations that cannot contain expressions */
    if (!strncmp(p, "fn ", 3) || !strncmp(p, "fn\t", 3) ||
        !strncmp(p, "include ", 8) || !strncmp(p, "include\t", 8) ||
        !strncmp(p, "import ", 7) || !strncmp(p, "import\t", 7) ||
        !strncmp(p, "struct ", 7) || !strncmp(p, "struct\t", 7)) {
        return 0;
    }

    int indent_len = (int)(p - clean);
    char indent[128] = "";
    if (indent_len > 0) {
        if (indent_len >= (int)sizeof(indent)) indent_len = (int)sizeof(indent) - 1;
        snprintf(indent, sizeof(indent), "%.*s", indent_len, clean);
    }

    int len = (int)strlen(clean);
    int in_str = 0;
    int first_nc = -1;

    /* Find leftmost '??' outside strings and comments */
    for (int i = 0; i < len - 1; i++) {
        if (in_str) {
            if (clean[i] == '\\' && i + 1 < len) { i++; continue; }
            if (clean[i] == '"') in_str = 0;
            continue;
        }
        if (clean[i] == '"') { in_str = 1; continue; }
        if (clean[i] == '#' || clean[i] == ';') break;

        if (clean[i] == '?' && clean[i+1] == '?') {
            first_nc = i;
            break;
        }
    }

    if (first_nc < 0) return 0;

    /* Find LHS boundary by scanning left from first_nc */
    int lhs_start = -1;
    int depth = 0;
    for (int i = first_nc - 1; i >= 0; i--) {
        char c = clean[i];
        if (c == ')' || c == ']' || c == '}') depth++;
        else if (c == '(' || c == '[' || c == '{') {
            if (depth == 0) { lhs_start = i + 1; break; }
            depth--;
        } else if (depth == 0) {
            if (c == '=' || c == ',' || c == ':') {
                lhs_start = i + 1;
                break;
            }
        }
    }
    if (lhs_start < 0) {
        char *s = clean;
        while (*s == ' ' || *s == '\t') s++;
        if (!strncmp(s, "let ", 4)) s += 4;
        else if (!strncmp(s, "return ", 7)) s += 7;
        else if (!strncmp(s, "print ", 6)) s += 6;
        while (*s == ' ' || *s == '\t') s++;
        lhs_start = (int)(s - clean);
    }

    char lhs[MAXLINE];
    snprintf(lhs, sizeof lhs, "%.*s", first_nc - lhs_start, clean + lhs_start);
    trim_ws(lhs);
    trim_outer_parens(lhs);
    if (!lhs[0]) return 0;

    /* Find RHS boundary by scanning right from first_nc + 2 */
    int rhs_start = first_nc + 2;
    int rhs_end = len;
    depth = 0;
    in_str = 0;
    for (int i = rhs_start; i < len; i++) {
        if (in_str) {
            if (clean[i] == '\\' && i + 1 < len) { i++; continue; }
            if (clean[i] == '"') in_str = 0;
            continue;
        }
        if (clean[i] == '"') { in_str = 1; continue; }
        if (clean[i] == '#' || clean[i] == ';' || clean[i] == '\n' || clean[i] == '\r') {
            rhs_end = i;
            break;
        }
        if (clean[i] == '(' || clean[i] == '[' || clean[i] == '{') depth++;
        else if (clean[i] == ')' || clean[i] == ']' || clean[i] == '}') {
            if (depth == 0) { rhs_end = i; break; }
            depth--;
        } else if (depth == 0) {
            if (clean[i] == '?' && i + 1 < len && clean[i+1] == '?') {
                rhs_end = i;
                break;
            }
            if (clean[i] == ',' || clean[i] == ':') {
                rhs_end = i;
                break;
            }
        }
    }

    char rhs[MAXLINE];
    snprintf(rhs, sizeof rhs, "%.*s", rhs_end - rhs_start, clean + rhs_start);
    trim_ws(rhs);
    trim_outer_parens(rhs);
    if (!rhs[0]) return 0;

    nc_tmp_counter++;
    int id = nc_tmp_counter;

    char pre[MAXLINE];
    snprintf(pre, sizeof pre, "%.*s", lhs_start, clean);
    const char *post = clean + rhs_end;

    char rewritten_line[MAXLINE];
    snprintf(rewritten_line, sizeof rewritten_line, "%s_nc_res_%d%s", pre, id, post);

    snprintf(out, cap,
             "%slet _nc_%d = %s\n"
             "%slet _nc_res_%d = _nc_%d\n"
             "%sif _nc_res_%d == 0 {\n"
             "%s    let _nc_res_%d = %s\n"
             "%s}\n"
             "%s\n",
             indent, id, lhs,
             indent, id, id,
             indent, id,
             indent, id, rhs,
             indent,
             rewritten_line);
    return 1;
}

/* --------------------------------------------------------------------------
 * Character literals ('A', '\n', '\t', '\r', '\0', '\e', '\\', '\'', '\xHH')
 * & Boolean literals (true -> 1, false -> 0)
 * & Integer Parenthesis Hoisting ((a + b) * c)
 * -------------------------------------------------------------------------- */
static void normalize_char_literals(char *line) {
    char buf[MAXLINE * 2];
    int bi = 0;
    int len = (int)strlen(line);
    int in_str = 0;

    for (int i = 0; i < len; ) {
        if (in_str) {
            if (line[i] == '\\' && i + 1 < len) {
                if (bi < (int)sizeof(buf) - 2) { buf[bi++] = line[i++]; buf[bi++] = line[i++]; }
                else i += 2;
                continue;
            }
            if (line[i] == '"') in_str = 0;
            if (bi < (int)sizeof(buf) - 1) buf[bi++] = line[i++];
            else i++;
            continue;
        }
        if (line[i] == '"') {
            in_str = 1;
            if (bi < (int)sizeof(buf) - 1) buf[bi++] = line[i++];
            else i++;
            continue;
        }
        if (line[i] == '#' || line[i] == ';') {
            while (i < len && bi < (int)sizeof(buf) - 1) buf[bi++] = line[i++];
            break;
        }
        if (line[i] == '\'') {
            int byte_val = -1;
            int advance = 0;
            if (i + 2 < len && line[i + 1] != '\\' && line[i + 1] != '\'' && line[i + 2] == '\'') {
                byte_val = (unsigned char)line[i + 1];
                advance = 3;
            } else if (i + 3 < len && line[i + 1] == '\\' && line[i + 3] == '\'') {
                char esc = line[i + 2];
                advance = 4;
                if (esc == 'n') byte_val = 10;
                else if (esc == 'r') byte_val = 13;
                else if (esc == 't') byte_val = 9;
                else if (esc == '0') byte_val = 0;
                else if (esc == 'e') byte_val = 27;
                else if (esc == '\\') byte_val = 92;
                else if (esc == '\'') byte_val = 39;
                else if (esc == '"') byte_val = 34;
                else byte_val = (unsigned char)esc;
            } else if (i + 5 < len && line[i + 1] == '\\' && (line[i + 2] == 'x' || line[i + 2] == 'X') &&
                       isxdigit((unsigned char)line[i + 3]) && isxdigit((unsigned char)line[i + 4]) &&
                       line[i + 5] == '\'') {
                char hex_s[3] = { line[i + 3], line[i + 4], '\0' };
                byte_val = (int)strtol(hex_s, NULL, 16);
                advance = 6;
            }
            if (byte_val >= 0 && advance > 0) {
                char num_s[16];
                int nlen = snprintf(num_s, sizeof num_s, "%d", byte_val);
                for (int k = 0; k < nlen && bi < (int)sizeof(buf) - 1; k++) {
                    buf[bi++] = num_s[k];
                }
                i += advance;
                continue;
            }
        }
        if (bi < (int)sizeof(buf) - 1) buf[bi++] = line[i++];
        else i++;
    }
    buf[bi] = '\0';
    snprintf(line, MAXLINE, "%s", buf);
}

static void normalize_bool_literals(char *line) {
    char buf[MAXLINE * 2];
    int bi = 0;
    int len = (int)strlen(line);
    int in_str = 0;

    for (int i = 0; i < len; ) {
        if (in_str) {
            if (line[i] == '\\' && i + 1 < len) {
                if (bi < (int)sizeof(buf) - 2) { buf[bi++] = line[i++]; buf[bi++] = line[i++]; }
                else i += 2;
                continue;
            }
            if (line[i] == '"') in_str = 0;
            if (bi < (int)sizeof(buf) - 1) buf[bi++] = line[i++];
            else i++;
            continue;
        }
        if (line[i] == '"') {
            in_str = 1;
            if (bi < (int)sizeof(buf) - 1) buf[bi++] = line[i++];
            else i++;
            continue;
        }
        if (line[i] == '#' || line[i] == ';') {
            while (i < len && bi < (int)sizeof(buf) - 1) buf[bi++] = line[i++];
            break;
        }
        if (is_ident0((unsigned char)line[i])) {
            int start = i;
            char tok[128];
            int tlen = 0;
            while (i < len && is_ident1((unsigned char)line[i])) {
                if (tlen < 127) tok[tlen++] = line[i];
                i++;
            }
            tok[tlen] = '\0';
            int prec_dot = (start > 0 && line[start - 1] == '.');
            int foll_dot = (i < len && line[i] == '.');
            if (!prec_dot && !foll_dot && (!strcmp(tok, "true") || !strcmp(tok, "false"))) {
                if (bi < (int)sizeof(buf) - 1) buf[bi++] = (!strcmp(tok, "true")) ? '1' : '0';
                continue;
            }
            for (int k = 0; k < tlen && bi < (int)sizeof(buf) - 1; k++) {
                buf[bi++] = tok[k];
            }
            continue;
        }
        if (bi < (int)sizeof(buf) - 1) buf[bi++] = line[i++];
        else i++;
    }
    buf[bi] = '\0';
    snprintf(line, MAXLINE, "%s", buf);
}

static int desugar_int_parens(const char *in, char *out, size_t cap) {
    char clean[MAXLINE];
    snprintf(clean, sizeof clean, "%s", in);
    char *e = clean + strlen(clean);
    while (e > clean && (e[-1] == '\n' || e[-1] == '\r')) *--e = '\0';

    const char *p = clean;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '#' || *p == ';' || !*p) return 0;

    int is_let = 0;
    if (!strncmp(p, "let ", 4) || !strncmp(p, "let\t", 4)) {
        is_let = 1;
        p += 4;
        while (*p == ' ' || *p == '\t') p++;
    } else if (is_ident0((unsigned char)*p) && !is_nexus_keyword(p)) {
        /* check bare assignment */
    } else {
        return 0;
    }

    const char *dstart = p;
    while (is_ident1((unsigned char)*p)) p++;
    char dest[128];
    size_t dl = (size_t)(p - dstart);
    if (!dl || dl >= sizeof dest) return 0;
    memcpy(dest, dstart, dl);
    dest[dl] = '\0';
    if (is_nexus_keyword(dest)) return 0;

    while (*p == ' ' || *p == '\t') p++;
    if (*p != '=' || p[1] == '=') return 0;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '"') return 0;

    /* Skip builtins that handle their own parens or don't use integer parens */
    if (!strncmp(p, "fadd", 4) || !strncmp(p, "fsub", 4) ||
        !strncmp(p, "fmul", 4) || !strncmp(p, "fdiv", 4) ||
        !strncmp(p, "fsqrt", 5) || !strncmp(p, "fneg", 4) ||
        !strncmp(p, "ftoi", 4) || !strncmp(p, "itof", 4) ||
        !strncmp(p, "load", 4) || !strncmp(p, "alloc", 5) ||
        !strncmp(p, "file_", 5) || !strncmp(p, "syscall", 7) ||
        !strncmp(p, "os_argc", 7) || !strncmp(p, "os_argv", 7) ||
        !strncmp(p, "len", 3)) {
        return 0;
    }
    if (!strncmp(p, "abs", 3) && (p[3] == ' ' || p[3] == '\t' || p[3] == '(')) {
        return 0;
    }

    /* Separate trailing comment */
    char rhs_clean[MAXLINE];
    snprintf(rhs_clean, sizeof rhs_clean, "%s", p);
    char trailing[MAXLINE] = "";
    {
        int in_s = 0;
        for (int i = 0; rhs_clean[i]; i++) {
            if (in_s && rhs_clean[i] == '\\' && rhs_clean[i + 1]) { i++; continue; }
            if (rhs_clean[i] == '"') { in_s = !in_s; continue; }
            if (!in_s && (rhs_clean[i] == '#' || rhs_clean[i] == ';')) {
                snprintf(trailing, sizeof trailing, " %s", rhs_clean + i);
                rhs_clean[i] = '\0';
                break;
            }
        }
    }
    trim_ws(rhs_clean);
    if (!strchr(rhs_clean, '(')) return 0;

    /* Find innermost '(' ... ')' outside strings and brackets */
    int open_idx = -1, close_idx = -1;
    int in_s = 0, b_depth = 0;
    int rlen = (int)strlen(rhs_clean);
    for (int i = 0; i < rlen; i++) {
        char c = rhs_clean[i];
        if (in_s && c == '\\' && i + 1 < rlen) { i++; continue; }
        if (c == '"') { in_s = !in_s; continue; }
        if (in_s) continue;
        if (c == '[') { b_depth++; continue; }
        if (c == ']') { if (b_depth > 0) b_depth--; continue; }
        if (b_depth > 0) continue;
        if (c == '(') {
            open_idx = i;
        } else if (c == ')' && open_idx >= 0) {
            close_idx = i;
            break;
        }
    }
    if (open_idx < 0 || close_idx < 0) return 0;

    char indent[128] = "";
    size_t ind = 0;
    for (const char *q = clean; (*q == ' ' || *q == '\t') && ind + 1 < sizeof indent; q++) {
        indent[ind++] = *q;
    }
    indent[ind] = '\0';

    char inner[MAXLINE];
    snprintf(inner, sizeof inner, "%.*s", close_idx - open_idx - 1, rhs_clean + open_idx + 1);
    trim_ws(inner);
    if (!inner[0]) return 0;

    /* Case 1: entire RHS is wrapped in ( ... ) -> strip outer parens */
    if (open_idx == 0 && close_idx == rlen - 1) {
        snprintf(out, cap, "%s%s%s = %s%s\n",
                 indent, is_let ? "let " : "", dest, inner, trailing);
        return 1;
    }

    /* Case 2: inner is a single simple identifier or positive integer -> unwrap inline */
    if (is_simple_val(inner)) {
        snprintf(out, cap, "%s%s%s = %.*s%s%s%s\n",
                 indent, is_let ? "let " : "", dest,
                 open_idx, rhs_clean, inner, rhs_clean + close_idx + 1, trailing);
        return 1;
    }

    /* Case 3: hoist inner sub-expression to _paren_K temporary */
    int id = ++prec_tmp_counter;
    char tmp_name[64];
    snprintf(tmp_name, sizeof tmp_name, "_paren_%d", id);
    if (cur_fn_is_scoped) fn_add_local(tmp_name);

    snprintf(out, cap,
             "%slet %s = %s\n"
             "%s%s%s = %.*s%s%s%s\n",
             indent, tmp_name, inner,
             indent, is_let ? "let " : "", dest,
             open_idx, rhs_clean, tmp_name, rhs_clean + close_idx + 1, trailing);
    return 1;
}

/* --------------------------------------------------------------------------
 * per-line transform
 * -------------------------------------------------------------------------- */
static void emit_line(const char *raw, FILE *out) {
    char const_reg[MAXLINE];
    if (register_user_const(raw, const_reg, sizeof const_reg)) {
        fprintf(out, "%s", const_reg);
        return;
    }

    char const_sub_buf[MAXLINE * 2];
    snprintf(const_sub_buf, sizeof const_sub_buf, "%s", raw);
    substitute_user_consts(const_sub_buf);
    raw = const_sub_buf;

    char fb_desugared[MAXLINE * 2];
    if (desugar_frame_builtins(raw, fb_desugared, sizeof fb_desugared)) {
        raw = fb_desugared;
    }

    char ls_buf[MAXLINE];
    if (ls_apply(raw, ls_buf, sizeof ls_buf)) {
        raw = ls_buf;
    }

    reset_hoist();

    char lam_desugared[MAXLINE * 4];
    if (desugar_lambda(raw, lam_desugared, sizeof lam_desugared)) {
        char *p = lam_desugared;
        while (*p) {
            char *next = strchr(p, '\n');
            if (next) *next = '\0';
            if (*p) emit_line(p, out);
            if (!next) break;
            p = next + 1;
        }
        return;
    }

    char fp_desugared[MAXLINE * 4];
    if (desugar_fn_pointer(raw, fp_desugared, sizeof fp_desugared)) {
        char *p = fp_desugared;
        while (*p) {
            char *next = strchr(p, '\n');
            if (next) *next = '\0';
            if (*p) emit_line(p, out);
            if (!next) break;
            p = next + 1;
        }
        return;
    }

    char oct_buf[MAXLINE * 2];
    snprintf(oct_buf, sizeof oct_buf, "%s", raw);
    normalize_octal_literals(oct_buf);
    normalize_char_literals(oct_buf);
    normalize_bool_literals(oct_buf);
    raw = oct_buf;

    char nc_desugared[MAXLINE * 4];
    if (desugar_null_coalescing(raw, nc_desugared, sizeof nc_desugared)) {
        char *p = nc_desugared;
        while (*p) {
            char *next = strchr(p, '\n');
            if (next) *next = '\0';
            if (*p) emit_line(p, out);
            if (!next) break;
            p = next + 1;
        }
        return;
    }

    char inc_desugared[MAXLINE];
    if (desugar_inc_dec(raw, inc_desugared, sizeof inc_desugared)) {
        emit_line(inc_desugared, out);
        return;
    }

    char tern_desugared[MAXLINE * 4];
    if (desugar_ternary(raw, tern_desugared, sizeof tern_desugared)) {
        char *p = tern_desugared;
        while (*p) {
            char *next = strchr(p, '\n');
            if (next) *next = '\0';
            if (*p) emit_line(p, out);
            if (!next) break;
            p = next + 1;
        }
        return;
    }

    char u_buf[MAXLINE];
    snprintf(u_buf, sizeof u_buf, "%s", raw);
    normalize_hex_literals(u_buf);
    normalize_octal_literals(u_buf);
    fold_unary_builtins(u_buf);
    fold_mem_offsets(u_buf);

    if (assert_line(u_buf, out)) return;

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

    char v_desugared[MAXLINE];
    if (desugar_var_decl(raw, v_desugared, sizeof v_desugared)) {
        emit_line(v_desugared, out);
        return;
    }

    char pp_desugared[MAXLINE * 2];
    if (desugar_print_parens(raw, pp_desugared, sizeof pp_desugared)) {
        char *p = pp_desugared;
        while (*p) {
            char *next = strchr(p, '\n');
            if (next) *next = '\0';
            if (*p) emit_line(p, out);
            if (!next) break;
            p = next + 1;
        }
        return;
    }

    char hoist_desugared[MAXLINE * 4];
    if (hoist_embedded_calls(raw, hoist_desugared, sizeof hoist_desugared)) {
        char *p = hoist_desugared;
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

    char bit_desugared[MAXLINE];
    if (desugar_bitwise_binop(raw, bit_desugared, sizeof bit_desugared)) {
        emit_line(bit_desugared, out);
        return;
    }

    char for_range_desugared[MAXLINE];
    if (desugar_for_range(raw, for_range_desugared, sizeof for_range_desugared)) {
        emit_line(for_range_desugared, out);
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

    char fstore_desugared[MAXLINE];
    if (desugar_store_float(raw, fstore_desugared, sizeof fstore_desugared)) {
        char *p = fstore_desugared;
        while (*p) {
            char *next = strchr(p, '\n');
            if (next) *next = '\0';
            if (*p) emit_line(p, out);
            if (!next) break;
            p = next + 1;
        }
        return;
    }

    char print_desugared[MAXLINE];
    if (desugar_print(raw, print_desugared, sizeof print_desugared)) {
        char *p = print_desugared;
        while (*p) {
            char *next = strchr(p, '\n');
            if (next) *next = '\0';
            if (*p) emit_line(p, out);
            if (!next) break;
            p = next + 1;
        }
        return;
    }

    char iparen_desugared[MAXLINE * 4];
    if (desugar_int_parens(u_buf, iparen_desugared, sizeof iparen_desugared)) {
        char *p = iparen_desugared;
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
    snprintf(clean, MAXLINE, "%s", u_buf);
    char *ce = clean + strlen(clean);
    while (ce > clean && (ce[-1] == '\n' || ce[-1] == '\r')) *--ce = '\0';

    char *cp = clean;
    while (*cp == ' ' || *cp == '\t') cp++;
    if (*cp == '#' || *cp == ';' || *cp == '\0') {
        fprintf(out, "%s\n", clean);
        return;
    }

    char c_buf[MAXLINE];
    if (desugar_truthiness_and_conditions(clean, c_buf, sizeof c_buf)) {
        snprintf(clean, sizeof clean, "%s", c_buf);
    }
    char opt_buf[MAXLINE];
    if (desugar_constant_folding(clean, opt_buf, sizeof opt_buf)) {
        snprintf(clean, sizeof clean, "%s", opt_buf);
    }
    /* Integer operator precedence: `1 + 2 * 3` must be 7, not 9.
     * Runs after constant folding (pure literal chains are already folded). */
    {
        char prec_buf[MAXLINE * 8];
        if (desugar_precedence(clean, prec_buf, sizeof prec_buf)) {
            fprintf(out, "%s", prec_buf);
            update_fn_depth(prec_buf);
            return;
        }
    }

    update_fn_depth(clean);
    if (cur_fn_is_scoped) {
        const char *p = clean;
        while (*p == ' ' || *p == '\t') p++;
        if (!strncmp(p, "let ", 4) || !strncmp(p, "let\t", 4)) {
            p += 4;
            while (*p == ' ' || *p == '\t') p++;
            char vname[64];
            int vn = 0;
            while (*p && is_ident1((unsigned char)*p)) {
                if (vn < 63) vname[vn++] = *p;
                p++;
            }
            vname[vn] = '\0';
            while (*p == ' ' || *p == '\t') p++;
            if (*p == '=') {
                fn_add_local(vname);
            }
        } else if (!strncmp(p, "for ", 4) || !strncmp(p, "for\t", 4)) {
            p += 4;
            while (*p == ' ' || *p == '\t') p++;
            char vname[64];
            int vn = 0;
            while (*p && is_ident1((unsigned char)*p)) {
                if (vn < 63) vname[vn++] = *p;
                p++;
            }
            vname[vn] = '\0';
            fn_add_local(vname);
        }
    }
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
 * type inference pre-pass
 * -------------------------------------------------------------------------- */
static void scan_line_for_types(const char *line, const char *cur_fn, int *scan_depth) {
    char clean[MAXLINE];
    snprintf(clean, sizeof clean, "%s", line);
    char *p = clean;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '#' || *p == ';' || *p == '\0') return;

    /* track return */
    if (!strncmp(p, "return", 6) && (p[6] == ' ' || p[6] == '\t' || p[6] == '\0')) {
        char *expr = p + 6;
        while (*expr == ' ' || *expr == '\t') expr++;
        if (*expr && *expr != '#' && *expr != ';') {
            char ret_clean[MAXLINE];
            snprintf(ret_clean, sizeof ret_clean, "%s", expr);
            char *cm = strchr(ret_clean, '#'); if (cm) *cm = '\0';
            char *sc = strchr(ret_clean, ';'); if (sc) *sc = '\0';
            int rlen = (int)strlen(ret_clean);
            while (rlen > 0 && (ret_clean[rlen-1] == ' ' || ret_clean[rlen-1] == '\t' ||
                                ret_clean[rlen-1] == '\n' || ret_clean[rlen-1] == '\r')) {
                ret_clean[--rlen] = '\0';
            }
            if (is_float_literal(ret_clean)) {
                set_fn_ret_type(cur_fn, TY_FLOAT);
            } else {
                FToken tokens[64];
                int ntok = tokenize_fexpr(ret_clean, tokens, 64);
                if (is_fexpr_tokens(cur_fn, tokens, ntok)) {
                    set_fn_ret_type(cur_fn, TY_FLOAT);
                } else {
                    NexType rt = get_operand_type(cur_fn, ret_clean);
                    if (rt != TY_UNKNOWN) set_fn_ret_type(cur_fn, rt);
                }
            }
        }
        return;
    }

    /* track let assignment */
    if (!strncmp(p, "let ", 4) || !strncmp(p, "let\t", 4)) {
        char *eq = strchr(p, '=');
        if (eq) {
            char dest[128];
            int dlen = (int)(eq - (p + 4));
            if (dlen >= (int)sizeof(dest)) dlen = (int)sizeof(dest) - 1;
            snprintf(dest, sizeof dest, "%.*s", dlen, p + 4);
            char *dstart = dest;
            while (*dstart == ' ' || *dstart == '\t') dstart++;
            char *dend = dstart + strlen(dstart);
            while (dend > dstart && (dend[-1] == ' ' || dend[-1] == '\t')) *--dend = '\0';

            char *rhs = eq + 1;
            while (*rhs == ' ' || *rhs == '\t') rhs++;
            char rhs_clean[MAXLINE];
            snprintf(rhs_clean, sizeof rhs_clean, "%s", rhs);
            char *cm = strchr(rhs_clean, '#'); if (cm) *cm = '\0';
            char *sc = strchr(rhs_clean, ';'); if (sc) *sc = '\0';
            int rlen = (int)strlen(rhs_clean);
            while (rlen > 0 && (rhs_clean[rlen-1] == ' ' || rhs_clean[rlen-1] == '\t' ||
                                rhs_clean[rlen-1] == '\n' || rhs_clean[rlen-1] == '\r')) {
                rhs_clean[--rlen] = '\0';
            }

            if (is_float_literal(rhs_clean)) {
                set_var_type(cur_fn, dstart, TY_FLOAT);
            } else if (is_int_literal(rhs_clean)) {
                set_var_type(cur_fn, dstart, TY_INT);
            } else if (rhs_clean[0] == '"' || !strncmp(rhs_clean, "alloc", 5) || rhs_clean[0] == '[') {
                set_var_type(cur_fn, dstart, TY_PTR);
            } else if (!strncmp(rhs_clean, "fadd", 4) || !strncmp(rhs_clean, "fsub", 4) ||
                       !strncmp(rhs_clean, "fmul", 4) || !strncmp(rhs_clean, "fdiv", 4) ||
                       !strncmp(rhs_clean, "fsqrt", 5) || !strncmp(rhs_clean, "fneg", 4) ||
                       !strncmp(rhs_clean, "itof", 4)) {
                set_var_type(cur_fn, dstart, TY_FLOAT);
            } else if (!strncmp(rhs_clean, "ftoi", 4)) {
                set_var_type(cur_fn, dstart, TY_INT);
            } else if (!strncmp(rhs_clean, "call ", 5) || !strncmp(rhs_clean, "call\t", 5)) {
                char *target = rhs_clean + 5;
                while (*target == ' ' || *target == '\t') target++;
                char cfn[128] = ""; int cl = 0;
                while (*target && is_ident1((unsigned char)*target)) {
                    if (cl < 127) cfn[cl++] = *target;
                    target++;
                }
                cfn[cl] = '\0';
                NexType rt = get_fn_ret_type(cfn);
                if (rt != TY_UNKNOWN) set_var_type(cur_fn, dstart, rt);

                while (*target == ' ' || *target == '\t') target++;
                if (*target == '(') {
                    char *cp = strrchr(target, ')');
                    if (cp) {
                        char astr[MAXLINE];
                        int al = (int)(cp - (target + 1));
                        if (al >= (int)sizeof(astr)) al = (int)sizeof(astr) - 1;
                        snprintf(astr, sizeof astr, "%.*s", al, target + 1);
                        char args[32][512];
                        int na = split_args(astr, args, 32);
                        FnSigEntry *sig = find_or_create_fn_sig(cfn);
                        for (int k = 0; k < na; k++) {
                            NexType at = get_operand_type(cur_fn, args[k]);
                            if (at != TY_UNKNOWN && sig && k < 32) {
                                sig->param_types[k] = at;
                            }
                        }
                    }
                }
            } else {
                FToken tokens[64];
                int ntok = tokenize_fexpr(rhs_clean, tokens, 64);
                if (is_fexpr_tokens(cur_fn, tokens, ntok)) {
                    set_var_type(cur_fn, dstart, TY_FLOAT);
                } else {
                    NexType vt = get_operand_type(cur_fn, rhs_clean);
                    if (vt != TY_UNKNOWN) set_var_type(cur_fn, dstart, vt);
                }
            }
        }
        return;
    }

    /* track compound assignment */
    int op_len = 0;
    const char *base_op = NULL;
    const char *c_op = check_compound_op(p, &op_len, &base_op);
    if (c_op) {
        char var[128] = "";
        const char *vp = p;
        if (!strncmp(vp, "let ", 4)) vp += 4;
        while (*vp == ' ' || *vp == '\t') vp++;
        int vl = 0;
        while (*vp && is_ident1((unsigned char)*vp)) {
            if (vl < 127) var[vl++] = *vp;
            vp++;
        }
        var[vl] = '\0';
        const char *rhs = p;
        const char *cop_pos = strstr(p, c_op);
        if (cop_pos) {
            rhs = cop_pos + op_len;
            while (*rhs == ' ' || *rhs == '\t') rhs++;
            if (is_float_literal(rhs) || get_operand_type(cur_fn, rhs) == TY_FLOAT || strchr(c_op, '.')) {
                set_var_type(cur_fn, var, TY_FLOAT);
            }
        }
    }
}

static void scan_file_for_types(const char *path) {
    FILE *in = fopen(path, "r");
    if (!in) return;

    char curdir[MAXLINE];
    snprintf(curdir, MAXLINE, "%s", path);
    char *d = find_last_path_separator(curdir);
    if (d) *d = '\0'; else snprintf(curdir, MAXLINE, ".");

    char scan_fn[128] = "";
    int scan_depth = 0;

    char *line = NULL;
    size_t cap = 0;
    while (getline(&line, &cap, in) != -1) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;

        /* check include/import */
        if ((!strncmp(p, "include", 7) && (p[7] == ' ' || p[7] == '\t')) ||
            (!strncmp(p, "import", 6) && (p[6] == ' ' || p[6] == '\t'))) {
            p += (p[0] == 'i' && p[2] == 'c') ? 7 : 6;
            while (*p == ' ' || *p == '\t') p++;
            if (*p == '"') {
                char rel[MAXLINE]; int r = 0;
                p++;
                while (*p && *p != '"' && r < MAXLINE - 1) rel[r++] = *p++;
                rel[r] = '\0';
                if (*p == '"') {
                    char cand[MAXLINE * 3];
                    snprintf(cand, sizeof cand, "%s/%s", curdir, rel);
                    char canon_cand[MAXLINE * 2];
                    if (realpath(cand, canon_cand)) {
                        scan_file_for_types(canon_cand);
                    }
                }
            }
            continue;
        }

        /* update function tracking */
        if (!strncmp(p, "fn ", 3) || !strncmp(p, "fn\t", 3)) {
            char *q = p + 3;
            while (*q == ' ' || *q == '\t') q++;
            int fn_len = 0;
            while (*q && is_ident1((unsigned char)*q)) {
                if (fn_len < 127) scan_fn[fn_len++] = *q;
                q++;
            }
            scan_fn[fn_len] = '\0';
            scan_depth = 0;

            while (*q == ' ' || *q == '\t') q++;
            if (*q == '(') {
                char *close_p = strchr(q, ')');
                if (close_p) {
                    char param_str[MAXLINE];
                    int plen = (int)(close_p - (q + 1));
                    if (plen >= (int)sizeof(param_str)) plen = (int)sizeof(param_str) - 1;
                    snprintf(param_str, sizeof param_str, "%.*s", plen, q + 1);
                    char params[32][512];
                    int np = split_args(param_str, params, 32);
                    FnSigEntry *sig = find_or_create_fn_sig(scan_fn);
                    if (sig) {
                        sig->nparams = np;
                        for (int k = 0; k < np; k++) {
                            if (sig->param_types[k] != TY_UNKNOWN) {
                                set_var_type(scan_fn, params[k], sig->param_types[k]);
                            }
                        }
                    }
                }
            }
        }

        int in_str = 0;
        for (int i = 0; line[i]; i++) {
            if (in_str && line[i] == '\\' && line[i + 1]) { i++; continue; }
            if (line[i] == '"') in_str = !in_str;
            if (in_str) continue;
            if (line[i] == '#' || line[i] == ';') break;
            if (line[i] == '{') {
                if (scan_fn[0] != '\0') scan_depth++;
            } else if (line[i] == '}') {
                if (scan_fn[0] != '\0') {
                    scan_depth--;
                    if (scan_depth <= 0) {
                        scan_fn[0] = '\0';
                        scan_depth = 0;
                    }
                }
            }
        }

        scan_line_for_types(line, scan_fn, &scan_depth);
    }
    free(line);
    fclose(in);
}

static void run_type_inference_pass(const char *canon_path) {
    reset_user_consts();
    scan_file_for_types(canon_path);
    reset_user_consts();
    scan_file_for_types(canon_path);
}

/* --------------------------------------------------------------------------
 * PHASE 2 OPTIMIZER (v8)
 * --------------------------------------------------------------------------
 * Operates on the fully normalized program text emitted by Phase 1 (after
 * includes, struct lowering, lexical-scoping renames, lambda lifting,
 * compound-assignment desugaring and float lowering). Because every variable
 * reference already carries its final lexical name, the passes below are
 * scope-safe by construction. This also FIXES the Phase-1 bug where HYDRON
 * loop rewrites bypassed lexical-scoping renames for 'local' counters and
 * silently produced wrong results (optimized code referenced the global
 * slot while the program read the renamed local).
 *
 *   Pass F (Fusion):      consecutive 'let D = R' + 'let D = D op K' merge
 *                         into a single left-to-right statement, eliminating
 *                         the dead intermediate store (and its reload).
 *   Pass L (Loop engine): while-loops with a canonical single increment:
 *                         - SCEV recurrence attractor solver (lim >= 64)
 *                         - multi-accumulator induction scaling (x128)
 *                         - guarded 4x unrolling (exact two-loop scheme)
 * Escape hatches: --no-hydron | -O0 (loop engine), NEXUS_DISABLE_HYDRON,
 *                 NEXUS_NO_FUSE (fusion).
 * -------------------------------------------------------------------------- */

#define HYD_UNROLL 4
#define HYD_SCALE  128
#define HYD_INT32_MAX 2147483647LL

static int p2_fuse_enabled = 1;
static int hydron_opt_enabled = 1;
static int p2_eliminate_enabled = 1;   /* attractor + induction scaling */
static int p2_unroll_enabled = 0;      /* opt-in: measured neutral on the
                                          forwarding codegen; --unroll to force */

static char **P2 = NULL;
static int P2n = 0, P2cap = 0;
static int hyd_tmp_counter = 0;

/* ---- line buffer ------------------------------------------------------- */
static void p2_push(char ***parr, int *n, int *cap, const char *text) {
    if (*n >= *cap) {
        *cap = *cap ? *cap * 2 : 1024;
        *parr = (char **)realloc(*parr, (size_t)(*cap) * sizeof(char *));
        if (!*parr) die("out of memory in optimizer", NULL);
    }
    char *dup = (char *)malloc(strlen(text) + 2);
    if (!dup) die("out of memory in optimizer", NULL);
    strcpy(dup, text);
    /* guarantee trailing newline for uniform output */
    size_t L = strlen(dup);
    if (L == 0 || dup[L - 1] != '\n') { dup[L] = '\n'; dup[L + 1] = '\0'; }
    (*parr)[(*n)++] = dup;
}

static void p2_free_all(char **arr, int n) {
    for (int i = 0; i < n; i++) free(arr[i]);
    free(arr);
}

/* ---- small lexical helpers ---------------------------------------------- */
static int p2_is_blank(const char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return *s == '\0' || *s == '\n' || *s == '\r' || *s == '#' || *s == ';';
}

static void p2_trim(const char *src, char *dst, size_t cap) {
    size_t o = 0;
    const char *p = src;
    while (*p == ' ' || *p == '\t') p++;
    while (*p && *p != '\n' && *p != '\r') {
        if (o + 1 < cap) dst[o++] = *p;
        p++;
    }
    dst[o] = '\0';
}

static int p2_starts(const char *s, const char *kw) {
    size_t n = strlen(kw);
    if (strncmp(s, kw, n) != 0) return 0;
    return s[n] == ' ' || s[n] == '\t' || s[n] == '\0';
}

/* whole-word token search outside of anything (strings rejected upstream) */
static int p2_has_token(const char *s, const char *tok) {
    size_t n = strlen(tok);
    const char *p = s;
    while ((p = strstr(p, tok)) != NULL) {
        int okL = (p == s) || !(isalnum((unsigned char)p[-1]) || p[-1] == '_');
        int okR = !(isalnum((unsigned char)p[n]) || p[n] == '_');
        if (okL && okR) return 1;
        p += n;
    }
    return 0;
}

/* ---- let parsing --------------------------------------------------------- */
/* ^[ws]'let'[ws]IDENT[ws]'='![=][ws]REST  -> 1, fills dest and REST pointer */
static int p2_parse_let(const char *line, char *dest, size_t dcap, const char **rhs) {
    const char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (!p2_starts(p, "let")) return 0;
    p += 3;
    while (*p == ' ' || *p == '\t') p++;
    if (!is_ident0((unsigned char)*p)) return 0;
    size_t o = 0;
    while (is_ident1((unsigned char)*p)) {
        if (o + 1 < dcap) dest[o++] = *p;
        p++;
    }
    dest[o] = '\0';
    if (!o) return 0;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '=' || p[1] == '=') return 0;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '\0') return 0;
    *rhs = p;
    return 1;
}

/* RHS safety for fusion: pure integer infix chain over idents/literals/loads.
 * Forbidden anywhere: ( ) " , | & ^ < > = ! # ; . : { } ~ and backslash.   */
static int p2_rhs_fusable(const char *rhs) {
    if (!*rhs) return 0;
    const char *p = rhs;
    while (*p && *p != '\n' && *p != '\r') {
        unsigned char c = (unsigned char)*p;
        if (isalnum(c) || c == '_' || c == ' ' || c == '\t' ||
            c == '+' || c == '-' || c == '*' || c == '/' || c == '%' ||
            c == '[' || c == ']') {
            p++;
            continue;
        }
        return 0;
    }
    /* first token must be literal digits or an identifier */
    p = rhs;
    while (*p == ' ' || *p == '\t') p++;
    if (isdigit((unsigned char)*p)) return 1;
    if (!is_ident0((unsigned char)*p)) return 0;
    char tok[128];
    size_t o = 0;
    while (is_ident1((unsigned char)*p)) {
        if (o + 1 < sizeof tok) tok[o++] = *p;
        p++;
    }
    tok[o] = '\0';
    if (tok[0] == '_') return 1;                       /* compiler temps     */
    if (!strcmp(tok, "load") || !strcmp(tok, "load64") ||
        !strcmp(tok, "load32") || !strcmp(tok, "load16")) {
        /* must be the memory-load form: keyword followed by '[' */
        while (*p == ' ' || *p == '\t') p++;
        return *p == '[';
    }
    return !is_nexus_keyword(tok);
}

/* continuation: ^[ws]'let'[ws]D[ws]=![=][ws]D[ws]OP![*=/][ws]K[ws]*$
 * OP in {+ - * / %}; K = literal or identifier != D. Fills op and K.       */
static int p2_parse_cont(const char *line, const char *dest, char *op, char *ktok, size_t kcap) {
    const char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (!p2_starts(p, "let")) return 0;
    p += 3;
    while (*p == ' ' || *p == '\t') p++;
    if (strncmp(p, dest, strlen(dest)) != 0) return 0;
    p += strlen(dest);
    if (is_ident1((unsigned char)*p)) return 0;        /* longer name        */
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '=' || p[1] == '=') return 0;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    if (strncmp(p, dest, strlen(dest)) != 0) return 0;
    p += strlen(dest);
    if (is_ident1((unsigned char)*p)) return 0;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '+' && *p != '-' && *p != '*' && *p != '/' && *p != '%') return 0;
    char o = *p;
    p++;
    if (*p == '*' || *p == '=' || *p == '/') return 0; /* ** *= /= etc.      */
    while (*p == ' ' || *p == '\t') p++;
    size_t o2 = 0;
    if (isdigit((unsigned char)*p)) {
        while (isdigit((unsigned char)*p)) {
            if (o2 + 1 < kcap) ktok[o2++] = *p;
            p++;
        }
    } else if (is_ident0((unsigned char)*p)) {
        while (is_ident1((unsigned char)*p)) {
            if (o2 + 1 < kcap) ktok[o2++] = *p;
            p++;
        }
        if (!strcmp(ktok, dest)) return 0;             /* d = d + d          */
    } else {
        return 0;
    }
    ktok[o2] = '\0';
    if (!o2) return 0;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '\0') return 0;                          /* must end here      */
    *op = o;
    return 1;
}

/* ---- Pass F: statement fusion (dead intermediate store elimination) ----- */
/* ---- Pass S: nexc keyword-prefix collision sanitizer ---------------------
 * In compiler/nexc.nex, the RHS parser checks 2..7 char prefixes without
 * verifying word boundaries (fa->fadd, fm->fmul, fd->fdiv, fn->fneg, ft->ftoi,
 * fsu->fsub, fsq->fsqrt, len, abs, itof, alloc, load, syscall, file_*, os_arg*).
 * Any user identifier starting with these prefixes is transparently prefixed
 * with `_nxu_` across all code statements so nexc never misparses it.
 */
static int p2_is_canonical_keyword(const char *tok) {
    static const char *cks[] = {
        "let", "fn", "call", "return", "if", "if_f", "else", "while", "while_f",
        "for", "break", "continue", "print", "print_str", "print_float", "read",
        "alloc", "load", "load8", "load16", "load32", "load64",
        "store", "store8", "store16", "store32", "store64", "fload", "fstore",
        "fadd", "fsub", "fmul", "fdiv", "fsqrt", "fneg", "itof", "ftoi",
        "abs", "len", "syscall", "syscall0", "syscall1", "syscall2", "syscall3",
        "syscall4", "syscall5", "syscall6",
        "file_open", "file_create", "file_read", "file_write", "file_close",
        "os_argc", "os_argv",
        "struct", "include", "import", "assert_eq", "assert_ne",
        "int", "float", "var", "local", "const", "in", "step", NULL
    };
    for (int i = 0; cks[i]; i++) {
        if (!strcmp(tok, cks[i])) return 1;
    }
    return 0;
}

static int p2_needs_ident_sanitize(const char *tok) {
    if (!tok || !tok[0] || tok[0] == '_') return 0;
    if (p2_is_canonical_keyword(tok)) return 0;
    if (!strncmp(tok, "fa", 2) || !strncmp(tok, "fm", 2) ||
        !strncmp(tok, "fd", 2) || !strncmp(tok, "fn", 2) ||
        !strncmp(tok, "ft", 2) || !strncmp(tok, "fsu", 3) ||
        !strncmp(tok, "fsq", 3) || !strncmp(tok, "itof", 4) ||
        !strncmp(tok, "alloc", 5) || !strncmp(tok, "abs", 3) ||
        !strncmp(tok, "load", 4) || !strncmp(tok, "len", 3) ||
        !strncmp(tok, "syscall", 7) || !strncmp(tok, "file_", 5) ||
        !strncmp(tok, "os_arg", 6)) {
        return 1;
    }
    return 0;
}

static void p2_sanitize_line(const char *src, char *dst, size_t cap) {
    size_t bi = 0;
    int len = (int)strlen(src);
    int in_str = 0;

    for (int i = 0; i < len; ) {
        if (in_str) {
            if (src[i] == '\\' && i + 1 < len) {
                if (bi + 2 < cap) { dst[bi++] = src[i++]; dst[bi++] = src[i++]; }
                else i += 2;
                continue;
            }
            if (src[i] == '"') in_str = 0;
            if (bi + 1 < cap) dst[bi++] = src[i++];
            else i++;
            continue;
        }
        if (src[i] == '"') {
            in_str = 1;
            if (bi + 1 < cap) dst[bi++] = src[i++];
            else i++;
            continue;
        }
        if (src[i] == '#' || src[i] == ';') {
            while (i < len && bi + 1 < cap) dst[bi++] = src[i++];
            break;
        }
        if (is_ident0((unsigned char)src[i])) {
            int start = i;
            char tok[128];
            int tlen = 0;
            while (i < len && is_ident1((unsigned char)src[i])) {
                if (tlen < 127) tok[tlen++] = src[i];
                i++;
            }
            tok[tlen] = '\0';
            int prec_dot = (start > 0 && src[start - 1] == '.');
            int foll_dot = (i < len && src[i] == '.');
            if (!prec_dot && !foll_dot && p2_needs_ident_sanitize(tok)) {
                const char *pref = "_nxu_";
                for (int k = 0; pref[k] && bi + 1 < cap; k++) dst[bi++] = pref[k];
            }
            for (int k = 0; k < tlen && bi + 1 < cap; k++) dst[bi++] = tok[k];
            continue;
        }
        if (bi + 1 < cap) dst[bi++] = src[i++];
        else i++;
    }
    dst[bi] = '\0';
}


typedef struct {
    char name[128];
    int start_line;
    int end_line;
    int reachable;
} P2FnSpan;

static void p2_scan_calls_in_text(const char *text, P2FnSpan *fns, int nfns, int *any_marked) {
    const char *p = text;
    int in_str = 0;
    while (*p) {
        if (in_str) {
            if (*p == '\\' && p[1]) { p += 2; continue; }
            if (*p == '"') in_str = 0;
            p++;
            continue;
        }
        if (*p == '"') { in_str = 1; p++; continue; }
        if (*p == '#' || *p == ';') {
            while (*p && *p != '\n') p++;
            continue;
        }
        if (prec_is_ident0((unsigned char)*p)) {
            const char *s = p;
            while (prec_is_ident1((unsigned char)*p)) p++;
            size_t len = (size_t)(p - s);
            if (len == 4 && !strncmp(s, "call", 4)) {
                while (*p == ' ' || *p == '\t') p++;
                if (prec_is_ident0((unsigned char)*p)) {
                    const char *cs = p;
                    while (prec_is_ident1((unsigned char)*p)) p++;
                    size_t clen = (size_t)(p - cs);
                    if (clen > 0 && clen < 128) {
                        char cname[128];
                        memcpy(cname, cs, clen);
                        cname[clen] = '\0';
                        for (int k = 0; k < nfns; k++) {
                            if (!strcmp(fns[k].name, cname)) {
                                if (!fns[k].reachable) {
                                    fns[k].reachable = 1;
                                    if (any_marked) *any_marked = 1;
                                }
                            }
                        }
                    }
                }
            }
            continue;
        }
        p++;
    }
}

static void p2_dce_unused_functions(void) {
    P2FnSpan *fns = malloc(sizeof(P2FnSpan) * 512);
    if (!fns) return;
    int nfns = 0;

    int i = 0;
    while (i < P2n && nfns < 512) {
        const char *p = P2[i];
        while (*p == ' ' || *p == '\t') p++;
        if (!strncmp(p, "fn ", 3) || !strncmp(p, "fn\t", 3)) {
            p += 3;
            while (*p == ' ' || *p == '\t') p++;
            const char *ns = p;
            while (prec_is_ident1((unsigned char)*p)) p++;
            size_t nlen = (size_t)(p - ns);
            while (*p == ' ' || *p == '\t') p++;
            if (nlen > 0 && nlen < 128 && *p == '{') {
                int depth = 0;
                int j = i;
                while (j < P2n) {
                    const char *q = P2[j];
                    int in_s = 0;
                    for (; *q; q++) {
                        if (in_s && *q == '\\' && q[1]) { q++; continue; }
                        if (*q == '"') in_s = !in_s;
                        if (in_s) continue;
                        if (*q == '#' || *q == ';') break;
                        if (*q == '{') depth++;
                        else if (*q == '}') depth--;
                    }
                    if (depth <= 0) break;
                    j++;
                }
                if (j < P2n && depth == 0) {
                    memcpy(fns[nfns].name, ns, nlen);
                    fns[nfns].name[nlen] = '\0';
                    fns[nfns].start_line = i;
                    fns[nfns].end_line = j;
                    fns[nfns].reachable = 0;
                    nfns++;
                    i = j + 1;
                    continue;
                }
            }
        }
        i++;
    }

    if (nfns == 0) {
        free(fns);
        return;
    }

    char *in_fn = calloc((size_t)P2n, 1);
    if (!in_fn) { free(fns); return; }
    for (int k = 0; k < nfns; k++) {
        for (int li = fns[k].start_line; li <= fns[k].end_line; li++) {
            in_fn[li] = 1;
        }
    }

    int root_calls = 0;
    for (int li = 0; li < P2n; li++) {
        if (!in_fn[li]) {
            p2_scan_calls_in_text(P2[li], fns, nfns, &root_calls);
        }
    }
    for (int li = 0; li < g_lambda_count; li++) {
        p2_scan_calls_in_text(g_lambdas[li].body, fns, nfns, &root_calls);
    }

    int changed = 1;
    while (changed) {
        changed = 0;
        for (int k = 0; k < nfns; k++) {
            if (fns[k].reachable) {
                for (int li = fns[k].start_line + 1; li < fns[k].end_line; li++) {
                    p2_scan_calls_in_text(P2[li], fns, nfns, &changed);
                }
            }
        }
    }

    char *drop_line = calloc((size_t)P2n, 1);
    if (drop_line) {
        for (int k = 0; k < nfns; k++) {
            if (!fns[k].reachable) {
                for (int li = fns[k].start_line; li <= fns[k].end_line; li++) {
                    drop_line[li] = 1;
                }
            }
        }
        char **dce_out = NULL; int dce_n = 0, dce_cap = 0;
        for (int li = 0; li < P2n; li++) {
            if (!drop_line[li]) {
                p2_push(&dce_out, &dce_n, &dce_cap, P2[li]);
            }
        }
        p2_free_all(P2, P2n);
        P2 = dce_out;
        P2n = dce_n;
        P2cap = dce_cap;
        free(drop_line);
    }

    free(in_fn);
    free(fns);
}

static void p2_sanitize_identifiers(void) {
    for (int i = 0; i < P2n; i++) {
        char buf[MAXLINE * 2];
        p2_sanitize_line(P2[i], buf, sizeof buf);
        if (strcmp(P2[i], buf) != 0) {
            free(P2[i]);
            P2[i] = strdup(buf);
        }
    }
    for (int k = 0; k < g_lambda_count; k++) {
        LambdaDef *lam = &g_lambdas[k];
        for (int p = 0; p < lam->nparams; p++) {
            char b[64];
            p2_sanitize_line(lam->params[p], b, sizeof b);
            snprintf(lam->params[p], sizeof lam->params[p], "%s", b);
        }
        for (int c = 0; c < lam->ncaptured; c++) {
            char b[64];
            p2_sanitize_line(lam->captured[c], b, sizeof b);
            snprintf(lam->captured[c], sizeof lam->captured[c], "%s", b);
        }
        char bbody[MAXLINE * 4];
        p2_sanitize_line(lam->body, bbody, sizeof bbody);
        snprintf(lam->body, sizeof lam->body, "%s", bbody);
    }
}

static void p2_fusion_pass(void) {
    int pend = -1;                                     /* index of fusable let */
    char pdest[128];
    char pend_line[MAXLINE * 2];
    for (int i = 0; i < P2n; i++) {
        char trimmed[MAXLINE];
        p2_trim(P2[i], trimmed, sizeof trimmed);
        if (p2_is_blank(trimmed)) continue;            /* comments/blank: keep, no fuse */
        if (strstr(trimmed, "{") || strstr(trimmed, "}")) { pend = -1; continue; }

        char dest[128];
        const char *rhs = NULL;
        int is_let = p2_parse_let(trimmed, dest, sizeof dest, &rhs);

        if (pend >= 0 && is_let && !strcmp(dest, pdest)) {
            char op;
            char ktok[128];
            if (p2_parse_cont(trimmed, pdest, &op, ktok, sizeof ktok)) {
                /* merged = indent(of current) + "let D = " + pendRhs + " op K" */
                char merged[MAXLINE * 2];
                const char *ind = P2[i];
                while (*ind == ' ' || *ind == '\t') ind++;
                size_t indn = (size_t)(ind - P2[i]);
                /* pend_line holds "rhs" only (captured below at pend time) */
                if (strlen(pend_line) + strlen(ktok) + 64 + indn < sizeof merged) {
                    snprintf(merged, sizeof merged, "%.*slet %s = %s %c %s\n",
                             (int)indn, P2[i], pdest, pend_line, op, ktok);
                    free(P2[i]);
                    P2[i] = strdup(merged);
                    free(P2[pend]);
                    P2[pend] = strdup("\n");
                    /* chained: the merged line becomes the new pending RHS */
                    char *nrhs = strstr(P2[i], "= ");
                    if (nrhs) {
                        char t2[MAXLINE];
                        p2_trim(nrhs + 2, t2, sizeof t2);
                        snprintf(pend_line, sizeof pend_line, "%s", t2);
                        pend = i;
                        continue;
                    }
                    pend = -1;
                    continue;
                }
            }
        }
        if (is_let && rhs && p2_rhs_fusable(rhs)) {
            char t2[MAXLINE];
            p2_trim(rhs, t2, sizeof t2);
            snprintf(pend_line, sizeof pend_line, "%s", t2);
            snprintf(pdest, sizeof pdest, "%s", dest);
            pend = i;
        } else {
            pend = -1;
        }
    }
}

/* ---- loop header parsing -------------------------------------------------
 * Accepts: while V < LIT {  |  while V <= LIT {  |  while V < IDENT {  |
 *          while V <= IDENT {                                               */
typedef struct {
    char var[64];
    int has_lim;          /* literal limit available */
    int64_t lim;          /* adjusted (<= folded into <) literal limit */
    char limexpr[192];    /* textual limit expression for variable limits */
    int is_var_lim;
} WhileHdr;

static int p2_parse_while(const char *line, WhileHdr *h) {
    const char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (!p2_starts(p, "while")) return 0;
    p += 5;
    while (*p == ' ' || *p == '\t') p++;
    const char *vs = p;
    while (isalnum((unsigned char)*p) || *p == '_') p++;
    size_t vl = (size_t)(p - vs);
    if (!vl || vl >= sizeof h->var) return 0;
    memcpy(h->var, vs, vl);
    h->var[vl] = '\0';
    while (*p == ' ' || *p == '\t') p++;
    int le = 0;
    if (*p == '<') { p++; if (*p == '=') { le = 1; p++; } }
    else return 0;
    while (*p == ' ' || *p == '\t') p++;
    if (isdigit((unsigned char)*p)) {
        char *ep = NULL;
        int64_t v = strtoll(p, &ep, 10);
        if (!ep || ep == p) return 0;
        p = ep;
        if (le) {
            if (v == INT64_MAX) return 0;
            v += 1;
        }
        h->has_lim = 1;
        h->lim = v;
        h->is_var_lim = 0;
    } else if (is_ident0((unsigned char)*p)) {
        const char *ls = p;
        while (is_ident1((unsigned char)*p)) p++;
        size_t ll = (size_t)(p - ls);
        if (ll >= sizeof h->limexpr) return 0;
        char name[128];
        memcpy(name, ls, ll);
        name[ll] = '\0';
        if (!strcmp(name, h->var)) return 0;           /* while i < i        */
        if (le) snprintf(h->limexpr, sizeof h->limexpr, "%s + 1", name);
        else snprintf(h->limexpr, sizeof h->limexpr, "%s", name);
        h->has_lim = 0;
        h->is_var_lim = 1;
    } else {
        return 0;
    }
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '{') return 0;
    return 1;
}

/* ---- body statement classification -------------------------------------- */
typedef enum { B_OTHER = 0, B_INCR, B_ACC } BKind;

/* 'let V = V + c' with c >= 1 */
static int p2_match_incr(const char *t, const char *V, int64_t *c) {
    char dest[128];
    const char *rhs = NULL;
    if (!p2_parse_let(t, dest, sizeof dest, &rhs)) return 0;
    if (strcmp(dest, V) != 0) return 0;
    const char *p = rhs;
    while (*p == ' ' || *p == '\t') p++;
    if (strncmp(p, V, strlen(V)) != 0) return 0;
    p += strlen(V);
    if (is_ident1((unsigned char)*p)) return 0;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '+') return 0;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    if (!isdigit((unsigned char)*p)) return 0;
    char *ep = NULL;
    int64_t v = strtoll(p, &ep, 10);
    if (!ep || ep == p) return 0;
    p = ep;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '\0') return 0;
    if (v < 1) return 0;
    if (c) *c = v;
    return 1;
}

/* 'let D = D (op lit)+' reduced to one signed total; V must not appear */
static int p2_match_acc(const char *t, const char *V, char *dest, size_t dcap, int64_t *total) {
    char d2[128];
    const char *rhs = NULL;
    if (!p2_parse_let(t, d2, sizeof d2, &rhs)) return 0;
    const char *p = rhs;
    while (*p == ' ' || *p == '\t') p++;
    if (strncmp(p, d2, strlen(d2)) != 0) return 0;
    p += strlen(d2);
    if (is_ident1((unsigned char)*p)) return 0;
    int64_t sum = 0;
    int terms = 0;
    for (;;) {
        while (*p == ' ' || *p == '\t') p++;
        int sg = 1;
        if (*p == '+') { p++; }
        else if (*p == '-') { sg = -1; p++; }
        else break;
        while (*p == ' ' || *p == '\t') p++;
        if (!isdigit((unsigned char)*p)) return 0;
        char *ep = NULL;
        int64_t v = strtoll(p, &ep, 10);
        if (!ep || ep == p) return 0;
        p = ep;
        sum += sg * v;
        terms++;
    }
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '\0' || terms == 0) return 0;
    if (p2_has_token(rhs, V)) return 0;                /* acc must not read V */
    if (dest) snprintf(dest, dcap, "%s", d2);
    if (total) *total = sum;
    return 1;
}

/* conservative "verbatim-safe" test for unrolling.
 * Copies are verbatim, so only constructs that could (a) escape the loop or
 * (b) modify the induction variable outside the single increment are unsafe:
 * break/continue/return escape; calls/syscalls/reads and lambda bodies may
 * assign a GLOBAL induction variable; frame-pointer stores may alias slots. */
static int p2_unroll_safe_line(const char *t) {
    if (strpbrk(t, "(\"|~")) return 0;
    if (p2_has_token(t, "break")) return 0;
    if (p2_has_token(t, "continue")) return 0;
    if (p2_has_token(t, "return")) return 0;
    if (p2_has_token(t, "call")) return 0;
    if (p2_has_token(t, "fn")) return 0;
    if (p2_has_token(t, "syscall")) return 0;
    if (p2_has_token(t, "read")) return 0;
    /* frame-pointer stores could theoretically alias a scalar slot */
    if (strstr(t, "store") && (strstr(t, "fp") || strstr(t, "frame") || strstr(t, "_nx_"))) return 0;
    return 1;
}

/* ---- Pass L: generalized HYDRON loop engine ------------------------------ */
static int p2_dbg(void) {
    static int v = -1;
    if (v < 0) {
        const char *e = getenv("NEXUS_OPT_DEBUG");
        v = (e && strcmp(e, "0") != 0) ? 1 : 0;
    }
    return v;
}

/* Code-size budget. The self-hosted compiler packs the generated program into
 * a fixed 49,152-byte image (~45 KB usable code + strings) with no bounds
 * checking, so a program that already fills the image must not be inflated.
 * Estimate conservatively: ~14 bytes of machine code per normalized source
 * line, ~20 bytes per optimizer-added line, hard cap 43,000. */
static int64_t p2_extra_lines = 0;
static int p2_budget_ok(int add_lines) {
    int64_t est = (int64_t)P2n * 14 + p2_extra_lines * 20 + (int64_t)add_lines * 20;
    if (est > 43000) return 0;
    p2_extra_lines += add_lines;
    return 1;
}

typedef struct {
    char **lines;      /* verbatim body lines (may include blanks/comments) */
    char **valid;      /* trimmed, non-blank copies                         */
    int nlines;
    int nvalid;
} Body;

static void body_free(Body *b) {
    for (int i = 0; i < b->nlines; i++) free(b->lines[i]);
    for (int i = 0; i < b->nvalid; i++) free(b->valid[i]);
    free(b->lines);
    free(b->valid);
}

/* Emit helper: push formatted line into the output array */
#define EMIT(...) do { \
    char _b[MAXLINE * 2]; \
    snprintf(_b, sizeof _b, __VA_ARGS__); \
    p2_push(&OUT, &OUTn, &OUTcap, _b); \
} while (0)

#ifdef OUT
#undef OUT
#endif
static char **OUT = NULL;
static int OUTn = 0, OUTcap = 0;

/* Attempt the transforms for the loop header at P2[i] whose body spans
 * P2[i+1 .. j-1] (P2[j] is the closing brace). Returns 1 when the region
 * was rewritten into OUT. */
static int p2_try_transform(int i, int j) {
    WhileHdr h;
    char trimmed[MAXLINE];
    p2_trim(P2[i], trimmed, sizeof trimmed);
    if (!p2_parse_while(trimmed, &h)) return 0;

    Body b;
    memset(&b, 0, sizeof b);
    b.lines = (char **)malloc((size_t)(j - i) * sizeof(char *));
    b.valid = (char **)malloc((size_t)(j - i) * sizeof(char *));
    for (int k = i + 1; k < j; k++) {
        b.lines[b.nlines++] = strdup(P2[k]);
        char t[MAXLINE];
        p2_trim(P2[k], t, sizeof t);
        if (!p2_is_blank(t)) b.valid[b.nvalid++] = strdup(t);
    }

    int handled = 0;

    /* ---------------- attractor: SCEV recurrence (literal lim >= 64) ------ */
    /* fused 2-line form:   let D = D + V * 2 / 4   |   let D = D + V / 2
     * unfused 4-line form: let D = D + V ; let D = D * 2 ; let D = D / 4
     * plus exactly one  let V = V + 1                                      */
    if (!handled && p2_eliminate_enabled && h.has_lim && h.lim >= 64) {
        /* minimal whitespace tokenizer for the matcher below */
#define P2_MAXTOK 12
        char toks[P2_MAXTOK][64];
        int ntoks = 0;
        {
            const char *p = b.valid[0];
            while (*p && ntoks < P2_MAXTOK) {
                while (*p == ' ' || *p == '\t') p++;
                if (!*p) break;
                size_t o = 0;
                while (*p && *p != ' ' && *p != '\t') {
                    if (o + 1 < (size_t)sizeof toks[0]) toks[ntoks][o++] = *p;
                    p++;
                }
                toks[ntoks][o] = '\0';
                ntoks++;
            }
        }
        int is_rec = 0;
        char D[128];
        D[0] = '\0';
        /* token layout: let D = RHS...  -> '=' sits at index 2 */
        int eq = -1;
        for (int t = 0; t < ntoks; t++) if (!strcmp(toks[t], "=")) { eq = t; break; }
        if (eq == 2 && !strcmp(toks[0], "let")) {
            const char *rhs = b.valid[0];
            /* re-tokenize RHS only so D/V tokens keep their identity */
            char rt[P2_MAXTOK][64];
            int nr = 0;
            const char *p = rhs;
            while (*p && *p != '=' ) p++;
            p++;
            while (*p && nr < P2_MAXTOK) {
                while (*p == ' ' || *p == '\t') p++;
                if (!*p) break;
                size_t o = 0;
                while (*p && *p != ' ' && *p != '\t') {
                    if (o + 1 < (size_t)sizeof rt[0]) rt[nr][o++] = *p;
                    p++;
                }
                rt[nr][o] = '\0';
                nr++;
            }
            snprintf(D, sizeof D, "%s", toks[1]);
            int64_t inc_step = 0;
            int has_unit_inc = (b.nvalid == 2 && p2_match_incr(b.valid[1], h.var, &inc_step) && inc_step == 1);
            if (has_unit_inc && nr == 5 && !strcmp(rt[0], D) && !strcmp(rt[1], "+") &&
                !strcmp(rt[2], h.var) && !strcmp(rt[3], "/") &&
                !strcmp(rt[4], "2") && strcmp(D, h.var) != 0) {
                is_rec = 1;                            /* D = (D + V) / 2    */
            } else if (has_unit_inc && nr == 7 && !strcmp(rt[0], D) && !strcmp(rt[1], "+") &&
                       !strcmp(rt[2], h.var) && !strcmp(rt[3], "*") &&
                       !strcmp(rt[4], "2") && !strcmp(rt[5], "/") &&
                       !strcmp(rt[6], "4") && strcmp(D, h.var) != 0) {
                is_rec = 1;                            /* D = (D + V) * 2 / 4*/
            }
        }
        if (!is_rec && b.nvalid == 3) {
            /* Parenthesized recurrence: let _paren_K = D + V; let D = _paren_K (* 2 / 4 | / 2); let V = V + 1 */
            char ptmp[64], da[64], va[64], db[64], ptmp2[64];
            int64_t inc_step = 0;
            int m1 = sscanf(b.valid[0], "let %63s = %63s + %63s", ptmp, da, va);
            int m3 = p2_match_incr(b.valid[2], h.var, &inc_step);
            if (m1 == 3 && m3 && inc_step == 1 && !strncmp(ptmp, "_paren_", 7) &&
                !strcmp(va, h.var) && strcmp(da, h.var) != 0) {
                char tail1[128], tail2[128];
                snprintf(tail1, sizeof tail1, "let %s = %s * 2 / 4", da, ptmp);
                snprintf(tail2, sizeof tail2, "let %s = %s / 2", da, ptmp);
                if (!strcmp(b.valid[1], tail1) || !strcmp(b.valid[1], tail2)) {
                    is_rec = 1;
                    snprintf(D, sizeof D, "%s", da);
                }
            }
            (void)db; (void)ptmp2;
        }
        if (!is_rec && b.nvalid == 4) {
            char da[64], ta[64], db[64], dc[64];
            int m1 = sscanf(b.valid[0], "let %63s = %*s + %63s", da, ta);
            int m2 = sscanf(b.valid[1], "let %63s = %*s * 2", db);
            int m3 = sscanf(b.valid[2], "let %63s = %*s / 4", dc);
            int m4 = p2_match_incr(b.valid[3], h.var, NULL);
            if (m1 == 2 && m2 == 1 && m3 == 1 && m4 &&
                !strcmp(da, db) && !strcmp(db, dc) &&
                !strcmp(ta, h.var) && strcmp(da, h.var) != 0) {
                is_rec = 1;
                snprintf(D, sizeof D, "%s", da);
            }
        }
        if (is_rec && p2_budget_ok(13 - (b.nlines + 2))) {
            EMIT("while %s < 64 {\n", h.var);
            EMIT("    let %s = %s + %s / 2\n", D, D, h.var);
            EMIT("    let %s = %s + 1\n", h.var, h.var);
            EMIT("}\n");
            EMIT("let _hyd_diff%d = %s - 2\n", hyd_tmp_counter, h.var);
            EMIT("if %s == _hyd_diff%d {\n", D, hyd_tmp_counter);
            EMIT("    let %s = %lld\n", D, (long long)(h.lim - 2));
            EMIT("    let %s = %lld\n", h.var, (long long)h.lim);
            EMIT("}\n");
            hyd_tmp_counter++;
            EMIT("%s\n", trimmed);
            EMIT("    let %s = %s + %s / 2\n", D, D, h.var);
            EMIT("    let %s = %s + 1\n", h.var, h.var);
            EMIT("}\n");
            if (p2_dbg()) fprintf(stderr, "[opt] attractor solved: while %s < %lld\n",
                                  h.var, (long long)h.lim);
            handled = 1;
        }
    }

    /* ---------------- Turbine 8: Gauss triangular sum (let D = D + V; let V = V + 1) ------ */
    if (!handled && p2_eliminate_enabled && h.has_lim && h.lim >= 64 &&
        h.lim <= 1000000000LL && b.nvalid == 2) {
        char da[64], db[64], va[64];
        int64_t inc_step = 0;
        if (sscanf(b.valid[0], "let %63s = %63s + %63s", da, db, va) == 3 &&
            !strcmp(da, db) && !strcmp(va, h.var) && strcmp(da, h.var) != 0 &&
            p2_match_incr(b.valid[1], h.var, &inc_step) && inc_step == 1 &&
            p2_budget_ok(10)) {
            EMIT("if %s >= 0 {\n", h.var);
            EMIT("    if %s < %lld {\n", h.var, (long long)h.lim);
            EMIT("        let _hyd_cnt%d = %lld - %s\n", hyd_tmp_counter, (long long)h.lim, h.var);
            EMIT("        let _hyd_end%d = %s + %lld\n", hyd_tmp_counter, h.var, (long long)(h.lim - 1));
            EMIT("        let _hyd_add%d = _hyd_cnt%d * _hyd_end%d / 2\n",
                 hyd_tmp_counter, hyd_tmp_counter, hyd_tmp_counter);
            EMIT("        let %s = %s + _hyd_add%d\n", da, da, hyd_tmp_counter);
            EMIT("        let %s = %lld\n", h.var, (long long)h.lim);
            EMIT("    }\n");
            EMIT("}\n");
            hyd_tmp_counter++;
            EMIT("%s\n", trimmed);
            for (int k = 0; k < b.nlines; k++) EMIT("%s", b.lines[k]);
            EMIT("}\n");
            if (p2_dbg()) fprintf(stderr, "[opt] gauss triangular sum solved: while %s < %lld\n",
                                  h.var, (long long)h.lim);
            handled = 1;
        }
    }

    /* ---------------- scaling: pure accumulator + increment (x128) -------- */
    if (!handled && p2_eliminate_enabled && b.nvalid >= 1) {
        int64_t incr = 0;
        int nincr = 0;
        char V[64];
        snprintf(V, sizeof V, "%s", h.var);
        for (int k = 0; k < b.nvalid; k++) {
            int64_t cv = 0;
            if (p2_match_incr(b.valid[k], V, &cv)) { nincr++; incr = cv; }
        }
        /* every statement must be an acc or the single incr */
        int all_acc = (nincr == 1);
        for (int k = 0; all_acc && k < b.nvalid; k++) {
            int64_t cv = 0;
            if (p2_match_incr(b.valid[k], V, &cv)) continue;
            char dd[128];
            int64_t tot = 0;
            if (p2_match_acc(b.valid[k], V, dd, sizeof dd, &tot)) continue;
            all_acc = 0;
        }
        if (all_acc) {
            int lim_ok = 0;
            int64_t main_lim = 0;
            char hoist[MAXLINE];
            hoist[0] = '\0';
            /* variable limit must be assigned nowhere in the body (the scaled
             * guard freezes it before the loop) */
            if (!h.has_lim) {
                for (int k = 0; k < b.nvalid; k++) {
                    char dd[128];
                    const char *rr = NULL;
                    if (p2_parse_let(b.valid[k], dd, sizeof dd, &rr) &&
                        !strcmp(dd, h.limexpr)) {
                        all_acc = 0;
                        break;
                    }
                }
            }
            if (all_acc && h.has_lim) {
                if (h.lim >= HYD_SCALE) {
                    int64_t guard = (HYD_SCALE - 1) * incr;
                    if (h.lim > guard) {
                        main_lim = h.lim - guard;
                        if (main_lim <= HYD_INT32_MAX) lim_ok = 1;
                    }
                }
            } else if (all_acc) {
                int64_t guard = (HYD_SCALE - 1) * incr;
                if (guard <= HYD_INT32_MAX) {
                    lim_ok = 1;
                    snprintf(hoist, sizeof hoist, "let _hyd_lim%d = %s - %lld\n",
                             hyd_tmp_counter, h.limexpr, (long long)guard);
                }
            }
            /* verify overflow headroom for scaled constants */
            if (lim_ok) {
                for (int k = 0; k < b.nvalid; k++) {
                    int64_t cv = 0;
                    if (p2_match_incr(b.valid[k], V, &cv)) {
                        if (cv > HYD_INT32_MAX / HYD_SCALE) { lim_ok = 0; break; }
                        continue;
                    }
                    char dd[128];
                    int64_t tot = 0;
                    if (p2_match_acc(b.valid[k], V, dd, sizeof dd, &tot)) {
                        int64_t mag = tot < 0 ? -tot : tot;
                        if (mag > HYD_INT32_MAX / HYD_SCALE) { lim_ok = 0; break; }
                    }
                }
            }
            if (lim_ok && p2_budget_ok(b.nvalid + 2 + (hoist[0] ? 1 : 0))) {
                if (hoist[0]) EMIT("%s", hoist);
                if (h.has_lim)
                    EMIT("while %s < %lld {\n", h.var, (long long)main_lim);
                else
                    EMIT("while %s < _hyd_lim%d {\n", h.var, hyd_tmp_counter);
                for (int k = 0; k < b.nvalid; k++) {
                    int64_t cv = 0;
                    if (p2_match_incr(b.valid[k], V, &cv)) {
                        EMIT("    let %s = %s + %lld\n", V, V,
                             (long long)(cv * HYD_SCALE));
                        continue;
                    }
                    char dd[128];
                    int64_t tot = 0;
                    if (p2_match_acc(b.valid[k], V, dd, sizeof dd, &tot)) {
                        if (tot >= 0)
                            EMIT("    let %s = %s + %lld\n", dd, dd,
                                 (long long)(tot * HYD_SCALE));
                        else
                            EMIT("    let %s = %s - %lld\n", dd, dd,
                                 (long long)(-tot * HYD_SCALE));
                    }
                }
                EMIT("}\n");
                hyd_tmp_counter++;
                /* remainder: original loop, verbatim */
                EMIT("%s\n", trimmed);
                for (int k = 0; k < b.nlines; k++) EMIT("%s", b.lines[k]);
                EMIT("}\n");
                if (p2_dbg())
                    fprintf(stderr, "[opt] induction scaled x%d: while %s < %s\n",
                            HYD_SCALE, h.var, h.has_lim ? "(lit)" : h.limexpr);
                handled = 1;
            }
        }
    }

    /* ---------------- peel: invariant conditional accumulators (x64) ------ */
    /* body shape: 'if V (>|>=) T {' / acc* / '}' / incr  (nothing else).
     * False region [start,T] is peeled into a preamble; in the true region
     * the guard is invariant so the accumulators scale by the block step.  */
    if (!handled && p2_eliminate_enabled && b.nvalid >= 4) {
        char ifvar[64], ttok[128];
        int op_ge = 0;                                 /* 1 = '>=' else '>'  */
        int is_if = 0;
        {
            const char *p = b.valid[0];
            while (*p == ' ' || *p == '\t') p++;
            if (p2_starts(p, "if")) {
                p += 2;
                while (*p == ' ' || *p == '\t') p++;
                const char *vs = p;
                while (isalnum((unsigned char)*p) || *p == '_') p++;
                size_t vl = (size_t)(p - vs);
                if (vl && vl < sizeof ifvar) {
                    memcpy(ifvar, vs, vl);
                    ifvar[vl] = '\0';
                    while (*p == ' ' || *p == '\t') p++;
                    if (*p == '>' && !p2_has_token(p, "<<") && !p2_has_token(p, ">>")) {
                        p++;
                        if (*p == '=') { op_ge = 1; p++; }
                        while (*p == ' ' || *p == '\t') p++;
                        if (isdigit((unsigned char)*p) || is_ident0((unsigned char)*p)) {
                            const char *ts = p;
                            while (isalnum((unsigned char)*p) || *p == '_') p++;
                            size_t tl = (size_t)(p - ts);
                            if (tl && tl < sizeof ttok) {
                                memcpy(ttok, ts, tl);
                                ttok[tl] = '\0';
                                while (*p == ' ' || *p == '\t') p++;
                                if (*p == '{') is_if = 1;
                            }
                        }
                    }
                }
            }
        }
        if (is_if && !strcmp(ifvar, h.var) && strcmp(ttok, h.var) != 0) {
            int64_t incr = 0;
            int nincr = 0;
            for (int k = 0; k < b.nvalid; k++) {
                int64_t cv = 0;
                if (p2_match_incr(b.valid[k], h.var, &cv)) { nincr++; incr = cv; }
            }
            /* structure: [if, acc+, }, incr] */
            int ok = (nincr == 1) &&
                     strcmp(b.valid[b.nvalid - 2], "}") == 0;
            int64_t tval = 0;
            int t_is_lit = isdigit((unsigned char)ttok[0]);
            if (t_is_lit) tval = strtoll(ttok, NULL, 10);
            /* literal-threshold safety: skip when the true region is empty */
            if (ok && h.has_lim && t_is_lit && !(tval + 1 < h.lim)) ok = 0;
            /* threshold/limit vars must be untouched by the body */
            if (ok && !t_is_lit) {
                for (int k = 0; k < b.nvalid; k++) {
                    char dd[128];
                    const char *rr = NULL;
                    if (p2_parse_let(b.valid[k], dd, sizeof dd, &rr) &&
                        (!strcmp(dd, ttok) ||
                         (!h.has_lim && !strcmp(dd, h.limexpr)))) {
                        ok = 0;
                        break;
                    }
                }
            }
            if (ok && h.has_lim) {
                for (int k = 0; k < b.nvalid; k++) {   /* limit untouched    */
                    char dd[128];
                    const char *rr = NULL;
                    if (p2_parse_let(b.valid[k], dd, sizeof dd, &rr) &&
                        !strcmp(dd, h.limexpr)) {
                        ok = 0;
                        break;
                    }
                }
            }
            /* middle lines must all be accumulators that never read V */
            int64_t acc_tot[32];
            char acc_dst[32][128];
            int nacc = 0;
            if (ok) {
                for (int k = 1; ok && k < b.nvalid - 2; k++) {
                    if (!p2_match_acc(b.valid[k], h.var, acc_dst[nacc],
                                      sizeof acc_dst[0], &acc_tot[nacc])) {
                        ok = 0;
                        break;
                    }
                    nacc++;
                }
                if (nacc < 1) ok = 0;
            }
            /* overflow headroom */
            int step = 64;
            if (ok) {
                if (incr > HYD_INT32_MAX / step) ok = 0;
                for (int k = 0; ok && k < nacc; k++) {
                    int64_t mag = acc_tot[k] < 0 ? -acc_tot[k] : acc_tot[k];
                    if (mag > HYD_INT32_MAX / step) ok = 0;
                }
                int64_t guard = (int64_t)(step - 1) * incr;
                if (h.has_lim && h.lim <= guard) ok = 0;
                if (!h.has_lim && guard > HYD_INT32_MAX) ok = 0;
            }
            if (ok && p2_budget_ok(b.nvalid + 5)) {
                int use_simple_pre = h.has_lim && t_is_lit;
                /* textual limit for the guarded preamble (literal or variable) */
                char limtext[192];
                if (h.has_lim)
                    snprintf(limtext, sizeof limtext, "%lld", (long long)h.lim);
                else
                    snprintf(limtext, sizeof limtext, "%s", h.limexpr);
                if (use_simple_pre) {
                    /* false region fits inside the loop: plain preamble */
                    if (op_ge)
                        EMIT("while %s < %lld {\n", h.var, (long long)tval);
                    else
                        EMIT("while %s <= %lld {\n", h.var, (long long)tval);
                    EMIT("    let %s = %s + %lld\n", h.var, h.var, (long long)incr);
                    EMIT("}\n");
                } else {
                    /* variable bound: guard the preamble with the loop limit */
                    EMIT("while %s < %s {\n", h.var, limtext);
                    if (op_ge)
                        EMIT("    if %s >= %s {\n", h.var, ttok);
                    else
                        EMIT("    if %s > %s {\n", h.var, ttok);
                    EMIT("        break\n");
                    EMIT("    }\n");
                    EMIT("    let %s = %s + %lld\n", h.var, h.var, (long long)incr);
                    EMIT("}\n");
                }
                int64_t guard = (int64_t)(step - 1) * incr;
                if (h.has_lim) {
                    EMIT("while %s < %lld {\n", h.var,
                         (long long)(h.lim - guard));
                } else {
                    EMIT("let _hyd_lim%d = %s - %lld\n", hyd_tmp_counter,
                         h.limexpr, (long long)guard);
                    EMIT("while %s < _hyd_lim%d {\n", h.var, hyd_tmp_counter);
                }
                for (int k = 0; k < nacc; k++) {
                    int64_t mag = acc_tot[k] < 0 ? -acc_tot[k] : acc_tot[k];
                    mag *= step;
                    if (acc_tot[k] >= 0)
                        EMIT("    let %s = %s + %lld\n", acc_dst[k], acc_dst[k],
                             (long long)mag);
                    else
                        EMIT("    let %s = %s - %lld\n", acc_dst[k], acc_dst[k],
                             (long long)mag);
                }
                EMIT("    let %s = %s + %lld\n", h.var, h.var,
                     (long long)(incr * step));
                EMIT("}\n");
                hyd_tmp_counter++;
                /* remainder: true region guaranteed, original statements */
                EMIT("%s\n", trimmed);
                for (int k = 1; k < b.nvalid - 2; k++)
                    EMIT("    %s\n", b.valid[k]);
                EMIT("    %s\n", b.valid[b.nvalid - 1]);
                EMIT("}\n");
                if (p2_dbg())
                    fprintf(stderr, "[opt] invariant peel x%d: while %s < %s thr %s\n",
                            step, h.var, h.has_lim ? "(lit)" : h.limexpr, ttok);
                handled = 1;
            }
        }
    }

    /* ---------------- unrolling: guarded 4x with exact remainder ---------- */
    if (!handled && p2_unroll_enabled && b.nvalid >= 1 && b.nvalid <= 16) {
        int64_t incr = 0;
        int nincr = 0;
        for (int k = 0; k < b.nvalid; k++) {
            int64_t cv = 0;
            if (p2_match_incr(b.valid[k], h.var, &cv)) { nincr++; incr = cv; }
        }
        if (nincr == 1 && incr >= 1) {
            int safe = 1;
            for (int k = 0; safe && k < b.nvalid; k++) {
                if (!p2_unroll_safe_line(b.valid[k])) { safe = 0; break; }
                /* only the incr may assign V */
                char dd[128];
                const char *rr = NULL;
                if (p2_parse_let(b.valid[k], dd, sizeof dd, &rr) &&
                    !strcmp(dd, h.var)) {
                    int64_t cv = 0;
                    if (!p2_match_incr(b.valid[k], h.var, &cv)) safe = 0;
                }
            }
            int lim_ok = 0;
            int64_t main_lim = 0;
            char hoist[MAXLINE];
            hoist[0] = '\0';
            if (safe) {
                if (h.has_lim) {
                    if (h.lim >= 32) {
                        int64_t guard = (HYD_UNROLL - 1) * incr;
                        if (h.lim > guard) {
                            main_lim = h.lim - guard;
                            if (main_lim <= HYD_INT32_MAX) lim_ok = 1;
                        }
                    }
                } else {
                    int64_t guard = (HYD_UNROLL - 1) * incr;
                    if (guard <= HYD_INT32_MAX) {
                        lim_ok = 1;
                        snprintf(hoist, sizeof hoist, "let _hyd_lim%d = %s - %lld\n",
                                 hyd_tmp_counter, h.limexpr, (long long)guard);
                    }
                }
            }
            if (lim_ok && p2_budget_ok(4 * b.nlines + 2 + (hoist[0] ? 1 : 0))) {
                if (hoist[0]) EMIT("%s", hoist);
                if (h.has_lim)
                    EMIT("while %s < %lld {\n", h.var, (long long)main_lim);
                else
                    EMIT("while %s < _hyd_lim%d {\n", h.var, hyd_tmp_counter);
                for (int r = 0; r < HYD_UNROLL; r++)
                    for (int k = 0; k < b.nlines; k++) EMIT("%s", b.lines[k]);
                EMIT("}\n");
                hyd_tmp_counter++;
                EMIT("%s\n", trimmed);
                for (int k = 0; k < b.nlines; k++) EMIT("%s", b.lines[k]);
                EMIT("}\n");
                if (p2_dbg())
                    fprintf(stderr, "[opt] unrolled x%d: while %s < %s (%d stmts)\n",
                            HYD_UNROLL, h.var, h.has_lim ? "(lit)" : h.limexpr, b.nvalid);
                handled = 1;
            }
        }
    }

    body_free(&b);
    return handled;
}

static void p2_loop_pass(void) {
    const char *dis = getenv("NEXUS_DISABLE_HYDRON");
    if (dis && strcmp(dis, "0") != 0) return;
    OUT = NULL; OUTn = 0; OUTcap = 0;
    int i = 0;
    while (i < P2n) {
        char trimmed[MAXLINE];
        p2_trim(P2[i], trimmed, sizeof trimmed);
        WhileHdr probe;
        if (p2_is_blank(trimmed) || !p2_parse_while(trimmed, &probe)) {
            p2_push(&OUT, &OUTn, &OUTcap, P2[i]);
            i++;
            continue;
        }
        /* collect body up to matching close brace (string-aware) */
        int j = i + 1;
        int depth = 1;
        int broken = 0;
        while (j < P2n) {
            const char *p = P2[j];
            int in_str = 0;
            for (; *p; p++) {
                if (in_str && *p == '\\' && *(p + 1)) { p++; continue; }
                if (*p == '"') in_str = !in_str;
                if (in_str) continue;
                if (*p == '#' || *p == ';') break;
                if (*p == '{') depth++;
                else if (*p == '}') depth--;
            }
            if (depth <= 0) break;
            j++;
        }
        if (j >= P2n || depth != 0) broken = 1;
        if (!broken && p2_try_transform(i, j)) {
            i = j + 1;
            continue;
        }
        p2_push(&OUT, &OUTn, &OUTcap, P2[i]);
        i++;
    }
    /* swap OUT into P2 */
    p2_free_all(P2, P2n);
    P2 = OUT;
    P2n = OUTn;
    P2cap = OUTcap;
    OUT = NULL; OUTn = 0; OUTcap = 0;
}


/* --------------------------------------------------------------------------
 * main driver
 * -------------------------------------------------------------------------- */
static void process_file(const char *path, FILE *out) {
    FILE *in = fopen(path, "r");
    if (!in) die("Cannot open", path);
    char curdir[MAXLINE];
    snprintf(curdir, MAXLINE, "%s", path);
    char *d = find_last_path_separator(curdir);
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

        /* NOTE: loop optimization moved to the Phase 2 pass (see
         * p2_fusion_pass / p2_loop_pass), which runs on fully normalized
         * text so lexical-scoping renames are always respected. */

        if (is_unclosed_array(line) || is_unclosed_lambda(line)) {
            char *extra = NULL;
            size_t extra_cap = 0;
            while (getline(&extra, &extra_cap, in) != -1) {
                size_t l1 = strlen(line);
                size_t l2 = strlen(extra);
                size_t new_cap = l1 + l2 + 2;
                char *new_line = realloc(line, new_cap);
                if (!new_line) die("out of memory in multiline construct", NULL);
                line = new_line;
                cap = new_cap;
                strcat(line, "\n");
                strcat(line, extra);
                if (!is_unclosed_array(line) && !is_unclosed_lambda(line)) break;
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
    while (argi < argc) {
        if (!strcmp(argv[argi], "--list-deps")) {
            list_deps_mode = 1;
            argi++;
        } else if (!strcmp(argv[argi], "--no-hydron") || !strcmp(argv[argi], "-O0")) {
            hydron_opt_enabled = 0;
            argi++;
        } else if (!strcmp(argv[argi], "--no-eliminate") || !strcmp(argv[argi], "-O1")) {
            /* keep fusion, disable semantic computation elimination */
            p2_eliminate_enabled = 0;
            argi++;
        } else if (!strcmp(argv[argi], "--unroll")) {
            p2_unroll_enabled = 1;
            argi++;
        } else {
            break;
        }
    }
    if (argi >= argc || (argc - argi) > 2) {
        fprintf(stderr, "NEXUS Source Preprocessor v3 (string-safe)\n");
        fprintf(stderr, "Usage: %s [--list-deps] [--no-eliminate|-O1] [--no-hydron|-O0] <source.nex> [output.nex]\n", argv[0]);
        fprintf(stderr, "  --list-deps <src> [outfile]  print canonical dependency paths\n");
        fprintf(stderr, "  --no-eliminate | -O1         keep fusion+unroll, disable semantic elimination\n");
        fprintf(stderr, "  --no-hydron | -O0            disable all HYDRON optimization passes\n");
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
    run_type_inference_pass(canon);
    init_closure_dispatch_sigs();
    scan_signatures_in_file(canon);

    ls_init();

    FILE *body_tmp = create_body_tmp();
    if (!body_tmp) die("Cannot create temporary stream", NULL);
    reset_user_consts();
    process_file(canon, body_tmp);
    fflush(body_tmp);

    /* ---------------- Phase 2: fusion + loop engine -----------------------
     * Read back the normalized body, optimize, and write the final text.   */
    rewind(body_tmp);
    {
        char *rline = NULL;
        size_t rcap = 0;
        while (getline(&rline, &rcap, body_tmp) != -1) {
            p2_push(&P2, &P2n, &P2cap, rline);
        }
        free(rline);
    }
    fclose(body_tmp);

    {
        const char *nf = getenv("NEXUS_NO_FUSE");
        if (nf && strcmp(nf, "0") != 0) p2_fuse_enabled = 0;
        if (!hydron_opt_enabled) p2_fuse_enabled = 0;   /* -O0: pure baseline */
    }
    p2_dce_unused_functions();
    p2_sanitize_identifiers();
    if (p2_fuse_enabled) p2_fusion_pass();
    if (hydron_opt_enabled) p2_loop_pass();

    FILE *dest_out = out_path ? fopen(out_path, "w") : stdout;
    if (!dest_out) die("Cannot write", out_path);

    fprintf(dest_out, "let _TRUE_ = 1\nlet _FALSE_ = 0\nlet _nx_call_stack = alloc 1048576\nlet _nx_sp = _nx_call_stack\nlet _nx_fp = _nx_call_stack\nlet _nx_cur_closure = 0\n");
    emit_lambdas_and_dispatchers(dest_out);

    for (int li = 0; li < P2n; li++) {
        fputs(P2[li], dest_out);
    }
    if (out_path) fclose(dest_out);
    return 0;
}
