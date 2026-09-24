/*
 * nextest.c - NEXUS Native Test Runner
 * ===========================================================================
 * Compiles and runs .nex test programs and validates their assertion output.
 *
 * Test convention:
 *   - A test is a normal .nex program (usually including stdlib/nstdlib.nex).
 *   - Use the assert_eq / assert_ne preprocessor macros or print "[FAIL]"
 *       lines manually.
 *   - Optional summary line: "ASSERTIONS: <n> passed, <m> failed"
 *       (printed by fn nx_assert_summary in stdlib/assert.nex).
 *
 * A test PASSES when:
 *   - the program compiles,
 *   - it exits with code 0,
 *   - its output contains no "[FAIL]" lines,
 *   - and (when a summary line is present) failed == 0 and
 *     the pass count is greater than zero.
 *
 * Usage:
 *   nextest [options] <test.nex> [more.nex ...]
 * Options:
 *   --quiet         only print failures and the final summary
 *   --fail-fast     stop at the first failing test
 *   --toolchain DIR project root (default: .., i.e. run from project root)
 *
 * Exit code: 0 when every test passes, 1 otherwise.
 *
 * Build:  gcc -O2 -o bin/nextest tools/nextest.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>

static char root[4096];
static const char *prep_bin = "bin/nexprep";
static const char *compiler_elf = "compiler/nexc.elf";
static const char *compiler_pe = "compiler/nexc.exe";
static const char *loader_bin = "bin/nexload";

static int quiet = 0;
static int fail_fast = 0;

static int exists(const char *p) {
    struct stat st;
    return stat(p, &st) == 0;
}

/* run a command: returns exit code; stdout captured into out (malloc'd), stderr inherited */
static char *run_capture(char *const argv[], int *rc) {
    int pipefd[2];
    if (pipe(pipefd) != 0) return NULL;
    pid_t pid = fork();
    if (pid < 0) return NULL;
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], 1);
        close(pipefd[1]);
        execvp(argv[0], argv);
        _exit(127);
    }
    close(pipefd[1]);
    size_t cap = 8192, len = 0;
    char *out = malloc(cap);
    ssize_t n;
    while ((n = read(pipefd[0], out + len, cap - len - 1)) > 0) {
        len += (size_t)n;
        if (cap - len < 256) { cap *= 2; out = realloc(out, cap); }
    }
    close(pipefd[0]);
    out[len] = '\0';
    int status;
    waitpid(pid, &status, 0);
    *rc = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return out;
}

/* strip \r and count failures + find summary line */
static void analyze(char *out, int *fail_lines, long *passed, long *failed, int *has_summary) {
    *fail_lines = 0; *passed = 0; *failed = 0; *has_summary = 0;
    char *line = out;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        /* strip \r */
        size_t L = strlen(line);
        while (L > 0 && line[L-1] == '\r') line[--L] = '\0';
        if (strstr(line, "[FAIL]")) (*fail_lines)++;
        if (!strncmp(line, "ASSERTIONS:", 11)) {
            *has_summary = 1;
            /* format: "ASSERTIONS: <n> passed, <m> failed" */
            *passed = strtol(line + 11, NULL, 10);
            char *p = strchr(line, ',');
            if (p) *failed = strtol(p + 1, NULL, 10);
        }
        line = nl ? nl + 1 : NULL;
    }
}

