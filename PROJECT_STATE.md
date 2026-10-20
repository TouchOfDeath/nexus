# NEXUS Project State & Memory Log

> **Last Updated**: 2026-10-20  
> **Current Version**: v1.2.0 — Nexus NumPy Numerical Computing Framework, N-D Strided Arrays, Linear Algebra (LU, QR, SVD, Cholesky, Solvers), IEEE-754 Math, Compensated Reductions, Statistics, PRNG, and Binary/CSV Serialization  
> **Compiler Architecture**: Self-Hosted Compiler Core in pure NEXUS (`compiler/nexc.nex`), 0% libc, direct Linux ELF64 syscalls + Native ARM64 Mach-O & Linux aarch64. Stage 5 bit-for-bit fixed-point convergence verified (`0` byte differences across 49,152 PE32+ bytes and 57,951 ELF64 bytes).

---

## 0.7 v1.2.0 — Nexus NumPy: Multidimensional Arrays, Linear Algebra, Statistics & Scientific Computing Engine (2026-10-20)
- **Engine Architecture & Modules (`stdlib/numpy.nex`, `stdlib/numpy/*.nex` — 7,375 lines)**:
  - `stdlib/numpy/core.nex`: Multidimensional `NdArray` memory layout, strata descriptors, custom strides, zero-copy slicing, 2D transpose views, multidimensional indexing parser (`M[i, j]`), and runtime error diagnostics.
  - `stdlib/numpy/creation.nex`: Array constructors (`zeros`, `ones`, `full`, `arange`, `linspace`, `logspace`, `eye`, `diag`, `from_buffer`).
  - `stdlib/numpy/ops.nex`: Vectorized element-wise arithmetic (`+`, `-`, `*`, `/`, `%`), comparison operators (`==`, `!=`, `<`, `<=`, `>`, `>=`), logical masking, and N-D broadcasting.
  - `stdlib/numpy/reduce.nex`: Compensated Neumaier reductions (`sum`, `prod`, `min`, `max`, `any`, `all`, `mean`, `ptp`, `argmin`, `argmax`, `cumsum`, `cumprod`).
  - `stdlib/numpy/math.nex`: Mathematical ufuncs with IEEE-754 compliance (trigonometry, exp, log, sqrt, power, clip, sign, round, floor, ceil).
  - `stdlib/numpy/linalg.nex`: Pure Nexus linear algebra engine (`dot`, `outer`, `matmul`, `trace`, `norm`, `det`, `inv`, `solve`, `lu`, `qr`, `cholesky`, `svd`, `eigvals`, `lstsq`).
  - `stdlib/numpy/stats.nex`: Descriptive statistics (`var`, `std`, `median`, `quantile`, `percentile`, `cov`, `corrcoef`).
  - `stdlib/numpy/random.nex`: Reproducible PRNG with 64-bit state (`random_seed`, `random_uniform`, `random_normal` Box-Muller, `random_integers`, `shuffle`, `permutation`).
  - `stdlib/numpy/sort.nex`: Merge-sort (`sort`, `sort_inplace`), index tracking (`argsort`), binary search (`searchsorted`), and deduplication (`unique`).
  - `stdlib/numpy/io.nex`: Binary file format with magic headers and checksums (`save_binary`, `load_binary`) and CSV serialization (`to_csv_string`, `from_csv_string`, `save_csv`, `load_csv`).
- **Compiler & Preprocessor Enhancements (`tools/nexprep.c`, `tools/nextest.c`, `nexus`)**:
  - Added native support for 2D array literals (`[[1.0, 2.0], [3.0, 4.0]]`) in `nexprep.c`.
  - Added multidimensional indexing desugaring (`matrix[i, j]` and `matrix[i, j] = val`).
  - Added `numpy.*` / `np.*` namespace dispatch and `NdArray__*` method dispatch.
  - Extended ABI calling conventions for `np_` and `_np_` subroutines.
  - Optimized preprocessor memory consumption by migrating high-depth macro buffers to static storage to eliminate stack frame pressure.
  - Auto-routing in `./nexus` and `nextest.c` to invoke extended capacity compiler (`nexc_xl.elf`) whenever `_np_` routines or large symbol counts are present.
- **Verification Suites & Examples**:
  - 5 comprehensive automated test suites (`tests/test_numpy_core.nex`, `tests/test_numpy_math_reduce.nex`, `tests/test_numpy_linalg.nex`, `tests/test_numpy_stats_random_io.nex`, `tests/test_numpy_failures.nex`) with **523 assertions passing 100%**.
  - 12 complete runnable example programs (`examples/numpy_01_basic.nex` .. `examples/numpy_12_least_squares.nex`) and benchmark suite (`examples/numpy_bench.nex`).
  - Comprehensive documentation (`docs/NUMPY_MANUAL.md`, `docs/NUMPY_API_REFERENCE.md`, `docs/NUMPY_BENCHMARKS.md`).

---

## 0.6 v1.1.0 — Aether: 2D & 3D Graphics, Audio, Input, Asset & Game Engine (2026-10-19)
- **Engine Architecture & Modules (`stdlib/aether.nex`, `stdlib/aether/*.nex`)**:
  - `stdlib/aether/core.nex`: Unified context memory (`_ae_ctx`), 31-bit packed positive ARGB color pipeline, 4 blend modes, monotonic frame timing, FPS calculation, error diagnostics, and draw telemetry.
  - `stdlib/aether/math.nex`: Fixed-point (1000 = 1.0) & float bridges, trig LUT (`ae_sin_deg`, `ae_cos_deg`), easing, `AeVec2`, `AeVec3`, `AeVec4`, `AeQuat`, `AeMat4` (TRS, LookAt, Perspective, Ortho), `AeAABB`, and `AeRay` intersections.
  - `stdlib/aether/input.nex`: Physical key state transitions (down/pressed/released/repeat), separate printable text input queue, mouse deltas & wheel, gamepad axes & deadzone, event ring buffer, focus-loss auto-release, and action mapping.
  - `stdlib/aether/window.nex`: High-DPI multi-window table, resize, fullscreen/borderless/minimize/restore, Linux X11 wire-protocol driver with automatic headless fallback, pixel/depth readback, FNV-1a checksums, and binary P6 PPM screenshot export.
  - `stdlib/aether/gfx2d.nex`: Clipped/blended primitives, lines, thick lines, gradients, `AeCamera2D` with world-to-screen picking, textures (filtering & wrapping), `AeSprite`, `AeSpriteSheet`, `AeAnimator`, command batching, and 8x8 font typography.
  - `stdlib/aether/gfx3d.nex`: Vertex/index buffers (`AeMesh`), procedural cube/plane/sphere/pyramid generators, Wavefront `.obj` loader, 32-bit depth buffer, backface culling, 7 shader profiles (Lambert, Phong, Toon, Normal, Wireframe), directional/point lights, planar shadow mapping, skybox, offscreen `AeRenderTarget` with post-processing, and hierarchical `AeNode3D` scene graph.
  - `stdlib/aether/audio.nex`: 16-voice stereo PCM mixer, waveform & sweep synthesizer with ADSR envelopes, RIFF/WAVE `.wav` loader & exporter, volume/pitch/pan modulation, and 3D spatial audio distance/panning attenuation.
  - `stdlib/aether/assets.nex` & `stdlib/aether/game.nex`: Reference-counted cached resource manager with missing-asset fallbacks, fixed-timestep `AeClock`, cooldown `AeTimer`, 2D/3D collision detection, debug wireframe overlays, and fade transitions.
