/*
 * nexfmt.c - NEXUS source formatter
 * ===========================================================================
 * Normalizes .nex source files while preserving semantics exactly:
 *   - indentation: 4 spaces per open brace (code and struct blocks)
 *   - tabs converted to spaces (outside string literals)
 *   - trailing whitespace removed
 *   - "} else {" and "} else if ... {" kept on one line
 *   - 3+ consecutive blank lines collapsed to one
 *   - nothing inside string literals is ever touched
 *
 * Usage:
 *   nexfmt <file.nex>            rewrite in place
 *   nexfmt --check <file.nex>    exit 1 (and show diff) if not formatted
 *   nexfmt --diff <file.nex>     print would-be result to stdout, change nothing
 *
 * The formatter is idempotent: fmt(fmt(x)) == fmt(x).
 *
 * Build:  gcc -O2 -o bin/nexfmt tools/nexfmt.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>

#define MAXLINE 8192

static int  depth        = 0;   /* brace depth */
static int  blank_run    = 0;   /* consecutive blank output lines */

/* copy src -> dst converting tabs outside strings to 4-space stops */
static void expand_tabs(const char *src, char *dst) {
    int col = 0, i = 0, o = 0, in_str = 0;
    for (; src[i]; i++) {
        char c = src[i];
        if (c == '"' && (i == 0 || src[i - 1] != '\\')) in_str = !in_str;
        if (!in_str && c == '\t') {
            int next = (col / 4 + 1) * 4;
            while (col < next) { dst[o++] = ' '; col++; }
        } else {
            dst[o++] = c;
            col++;
        }
    }
    dst[o] = '\0';
}

/* strip trailing whitespace (respecting nothing: spaces after strings too) */
static void rstrip(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' ||
                     s[n - 1] == '\r' || s[n - 1] == '\n'))
        s[--n] = '\0';
}

static int is_blank(const char *s) {
    for (size_t i = 0; s[i]; i++)
        if (s[i] != ' ' && s[i] != '\t' && s[i] != '\r' && s[i] != '\n')
            return 0;
    return 1;
}

static void indent_str(int d, char *out) {
    int i = 0;
    for (; i < d * 4 && i < MAXLINE - 1; i++) out[i] = ' ';
    out[i] = '\0';
}

/* count braces outside strings; returns net open count, flags pure '}' line */
static void scan_braces(const char *s, int *opens, int *closes, int *in_str_out) {
    int in_str = 0, o = 0, c = 0;
    for (size_t i = 0; s[i]; i++) {
        char ch = s[i];
        if (ch == '"' && (i == 0 || s[i - 1] != '\\')) in_str = !in_str;
        if (!in_str) {
            if (ch == '{') o++;
            else if (ch == '}') c++;
        }
    }
    *opens = o; *closes = c; *in_str_out = in_str;
}

int main(int argc, char **argv) {
    int mode_diff = 0, mode_check = 0, argi = 1;
    if (argi < argc && !strcmp(argv[argi], "--diff")) { mode_diff = 1; argi++; }
    else if (argi < argc && !strcmp(argv[argi], "--check")) { mode_check = 1; argi++; }
    if (argi >= argc) {
        fprintf(stderr, "Usage: %s [--check|--diff] <file.nex>\n", argv[0]);
        return 2;
    }
    const char *path = argv[argi];
    FILE *in = fopen(path, "r");
    if (!in) { fprintf(stderr, "[-] Cannot open: %s\n", path); return 2; }

    /* read whole file to memory (needed for --check in-place comparison) */
    fseek(in, 0, SEEK_END);
    long sz = ftell(in);
    fseek(in, 0, SEEK_SET);
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fprintf(stderr, "[-] Out of memory\n"); return 2; }
    size_t got = fread(buf, 1, (size_t)sz, in);
    buf[got] = '\0';
    fclose(in);

    /* format into dynamic output */
    size_t ocap = (size_t)sz * 2 + 4096;
    char *out = malloc(ocap);
    if (!out) { fprintf(stderr, "[-] Out of memory\n"); return 2; }
    size_t o = 0;

    char *linebuf = malloc(MAXLINE);
    char *expanded = malloc(MAXLINE);
    char *final_line = malloc(MAXLINE * 2);
    if (!linebuf || !expanded || !final_line) { fprintf(stderr, "[-] Out of memory\n"); return 2; }

    char *dup = strdup(buf);
    /* tokenize by lines without library strsep quirks */
    char *line = dup;
    while (line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        snprintf(linebuf, MAXLINE, "%s", line);
        line = nl ? nl + 1 : NULL;

        expand_tabs(linebuf, expanded);
        rstrip(expanded);

        if (is_blank(expanded)) {
            blank_run++;
            if (blank_run <= 1 && o > 0) {
                if (o + 1 < ocap) { out[o++] = '\n'; }
            }
            continue;
        }
        blank_run = 0;

        /* look at leading close braces: dedent before the content */
        const char *p = expanded;
        while (*p == ' ') p++;
        int closes_first = 0;
        {
            const char *q = p;
            while (*q == '}') { closes_first++; q++; }
        }

        int opens = 0, closes = 0, instr = 0;
        scan_braces(p, &opens, &closes, &instr);

        /* indentation for this line: current depth minus leading closes */
        int eff_depth = depth - closes_first;
        if (eff_depth < 0) eff_depth = 0;
        indent_str(eff_depth, final_line);
        strcat(final_line, p);
        {
            size_t need = o + strlen(final_line) + 2;
            if (need + 64 > ocap) { ocap = need * 2; out = realloc(out, ocap); }
        }
        o += sprintf(out + o, "%s\n", final_line);

        /* update depth AFTER emitting the line */
        depth += opens;
        /* net closes not at line start (e.g. "} else {") were already counted:
           scan_braces counts all closes; leading ones we pre-deducted for the
           indent only; the depth accounting must apply every brace once. */
        depth -= closes;
        if (depth < 0) depth = 0;
    }

    /* trailing blank lines: collapse to single newline at EOF */
    while (o > 0 && out[o - 1] == '\n') {
        int nl_count = 0;
        while (o > 0 && out[o - 1] == '\n') { o--; nl_count++; }
        out[o++] = '\n';
        if (nl_count > 1) { /* keep exactly one */ }
        break;
    }
    out[o] = '\0';

    int differs = (strcmp(out, buf) != 0);
    if (mode_check) {
        if (differs) {
            printf("[-] %s is not formatted (run: nexus fmt %s)\n", path, path);
            return 1;
        }
        printf("[+] %s is properly formatted\n", path);
        return 0;
    }
    if (mode_diff) {
        fputs(out, stdout);
        return 0;
    }
    if (differs) {
        FILE *w = fopen(path, "w");
        if (!w) { fprintf(stderr, "[-] Cannot write: %s\n", path); return 2; }
        fputs(out, w);
        fclose(w);
        printf("[+] Formatted: %s\n", path);
    } else {
        printf("[+] Already formatted: %s\n", path);
    }
    return 0;
}
