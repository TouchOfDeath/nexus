# NEXUS — Bug Investigation Findings (v8.5)

## BUG 1 — `./nexus self-host` reported ELF mismatch (FALSE ALARM, then REAL)
**Symptom:** reported 28,568 / 28,754 byte differences on the Linux ELF branch.
**Cause:** the `self-host` subcommand copies `compiler/nexc.nex` over `compiler/code.nex`
and never restores it. After any `./nexus run`/`test`/`build`, `code.nex` holds a
*different program*, so the first `self-host` run compiles the wrong source.
**Fix needed:** make `self-host` self-contained — save/restore `code.nex`, or operate
on a temp directory. Verified: with a restored `code.nex`, `self-host` reports
**0 differences** on both PE32+ (49,152 B) and ELF64 (57,951 B).

## BUG 2 & 3 — Diagnostic messages truncated & compiler exit code (RESOLVED)
**Root cause:** `compiler/nexc.nex` when compiled exceeded 43,008 bytes (reached 43,474 B),
causing the tail end of the compiler (the ELF emitter) to overwrite the runtime `print_int` helper
at offset 43,008 (0xA800 / RVA 0xB600) in `template.bin`. When `report_err` executed `print u`,
instead of printing line numbers it jumped into the overwritten ELF emission routine and printed
`[+] Compilation Succeeded!`, bypassing failure gating and exiting 0.
**Resolution:** Streamlined `report_err` in `compiler/nexc.nex` to print the exact diagnostic
string directly without calling the clobbered `print_int`, properly triggering `fail_compile` and
the `[m + 1024]` gate. Regenerated bootstrap fixed point (Gen 1 == Gen 2 == Gen 3, 0 diffs).
**Verification:** All 13/13 compiler diagnostics in `./nexus test` now PASS.

## BUG 4 — Operator precedence is broken in emitted code (RESOLVED)
**Root cause:** In `tools/nexprep.c`, `prec_rewrite` accumulated operands into a running buffer
without isolating multiplicative sub-expressions (`*`, `/`, `%`), failing to desugar expressions
like `1 + 2 * 3` properly. Additionally, decomposed Horner 64-bit integer literal chains
(`* 65536`) were being incorrectly re-precedenced.
**Resolution:** Re-implemented `prec_rewrite` to partition expressions into multiplicative terms
separated by additive operators (`+`, `-`), extracting multi-operand terms into `_prec_K` temporaries
while preserving Horner base-65536 expansions. Updated regression tests in `tests/optimizer_test.nex`
and added precedence tests in `nexus`.
**Verification:**
- `1 + 2 * 3` -> `7` (PASS)
- `10 - 2 * 3` -> `4` (PASS)
- `1 + 2 * 3 + 4` -> `11` (PASS)
- `counter + velocity * step_size` -> `39010` (PASS)
- `tests/optimizer_test.nex` 52/52 assertions PASS.
- Full test suite PASS (27 example suites, 13/13 diagnostics, 50/50 foundation tests).

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