static int run_test(const char *path) {
    char compiler_dir[8192];
    snprintf(compiler_dir, sizeof compiler_dir, "%s/compiler", root);

    /* 0. remove stale outputs so a failed compile cannot execute leftovers */
    {
        char p1[8192], p2[8192];
        snprintf(p1, sizeof p1, "%s/compiler/app.exe", root);
        snprintf(p2, sizeof p2, "%s/compiler/app.elf", root);
        unlink(p1); unlink(p2);
    }

    /* 1. preprocess (assert macros, includes, structs) */
    char *prep_argv[] = { (char *)prep_bin, (char *)path, (char *)"compiler/code.nex", NULL };
    int rc;
    free(run_capture(prep_argv, &rc));
    if (rc != 0) {
        printf("[-] %-40s FAIL (preprocess)\n", path);
        return 0;
    }

    /* 2. compile (from the compiler dir: it reads code.nex in cwd) */
    if (chdir(compiler_dir) != 0) { perror("[-] compiler dir"); return 0; }
    char *cargv[3];
    if (exists("nexc.elf")) {
        cargv[0] = (char *)"./nexc.elf";
        cargv[1] = NULL;
    } else {
        cargv[0] = (char *)"../bin/nexload";
        cargv[1] = (char *)"nexc.exe";
    }
    cargv[2] = NULL;
    int crc;
    char *cout = run_capture(cargv, &crc);
    free(cout);
    if (!exists("app.elf")) {
        printf("[-] %-40s FAIL (compile, output gated)\n", path);
        chdir(root);
        return 0;
    }

    /* 3. execute (cwd stays in the compiler dir) */
    char *run_argv[] = { (char *)"./app.elf", NULL };
    int erc;
    char *out = run_capture(run_argv, &erc);

    /* 4. analyze (keep an untouched copy for [FAIL] echoing) */
    char *orig = out ? strdup(out) : NULL;
    int fail_lines = 0, has_summary = 0;
    long passed = 0, failed = 0;
    if (out) analyze(out, &fail_lines, &passed, &failed, &has_summary);

    int pass = (erc == 0) && (fail_lines == 0) && (!has_summary || (failed == 0 && passed > 0));
    if (pass) {
        if (!quiet) printf("[+] %-40s PASS (%ld assertions)\n", path, has_summary ? passed : 0L);
    } else {
        printf("[-] %-40s FAIL", path);
        if (erc != 0) printf(" exit=%d", erc);
        if (fail_lines) printf(" fail_lines=%d", fail_lines);
        if (has_summary && failed) printf(" summary_failed=%ld", failed);
        printf("\n");
        if (orig) {
            /* print [FAIL] lines for diagnosis */
            char *line = orig;
            while (line && *line) {
                char *nl = strchr(line, '\n');
                if (nl) *nl = '\0';
                if (strstr(line, "[FAIL]")) printf("      %s\n", line);
                line = nl ? nl + 1 : NULL;
            }
        }
    }
    chdir(root);
    free(out);
    free(orig);
    return pass;
}

int main(int argc, char **argv) {
    /* resolve toolchain root: default = parent of cwd (run from project root:
       root = cwd). Accept --toolchain DIR override. */
    if (!getcwd(root, sizeof root)) return 1;
    const char *files[256];
    int nfiles = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--quiet")) quiet = 1;
        else if (!strcmp(argv[i], "--fail-fast")) fail_fast = 1;
        else if (!strcmp(argv[i], "--toolchain")) {
            if (++i >= argc) { fprintf(stderr, "[-] --toolchain needs a directory\n"); return 1; }
            snprintf(root, sizeof root, "%s", argv[i]);
        }
        else if (argv[i][0] == '-') { fprintf(stderr, "[-] unknown option %s\n", argv[i]); return 1; }
        else if (nfiles < 256) files[nfiles++] = argv[i];
    }
    if (nfiles == 0) {
        fprintf(stderr, "NEXUS Native Test Runner\n");
        fprintf(stderr, "Usage: nextest [--quiet] [--fail-fast] <test.nex> [more.nex ...]\n");
        return 1;
    }

    if (chdir(root) != 0) { perror("[-] toolchain root"); return 1; }

    int npass = 0;
    for (int i = 0; i < nfiles; i++) {
        if (run_test(files[i])) npass++;
        else if (fail_fast) break;
    }
    printf("\nnextest: %d/%d test file(s) passed\n", npass, nfiles);
    return npass == nfiles ? 0 : 1;
}