- **Compiler & Preprocessor Enhancements (`tools/nexprep.c`, `compiler/nexc_xl.elf`, `tools/nexelf.c`)**:
  - Unquoted `import aether` and submodule imports, namespace method dispatch (`aether.*`, `ae.*`, `Color.*`), zero-overhead register-slot calling convention (`uses_nx_abi`), DAG call-graph-height local variable coalescing (Phase 2B), direct parameter slot passing, and multi-return tuple destructuring.
  - Extended-capacity self-hosted compiler (`compiler/nexc_xl.elf` + `compiler/template_xl.bin`) supporting 241 KB of code capacity and 4,096 variable slots, integrated seamlessly into `./nexus` CLI driver while preserving 100% bit-for-bit Stage 5 self-hosting parity.
- **Verification Suites & Examples**:
  - 3 comprehensive automated test suites (`tests/test_aether_unit.nex`, `tests/test_aether_render.nex`, `tests/test_aether_failures.nex`) with 126 assertions passing 100%.
  - 12 complete runnable example programs (`examples/aether_01_shapes.nex` .. `examples/aether_12_game3d.nex`).
  - Cross-platform C hardware abstraction reference layer (`runtime/aether_pal.h`, `runtime/aether_pal.c`).
  - Comprehensive documentation (`docs/AETHER_GRAPHICS_MANUAL.md`, `docs/AETHER_API_REFERENCE.md`, `docs/AETHER_ARCHITECTURE.md`).

## 0.5 v1.0.0 — Formal Release: Complete Language Loop, First-Class Strings, High-Level I/O & 5/5 Domain Applications (2026-10-19)
- **Self-Hosted Compiler Core Upgrades (`compiler/nexc.nex`)**:
  - **Variable-Path File I/O**: `file_open <var>` and `file_create <var>` now accept string pointer variables in addition to string literals.
  - **`print_raw <str_or_var>`**: Added native inline output without trailing `\r\n`.
  - **`exit <code_or_var>`**: Added native process exit statement emitting `ExitProcess` / Linux `sys_exit`.
  - **Critical Slot-0 Shadow Space Bugfix**: Shifted variable slot 0 from `[rbp + 48]` (`0x30`) to `[rbp + 56]` (`0x38`) so Windows x64 shadow space (`[rsp + 0x20..0x30]`) used by `CreateFileA` (`file_open`/`file_create`) and `print_str` (`0x0A0D` CRLF buffer) never corrupts variable 0.
  - **`len` / `abs` Line-Eating Bugfix**: Changed `call skip_ws` to `call skip_sp` after unparenthesized `len <var>` and `abs <var>` so trailing newlines are never consumed prematurely.
  - **Stage 5 Fixed-Point Verification**: Re-bootstrapped `nexc.exe`, `nexc.elf`, `nexc[2-4].*`, and `nexc.hex` (`Gen1 == Gen2 == Gen3 == Gen4`, 0 byte diffs).
- **Preprocessor, Type Inference & Ergonomics Engine (`tools/nexprep.c`)**:
  - **First-Class String Type (`TY_STR`)**: Full multi-pass type inference (`ntype_seen = 0` reset across 3 passes, `_ls_<N>_` prefix stripping, parameter/return/`??`/`?:` propagation) so `print s` and `print(s)` automatically emit `print_str` for string variables, parameters, and return values.
  - **String Concatenation (`+`), Equality (`==`, `!=`), `str(int)`, and `int(str)`**: Added on-demand native runtime helpers (`_nx_rt_strcat`, `_nx_rt_streq`, `_nx_rt_itoa`, `_nx_rt_atoi`), plus string pattern matching in `match`.
  - **Unified Stdlib Calling Convention**: Expanded `MAXFNS` and `MAX_FN_SIGS` to `2048` and enabled calling all 250+ `stdlib/*.nex` subroutines (`fn nx_... {`) directly via parameterized syntax `nx_fn(arg0, arg1, ...)` while preserving `nx_i0..nx_r0` compatibility.
  - **Boolean Logic & Complex Conditions**: Added `&&` (`and`), `||` (`or`), `!` (`not`), relational RHS in `let` (`let ok = a > b`), and complex expressions in `if` and `while` (`while d * d <= n && found == 0`).
  - **Bounded Temporary Slot Reuse**: Reused `_as_l`/`_as_r`/`_as_ok`, `_c_arg_0..7`, and modulo-16 expression temporaries, saving 300+ stack slots in large programs.
- **Standard Library & Toolchain Completion**:
  - Created `stdlib/io.nex` (`nx_read_file`, `nx_write_file`, `nx_write_file_bytes`, `nx_append_file`, `nx_file_exists`, `nx_read_line`, `nx_eprint`) and bundled it in `stdlib/nstdlib.nex`.
  - Upgraded `stdlib/hashmap.nex` with automatic capacity doubling (`nx_hashmap_grow`), tombstone deletion (`nx_hashmap_remove`), and string-keyed dictionary operations (`nx_hashmap_put_str`, `nx_hashmap_get_str`, `nx_hashmap_contains_str`, `nx_hashmap_remove_str`).
  - Added allocating string helpers to `stdlib/strings.nex` (`nx_str_slice`, `nx_str_concat_new`, `nx_str_trim_new`, `nx_int_to_str_new`, `nx_str_from_char`).
  - Completed all 5 real-world domain applications in `docs/LANGUAGE_AUDIT.md` (`examples/cli_tool_grep_wc.nex`, `examples/csv_processor.nex`, `examples/data_structures_showcase.nex`, `examples/http_protocol_demo.nex`, `examples/neural_network.nex`) and authored the formal specification `docs/SPECIFICATION.md`.

## 0.4.5 v8.9 — HYDRON Phase 5 Ultra Engine: 12 Turbines, Codegen Peepholes & O(1) Bitwise Shifts (2026-10-18)
- **Compiler Codegen Superoptimizations (`compiler/nexc.nex`)**:
  - Turbine 2: 3-cycle signed division by 2 (`cqo; sub rax, rdx; sar rax, 1`).
  - Turbine 3: Dead-store rewind operating directly on `%rax`.
  - Turbine 4: Hardware peephole shift-divide fusion & direct memory RMW (`addq`/`subq`/`cmpq [rbp+slot], imm32`).
  - Factored `st_rax` and `check_p2`, shrinking machine code by 1,050 bytes (+1,053 bytes `.text` headroom).
- **HYDRON Loop Solvers (`tools/nexprep.c`, `tools/nexir.c`, `tools/nexarm64.c`, `stdlib/bitwise.nex`)**:
  - Turbines 5-8: O(1) multi-accumulator linear induction, invariant conditional accumulator, dual-manifold SCEV recurrence attractor, and extended Gauss triangular summation.
  - Turbines 9-12: Mixed-precedence sub-term folding, geometric doubling/affine recurrence, O(1) binary shift decomposition, and lazy runtime deduplication.
  - Sub-microsecond timer in `--bench`, 146,000x loop speedup, and 100% bit-for-bit Stage 5 convergence.

