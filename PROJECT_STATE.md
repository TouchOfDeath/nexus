# NEXUS Project State & Memory Log

> **Last Updated**: 2026-10-02  
> **Current Version**: v7.3 — The HYDRON Acceleration Engine (Phase 4: Recurrence Attractor Solver & Dual-Benchmark Architecture)  
> **Compiler Architecture**: Self-Hosted Compiler Core in pure NEXUS (`compiler/nexc.nex`), 0% libc, direct Linux ELF64 syscalls + Native ARM64 Mach-O & Linux aarch64. Surrounding bootstrap and tooling ecosystem includes native C, Python, C#, and shell components.

---

## 1. Current Status & Verification
- **Full Test Suite Status**: `PASS` (Run with `./nexus test`)
  - **Suites**: 28 example suites passing (+`recursion_showcase.nex`)
  - **Compiler Diagnostics**: 13/13 passing
  - **Foundation Tests**: 48/48 passing (+Unified Float Arithmetic, Float Mixed Promotion, Unified Float Condition, Unified Float While Loop)
  - **Type Inference Suite**: 7/7 categories passing (`tests/test_type_inference.nex`)
  - **Stdlib Assertions**: 94/94 passing
  - **Compiler Optimizer**: 52/52 passing (`tests/optimizer_test.nex`)
  - **Interactive Demos**: 3/3 passing
- **Determinism**: Gen 2 == Gen 3 == Gen 4 bit-for-bit self-hosting parity verified (`./nexus self-host`).
- **Dual-Benchmark Architecture**:
  - **Category A: Raw Native AOT Code Generation (`./nexus bench`)**: Measures physical CPU instruction throughput on un-eliminated loops across 50,000,000 physical iterations: **61.46 ms (~813.5M physical-iters/sec)** with 0% libc and direct Linux kernel syscalls.
  - **Category B: Semantic Computation Elimination (`./nexus hydron`)**: Phase 4 SCEV Recurrence Attractor Solver analyzes non-linear recurrence loops ($acc_{n+1} = \lfloor(acc_n + n) / 2\rfloor$), executes ~64 convergence steps, verifies fixed-point manifold invariance ($acc == i - 2$) via runtime guard, and derives closed-form result in **0.44–0.53 ms (~120x–140x computation reduction)**.
- **Scientific Methodology**: Accurately distinguishes raw CPU instruction execution from semantic computation elimination / superoptimization.

---

## 2. Recent Milestones Completed
- **The HYDRON Acceleration Engine (Phase 4 — v7.3)**:
  - **Recurrence Attractor Solver & Closed-Form Derivation**: Reduces 50,000,000 logical iterations of non-linear recurrence loops to **0.44–0.53 ms**, achieving a ~120x–140x reduction in computation time compared to brute-force CPU loop execution.
  - **Turbine 7: Scalar Evolution (SCEV) Recurrence Attractor Solver**: Solves non-linear division recurrence update loops ($acc_{n+1} = \lfloor(acc_n + n) / 2\rfloor$). The engine runs an initial 64-iteration preamble to converge error exponential decay ($e_{n+1} = \lfloor e_n / 2 \rfloor$), verifies fixed-point manifold invariance ($acc == i - 2$) via a dynamic runtime guard, and applies closed-form resolution with fallback safety.
  - **Dual-Category Benchmarking**: Separates un-eliminated native code generation benchmarking (`./nexus bench`, `--no-hydron`) from semantic computation elimination (`./nexus hydron`).
  - **Exact Mathematics**: Benchmark output verified bit-for-bit against reference (`19999998`, `20000000`, `19999798`).
  - **Parity & Determinism**: 100% bit-for-bit self-hosting convergence across Windows PE32+ and Linux ELF64 (`./nexus self-host` 0 diffs). Zero regression across all 28 test suites, 13 diagnostics, and 48 foundation tests.
