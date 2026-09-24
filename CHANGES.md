# CHANGES.md — NEXUS Ecosystem Changelog

## v5.8 — Mathematics & Scientific Computing Suite (current)

Comprehensive mathematical and scientific computing expansion for discrete math,
linear algebra, physical simulations, AI, fractals, exact fractions, and statistics.
All work verified on Linux x86-64 with `./nexus test` (26 example suites,
13/13 diagnostics, 39/39 foundation tests, 3/3 interactive tests, 94/94 stdlib
assertions) plus 100% bit-for-bit self-hosting convergence (**0 differences
across Gen 2 == Gen 3 == Gen 4**).

### 1. Language: Native Exponentiation Operator (`**`, `**=`)
- **Operator**: `let x = a ** b` desugars into fast $O(\log b)$ binary exponentiation.
- **Compound**: `x **= 3` desugars via `check_compound_op` into `let x = x ** 3`.
- **Zero/Negative Handling**: $a^0 = 1$; negative integer exponents evaluate to 0.

### 2. Standard Library Mathematical Suite
- **Combinatorics (`stdlib/combinatorics.nex`)**:
  - `nx_fact`: Factorials up to $20!$ (with overflow sentinel).
  - `nx_perm_count`: Permutations $P(n, k)$.
  - `nx_comb_count`: Combinations $C(n, k) = \binom{n}{k}$ with multiplicative cancellation.
  - `nx_catalan`: Catalan numbers $C_n$.
  - `nx_derangements`: Subfactorials $!n$.
  - `nx_perm_next`: In-place lexicographical permutation generator (Dijkstra/Pandita algorithm).
- **Set Theory (`stdlib/sets.nex`)**:
  - $O(n + m)$ linear two-pointer set operations on sorted arrays: `nx_set_union`,
    `nx_set_intersect`, `nx_set_diff`, `nx_set_sym_diff`.
  - Predicates: `nx_set_is_subset`, `nx_set_is_equal`, `nx_set_is_disjoint`.
  - Membership: $O(\log n)$ binary search via `nx_set_contains`.