## 0.4 v8.8 — Struct Methods (`impl`), Struct Literals, Enums, `match`, 64-Bit Struct Bugfix, HYDRON Turbine 9 & IDE Data Structures (2026-10-18)
- **Critical 64-Bit Struct Field Load/Store Bugfix (`tools/nexprep.c`)**:
  - Previously, `tools/nexprep.c` emitted 8-bit `load` and `store` instructions for struct fields even though `S[st].offs[nf] = nf * 8` allocated 64-bit (8-byte) slots, causing values `> 255` (`p.x = 1000` -> `232`), negative integers (`p.y = -50` -> `0`), 64-bit integers, and pointers/strings stored in struct fields to be silently truncated or corrupted.
  - Upgraded `add_hoist`, `transform_seg`, and `emit_line` to emit 64-bit `load64` and `store64`, and routed negative / 64-bit / string / call field assignments through `emit_line`.
- **Features #11, #12, #13: Typed Structs, Struct Literal Initializers (`Point { x: 10, y: 20 }`) & Struct Methods (`impl Point { ... }`)**:
  - **Feature #11**: `struct Name { x: int, y: int }` now accepts comma-separated and type-annotated field declarations as well as single-line struct declarations.
  - **Feature #13**: Single-line and multi-line struct literal initializers `let p = Vec3 { x: 100000, y: -5000, z: 9876543210 }` automatically allocate `Vec3.size`, validate field names, assign specified fields, and zero-initialize omitted fields.
  - **Feature #12**: `impl StructName { fn method(&mut self, ...) { ... } fn create(...) { ... } }` attaches instance methods and associated functions to structs, supporting both `obj.method(args...)` (with automatic `self` injection and scoped field resolution) and `StructName.create(args...)` / `StructName::create(args...)`.
- **Features #16 & #17: Enum Declarations (`enum`) & Pattern Matching (`match`)**:
  - **Feature #16**: `enum TokenKind { TokEof, TokIdent, TokNumber = 10, TokPlus }` defines compile-time integer constants accessible via `TokenKind.TokIdent`, `TokenKind::TokNumber`, `TokenKind_TokPlus`, or unqualified `TokStar`.
  - **Feature #17**: `match expr { pat1 => { ... } pat2 | pat3 => { ... } _ => { ... } }` supports single patterns, multi-value OR (`|`) patterns, enum constants, block arms, single-statement arms, and wildcard (`_`) fallback arms.
- **Feature #31: Scientific Notation Float Literals & Single-Line Blocks**:
  - Added `normalize_scientific_floats` (`2.5e4`, `1.5e-3`, `1.25e2`) and `desugar_single_line_block` (`if cond { stmt } else { stmt }`) in `tools/nexprep.c`, plus `else if` and float opcode (`fadd`, `fsub`, `fmul`, `fdiv`, `fsqrt`, `fneg`, `ftoi`, `itof`) support in `tools/nexinterp.py`.
- **HYDRON Turbine 9: Algebraic Identity & Annihilation Pass (`p2_algebraic_pass`)**:
  - Simplifies `x + 0`, `0 + x`, `x - 0`, `x * 1`, `1 * x`, `x / 1`, `x * 0`, `0 * x`, `x - x`, `x % 1` and eliminates no-op self-assignments `let x = x`.
- **New Standard Library Modules (`stdlib/`) & Regression Suites (`tests/`)**:
  - **Features #72 & #73: Ordered Map (`BTreeMap`) & Ordered Set (`BTreeSet`) (`stdlib/btree.nex`, `tests/test_btree.nex`)**: `nx_btmap_new`, `nx_btmap_put`, `nx_btmap_get`, `nx_btmap_contains`, `nx_btmap_remove`, `nx_btmap_min_key`, `nx_btmap_max_key`, `nx_btmap_lower_bound`, `nx_btmap_key_at`, `nx_btmap_val_at`, `nx_btset_new`, `nx_btset_add`, `nx_btset_contains`, `nx_btset_remove`, `nx_btset_at`.
  - **Feature #82: Augmented Interval Tree (`stdlib/interval_tree.nex`, `tests/test_interval_tree.nex`)**: `nx_itree_new`, `nx_itree_insert`, `nx_itree_first_overlap`, `nx_itree_query_overlap`, `nx_itree_query_point`, `nx_itree_len`, `nx_itree_clear`.
  - **Feature #84: Line Offset Index Table (`stdlib/line_index.nex`, `tests/test_line_index.nex`)**: `nx_line_index_build`, `nx_line_count`, `nx_line_start_offset`, `nx_line_end_offset`, `nx_line_length`, `nx_offset_to_line`, `nx_line_col_to_offset`.
  - **Feature #95: String Interning Pool (`stdlib/interner.nex`, `tests/test_interner.nex`)**: `nx_intern_new`, `nx_intern_str`, `nx_intern_find`, `nx_intern_resolve`, `nx_intern_count`.
  - **Feature #104: Segment Tree (`stdlib/segment_tree.nex`, `tests/test_segment_tree.nex`)**: `nx_segtree_new`, `nx_segtree_build`, `nx_segtree_update`, `nx_segtree_add`, `nx_segtree_query_sum`, `nx_segtree_query_min`, `nx_segtree_query_max`.
  - **Features #141, #142, #145, #151: Fuzzy Matcher, ANSI Stripper, URL Codec & Glob Matcher (`stdlib/fuzzy_glob.nex`, `tests/test_fuzzy_glob.nex`)**: `nx_fuzzy_score`, `nx_fuzzy_match`, `nx_ansi_strip`, `nx_url_encode`, `nx_url_decode`, `nx_glob_match`.

## 0.3 v8.7 — Compiler Bugfixes, HYDRON Turbine 8 + Call-Graph DCE & Standard Library Expansion (2026-10-18)
- **Critical HYDRON SCEV Recurrence False-Positive Fix + Turbine 8 Gauss Triangular Summation (`tools/nexprep.c`)**:
  - Fixed a severe bug in `p2_try_transform` where 2-line triangular summation loops (`while m <= 1000 { let sum = sum + m; let m = m + 1 }`) were falsely classified (`nr == 3`) as division recurrence attractors ($acc_{n+1} = \lfloor(acc_n + i)/2
floor$), corrupting the sum `1..1000` from `500500` into `999` (`tests/test_hydron_overflow.nex` assertion 11).
  - Added **HYDRON Turbine 8**: Closed-Form Gauss Triangular Series Elimination ($O(1)$ reduction $\sum_{v=v_0}^{L-1} v = rac{(L - v_0)(v_0 + L - 1)}{2}$) for `step == 1` non-negative bounds with exact fallback safety.
  - Preserved parenthesized and unparenthesized division recurrence attractor patterns (`let acc = (acc + i) * 2 / 4` and `let acc = acc + i * 2 / 4`).
- **Transitive Call-Graph Dead Function Elimination — Feature #6 (`tools/nexprep.c`)**:
  - Added `p2_dce_unused_functions()` to Phase 2 of `nexprep`, computing transitive reachability from top-level statements and lifted closures (`g_lambdas`) across `call <fn>` edges and stripping unreachable `fn` definitions prior to machine-code emission.
  - Prevents large standard-library includes (`stdlib/nstdlib.nex`) from exhausting the 49,152-byte PE32+ `.text` budget (`0xA800` `print_int` boundary) and speeds up compilation by 3x–5x.