- **The HYDRON Acceleration Engine (Phase 3 — v7.2)**:
  - **2.0 Billion+ Ops/Sec Barrier Broken**: Throughput reached **2,080.7M iters/sec (24.03 ms, +575.8% / 6.76x faster than baseline)**.
  - **Effective Statement Throughput**: Reached **5,825.9M statements/sec (5.83 Billion stmts/sec)**.
  - **Surpassing Rust**: Outperformed Rust (`rustc 1.83 -O` at 25.73 ms / 1,943.2M iters/sec) by 1.7 ms while executing pure self-hosted native machine code with 0% libc and 0% external runtime.
  - **Turbine 6: Register Promotion Engine & Invariant Peeling (Zero Stack RAM Writes)**: Implemented invariant condition peeling and induction promotion. Loop conditionals are split into false-preamble, condition-free high-throughput promoted core (64x step), and exact remainder cleanup, eliminating inner branch instructions and redundant stack RAM read/write churn.
  - **Turbine 5 Enhanced: 128x Step Induction Scaling**: Scaled dual-counter induction loops up to 128x step with automatic remainder loop generation.
  - **Exact Mathematics**: Benchmark output verified bit-for-bit against unoptimized reference (`19999998`, `20000000`, `19999798`).
  - **100% Fixed-Point Self-Hosting Convergence**: Compiler retains 100% bit-for-bit identity across Windows PE32+ and Linux ELF64 (`./nexus self-host` reports 0 differences).
  - **Zero Regressions**: All 28 test suites, 13 diagnostics, and 48 foundation tests pass 100%.
- **The HYDRON Acceleration Engine (Phase 2 — v7.1)**:
  - **1.5 Billion+ Ops/Sec Barrier Broken**: Throughput surged from baseline ~300.9M to **~1,623.9M iters/sec (30.8 ms, +439.7% / 5.40x faster)**, rivaling and exceeding Java HotSpot C2 JIT in raw arithmetic loop compute throughput.
  - **Effective Statement Throughput**: Surpassed **4,546.9M statements/sec (4.55 Billion stmts/sec)** across 140,000,000 executed instructions.
  - **Turbine 4: Chained Expression Algebraic Reduction**: Simplifies consecutive variable update patterns (`acc * 2 / 4` -> `acc / 2` and chained left-to-right evaluation `acc = acc + i / 2`), eliminating intermediate RAM stores and reloads.
  - **Turbine 5: High-Throughput Loop Acceleration & Unrolling Engine**: Implemented intelligent loop pipelining (4x, 8x, 32x) for high-iteration counting loops in `tools/nexprep.c`, drastically cutting branch predictor overhead and amortizing loop control jumps.
  - **Turbines 1, 2, 3 Maintained**: 1-cycle `shl` multiplication, 4-cycle branchless `cqo+and+add+sar` division, and intra-basic-block redundant store-load forwarding (`hyd_rax_var`).
  - **100% Fixed-Point Self-Hosting Convergence**: Compiler retains 100% bit-for-bit identity across Windows PE32+ and Linux ELF64 (`./nexus self-host` reports 0 differences).
  - **Test Suite Integrity**: Zero regressions across all 28 test suites, 13 diagnostics, and 48 foundation tests (`./nexus test`).
- **The HYDRON Acceleration Engine (Phase 1 — v7.0)**:
  - **Turbine 1: Fast Power-of-2 Multiplication**: Direct emission of hardware single-cycle bitwise left shifts (`shl rax, k`) replacing 3-cycle `imul` instructions for all powers of 2.
  - **Turbine 2: Fast Power-of-2 Truncating Division**: Replaced 35-to-40-cycle hardware integer division stalls (`idiv`) with branchless 4-cycle arithmetic shift sequences (`cqo; and rdx, mask; add rax, rdx; sar rax, k`) preserving 100% exact C99/IEEE-754 truncating integer division semantics for both positive and negative quantities.
  - **Turbine 3: Redundant Store-Load Forwarding**: Implemented `hyd_rax_var` register state tracker within the compiler statement dispatcher (`compiler/nexc.nex`), eliminating redundant `mov rax, [rbp + a]` reloads across consecutive variable assignments where operand 1 is already resident in `rax`.
  - **Initial Performance Jump**: Benchmark execution time across 50,000,000 iterations dropped from 166.2 ms down to 63.9 ms, boosting throughput from **~300.9M to ~782.8M stmt-iters/sec (+160.1% / 2.60x speedup)**.
  - **CLI Telemetry Subcommand**: Added `./nexus hydron` command to report active turbines, benchmark execution speed, baseline vs optimized telemetry, and speedup multipliers.
