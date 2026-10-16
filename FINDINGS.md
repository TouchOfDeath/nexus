# NEXUS — Bug Investigation Findings (v8.5)

## BUG 1 — `./nexus self-host` reported ELF mismatch (FALSE ALARM, then REAL)
**Symptom:** reported 28,568 / 28,754 byte differences on the Linux ELF branch.
**Cause:** the `self-host` subcommand copies `compiler/nexc.nex` over `compiler/code.nex`
and never restores it. After any `./nexus run`/`test`/`build`, `code.nex` holds a
*different program*, so the first `self-host` run compiles the wrong source.
**Fix needed:** make `self-host` self-contained — save/restore `code.nex`, or operate
on a temp directory. Verified: with a restored `code.nex`, `self-host` reports
**0 differences** on both PE32+ (49,152 B) and ELF64 (57,951 B).

## BUG 2 — Compiler CLI always claims success (exit code 0)
`nexc.elf` / `nexc.exe` print `[+] Compilation Succeeded!` and exit 0 **even when
compilation failed and no binary was produced**. Build scripts that trust the exit
code silently proceed with a stale/absent artifact.
**Fix needed:** emit the failure message and exit non-zero when the gate
(`[m + 1024] != 0`) is set.

## BUG 3 — Diagnostic messages are truncated
`report_err` prints `[!] Line:` but the line number, column and the actual error
text are missing, even though `nexc.nex` contains correct `print u`, `print a` and
11 error strings. Traced with `strace`: `write(1, "[!] Line:\r\n", 11)` — the
subsequent `print <var>` statements emit nothing. The app.exe's own PE IAT slots
(RVA 0xC028..0xC058) are **all zero** in the template, so `call [rip+disp]` through
them would fault; the ELF bootstrap stub patches them, but the values written are
wrong for the diagnostic path.
**Impact:** all 13 compiler diagnostics in `./nexus test` report `0/13`, and the
`hashset` toolchain note about a "Condition Expr Trap" is a symptom of this, not a
language limitation.
**Fix needed:** root-cause the `print <var>` codegen (`call print_int` at RVA 0xB600)
in `compiler/nexc.nex` / `tools/nexprep.c`.

## BUG 4 — Operator precedence is broken in emitted code (confirmed, real)
Preprocessor folds pure-literal chains correctly (`let c = 4 + 2 * 3` -> `18`,
left-to-right per `eval_int_chain`), but **mixed variable/literal chains** are
emitted strictly left-to-right with no precedence:

| Source | NEXUS | Correct |
|---|---|---|
| `let c = 4 + 2 * 3` | 10 | 10 |
| `let c = 1 + 2 * 3` | 9 | 7 |
| `let c = 10 - 2 * 3` | 24 | 4 |
| `let c = 1 + 2 * 3 + 4` | 13 | 11 |
| `let c = counter + velocity * step_size` | 39050 | 39050 (coincidence) |

**Where:** the operator loop in `compiler/nexc.nex` (~line 2573-2860) loads Op1 into
`rax` then applies each following operator immediately. There is no precedence
climbing and no temporary stack slots.
**Impact:** HIGH. Silently wrong arithmetic for any expression mixing `+`/`-` with
`*`/`/`. The 48 "foundation tests" do not cover this; `counter + velocity * step_size`
happens to match because left-to-right gives `(10+7800)*5 = 39050` and the "expected"
value was recorded from the buggy output.
**Fix needed:** implement precedence-aware expression parsing (two-level: additive
over multiplicative) with a temp slot for the multiplicative sub-result.

## BUG 5 — `./nexus build <file> -o <out>` is unsupported
The usage text advertises `-o`, but `build` treats the third argument as a flag
(`--clean` only) and always writes `<base>.exe` / `<base>.elf` next to the source.

## BUG 6 — `nexelf` silently accepts a broken input
`tools/nexelf.c` calls `chmod(out_path, 0755)` without `#include <sys/stat.h>`'s
`chmod` prototype under strict C, and it never validates the *output*. Also
`g_stubs[]` copies 0xAE00 bytes from 0x40AFFF to 0x40BDFF (overlapping forward
move) — benign on glibc mmap but relies on undefined `rep movsb` overlap behaviour.

## Verified working (no bug)
- `./nexus test` example/foundation/stdlib/optimizer suites all pass.
- `./nexus bench` — 71.7 ms / ~698M physical-iters/sec.
- `./nexus hydron` — 0.42 ms, 150.7x.
- Tri-platform emission (PE32+ / ELF64 / Mach-O) produces valid headers.
- The self-hosted ELF bootstrap correctly re-locates the image by +0xE00 and patches
  the IAT slots for `GetStdHandle`/`WriteFile`/`ExitProcess`/`ReadFile`/
  `VirtualAlloc`/`CreateFileA`/`CloseHandle`.