- **Self-Hosted Lexer RHS Keyword Prefix Collision Sanitizer (`tools/nexprep.c`)**:
  - Fixed a longstanding crash/miscompilation where user variables starting with `fa`, `fm`, `fd`, `fn`, `ft`, `fsu`, `fsq`, `len`, `abs`, `itof`, `alloc`, `load`, `syscall`, `file_`, or `os_arg` (such as `facc`, `length`, `abs_val`) collided with `compiler/nexc.nex`'s prefix-based RHS parser (`let x = length` previously crashed with SIGSEGV RC 139, and `assert_eq facc, 1998` ate the following line).
  - Implemented `p2_sanitize_identifiers()` to transparently prefix colliding user variable names with `_nxu_` while preserving 100% Stage 5 bit-for-bit self-hosting convergence (`0 byte differences`).
- **Full Integer Parentheses `(...)`, Precedence, Character Literals `'c'` (#29), and Boolean Literals `true`/`false` (#34)**:
  - Implemented innermost-out integer parenthesized sub-expression hoisting (`desugar_int_parens`), recursive-descent constant folding (`eval_int_chain`), character literal normalization (`'A'`, `'\n'`, `'\t'`, `'\r'`, `'\0'`, `'\e'`, `'\\'`, `'\''`, `'\xHH'`), and `true`/`false` literal normalization in `tools/nexprep.c`.
  - Upgraded `tools/nexinterp.py` and `tools/nexir.c` with matching parenthesized and precedence-aware expression evaluation.
- **Headless X11 Offscreen Fallback Mode (`stdlib/x11.nex`)**:
  - Added automatic headless fallback (`x11_headless`) when `/tmp/.X11-unix/X0` is unavailable, allowing `examples/sprite_showcase.nex`, `tests/font_test.nex`, `tests/x11_stdlib_test.nex`, and `tests/x11_window_test.nex` to execute and verify X11 wire-protocol buffers in headless CI environments.
- **New Standard Library Modules & Extensions**:
  - **Feature #69: Gap Buffer (`stdlib/gap_buffer.nex`)**: Contiguous sliding-gap text buffer (`nx_gb_new`, `nx_gb_len`, `nx_gb_gap_size`, `nx_gb_cursor`, `nx_gb_move_cursor`, `nx_gb_insert_char`, `nx_gb_insert_str`, `nx_gb_delete_back`, `nx_gb_delete_fwd`, `nx_gb_get_char`, `nx_gb_to_str`, `nx_gb_clear`). Verified in `tests/test_gap_buffer.nex`.
  - **Feature #79: Bloom Filter (`stdlib/bloom_filter.nex`)**: Probabilistic bit-array set membership filter using SplitMix64 + FNV-1a double hashing (`nx_bloom_new`, `nx_bloom_add`, `nx_bloom_add_str`, `nx_bloom_might_contain`, `nx_bloom_might_contain_str`, `nx_bloom_count`, `nx_bloom_popcount`, `nx_bloom_clear`). Verified in `tests/test_bloom_filter.nex`.
  - **Features #89 & #90: KMP & Boyer-Moore-Horspool Substring Search (`stdlib/pattern_search.nex`)**: Linear-time Knuth-Morris-Pratt (`nx_kmp_search`, `nx_kmp_count`) and bad-character skip-table Boyer-Moore-Horspool (`nx_bmh_search`). Verified in `tests/test_pattern_search.nex`.
  - **Feature #105: Fenwick Tree / Binary Indexed Tree (`stdlib/fenwick.nex`)**: $O(\log N)$ prefix sum, range sum, point update, and binary-lifting `find_kth` (`nx_fenwick_new`, `nx_fenwick_add`, `nx_fenwick_prefix_sum`, `nx_fenwick_range_sum`, `nx_fenwick_get`, `nx_fenwick_set`, `nx_fenwick_find_kth`). Verified in `tests/test_fenwick_uf.nex`.
  - **Feature #107: Disjoint-Set / Union-Find (`stdlib/union_find.nex`)**: Path-halving + union-by-size partition forest (`nx_uf_new`, `nx_uf_find`, `nx_uf_union`, `nx_uf_connected`, `nx_uf_set_size`, `nx_uf_count_sets`). Verified in `tests/test_fenwick_uf.nex`.
  - **Feature #144: RFC 4648 Base64 Encoder & Decoder (`stdlib/base64.nex`)**: Self-allocating Base64 string and byte-buffer encoder/decoder (`nx_base64_encode`, `nx_base64_encode_bytes`, `nx_base64_decode`). Verified in `tests/test_base64.nex`.
  - **Features #135, #136, #137, #139: String Case, Padding, Replace & Levenshtein (`stdlib/strings.nex`)**: Added `nx_str_to_upper`, `nx_str_to_lower`, `nx_str_pad_left`, `nx_str_pad_right`, `nx_str_index_of`, `nx_str_contains`, `nx_str_replace`, and Wagner-Fischer `nx_str_levenshtein`. Verified in `tests/test_strings_v87.nex`.

## 0.2 v8.6 — Foundational Prerequisite Bundle (2026-10-17)
- **Features #87 & #88: Fast Memory Move / Set / Zero (`stdlib/memory.nex`)**: Byte-granular bulk memory primitives. `nx_memmove` is overlap-safe (forward copy when dst < src, backward copy when dst > src, no-op on dst == src and zero-length requests); `nx_memcpy` provides forward-only disjoint copies; `nx_memset` fills arbitrary ranges with a byte value (low-byte truncation); `nx_memzero` clears ranges. All return the destination pointer for call chaining. Verified with dedicated regression suite in `tests/test_memory.nex`.
- **Feature #99: Growable ByteBuffer (`stdlib/byte_buffer.nex`)**: Heap-backed byte buffer (32-byte descriptor) with automatic capacity doubling. Little-endian `nx_buf_write_u8/u16/u32/u64` appends plus raw `nx_buf_write_bytes` blocks; offset-based `nx_buf_read_u8/u16/u32/u64` with out-of-bounds-safe zeros; `nx_buf_len`, `nx_buf_cap`, `nx_buf_clear`, and `nx_buf_to_ptr` for direct data access. Designed for X11 wire-protocol and ELF-header serialization. Verified in `tests/test_byte_buffer.nex`.
- **Feature #100: Bit Stream Reader & Writer (`stdlib/bit_stream.nex`)**: LSB-first bit-level serial I/O matching DEFLATE-style packing. Writer appends 1-63 bit fields into a doubling buffer (`nx_bit_write`), flushes to byte boundaries (`nx_bit_flush`); reader wraps any byte buffer with cursor-based `nx_bit_read`, position, and bits-remaining queries. Verified in `tests/test_bit_stream.nex`.
- **Feature #76: Priority Queue / Binary Heap (`stdlib/priority_queue.nex`)**: Min-heap and max-heap modes with O(log n) iterative sift-up/sift-down, auto-doubling storage, peek/len/is-empty queries, duplicate and negative value support, and heapsort-verified ordering. Verified in `tests/test_priority_queue.nex`.
- **Feature #77: Doubly-Linked List (`stdlib/linked_list.nex`)**: Node-based bidirectional list (24-byte nodes) with O(1) push/pop at both ends, O(1) removal by node handle, head/tail/value accessors, and directly walkable prev/next links. Verified in `tests/test_linked_list.nex`.
- **Feature #80: Trie / Prefix Tree (`stdlib/trie.nex`)**: Character trie using parallel branch arrays per node (any byte 1..255 branches). Supports insert with duplicate detection, exact contains, prefix queries, terminator-based word removal preserving shared prefixes, and the empty-string word. Verified in `tests/test_trie.nex`.
- **Feature #94: LRU Cache (`stdlib/lru_cache.nex`)**: Fixed-capacity cache combining an open-addressing hash table with an intrusive doubly-linked recency list. O(1) get/put with recency refresh, tail eviction with tombstone revival on re-insert, automatic table rebuild when tombstones fill, value-0-safe membership queries, and clear. Verified in `tests/test_lru_cache.nex`.
- **Feature #96: Arena Bump Allocator (`stdlib/arena.nex`)**: Chunked bump allocator with 8-byte aligned O(1) allocations, automatic chunk chaining (default and oversized chunk sizes), used/capacity/chunk-count metrics, and a reset pass that is O(chunks) while retaining chunks for reuse. Verified in `tests/test_arena.nex`.
- **Features #113-115: UTF-8 Validate / Decode / Encode (`stdlib/unicode.nex`)**: Strict RFC 3629 primitives over raw byte buffers. `nx_utf8_validate` enforces 1-4 byte sequence boundaries and rejects overlong encodings, surrogate encodings, out-of-range scalars, and truncated tails; `nx_utf8_decode_next` returns the scalar value with bytes consumed in `nx_r1` ((-1, 0) on malformed input); `nx_utf8_encode` maps scalars to 1-4 bytes and rejects invalid inputs with a 0-byte return. Verified in `tests/test_unicode.nex`.
- **Features #133 & #134: String Split / Join (`stdlib/strings.nex`)**: `nx_str_split` splits at any delimiter byte into a freshly allocated token array (`A[0]` = count, `A[1..N]` = null-terminated tokens) honoring empty, consecutive, leading, and trailing delimiters; `nx_str_join` concatenates a qword array of strings with an arbitrary separator into a single allocation. Verified in `tests/test_string_split_join.nex`.
- **Feature #93: Binary Search Primitives (`stdlib/search.nex`)**: Exact-match (deterministic first occurrence), lower-bound, and upper-bound searches over ascending sorted 64-bit arrays with overflow-free midpoints, half-open duplicate ranges, and ascending insertion points. Verified in `tests/test_search.nex`.
- Verification: full `./nexus test` run passes 26 example suites, 13/13 diagnostics, 50/50 foundation tests, 3/3 interactive tests, and all 26 stdlib regression suites (122-assertion stdlib, optimizer, type-inference, native IDE, HYDRON, string escapes, piece table, syntax, loop ranges, hashset, bitset, ring buffer, memory, ByteBuffer, bit stream, priority queue, linked list, trie, LRU, arena, unicode, split/join, binary search). `./nexus self-host` reports 0 byte differences across 49,152 bytes on PE32+ and 57,951 bytes on Linux ELF64.

## 0.1 Unreleased — Standard Library Additions & Compiler Bugfixes (2026-10-17)
- **Features #75 & #74: Circular Ring Buffer / Deque (`stdlib/ring_buffer.nex`)**: Production-grade, 48-byte heap-descriptor double-ended circular buffer with 25 standard library subroutines. Supports dual operating modes: dynamic auto-growing Deque (geometric doubling capacity expansion with amortized O(1) push and pop at both head and tail) and fixed-capacity circular buffer (bounded memory window with eviction tracking via `nx_rb_dropped_count`). Features logical O(1) indexing (`nx_rb_get`, `nx_rb_set`), element searching (`nx_rb_contains`, `nx_rb_index_of`), contiguous array export (`nx_rb_to_array`), deep copying (`nx_rb_clone`), equality testing (`nx_rb_equals`), in-place reversal (`nx_rb_reverse`), bidirectional rotation (`nx_rb_rotate`), and buffer compaction (`nx_rb_shrink_to_fit`). Verified with dedicated regression suite in `tests/test_ring_buffer.nex`.
- **Operator Precedence in Emitted Code (BUG 4 RESOLVED)**: Fixed `prec_rewrite` in `tools/nexprep.c` to partition mixed infix expressions into multiplicative terms (`*`, `/`, `%`) separated by additive operators (`+`, `-`), extracting multi-operand terms into `_prec_K` temporaries while properly preserving Horner base-65536 expansions. `1 + 2 * 3 == 7`, `10 - 2 * 3 == 4`, `1 + 2 * 3 + 4 == 11`.
- **Compiler Diagnostic Error Reporting & Exit Gating (BUGS 2 & 3 RESOLVED)**: Streamlined `report_err` in `compiler/nexc.nex` to print exact diagnostic strings without invoking the clobbered helper offset, preventing runaway jumps to the ELF emitter on syntax failures. All 13/13 compiler diagnostics now pass and failure gating correctly suppresses `app.exe`/`app.elf` on error.
- **Feature #78: Dense BitSet / BitVector (`stdlib/bitset.nex`)**: Space-efficient bitset packing 8 flags per byte. Supports O(1) bit testing, setting, clearing, toggling, popcount/cardinality, first/next set/clear bit scanning, deep cloning, equality comparison, and in-place set logic (`AND`, `OR`, `XOR`, `NOT`). Verified with dedicated regression suite in `tests/test_bitset.nex`.
- Added `nx_str_starts_with` and `nx_str_ends_with` with empty and overlong input coverage (roadmap feature 138).
- Added `nx_str_trim` for ASCII space and control whitespace bytes 9–13; Unicode whitespace remains unsupported (partial roadmap feature 132).
- Added `nx_str_normalize_newlines` for mixed CRLF, LF, and CR input, including mutable in-place normalization (roadmap feature 148).
- Added `nx_str_escape_literal` for quoted escaped string literals, including `\xHH` for non-printable bytes (roadmap feature 147).
- Standard-library regression coverage increased from 94 to 122 assertions.
- Verification: 27 example suites, 13/13 diagnostics, 50/50 foundation tests, stdlib, optimizer, interactive, type-inference, IDE, HYDRON, string-escape, hashset, bitset, and ring buffer suites passed. All green.

## 0. v8.2+ String Escapes & Optimization Release Snapshot (2026-10-13)
- **Phase 1 String Escapes (COMPLETE)**: Full standard escape sequence decoding (`\n`, `\t`, `\r`, `\\`, `\"`, `\0`, `\e`, `\xHH`) directly emitted into binary `.rdata` by the self-hosted compiler core (`compiler/nexc.nex`).
  - Preprocessor (`tools/nexprep.c`), ARM64 emitter (`tools/nexarm64.c`), and semantic interpreter (`tools/nexinterp.py`) updated with escape-aware string boundary scanning.
  - `len "..."` constant folding updated to accurately count decoded byte lengths.
  - Windows SDK `#undef OUT` compatibility fix applied, enabling seamless cross-compilation with MinGW and MSVC.
  - Dedicated regression suite created: `tests/test_string_escapes.nex` (PASS).
- **Phase 2 Loop Optimizer & Scoped Fusion (COMPLETE)**:
  - Statement fusion: consecutive variable mutations merge into single left-to-right evaluations, eliminating dead intermediate memory stores.
  - Generalized HYDRON loop engine: multi-accumulator induction scaling (x128), invariant conditional peeling (x64), and SCEV recurrence attractor.
  - Lexical scoping integrity: all loop transforms operate on resolved lexical scope names (`_ls_`), eliminating variable shadowing bugs.
  - Code-size budget estimator protects 49,152-byte compiler output image against buffer overflow.
- **Numbers**:
  - Raw AOT throughput: **~800.2M physical-iters/sec** (62.48 ms over 50M physical iterations).
  - HYDRON semantic elimination: **0.40 ms** (~156.7x reduction factor).
  - Bootstrap: Gen1 == Gen2 == Gen3 bit-for-bit self-hosting convergence (49,152 bytes PE32+, 57,951 bytes ELF64, 0 differences).

## 1. Current Status & Verification
- **Full Test Suite Status**: `PASS` (Run with `./nexus test`)
  - **Suites**: 27 example suites passing (+`recursion_showcase.nex`, `rocket_physics.nex`, etc.)
  - **Compiler Diagnostics**: 13/13 passing
  - **Foundation Tests**: 50/50 passing
  - **Type Inference Suite**: 7/7 categories passing (`tests/test_type_inference.nex`)
  - **Stdlib Assertions**: 122/122 passing
  - **Compiler Optimizer**: 52/52 passing (`tests/optimizer_test.nex`)
  - **Interactive Demos**: 3/3 passing
  - **Standard Priority Suites**: 
    - `tests/test_string_escapes.nex`: PASS (Phase 1 escapes: `\n`, `\t`, `\r`, `\\`, `\"`, `\0`, `\e`, `\xHH`)
    - `tests/test_bitwise_ops.nex`: 20/20 PASS (native bitwise `&`, `|`, `^`, `~`, `<<`, `>>`)
    - `tests/test_hex_binary.nex`: 18/18 PASS (native `0x...` hex and `0b...` binary literals)
    - `tests/test_vec.nex`: 11/11 PASS (dynamic growable heap vector)
    - `tests/test_hashmap.nex`: 13/13 PASS (string-keyed linear probing hash map)
    - `tests/test_poll_events.nex`: 13/13 PASS (non-blocking `poll`/`epoll` async event loop)
    - `tests/test_piece_table.nex`: PASS (Feature #67 Piece Table: insert, delete, coalescing, slice export, line navigation, atomic snapshot undo/redo)
    - `tests/test_syntax_v83.nex`: PASS (Features #30, #35, #36: Octal literals `0o...`, increment/decrement `++`/`--`, ternary expressions `cond ? a : b`)
    - `tests/test_loop_ranges_v84.nex`: PASS (Features #37, #50, #59: Compile-time `const`, range loops `0..N` / `0..=N` with `step`, null coalescing `??`)
    - `tests/test_hashset.nex`: 117/117 PASS (Feature #71 string/integer hash set: add/add_str/contains/contains_str/remove/remove_str/len with tombstones & rehash)
    - `tests/test_hydron_correctness.nex`: 13/13 PASS (boundary & scoping verification)
    - `tests/test_hydron_overflow.nex`: 21/21 PASS (64-bit integer overflow & boundary safety)
- **Determinism**: Gen 1 == Gen 2 == Gen 3 bit-for-bit self-hosting parity verified via `./nexus bootstrap` and `./nexus self-host`.
- **Dual-Benchmark Architecture**:
  - **Category A: Raw Native AOT Code Generation (`./nexus bench`)**: Measures physical CPU instruction throughput on un-eliminated loops across 50,000,000 physical iterations: **62.48 ms (~800.2M physical-iters/sec)** with 0% libc and direct Linux kernel syscalls.
  - **Category B: Semantic Computation Elimination (`./nexus hydron`)**: Phase 4 SCEV Recurrence Attractor Solver analyzes non-linear recurrence loops ($acc_{n+1} = \lfloor(acc_n + n) / 2\rfloor$), executes ~64 convergence steps, verifies fixed-point manifold invariance ($acc == i - 2$) via runtime guard, and derives closed-form result in **0.40 ms (~156.7x computation reduction)**.
- **Scientific Methodology**: Accurately distinguishes raw CPU instruction execution from semantic computation elimination / superoptimization.

---

## 2. Recent Milestones Completed
- **NEXUS Feature #71 — Hash Set (`stdlib/hashset.nex`) (2026-10-16)**:
  - **String/Integer HashSet (`stdlib/hashset.nex`)**: Open-addressing set collection (modeled on `stdlib/hashmap.nex`) storing unique 64-bit integer AND null-terminated string elements.
  - **Capabilities**: `nx_hashset_new` (capacity 16), `nx_hashset_add` / `nx_hashset_add_str` (returns 1 inserted / 0 duplicate — duplicate elimination), `nx_hashset_contains` / `nx_hashset_contains_str` (O(1) membership), `nx_hashset_remove` / `nx_hashset_remove_str` (tombstone-based removal, probing chains stay correct), `nx_hashset_len`/`nx_hashset_cap`/`nx_hashset_clear`.
  - **Design**: Per-slot state byte (`0`=empty, `1`=integer, `2`=string, `3`=tombstone) allows integer element `0` as a fully valid member (no sentinel collision like hashmap key 0); strings compared by CONTENT via `nx_str_cmp` so distinct literals with equal bytes count as one element; automatic capacity doubling with full rehash whenever live count reaches 1/2 load (16→32→64→128, exercised by tests).
  - **Toolchain notes**: The canonical (git-tracked) `bin/nexc.*` toolchain rejects arithmetic expressions inside `if`/`while` conditions ("Condition Expr Trap") and renders chained numeric expressions left-to-right (verified: `1000 + i * 5 == (1000 + i) * 5`) — grow-trigger conditions use precomputed load temporaries, and tests precompute strides accordingly.
  - **Verification**: `tests/test_hashset.nex` — 117/117 PASS on native Linux ELF64 (`./nexus run`); integrated into `./nexus test` (hashset suite block, alongside piece_table pattern).
- **NEXUS v8.5 — Modern Loop & Range Ergonomics Bundle (Features #37, #50, #59) (2026-10-14)**:
  - **Feature #37: Compile-Time Constants (`const`)**: Full support for `const NAME = <val_or_expr>` across top-level and local scopes. Immediate integer arithmetic chains (e.g. `4 * 10` -> `40`) are folded at compile time, and constant identifiers are substituted as immediate values outside strings and comments, eliminating unnecessary stack/heap allocations and memory loads.
  - **Feature #50: Range Expressions in Loops (`start..end`, `start..=end`)**: Exclusive (`..`) and inclusive (`..=`) range syntax for clean loops `for i in 0..10 { ... }`, with optional positive/negative stride syntax (`for i in 0..10 step 2 { ... }`, `for i in 10..0 step -2 { ... }`). Seamlessly desugared to canonical compiler headers with full lexical scoping and stack frame registration.
  - **Feature #59: Null Coalescing Operator (`??`)**: Concise fallback expressions `let title = opt_title ?? "Untitled"`. Left-to-right desugaring with scoped `_nc_` / `_nc_res_` temporaries and conditional branches guarantees short-circuit execution: the fallback expression is never evaluated when LHS is non-zero. Hardened string scanning in `desugar_bitwise_binop` to prevent `=` inside string literals from triggering false assignments.
  - **Multi-Platform Verification & Self-Hosting Parity**:
    - Direct Linux ELF64: PASS (`./nexus run tests/test_loop_ranges_v84.nex`).
    - Reference Semantic Interpreter: PASS (`./nexus interpret tests/test_loop_ranges_v84.nex`).
    - Apple Silicon ARM64 Mach-O: PASS (`./nexus arm64 tests/test_loop_ranges_v84.nex`).
    - Linux AArch64 ELF64: PASS (`./nexus arm64 --target linux tests/test_loop_ranges_v84.nex`).
    - Full Test Suite: 29/29 PASS (`./nexus test`).
    - Self-Hosting Determinism: 100% bit-for-bit identical fixed point (`./nexus self-host`, 0 byte differences across PE32+ and ELF64).
- **NEXUS v8.4 — Syntax Ergonomics Bundle (Features #30, #35, #36) (2026-10-14)**:
  - **Feature #30: Octal Number Literals**: Full support for POSIX file permissions and bitmasks via `0o...` / `0O...` (e.g., `0o755`, `0o644`, `0o10`). Transparently desugared in `tools/nexprep.c` to decimal literals outside string literals and comments, and supported natively in `tools/nexinterp.py`.
  - **Feature #35: Increment / Decrement Operators (`++`, `--`)**: Postfix and prefix `++`/`--` across standalone statements (`i++`, `i--`, `++i`, `--i`), optional declaration prefixes (`let count++`), struct field modifications (`pt.x++`, `pt.y--`), and both 2-comma (`for i = 0, i < 10, i++ {`) and 3-comma (`for i, 0, 10, i++ {`) loop headers. Emits clean scalar updates preserving comments and stack balance.
  - **Feature #36: Ternary / Conditional Expressions (`cond ? true_val : false_val`)**: Native inline conditional expressions across variable assignments, return statements, and function argument lists. Right-associative chained else-if evaluation (`a ? 1 : b ? 2 : 3`) and short-circuit evaluation via `if ... { let _tern_N = ... } else { let _tern_N = ... }` ensuring branch isolation (e.g. recursive `fib(n)`).
  - **Multi-Platform Verification & Self-Hosting Parity**:
    - Direct Linux ELF64: PASS (`./nexus run tests/test_syntax_v83.nex`).
    - Reference Semantic Interpreter: PASS (`./nexus interpret tests/test_syntax_v83.nex`).
    - Apple Silicon ARM64 Mach-O: PASS (`./nexus arm64 tests/test_syntax_v83.nex`).
    - Linux AArch64 ELF64: PASS (`./nexus arm64 --target linux tests/test_syntax_v83.nex`).
    - Full Test Suite: 28/28 PASS (`./nexus test`).
    - Self-Hosting Determinism: 100% bit-for-bit identical fixed point (`./nexus self-host`, 0 byte differences across PE32+ and ELF64).
- **NEXUS v8.3 — Feature #67 Piece Table Text Buffer (2026-10-14)**:
  - **Production-Grade Text Buffer (`stdlib/piece_table.nex`)**: Solves the 16KB static buffer limit in `examples/nexus_ide.nex`. Implements a two-buffer piece table (read-only original buffer + append-only add buffer) over a 40-byte doubly-linked piece chain.
  - **Capabilities**: O(1) contiguous keystroke coalescing, middle insertion via piece splitting, arbitrary range deletion with boundary trimming and node unlinking, character lookup (`nx_pt_get_char`), whole/slice buffer export (`nx_pt_export_to_buf`, `nx_pt_export_slice`), line count and 1-based line offset navigation (`nx_pt_line_count`, `nx_pt_line_to_offset`), and atomic snapshot undo/redo (`nx_pt_snapshot`, `nx_pt_restore`).
  - **Verification**: Verified 100% PASS on native Linux ELF64 (`./nexus run tests/test_piece_table.nex`), reference interpreter (`./nexus interpret tests/test_piece_table.nex`), and Apple Silicon ARM64 Mach-O (`./nexus arm64 tests/test_piece_table.nex`). Integrated into `./nexus test`.
- **NEXUS v8.2+ — Phase 1 String Escapes (2026-10-13)**:
  - **Self-Hosted Core Decoding (`compiler/nexc.nex`)**: Implemented `parse_str_lit` subroutine, decoding `\n` (10), `\r` (13), `\t` (9), `\0` (0), `\e` (27), `\"` (34), `\\` (92), and `\xHH` directly into read-only binary `.rdata`.
  - **Comprehensive Compiler Integration**: Connected `parse_str_lit` across all 5 string parsing sites: `let <dest> = "..."`, `print "..."`, `len "..."`, `file_open`, and `file_create`.
  - **Lexer Token Collision Guard**: Carefully structured local decoder identifiers to avoid float keyword collisions (`fa`, `fs`, `fm`, `fd`, `fn`, `ft`).
  - **String Preprocessor & Ecosystem Updates**:
    - Updated string scanner loops in `tools/nexprep.c` to handle escaped backslashes and quotes without premature boundary toggling.
    - Updated constant folding in `fold_unary_builtins` for `len "..."` to accurately count decoded byte lengths.
    - Updated ARM64 instruction emitter (`tools/nexarm64.c`) with `\e` and `\xHH` support in `add_string`.
    - Updated reference semantic interpreter (`tools/nexinterp.py`) with escape unescaping and block `} else {` execution.
  - **Tri-Platform Windows & Linux Parity**:
    - Added `#ifdef OUT / #undef OUT` guard to `tools/nexprep.c` preventing collisions with Windows SDK `<windef.h>`.
    - Cross-compiled `bin/nexprep.exe` and `bin/nexarm64.exe` using MinGW.
    - Preserved 100% of Windows toolchain (`tools/tcc`, `nexus.cmd`, `nexus.ps1`, `build_all.ps1`).
  - **Regression Test Suite**: Created `tests/test_string_escapes.nex` verifying all escapes with `load`, `len`, `print`, and `assert_eq`. Integrated into `./nexus test` (PASS).
  - **Deterministic Self-Hosting Convergence**: `./nexus bootstrap` and `./nexus self-host` verified bit-for-bit identical across all stages (Gen1 == Gen2 == Gen3, 49,152 bytes PE32+, 57,951 bytes ELF64).

- **NEXUS v8.2 — Native Windows Port & Zero-Dependency Toolchain**:
  - **Zero-Dependency Native Windows Toolchain**: Self-contained toolchain setup using bundled Tiny C Compiler (`tools/tcc/tcc/tcc.exe`, 480 KB) and built-in Windows .NET C# compiler (`csc.exe`), eliminating any requirements for Visual Studio, C++ SDKs, Python, or Linux/WSL.
  - **All 13 Toolchain & Diagnostic Binaries Built as Native Windows EXEs**:
    - Core Toolchain: `bin/nexprep.exe`, `bin/nexfmt.exe`, `bin/nexelf.exe`, `bin/nexmacho.exe`, `bin/nexarm64.exe`, `bin/nexir.exe`.
    - Diagnostic Suite: `bin/nexpedump.exe`, `bin/nexdisasm.exe`, `bin/nexdiff.exe`, `bin/nexdebug.exe`, `bin/nexfuzz.exe`, `bin/nexbench.exe`, `bin/nextest.exe`.
  - **Native Stack Reserve Patching (`tools/patch_stack.ps1`)**: Modified PE header `SizeOfStackReserve` to 64 MB (`0x04000000`), completely resolving MSVCRT stack overflow exceptions (0xC00000FD) during recursive AST traversals in `nexprep.exe`.
  - **Win32 ABI Calling Convention & Shadow Space Fix**: Resolved fatal Access Violations (0xC0000005) inside functions by dynamically aligning RSP to 16 bytes and allocating 32 bytes of shadow space prior to Win32 API calls (`VirtualAlloc` in `alloc` statements and `WriteFile`).
  - **Lexer Prefix Collision Fix in `compiler/nexc.nex`**: Corrected 4-character keyword detection for `itof` (`i-t-o-f`), preventing loop counter variables like `iter` from erroneously triggering float conversions and infinite loops.
  - **Unified Windows CLI Drivers (`nexus.cmd` & `nexus.ps1`)**: Complete parity with Linux `./nexus` driver supporting `nexus run`, `nexus build`, `nexus compile`, `nexus test`, `nexus self-host`, `nexus bench`, `nexus hydron`, `nexus disasm`, `nexus dump`, `nexus diff`, `nexus debug`, `nexus fmt`, `nexus elf`, `nexus macho`, `nexus arm64`, `nexus ir`.
  - **Automated Master Build Pipeline (`build_all.ps1`)**: Automated 6-step build, verification, multi-platform conversion, and Stage 5 self-hosting bit-for-bit parity check.
  - **Stage 5 Bit-for-Bit Self-Hosting Parity on Windows**: Verified that `compiler/nexc.exe` compiling `compiler/nexc.nex` converges to a bit-for-bit identical fixed point (**0 byte differences across all 49,152 bytes**).
  - **Test Suite Pass Rate**: 37/39 tests in `tests/` passing (94.9%) and 28/37 in `examples/` passing natively on Windows.

- **NEXUS v8.1 / Milestone 2 — 10 Priority Systems & Language Capabilities**:
  - **Hex & Binary Literals (`0x...`, `0b...`)**: Upgraded `compiler/nexc.nex` `parse_uint` to parse base-16 and base-2 literals natively, eliminating decimal manual translations for bitmasks and X11 graphics.
  - **Native Bitwise Operators & Compound Updates**: Added `&`, `|`, `^`, `~`, `<<`, `>>` and compound operators `&=`, `|=`, `^=`, `<<=`, `>>=` in `stdlib/bitwise.nex` and `tools/nexprep.c`.
  - **Dynamic Array (`Vec`)**: Added heap-backed growable array in `stdlib/vec.nex` with amortized O(1) capacity doubling (`vec_push`, `vec_pop`, `vec_get`, `vec_set`, `vec_free`).
  - **Associative Hash Map (`HashMap`)**: Implemented linear probing hash map in `stdlib/hashmap.nex` with string hashing, dynamic slot storage, and O(1) lookups.
  - **Non-Blocking Asynchronous Event Loop (`poll`/`epoll`)**: Direct Linux kernel syscall wrappers (`sys_poll`, `sys_epoll_create1`, `sys_epoll_ctl`, `sys_epoll_wait`) in `stdlib/poll.nex` enabling responsive UI event loops.
  - **N-IR ARM64 Machine Codegen**: Added standalone Linux aarch64 ELF64 (`--emit-aarch64-elf`) and macOS Apple Silicon Mach-O (`--emit-aarch64-macho`) emitters to `tools/nexir.c`.
  - **Automated Delta Debugger / Test Reducer**: Built `tools/nexdelta.py` (`./nexus delta <failing.nex>`) to automatically minimize failing differential fuzzer test cases down to minimal reproducers.
  - **HYDRON 64-bit Overflow & Boundary Suite**: Verified that recurrence attractor transformations adhere to machine modulo $2^{64}$ limits across `INT64_MAX`, negative loop strides, and division boundaries (`tests/test_hydron_overflow.nex`).
  - **Official Deterministic 3-Stage Bootstrap**: Created `tools/nexbootstrap.py` and top-level `./nexus bootstrap` driver proving Gen 1 == Gen 2 == Gen 3 bit-for-bit self-hosting convergence.
- **NEXUS v8 Compiler Architecture (Milestone 1 — N-IR Pipeline & Native ELF64 Codegen)**:
  - **Typed CFG Intermediate Representation**: Implemented 3-address basic-block-structured IR with typed virtual registers, memory slots (`alloc`, `load64`, `store64`), and explicit CFG terminators (`br`, `br_cond`, `ret`, `call`).
  - **4-Pass N-IR Optimizer Pipeline (`tools/nexir.c`)**:
    - *Pass 1*: Constant Folding & Local Propagation within basic blocks.
    - *Pass 2*: Algebraic Simplification & Strength Reduction ($x + 0 \to x$, $x \times 1 \to x$, $x \times 2^k \to x \ll k$, $x \oplus x \to 0$).
    - *Pass 3*: Dead Code Elimination (DCE) on unreachable CFG blocks and unused pure virtual registers.
    - *Pass 4*: HYDRON IR Recurrence & Attractor Solver — lifts HYDRON from textual preprocessor into CFG loop analysis, solving $O(N)$ recurrence loops in $O(1)$ directly in the IR.
  - **Native x86-64 Machine Codegen & Standalone Linux ELF64 Emitter (`./nexus ir --emit-elf`)**:
    - Translates typed N-IR directly into executable x86-64 machine code instructions.
    - Two-pass label resolution and relocation linker for relative jumps and function calls.
    - Direct Linux kernel syscall dispatch (0% libc, 0% external runtime) for integer printing (`sys_write`), string output, dynamic heap allocation, and clean process exit (`sys_exit`).
    - Packages output into standalone, self-contained Linux ELF64 executable binaries (`0x401000` entry point).
  - **Direct Execution N-IR VM**: Integrated 64-bit virtual machine in `bin/nexir` executing raw and optimized N-IR directly with 16MB simulated physical memory.
  - **7-Way Differential Testing Oracle (`tools/nexfuzz.py`)**: Randomized differential fuzzer verifying bit-for-bit semantic identity across:
    $$\text{Reference Interpreter} \equiv \text{nexc (-O0)} \equiv \text{nexc (HYDRON)} \equiv \text{N-IR VM (-O0)} \equiv \text{N-IR VM (--opt)} \equiv \text{N-IR ELF (-O0)} \equiv \text{N-IR ELF (--opt)}$$
  - **Toolchain Driver**: Added `./nexus ir [flags] <file.nex>` supporting `--opt`, `--run`, `--emit-elf`, and IR dumping.
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