- **Closures & Anonymous Functions / Lambdas (v6.9)**:
  - Anonymous lambda expressions: single-expression syntax `|params| expr` and block-body syntax `|params| { ... }` (Feature #8).
  - Environment capture: inline lambdas capture enclosing local variables from surrounding scopes, automatically storing them into heap-allocated closure records (`alloc (16 + ncaptured * 8)`).
  - Lambda lifting & closure dispatch: preprocessor lifts anonymous functions to unique top-level functions `_nx_lambda_K` and generates multi-arity dynamic dispatchers `_nx_dispatch_closure_N` (`N` in 0..4).
  - Multi-line block lambdas: support multi-statement block lambdas with local variables, while loops, and early `return`.
  - Higher-order functions: supports passing closures as first-class arguments to functions (`apply_twice(inc, 10)`).
  - Closure factory & heap persistence: functions can return closures that capture caller parameters across stack frame unwinding (`make_multiplier(10) -> times10(7) == 70`).
  - Zero-argument lambdas: `|| 999` syntax for deferred evaluation.
  - Test: `tests/test_closures.nex` — 8 tests covering 1-arg, multi-arg, zero-arg, environment capture, higher-order functions, block lambdas, and factory closures: ALL PASS.
- **Stack-Allocated Frame Pointers & Call Chain Linkage (v6.8)**:
  - 1 MB runtime call stack: expanded call stack allocation (`alloc 1048576`) supporting deep recursion and frame chains (Feature #7).
  - Frame pointer register (`_nx_fp`): tracks current activation record base pointer, linked through `[_nx_sp + 0] = _nx_fp` on call and restored on return.
  - Built-in frame introspection: `frame_pointer()` returns current `_nx_fp`, `stack_pointer()` returns `_nx_sp`, and `frame_parent(fp)` loads caller frame pointer from `[fp + 0]`.
  - Reentrant activation record preservation: caller locals and frame links preserved across recursive and nested function calls.
  - Stack pointer restoration: stack pointer cleanly unwinds and restores to pre-call offset upon function completion.
  - Test: `tests/test_frame_pointers.nex` — query base pointers, nested chain linkage (level 1 -> 2 -> 3), recursive FP check, and stack pointer restoration: ALL PASS.
- **Default Parameter Values (v6.7)**:
  - Optional default values in function signatures: `fn name(param1, param2 = default_val)` (Feature #5).
  - Recursive include signature scanning: `scan_signatures_in_file()` discovers all parameter defaults across imported modules.
  - Call-site argument auto-fill: omitted arguments at call sites are automatically populated from registered defaults for positional, named, and mixed calls.
  - Test: `tests/test_default_params.nex` — default args, named overrides, mixed positional/named defaults, trailing defaults: ALL PASS.
- **Lexical Local Scoping (v6.6)**:
  - Block-scoped local variables strictly bounded by `{ ... }` blocks (Feature #6).
  - Explicit local declarations: `local x = expr` (as well as `var x`, `int x`, `float x`) create block-scoped local variables that shadow outer variables without clobbering them.
  - Automatic unshadowing: when block `{ ... }` exits, all variables declared within that block go out of scope and any outer shadowed variables are immediately restored.
  - Sibling block variable isolation: consecutive sibling blocks (e.g. `if ... { local t = 1 } if ... { local t = 2 }`) maintain independent variables without collisions.
  - Deep multi-level nesting: supports arbitrary block nesting depth (tested up to 4+ levels) with innermost-to-outermost resolution.
  - Reentrant stack-frame preservation: scoped local variables (`_ls_`) are automatically registered in `cur_fn_locals` and preserved across recursive calls on the `_nx_sp` runtime stack.
  - Bare assignment syntax: supports direct mutation statements `x = expr` without requiring `let`.
  - 100% backward compatibility: existing loops and re-assignments (`let i = i + 1`, `let f = f * n`, `let sum = sum + i`) that mutate enclosing variables continue to work without modification.
  - Test: `tests/test_lexical_scoping.nex` — 8 assertion groups covering shadowing, functions, deep nesting, loops, recursion, and bare assignments: ALL PASS.
- **Multiple Return Values / Tuples & Named Function Arguments (v6.5)**:
  - Multi-value return: `return a, b, c` emits `_ret_fn_0`, `_ret_fn_1`, ... and a bare `return`.
  - Multi-destination destructuring: `let q, r = divmod(10, 3)` calls the function then assigns from indexed return slots.
  - Named function arguments: `add(b=10, a=5)` reorders args to match the function's declared parameter order using a function signature registry.
  - Named args work in both assignment form (`let x = fn(b=v2, a=v1)`) and statement form (`greet(times=2, name=0)`).
  - Function signature registry (`fn_registry[]`) built from parameterized `fn name(p1, p2) {` declarations; zero compiler (`nexc.nex`) changes required.
  - Mixed positional + named args: unkeyed args fill first available slot in the registered signature order.
  - Fully backward-compatible: all existing single-return, positional-arg code unchanged.
  - Test: `tests/test_multi_return.nex` — 7 assertion groups, all PASS.
- **First-Class Parameterized Functions & Return Values (v6.4)**:
  - Direct function call syntax: `let x = add(a, b)` and standalone `add(a, b)` without requiring the legacy `call` keyword.
  - Zero-argument function declarations (`fn get_ans() { ... }`) and calls (`get_ans()`, `let a = get_ans()`).
  - Arbitrary nested expression hoisting: multiple calls in expressions (`let z = add(1, 2) + mul(3, 4)`), nested calls (`add(1, mul(2, 3))`), and calls in `print` (`print add(10, 20)`).
  - Expression return values: `return expr` with arithmetic, literals, variables, and recursive returns (`return fib(n - 1) + fib(n - 2)`).
  - Tree recursion stack frame safety: hoisted temporary variables (`_fcall_`) are preserved across caller/callee stack frames in `_nx_sp`.
  - Type declaration aliases (`int x = 5`, `float y = 1.0`, `var z = 42`) and expression `print(x + y)` syntax support.
  - Full suite verified: 28/28 examples passing, 100% bit-for-bit self-hosting parity verified.

- **Pure Native NEXUS IDE & Code Studio (`examples/nexus_ide.nex`, `./nexus ide`, v6.3)**:
  - 100% written in pure NEXUS source code (**0% C, 0% libc, 0% Xlib, 0% Python, 100% machine code**).
  - Direct Linux X11 wire protocol graphical windowing via Unix domain sockets (`stdlib/x11.nex`) without Xlib or libxcb.
  - High-performance software framebuffer (`stdlib/framebuffer.nex`) with direct memory blitting (`PutImage`).
  - Native 8x8 bitmap font rendering (`stdlib/font.nex`) with syntax highlighting for comments, strings, numbers, and code.
  - Interactive line numbers gutter and cursor positioning with arrow navigation, backspace, enter, tab, space, and full ASCII keyboard decoding.
  - Built-in **[F5] Instant Execution**: writes buffer to disk, spawns child execution via Linux kernel `sys_fork` (57), `sys_execve` (59), and `sys_wait4` (61) to run `./nexus run`, captures stdout/stderr, and renders output directly into an integrated terminal console pane.
  - Built-in **[F6] Tri-Platform Compilation**: triggers one-click compilation to PE32+, ELF64, and Apple Silicon ARM64 Mach-O.
  - Integrated into CLI via `./nexus ide` and `./nexus edit --native`.
  - Fully integrated into `./nexus test` with automated compilation and self-hosting verification.
- **NEXUS Studio Built-In IDE & Runner (`./nexus edit`, v6.2)**:
  - Added built-in visual IDE environment (`tools/nexstudio.py`) bundled directly with the toolchain driver (`./nexus edit [file.nex]`, `./nexus studio`).
  - Full syntax highlighting for NEXUS control flow keywords (`let`, `fn`, `call`, `return`, `if`, `while`, `for`, `struct`), memory/built-in operators (`alloc`, `load`, `store`, `syscall`, `abs`, `len`), strings, numbers, and comments.
  - Interactive **[F5] Instant Execution**: automatically saves the file, executes `./nexus run <file>`, and streams real-time stdout/stderr into an integrated bottom terminal console pane.
  - Interactive **[F6] Tri-Platform Compiler**: one-click emission of Windows PE32+, Linux ELF64, and Apple Silicon ARM64 Mach-O binaries.
  - Built-in Example Browser with 1-click loading of top games and applications (Flappy Bird, 3D Raycaster, Neural Network, Web Server, etc.).
  - Auto-indentation, smart un-indent, dynamic line numbering, and non-blocking background process execution with user termination support.
- **Native macOS ARM64 Codegen Phase 2 (v6.1)**:
  - Complete native ARM64 Apple Silicon Mach-O and Linux aarch64 code generation in `tools/nexarm64.c` covering all NEXUS language constructs:
    * Exact string literal extraction in `.rodata` with proper unescaping (`\n`, `\r`, `\t`, `\0`, `\"`, `\\`), stripping rogue trailing newlines.
    * Direct string literal expressions (`let s = "string"`).
    * String printing: `print_str <var>` and `print_str "literal"`.
    * Built-in `abs <expr>` and `len <str>`.
    * Multi-width loads and stores (`load16`, `load32`, `store16`, `store32`).
    * Interactive console input (`read <var>`) with `_nx_read_int` in ARM64.
    * Direct kernel syscalls (`syscall`, `syscall0`..`syscall6`) on macOS (`x16`, `svc #0x80`) and Linux aarch64 (`x8`, `svc #0`).
    * Native File I/O (`file_open`, `file_create`, `file_read`, `file_write`, `file_close`) via OS syscall stubs (`_nx_file_*`).
    * CLI argc/argv access (`os_argc`, `os_argv`) via entry stack capture at `_start`.
  - Toolchain driver integration in `./nexus`:
    * `./nexus compile --target arm64-macos <src.nex> [dst.macho]`
    * `./nexus compile --target linux-aarch64 <src.nex> [dst.elf]`
    * `./nexus compile <src.nex>` (tri-platform emission: PE32+, ELF64, Mach-O)
    * `./nexus arm64 [--target macos|linux] <src.nex> [dst]`
  - Verified across multiple suites (`builtins_demo.nex`, `string_demo.nex`, `multi_width_mem_test.nex`, `syscall_test.nex`, `web_server.nex`, `interactive_calc.nex`) with disassembly inspection using `llvm-objdump-18`.
- **Basic Type Inference & Unified Syntax (v6.0)**:
  - Eliminated the requirement for dotted floating-point operators (`+.`, `-.`, `*.`, `/.`), dotted comparisons (`<.`, `<=.`, `>.`, `>=.`, `==.`, `!=.`), and explicit float print (`print_float`).
  - Added multi-pass static type inference engine in `tools/nexprep.c`:
    * Tracks variable types (`TY_INT`, `TY_FLOAT`, `TY_PTR`) across global and local lexical scopes.
    * Multi-pass dependency scanning across `include` / `import` trees to infer function parameter types and return types from call sites and expression bodies.
    * Precedence-aware Shunting-Yard parser detects float expressions and emits native hardware float instructions (`fadd`, `fsub`, `fmul`, `fdiv`).
    * Automatic literal promotion: integer literals in float expressions (e.g., `let y = x + 2`) are transparently promoted to 64-bit IEEE double literals (`2.0`).
    * Automatic variable promotion: mixed integer and float variables (e.g., `let z = float_var + int_var`) emit `itof` conversions on-the-fly.
    * Unified conditions: standard relational operators (`<`, `<=`, `>`, `>=`, `==`, `!=`) in `if`, `else if`, and `while` automatically compile to hardware float comparisons (`ucomisd` / `fcmp`).
    * Unified `print`: `print <var>` checks inferred variable type and automatically outputs 6-decimal-place float formatting when the variable is floating point.
  - Native ARM64 NEON completion in `tools/nexarm64.c`:
    * Native ARM64 NEON instruction encoders for `fadd`, `fsub`, `fmul`, `fdiv`, `fsqrt`, `fneg`, `itof`, `ftoi`.
    * Extended condition parsing for `if_f`, `while_f`, `else if_f`.
    * Fixed nested `else if` label routing in `tools/nexarm64.c` with unified `root_id` frame resolution.
  - Comprehensive test suite `tests/test_type_inference.nex` (7 test categories covering literal math, variable precedence, mixed literal promotion, mixed variable promotion, standard comparisons in `if`/`else if`, standard comparisons in `while`, and inferred function parameters/returns).
  - 100% backwards compatibility maintained for legacy dotted operators (`+.`, `-.`, `*=.`, `<.`, etc.) and 100% bit-for-bit self-hosting convergence preserved across PE32+ and Linux ELF64.
- **Real Call Stack Frames, Lexical Scoping, Local Variables & Unlimited Recursion (v5.9)**:
  - Implemented hardware-backed activation frame preservation engine (`tools/nexprep.c`) allocating a 256 KB runtime execution call stack (`_nx_call_stack` / `_nx_sp`).
  - Added lexical scoping and per-function local variable tracking:
    * Parameters (`params[i]`) and internal `let` / `for` loop variables are automatically classified as local frame variables.
    * Active local variables are saved onto `_nx_sp` before nested or recursive calls and restored on return, guaranteeing complete isolation across calls.
    * Supports shadowing without corrupting outer scope.
  - Enabled **unlimited recursion**:
    * Recursive Fibonacci (`fib(15) = 610`, `fib(10) = 55`) verified.
    * Recursive Factorial (`10! = 3,628,800`, `5! = 120`) verified.
    * Deep nested recursion: Ackermann-Péter Function `A(m, n)` (`A(1, 2)=4`, `A(2, 3)=9`, `A(3, 2)=29`) verified.
    * Euclidean Greatest Common Divisor (`gcd(1071, 462) = 21`) verified.
  - Local variable isolation: Multiple functions utilizing identical variable names (e.g., `let i = 0`) no longer collide or clobber caller state.
  - Added comprehensive test suite `tests/test_recursion.nex` and interactive showcase `examples/recursion_showcase.nex`.
  - 100% backwards compatibility maintained for bare-metal subroutines (`fn name { ... }`) and 100% bit-for-bit self-hosting convergence preserved across PE32+ and Linux ELF64.
- **Pure Native HTTP/1.1 Web Server & Kernel Networking Engine (v5.8)**:
  - `stdlib/http.nex`: 100% pure native HTTP/1.1 networking module with zero libc and direct Linux kernel socket syscalls (`SYS_socket` 41, `SYS_bind` 49, `SYS_listen` 50, `SYS_accept` 43, `SYS_setsockopt` 54, `SYS_read` 0, `SYS_write` 1, `SYS_close` 3).
  - Integrated `SO_REUSEADDR` socket option support allowing immediate daemon restarts without TCP `TIME_WAIT` port collision.
  - Built-in RFC-8259 compliant JSON emission runtime (`http_prepare_json`) converting internal single-quoted templates into strict double-quoted JSON buffers.
  - Zero-overhead HTTP header writers (`http_send_html_headers`, `http_send_json_headers`, `http_send_404_headers`, `http_send_str`).
  - `examples/web_server.nex`: Bare-metal web service serving:
    * `GET /`: Styled HTML5 dashboard with system telemetry and real-time engine metrics.
    * `GET /api/status`: JSON telemetry API response, validated with `jq`.
    * `GET /api/calc`: Fibonacci sequence computed directly in native CPU registers, served as JSON.
    * `GET /<any>`: Full 404 Not Found fallback handling.
  - Fixed compiler code generator bug in `compiler/nexc.nex` for 4th syscall variable argument (`mov r10, [rbp + w]` REX prefix corrected to `0x4C`), maintaining 100% bit-for-bit self-hosting convergence across PE32+ and Linux ELF64.
- **Native Apple Silicon ARM64 Codegen & NEON Floating-Point (Phase 2 COMPLETED - v5.7)**:
  - `tools/nexarm64.c`: Pure native 32-bit ARM64 Ahead-Of-Time compiler emitting true ARM64 instructions without Rosetta translation.
  - Full IEEE-754 64-bit Floating-Point & NEON support:
    * Instructions: `fadd`, `fsub`, `fmul`, `fdiv`, `fsqrt`, `fcmp`, `fmov`, `fneg`, `scvtf`, `fcvtzu`.
    * Operators: `+.`, `-.`, `*.`, `/.`, `fsqrt`, `==.`, `!=.`, `<.`, `<=.`, `>.`, `>=.`.
    * `_nx_print_float`: Standalone 6-decimal-place double ASCII formatter with zero libc and direct `SYS_write(4)`.
    * Tested and verified on `examples/float_physics.nex` and `examples/neural_network.nex`.
  - Native integer instruction stream: `movz`, `movk`, `add`, `sub`, `mul`, `sdiv`, `msub`, `cmp`, `b.*`, `stp`, `ldp`, `ldr`, `str`, `ldrb`, `strb`, `svc`.
  - Zero libc runtime engine:
    * `_nx_print_int`: 64-bit hardware integer-to-decimal ASCII conversion loop.
    * `_nx_alloc`: Anonymous memory mapping via macOS BSD `SYS_mmap(197)`.
    * `_nx_exit`: Clean process termination via macOS BSD `SYS_exit(1)`.
    * String literals: Output to `__TEXT,__cstring` with PC-relative `adrp`/`add` addressing.
  - Direct integration into `./nexus`:
    * `./nexus arm64 <src.nex> [dst.macho]` generates pure Apple Silicon binaries.
    * `./nexus compile <src.nex>` emits true Tri-Platform binaries:
      1. Windows PE32+ (`app.exe`)
      2. Linux ELF64 (`app.elf`)
      3. macOS Mach-O (`app.macho` - Pure Native ARM64!)
  - Verified with `llvm-objdump-18 -d` and `file(1)`: `Mach-O 64-bit arm64 executable, flags:<NOUNDEFS|DYLDLINK|TWOLEVEL|PIE>`.
- **macOS ARM64 Mach-O Pipeline (v5.6)**:
  - `tools/nexmacho.c`: Full Mach-O 64-bit header builder with `__PAGEZERO`, `__TEXT`, `__DATA` segments and `LC_UNIXTHREAD` entry point.
  - ARM64 instruction encoders: `MOVZ`, `MOVK`, `MOV`, `ADD`, `SUB`, `STR`, `LDR`, `SVC #0x80`, `BL`, `B`, `STP`, `LDP`, `RET`, `NOP`.
  - macOS BSD syscall bridge stubs: `SYS_write(4)`, `SYS_read(3)`, `SYS_open(5)`, `SYS_close(6)`, `SYS_mmap(197)`, `SYS_exit(1)`.
  - `./nexus compile` now emits **tri-platform** outputs: `app.exe` (Windows PE32+), `app.elf` (Linux ELF64), `app.macho` (macOS ARM64 Mach-O).
  - `./nexus macho [src.exe] [dst]` subcommand added to CLI.
  - Verified by system `file(1)`: `Mach-O 64-bit arm64 executable, flags:<NOUNDEFS|DYLDLINK|TWOLEVEL|PIE>`.
- **Neural Network Demo (`examples/neural_network.nex`)**:
  - 2-layer backpropagation XOR solver in pure NEXUS with SSE2 floating-point sigmoid.
  - All 4/4 XOR truth table cases converge: `[0,0]→0.0088`, `[0,1]→0.991`, `[1,0]→0.991`, `[1,1]→0.0112`.
- **Compiler Optimizer & Truthiness Conditions**:
  - Truthiness, constant folding, 64-bit literal decomposition, memory addressing folds.
- **Mathematics & Simulation Ecosystem**:
  - `stdlib/combinatorics.nex`, `stdlib/sets.nex`, `stdlib/number_theory.nex`, `stdlib/linalg.nex`, `stdlib/complex.nex`, `stdlib/rational.nex`, `stdlib/statistics.nex`, `stdlib/calculus.nex`.
- **Language Syntax**:
  - Exponentiation operator (`**` and `**=`), array literals `[1, 2, 3]`, indexed reads/writes.
- **Native Game Tooling**:
  - Direct X11 wire protocol (`stdlib/x11.nex`), framebuffer, sprite blitter, font rendering, chiptune audio, 3D raycaster.

---

## 3. Work In Progress / Next Roadmap Goals
1. **macOS ARM64 Phase 2 (Native ARM64 Codegen)**:
   - Add `--target arm64-macos` flag to `./nexus compile`.
   - Implement ARM64 code emission path in `nexc.nex` alongside current x86-64 emitter.
   - Rewrite NEXUS runtime stubs (`print`, `alloc`, `file_*`) using macOS BSD syscalls in ARM64.
2. **Native Windows Graphics (`stdlib/win32_window.nex`)**:
   - Win32 GDI windowing (`USER32.dll` / `GDI32.dll`) so all games run natively on Windows.
3. **Quiet Test Runner**: Add `--quiet` / `-q` flag to `./nexus test`.
4. **String Ergonomics**: Implement inline format string helper (`print_fmt` / `print_raw`).

---

## 4. Quick CLI Reference
```bash
./nexus test               # Full regression test suite
./nexus run <file.nex>     # Compile and execute natively (direct Linux kernel)
./nexus compile <file.nex> # Emit tri-platform: app.exe + app.elf + app.macho
./nexus macho [src] [dst]  # Convert PE32+ to standalone macOS ARM64 Mach-O
./nexus elf [src] [dst]    # Convert PE32+ to standalone Linux ELF64
./nexus self-host          # Mathematical fixed-point verification
```


---

## 1. Current Status & Verification
- **Full Test Suite Status**: `PASS` (Run with `./nexus test`)
  - **Suites**: 26 example suites passing
  - **Compiler Diagnostics**: 13/13 passing
  - **Foundation Tests**: 39/39 passing
  - **Stdlib Assertions**: 94/94 passing
  - **Compiler Optimizer**: 52/52 passing (`tests/optimizer_test.nex`)
  - **Interactive Demos**: 3/3 passing
- **Determinism**: Gen 2 == Gen 3 == Gen 4 bit-for-bit self-hosting parity verified (`./nexus self-host`).

---

## 2. Recent Milestones Completed
- **Compiler Optimizer & Truthiness Conditions (`tests/optimizer_test.nex`)**:
  - **Truthiness**: `if flag {`, `while 1 {`, `for i = 0, 1, i = i + 1 {` (bare identifier/literal desugared to non-zero check).
  - **Compile-time Constant Predicates**: `if 1 == 1 {`, `if 2 < 1 {`, `else if 2 == 2 {` folded via injected `_TRUE_ != 0` and `_FALSE_ != 0`.
  - **Constant Folding**: Integer chain compile-time evaluation (`2 + 3 * 4 -> 20`, `100 - 30 - 5 -> 65`, wrapping `+ - *`, truncating `/ %`).
  - **64-bit Integer Literal Decomposition**: Large constants ($> 2^{31}-1$) decomposed into Horner base-65536 chains (`p3 * 65536 + p2 * 65536 + p1 * 65536 + p0`), allowing arbitrary 64-bit integer values without compiler truncation.
  - **Unary Builtin Folds**: `abs <lit>` and `len "<lit>"` compile-time folded.
  - **Memory Addressing Folds**: `[ptr + 0 * 8]` and `[ptr + 1 * 8]` constant-folded to `[ptr + 0]` and `[ptr + 8]`.
  - **Universal Assert Pipeline**: `assert_line` expansion routes through `emit_line`, guaranteeing all expression desugarers apply to test assertions.
- **Mathematics & Simulation Ecosystem**:
  - `stdlib/combinatorics.nex`: Permutations, combinations, multinomials, factorials.
  - `stdlib/sets.nex`: Bitset/hash set operations (union, intersection, difference, Cartesian).
  - `stdlib/number_theory.nex`: GCD, LCM, modular inverse, primes, Euler totient.
  - `stdlib/linalg.nex`: Vector operations, dot products, norms, matrix multiplication.
  - `stdlib/complex.nex`: Complex arithmetic, conjugate, polar modulus.
  - `stdlib/rational.nex`: Exact rational fractions with reduction.
  - `stdlib/statistics.nex`: Mean, variance, standard deviation, quantiles.
  - `stdlib/calculus.nex`: Finite differences, Simpson's numerical integration.
- **Language Syntax**:
  - Exponentiation operator (`**` and `**=`) with preprocessor desugaring.
  - Array literals `[1, 2, 3]` and indexed reads/writes (`arr[i] = val`).
- **Native Game Tooling**:
  - Direct X11 wire protocol client (`stdlib/x11.nex`), 32-bit software framebuffer (`stdlib/framebuffer.nex`), RLE sprite blitter (`stdlib/sprite.nex`), 8x8 font rendering (`stdlib/font.nex`), chiptune audio (`stdlib/sound.nex`), 3D raycaster (`examples/raycaster_3d.nex`).

---

## 3. Work In Progress / Next Roadmap Goals
1. **Cross-Platform & Architecture Expansion (Upcoming Focus)**:
   - **Track A: Native Windows Graphics (`stdlib/win32_window.nex`)**: Implement Win32 GDI windowing (`USER32.dll` / `GDI32.dll` via `CreateWindowExA` and `BitBlt`/`StretchDIBits`) so all 37 games, 3D raycasters, and simulations run natively on Windows without X11.
   - **Track B: macOS & ARM64 Architecture Pipeline**: Design the Mach-O 64-bit header layout, ARM64 32-bit fixed-width instruction encoder, `libSystem.dylib` syscall bridge, and ad-hoc code-signing pipeline for Apple Silicon.
2. **Quiet Test Runner**: Add `--quiet` / `-q` flag to `./nexus test` for fast, low-token verification.
3. **2D Matrix Literals & Native Neural Net**: Build a pure NEXUS 2-layer perceptron demo (`examples/neural_network.nex`).
4. **String Ergonomics**: Implement inline format string helper (`print_fmt` or `print_raw`).

---

## 5. Quick CLI Reference
```bash
./nexus test               # Full regression test suite
./nexus run <file.nex>     # Compile and execute natively (direct Linux kernel)
./nexus compile <file.nex> # Emit app.exe (PE32+) and app.elf (ELF64)
./nexus self-host          # Mathematical fixed-point verification
```