- **Number Theory & Hyperoperations (`stdlib/number_theory.nex`)**:
  - Modular arithmetic: `nx_mod_pow` (binary square-and-multiply), `nx_ext_gcd` (Extended Euclidean algorithm), `nx_mod_inv` (modular inverse).
  - Totient: `nx_totient` (Euler's totient function $\phi(n)$).
  - **Hyperoperations**:
    - `nx_tetrate`: Power tower $a \uparrow\uparrow b$ with 64-bit overflow guard.
    - `nx_mod_tetrate`: Modular tetration $(a \uparrow\uparrow b) \pmod m$ via Euler reduction.
    - `nx_pentate`: Pentation $a \uparrow\uparrow\uparrow b$.
- **Linear Algebra (`stdlib/linalg.nex`)**:
  - Vector math: `nx_vec_dot` (AI neuron dot product), `nx_vec_add`, `nx_vec_sub`, `nx_vec_scale`.
  - 3D Geometry: `nx_vec3_cross` (vector cross product), `nx_vec3_mag_sq`.
  - Matrix math: `nx_mat_mul` ($M \times K \times N$ matrix multiplication), `nx_mat_transpose`.
- **Complex Numbers & Fractals (`stdlib/complex.nex`)**:
  - Coordinates: $(a + bi)$ addition, subtraction, Gaussian integer & scaled multiplication (`nx_cx_mul`), magnitude squared (`nx_cx_mag_sq`).
  - Escape Engine: `nx_mandelbrot_iter` (Mandelbrot escape-time algorithm for real-time fractal rendering).
- **Exact Fraction Arithmetic (`stdlib/rational.nex`)**:
  - Canonical irreducible form via GCD reduction: `nx_rat_create`, `nx_rat_add`, `nx_rat_sub`, `nx_rat_mul`, `nx_rat_div`, `nx_rat_cmp`.
  - 0 floating-point rounding drift.
- **Statistics & Random Numbers (`stdlib/statistics.nex`)**:
  - PRNG: 64-bit SplitMix64 generator: `nx_rand`, `nx_rand_range`, `nx_rand_seed`.
  - Descriptive stats: `nx_stat_mean`, `nx_stat_variance`, `nx_stat_stdev`, `nx_stat_median`.
- **Numerical Calculus (`stdlib/calculus.nex`)**:
  - Numerical differentiation: `nx_diff_central` (central difference derivative).
  - Numerical integration: `nx_simpson_integrate` (Simpson's 1/3 rule).

### 3. Examples & Tests
- New showcase: [`examples/math_ecosystem_showcase.nex`](file:///home/lifelonglearner/nexus_project/examples/math_ecosystem_showcase.nex)
- 9 dedicated test suites: `test_exponentiation.nex`, `test_combinatorics.nex`, `test_sets.nex`, `test_number_theory.nex`, `test_linalg.nex`, `test_complex.nex`, `test_rational.nex`, `test_statistics.nex`, `test_calculus.nex`.

## v5.7 — Array Literals & Bracket Indexing

All work verified on Linux x86-64 with the project's own toolchain. After
every change: `./nexus test` (24 example suites, 13/13 diagnostics,
36/36 foundation tests, 3/3 interactive tests, 94/94 stdlib assertions) plus
a fresh bit-for-bit self-hosting proof — **0 differences across all 49,152
bytes (PE) and 57,951 bytes (ELF)**.

### 1. Array Literals (`tools/nexprep.c`)
- **Inline syntax**: `let arr = [10, 20, 30]` allocates N × 8 bytes on the
  heap and stores each element via `store64`.
- **Multi-line literals**: an opening `[` without a closing `]` on the same
  line accumulates continuation lines until `]` is found.
- Comments are preserved inside multi-line array declarations.
- Expressions in element positions are fully supported (variables, arithmetic
  sub-expressions, float literals).

### 2. Bracket Indexing
- **Read**: `let x = arr[0]` → `load64 [arr + 0]`;
  `let y = arr[i]` → `load64 [arr + i * 8]`.
- **Write**: `arr[0] = 99` → `store64 [arr + 0] 99`;
  `arr[i] = val` → `store64 [arr + i * 8] val`.
- **Compound update**: `arr[0] += 5`, `arr[i] *= 2` — read-modify-write
  using internal temporaries (`_arr_cval_N`).
- **Expressions**: `let dot = arr[0] * brr[0]` — hoists each indexed read to
  a temp (`_a_arr_0`) before the current line.
- **In conditions**: `if arr[i] > 5 {` — same hoisting mechanism.

### 3. Verification & Testing
- Shipped `examples/array_literals.nex` — full showcase (primes, palette RGB,
  loop sum, dot product, float arrays), exits with code 42.
- Shipped `tests/test_array_literals.nex` — 13 assertions covering all syntax
  forms, passes `./nexus test` without regressions.
- Added 5 new foundation tests to the automated test harness (total 36/36 passing).
- Registered `array_literals.nex` in `build_all.sh` Stage 5 batch.
- Zero-cost architecture: **0 bytes added** to `compiler/nexc.nex` machine
  code; self-host convergence remains 100% bit-for-bit identical across Windows
  PE32+ (49,152 bytes) and Linux ELF64 (57,951 bytes).

## v5.6 — Function Parameters & Return Values

All work verified on Linux x86-64 with the project's own toolchain. After
every change: `./nexus test` (23 example suites, 13/13 diagnostics,
29/29 foundation tests, 3/3 interactive tests, 94/94 stdlib assertions) plus
a fresh bit-for-bit self-hosting proof — **Gen 2 == Gen 3 == Gen 4 with 0
differences across all 49,152 bytes (PE) and 57,951 bytes (ELF)**.

### 1. Parameterized Functions (`tools/nexprep.c`)
- **Clean Syntax**: Functions can define parameters within parentheses: `fn <name>(<p0>, <p1>, ...) {`.
- Transparently binds parameters to caller-provided argument values (`_arg_<name>_0`, `_arg_<name>_1`, etc.).
- Backwards compatible with legacy parameterless subroutines (`fn <name> {`).

### 2. Return Values (`return <expr>`)
- Support returning values and expressions directly via `return <expr>` (e.g. `return a + b`, `return 42`, `return a +. b`).
- Early returns inside branching constructs (`if`, `else`, `while`) immediately transfer execution control back to the caller.
- Bare `return` and implicit exit at function closing `}` remain fully supported.

### 3. Call Expressions & Invocations
- **Assignment from Call**: `let <var> = call <name>(<arg0>, <arg1>, ...)` captures return values effortlessly.
- **Void Calls**: `call <name>(<args>)` passes parameters to side-effecting functions.
- **Arbitrary Call Nesting**: Supports arbitrary nested calls such as `call foo(call bar(x), y)`. Hoists nested call results sequentially into temporary variables (`_c_tmp_N`), avoiding parameter register and slot clobbering.
- **Float & Expression Arguments**: Full integration with floating-point math (`call lerp(10.0, 20.0, 0.5)`) and compound arithmetic expressions in arguments.

### 4. Compound Assignment Operators (`tools/nexprep.c`)
- **In-Place Updates**: Direct support for `+=`, `-=`, `*=`, `/=`, `%=` (both `let x += 1` and `x += 1`).
- **Floating-Point Compound Operators**: Full support for `+=.`, `-=.`, `*=.`, `/=.` (e.g. `pos +=. 0.5`).
- **Struct Member Compound Assignment**: In-place member writes `p.x += 10` automatically lower to loaded arithmetic and indexed store.
- **For Loop Headers**: Direct support for compound updates in for loop headers: `for i = 0, i < 10, i += 1 {`.

### 5. Verification & Testing
- Shipped `examples/parameterized_functions.nex` demonstrating function definitions, return values, clamp logic, multi-argument math, float operations, and nested calls.
- Shipped `tests/test_fn_params.nex` and `tests/test_compound_ops.nex` comprehensive verification suites.
- Added 6 new foundation tests to the automated test harness (total 31/31 passing).
- Zero-cost architecture: 0 bytes added to `compiler/nexc.nex` machine code, keeping self-host convergence 100% bit-for-bit identical across Windows PE32+ (49,152 bytes) and Linux ELF64 (57,951 bytes).

## v5.5 — Floating-Point Arithmetic (x86-64 SSE2) — Systems & Science

All work verified on Linux x86-64 with the project's own toolchain. After
every change: `./nexus test` (22 example suites, 13/13 diagnostics,
25/25 foundation tests, 3/3 interactive tests, 94/94 stdlib assertions) plus
a fresh bit-for-bit self-hosting proof — **Gen 2 == Gen 3 == Gen 4 with 0
differences across all 49,152 bytes (PE) and 57,951 bytes (ELF)**.

### 1. Native x86-64 SSE2 Floating-Point Primitives (`compiler/nexc.nex`)

- **SSE2 Scalar Math Instructions**:
  - `fadd <op1> <op2>`: `addsd xmm0, xmm1` (`F2 0F 58 C1`)
  - `fsub <op1> <op2>`: `subsd xmm0, xmm1` (`F2 0F 5C C1`)
  - `fmul <op1> <op2>`: `mulsd xmm0, xmm1` (`F2 0F 59 C1`)
  - `fdiv <op1> <op2>`: `divsd xmm0, xmm1` (`F2 0F 5E C1`)
  - `fsqrt <op>`: `sqrtsd xmm0, xmm0` (`F2 0F 51 C0`)
  - `fneg <op>`: In-place floating-point negation using `xorpd` (`66 0F 57 C9`) and `subsd` (`F2 0F 5C C8`).
- **Data Type Conversions**:
  - `itof <op>`: Convert 64-bit integer to 64-bit double precision float via `cvtsi2sd xmm0, rax` (`F2 48 0F 2A C0`).
  - `ftoi <op>`: Convert 64-bit double precision float to 64-bit integer via `cvttsd2si rax, xmm0` (`F2 48 0F 2C C0`).
- **Float Literals**:
  - Lexical parsing of decimal float literals (`3.14159`, `-2.5`, `0.001`) directly in source code.
  - Dynamically synthesizes IEEE 754 64-bit float via integer-fractional division using SSE2 registers (`cvtsi2sd` + `divsd` + `addsd`).
- **Floating-Point Conditions & Loops**:
  - `if_f`, `while_f`, `else if_f` condition forms comparing 64-bit float operands via `ucomisd xmm0, xmm1` (`66 0F 2E C1`).
  - Supports all standard relational operators (`==`, `!=`, `<`, `<=`, `>`, `>=`).
- **Runtime Helper**:
  - Native `print_float` runtime routine assembled and integrated into `compiler/template.bin` at offset `0xA940` (`43328`), formatting 6-decimal-place ASCII floats directly to `stdout`.

### 2. Preprocessor Infix Syntax Sugar (`tools/nexprep.c`)

- Infix float arithmetic sugar:
  - `let z = a +. b` -> `let z = fadd a b`
  - `let z = a -. b` -> `let z = fsub a b`
  - `let z = a *. b` -> `let z = fmul a b`
  - `let z = a /. b` -> `let z = fdiv a b`
  - Multi-operand chaining: `let z = a +. b *. c` desugars into hoisted temporary intermediate operations.
- Infix float relational sugar:
  - `if a <. b {` -> `if_f a < b {`
  - `if a <=. b {` -> `if_f a <= b {`
  - `if a >. b {` -> `if_f a > b {`
  - `if a >=. b {` -> `if_f a >= b {`
  - `if a ==. b {` -> `if_f a == b {`
  - `if a !=. b {` -> `if_f a != b {`
  - Transparently desugars in `if`, `while`, and `else if` statements while preserving string literals and comments untouched.

### 3. Verification, Self-Hosting & Applications

- **Streamlined Compiler Implementation**: Fits within the strict 43,008-byte code boundary of `template.bin` (compiled size 40,803 bytes, leaving 2,205 bytes of safety headroom before `print_int` at offset 43,008).
- **Self-Hosting Bit-for-Bit Parity**: Gen 2 == Gen 3 == Gen 4 confirmed with 0 byte differences across 49,152 bytes (PE32+) and 57,951 bytes (ELF64).
- **Diagnostics & Regression Suite**: 13/13 diagnostics pass; 25/25 foundation tests pass (including 5 new SSE2 float tests); 22/22 example suites pass; 3/3 interactive tests pass.
- **Precision Physics Showcase**: Added `examples/float_physics.nex` simulating Earth orbital mechanics (escape velocity, LEO circular velocity) and 2D ballistic numerical integration with kinetic energy calculations.

## v5.4 — Self-Hosted Interactive Console Input ('read <var>')

All work verified on Linux x86-64 with the project's own toolchain. After
every change: `./nexus test` (21 example suites, 13/13 diagnostics,
20/20 foundation tests, 3/3 interactive tests, 94/94 stdlib assertions) plus
a fresh bit-for-bit self-hosting proof — **Gen 2 == Gen 3 == Gen 4 with 0
differences across all 49,152 bytes (PE) and 57,951 bytes (ELF)**.

### 1. Language & Compiler: `read <var>` (compiler, `nexc.nex`)

- Re-implemented `read <var>` in the self-hosting compiler `compiler/nexc.nex`.
- Emits call to in-binary runtime helper `read_int` at offset `0xA880` (`43136`)
  via relative displacement `43132 - p`.
- Preserves stream pipelining: `read_int` reads standard input byte-by-byte
  via Win32 `ReadFile` / Linux `sys_read(0, ...)` without consuming unread tokens,
  accumulating signed 64-bit integers with sign-flag negation.
- Stores parsed integer result from `RAX` directly into local variable frame slot
  `[rbp + d]` (`48 89 85 <d32>`).
- Supports multi-letter variables and subroutine variables seamlessly.

### 2. Diagnostics: Statement Error Code `w = 11`

- Added exact diagnostic validation: `'Expected variable name in read statement'` (w=11).
- Validates identifier start characters (`a-z`, `A-Z`, `_`); rejects empty tokens
  or numbers (`read 123`) with line & column reporting, gating binary generation.

### 3. Verification & Dual Target Execution

- Updated `build_all.sh` Step 4: compiles `interactive_calc.nex` and `prime_checker.nex`
  with the production self-hosted compiler `nexc.exe` (retiring bootstrap binary).
- Updated `build_all.sh` Step 10B: verified direct native Linux kernel ELF64 execution
  of interactive calculations and prime testing with 0% loader.
- Added 3 interactive console regression tests to `./nexus test` (`interactive_calc`,
  `prime_checker` 17=PRIME, 24=COMPOSITE).

## v5.3 — Loop Control, Incremental Builds & Native Testing

All work verified on Linux x86-64 with the project's own toolchain. After
every change: `./nexus test` (17 example suites, 11/11 diagnostics,
20/20 foundation tests, 94/94 stdlib assertions) plus a fresh bit-for-bit
self-hosting proof — **Gen 2 == Gen 3 == Gen 4 with 0 differences across
all 49,152 bytes (PE) and 57,951 bytes (ELF)**.

### 1. Language: `break`, `continue`, `for` (compiler, `nexc.nex`)

- `break` / `continue` bind to the innermost enclosing `while`/`for`,
  never cross function boundaries, and are implemented with linked jump
  chains (same back-patching machinery as `else if` chains) stored in new
  compiler scratchpad arrays at `m + 6144 / 6656 / 7168`.
- `for` uses C-style headers with **comma separators**
  (`for i = 0, i < 10, i = i + 1 {`) — `;` is a line comment in NEXUS and
  cannot appear in a header. init/cond/update reuse the shared
  `parse_cond_core` / `parse_set` machinery; the update segment is
  re-parsed at loop close via source rewind.
- New diagnostics: `'break' outside of a loop` (w=8), `'continue' outside
  of a loop` (w=9), `Expected ',' in for header` (w=10). All gated.
- New/changed subroutines: `parse_cond_core`, `find_loop`, `chain_jump`,
  `patch_list`, `parse_set`, plus factored `ld_rax`/`ld_rcx`/`ld_rdx` and
  `fail_compile` helpers.

### 2. Binary layout: output enlarged 32,768 -> 49,152 bytes

The self-hosting compiler's own code outgrew the old fixed 32 KiB template
(only ~828 bytes of headroom). The output image is now 49,152 bytes with
`.text` raw size 0xAE00 and `.rdata` moved to file offset 0xB000 / RVA
0xC000; the `print_int`/`read_int` helpers relocated from 0x6E80 to 0xA800.
Coordinated updates: `template.bin` (regenerated), `elf_parts.bin` (ELF
entry 0x40E000, filesz 57,951, patched syscall-stub relocator),
`tools/nexelf.c` (size checks, stub placement, regenerated `g_stubs`),
and binary-patched bootstrap bridge `compiler/nexc_bridge.exe` (kept in
tree for provenance). `tools/nexload.c` needed no change (fully
header-driven). **Ripple fix:** numeric memory indices in `parse_mem_addr`
now honour `* n` scaling (`store64 [p + 2 * 8]`), which previously only
worked with variable indices.

### 3. Build system: incremental `nexus build <file.nex>` (new)

- Dependency graph via new `bin/nexprep --list-deps` (recursive canonical
  include list); fingerprint = SHA-256 over mtimes+sizes of source,
  includes, template, elf_parts and the compiler binaries.
- Cache in `<source-dir>/.nexuscache`; unchanged inputs print
  `[up-to-date]` and skip compilation entirely (~1-2 ms). `--clean`
  removes outputs + cache. Outputs land next to the source as
  `<name>.exe` / `<name>.elf`.
- `./nexus build` without arguments still runs the full 10-stage master
  pipeline (unchanged semantics).

### 4. Native test runner: `bin/nextest` + `nexus test --file` (new)

- `tools/nextest.c` (pure C, zero deps): preprocesses, compiles, executes
  and validates each test file; counts `[FAIL]` lines, parses the
  `ASSERTIONS: <n> passed, <m> failed` summary, supports `--quiet` and
  `--fail-fast`; removes stale outputs so a gated compile can never run
  leftovers.
- `tools/nexprep.c` v3 adds `assert_eq <A>, <B>` / `assert_ne <A>, <B>`
  macros expanded into counting compare-and-report blocks.
- `stdlib/assert.nex`: `nx_assert_passes` / `nx_assert_fails` counters and
  `nx_assert_summary` (single-line report built with `nx_str_concat` /
  `nx_int_to_str`).
- Test files: `tests/loop_control_test.nex` (break/continue/for/nested),
  `tests/array_ops_test.nex` (byte + qword arrays incl. sort).

### 5. Standard library growth

- `stdlib/qarrays.nex` (NEW): 10 qword-array functions over native
  `load64`/`store64` scaled indexing — `nx_qarr_fill`, `nx_qarr_iota`,
  `nx_qarr_sum`, `nx_qarr_max`, `nx_qarr_min`, `nx_qarr_index_of`,
  `nx_qarr_reverse`, `nx_qarr_count`, `nx_qarr_sort` (insertion sort),
  `nx_qarr_avg`.
- `stdlib/arrays.nex`: added `nx_arr_sort` and `nx_arr_avg`.
- `stdlib/nstdlib.nex`: master include now pulls in qarrays + assert.

### 6. Tooling & docs

- `./nexus test` diagnostics grew to 11 (break/continue-outside-loop,
  for-header comma); foundation suite grew to 20 (for accumulate, break,
  continue, store64/load64 numeric scaling, stdlib sort + assert).
- VS Code grammar: `break`/`continue`/`for` keywords.
- Docs: LANGUAGE_REFERENCE (5.4-5.6), TUTORIAL (Step 4 + testing note),
  README, ECOSYSTEM_ROADMAP updated.
- Known gap documented: `read <var>` appears in examples/docs but is not
  yet implemented in the current `nexc.nex` dispatcher (interactive
  examples are skipped by the suite). Logged in the roadmap for v5.4.

---

## 1. Standard library (`stdlib/`) — NEW

A source-level library loaded through the existing preprocessor: zero
compiler changes, zero risk to self-hosting parity.

- `stdlib/math.nex` — `nx_max`, `nx_min`, `nx_clamp`, `nx_sign`, `nx_pow`,
  `nx_isqrt`, `nx_gcd`, `nx_lcm`, `nx_fib`, `nx_is_prime`, `nx_digits`
- `stdlib/arrays.nex` — `nx_arr_fill`, `nx_arr_iota`, `nx_arr_sum`,
  `nx_arr_max`, `nx_arr_min`, `nx_arr_index_of`, `nx_arr_reverse`,
  `nx_arr_count` (byte arrays over `alloc` buffers)
- `stdlib/strings.nex` — `nx_str_len`, `nx_str_copy`, `nx_str_cmp`,
  `nx_str_concat`, `nx_str_chr`, `nx_str_to_int`, `nx_int_to_str`
- `stdlib/nstdlib.nex` — master include (one line loads everything)

Design notes:
- Documented global calling convention: inputs `nx_i0..nx_i2`, result
  `nx_r0`, scratch `nx_s0..nx_s7` clobbered per call. Only 12 of the 120
  variable slots consumed by the entire library.
- Every stdlib function is a leaf (no stdlib-internal calls), so the shared
  scratch pool is safe between calls.
- Guards the ecosystem against the two classic global-only pitfalls:
  expression-free conditions in every branch, and one-operation-per-statement
  decomposition (NEXUS has no operator precedence or parentheses).

## 2. Preprocessor v2 (`tools/nexprep.c`, `bin/nexprep`) — REWRITTEN

The preprocessor previously shipped as a binary without source. Now:

- Clean-room C implementation checked into `tools/` (the `nexus` driver
  auto-rebuilds it from source).
- **Per-object struct typing**: `obj.field` resolves through the object's
  allocation type (`let o = alloc Name.size`), fixing silent field-offset
  collisions between structs that reuse field names (old behavior: one
  global first-wins map).
- **Include resolution hardened**: including-file-relative first, then CWD,
  then every ancestor directory — `include "stdlib/nstdlib.nex"` now works
  from any subdirectory and from any working directory.
- **Circular include detection** with `# --- SKIP INCLUDE ---` markers.
- Byte-identical output verified against the previous binary for the full
  struct and multi-file example surface (`examples/struct_demo.nex`,
  `examples/multi_file_demo.nex`).

## 3. Developer tooling (`./nexus` driver) — EXTENDED

- `./nexus repl` — stateful interactive session (`tools/nexrepl.sh`):
  recompiles the running session per line via the self-hosted compiler,
  shows only new output, rejects broken lines without losing state,
  block accumulation across lines, `:help :clear :history :save :stdlib :quit`,
  stdlib preloaded (`:stdlib off` to disable).
- `./nexus new <name>` — project scaffold: `main.nex`, vendored `stdlib/`
  copy, `nexus.project` manifest, self-contained `build.sh`, README.
- `./nexus fmt <file>` — source formatter (`tools/nexfmt.c`): stable 4-space
  indentation, tab expansion outside strings, trailing-whitespace removal,
  blank-line collapsing; idempotent (`fmt(fmt(x)) == fmt(x)`), `--check`
  and `--diff` modes.
- `./nexus test` — now also runs the stdlib suite and reports it.

## 4. Documentation (`docs/`) — NEW

- `docs/LANGUAGE_REFERENCE.md` — complete grammar: lexical rules, every
  statement form, expression semantics (left-to-right, no precedence, no
  parentheses), condition grammar (`var op var|num` ONLY — with a warning
  about the silent-miscompile trap), memory model, file I/O, include
  resolution, structs, limits and gotchas (auto-vivifying variables, 32-bit
  literals, byte-only memory, CRLF output).
- `docs/TUTORIAL.md` — 10-step hands-on from hello world to the stdlib,
  every snippet compiled and executed before publication (480, 55, 45,
  256, 70, 8, 21).
- `docs/BUILTINS.md` — compiler built-ins + full stdlib API with the
  calling convention; worked example verified (15 primes below 50).

## 5. Editor support (`editors/vscode-nexus/`) — NEW

VS Code extension: `.nex` language registration, TextMate grammar covering
keywords, built-ins, stdlib functions, struct member access, strings,
comments, numbers, operators; comment/bracket/auto-close configuration;
`~/.vscode/extensions` and `vsce package` install paths documented.

## 6. Tests and examples

- `tests/stdlib_test.nex` — NEW: 94 assertions covering every stdlib
  function's contract including edge cases (nx_pow negative exponent,
  nx_isqrt perfect squares/non-squares/negative, nx_gcd(0,0), primes
  7917/7919, empty strings, int<->str round-trips).
- `examples/stdlib_demo.nex` — NEW: runnable tour of all three stdlib modules.
- `examples/prime_sieve.nex` — NEW: Eratosthenes sieve (prints 25 primes
  below 100), doubles as the LANGUAGE_REFERENCE capstone example.

## 7. Repository hygiene

- Bootstrap chain binaries (`nexc2/3/4.exe`, `nexc_bootstrap.exe`,
  `mini_nexc.exe`) retained for the build pipeline; historical `.bak`,
  `.stage*`, `.step*` snapshots of `nexc.nex` remain untouched.
- `README.md` gained "The NEXUS Ecosystem" section.
- `ECOSYSTEM_ROADMAP.txt` annotated with per-item completion status.

## 8. Condition Expression Trap & Compiler Diagnostics Fixed (v5.2)

- **Strict Condition Validation (`compiler/nexc.nex`):**
  - Conditions previously permitted silent miscompilations on compound expressions (e.g. `if a * a > n {` emitted `if a != 0` and skipped remaining tokens).
  - Enforced strict validation of relational operators (`==`, `!=`, `<`, `<=`, `>`, `>=`). Single `=` (assignment in condition) and non-relational operators immediately fail.
  - Enforced strict line-terminating `{` requirement after condition heads. Missing `{`, compound `and`/`or`, or trailing garbage expressions trigger an immediate compile error.
  - Gated compilation: on condition error, binary generation is suppressed (`app.exe` is not produced) and line/column coordinates are reported.
  - Diagnostics error codes added:
    - Code 6: `[!] Error: Invalid relational operator in condition`
    - Code 7: `[!] Error: Expected '{' after condition`
- **Self-Hosting Parity Maintained:**
  - Machine code footprint carefully kept below the 28,288 byte PE code ceiling (~680 bytes of safety headroom remaining).
  - Bitwise convergence verified: `cmp -l nexc3.exe nexc4.exe` shows **0 differences across all 32,768 bytes**.
  - Promoted across all active bootstrap and standalone compiler binaries (`nexc.exe`, `nexc3.exe`, `nexc4.exe`, `nexc.elf`).
- **Test Coverage:**
  - Added Condition Expression Trap, Missing `{` in Condition, and Single `=` in Condition test cases to `./nexus test` (8/8 diagnostics passing) and `build_all.sh` (7/7 diagnostics passing).

## 9. Native 64-Bit Memory Operations (`load64` / `store64`) (v5.3)

- **Native Quadword Memory Semantics (`compiler/nexc.nex`):**
  - Added native 64-bit memory load: `load64 [<ptr> + <idx>]` (`49 8B 00`, `mov rax, [r8]`).
  - Added native 64-bit memory store: `store64 [<ptr> + <idx>] <val>` (`49 89 00`, `mov [r8], rax`).
  - Preserved existing 8-bit byte memory operations: `load` (`49 0F B6 00`, `movzx rax, byte ptr [r8]`) and `store` (`41 88 00`, `mov byte ptr [r8], al`).
  - Factored memory address parser into dedicated `fn parse_mem_addr`, deduplicating 124 lines across load and store handlers while ensuring safe register preservation without clobbering caller slots.
- **Scaled Indexing (`* 8`):**
  - Native variable offsets support scaled indexing `* 8` directly in address brackets: `[<ptr> + <idx> * 8]`.
  - Emits `shl r9, 3` (`49 C1 E1 03`) and `add r8, r9` (`4D 01 C8`).
- **Chained Load Expressions:**
  - Integrated `load` / `load64` directly into the arithmetic expression loop (`Case 0`), allowing expressions like `let k = load [p + 0] + 10 * 2` to evaluate without intermediary variables.
- **Self-Hosting Parity & Footprint Verification:**
  - Emitted machine code size safely below the 28,288 byte PE code ceiling (offset 27,966 with +322 bytes headroom).
  - Bit-for-bit convergence verified across Windows PE32+ and Linux ELF64: `0 differences across 32,768 bytes` in both `cmp -l nexc3.exe nexc4.exe` and `cmp -l app.elf nexc.elf`.
  - Promoted to production compiler binaries (`nexc.exe`, `nexc3.exe`, `nexc4.exe`, `nexc.elf`).
- **Test Coverage:**
  - Added test cases in `examples/master_test_suite.nex` covering 64-bit immediate offset stores/loads (`123456789`), scaled index operations (`777`), and chained load expressions (`104`). All 27 regression points verified.

## Verification summary

| Gate | Result |
|------|--------|
| `./nexus build` | 10/10 stages PASS, self-hosting parity 0 byte diffs (PE & ELF) |
| `./nexus test` | 17 example suites, 8/8 diagnostics, 15/15 foundation, 94/94 stdlib |
| fmt idempotency | md5-stable across stdlib/test/example files |
| REPL smoke | stateful vars, functions, stdlib calls, rejection path, :save |

## Known limitations intentionally left for a future stage

- No incremental build system / dependency tracking (`nexus.project` is a
  manifest only) — roadmap item 6 stays PARTIAL.
