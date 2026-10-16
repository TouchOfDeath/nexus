# NEXUS Built-ins and Standard Library Reference

Two layers ship with NEXUS:

1. **Compiler built-ins** — keywords burned into `nexc` itself
2. **Standard library** — pure NEXUS source in `stdlib/`, loaded with one
   `include "stdlib/nstdlib.nex"` line

---

## Part 1 — Compiler built-ins

### Output and input

| Statement | Behavior |
|-----------|----------|
| `print "text"` | print string literal (supports `\n`, `\t`, `\r`, `\\`, `\"`, `\0`, `\e`, `\xHH`) + CRLF |
| `print <var>` | print signed 64-bit integer variable + CRLF |
| `print <int>` | print integer literal + CRLF |
| `print_str <var>` | print string variable contents + CRLF |
| `print_str "text"` | print string literal + CRLF |
| `print_float <var>` | print 64-bit float variable formatted with 6 decimal places |
| `read <var>` | read one signed decimal integer line from stdin |

### Expressions (right side of `let`)

| Form | Result |
|------|--------|
| `alloc <n>` | pointer to n zero-initialized bytes (VirtualAlloc / mmap) |
| `load [p + i]` | read 8-bit byte (0..255) from address `p + i` |
| `load64 [p + i]` | read 64-bit signed integer from address `p + i` |
| `load64 [p + i * 8]` | read 64-bit integer with scaled variable index |
| `load32 [p + i]` | read 32-bit integer doubleword from address `p + i` |
| `load16 [p + i]` | read 16-bit integer word from address `p + i` |
| `"text"` | pointer to null-terminated string in `.rdata` (decoded escape sequences) |
| `len s` | byte length of string variable or literal (decoded bytes, excluding terminator) |
| `abs <x>` | absolute value; `<x>` is a variable, integer, or negative literal like `abs -9` |
| `fadd / fsub / fmul / fdiv` | 64-bit scalar float arithmetic (`xmm0`, `xmm1`) |
| `fsqrt <x>` | 64-bit scalar float square root |
| `fneg <x>` | 64-bit float negation |
| `itof <x>` / `ftoi <x>` | integer-to-float / float-to-integer conversion |
| `frame_pointer()` | returns current stack frame pointer (`RBP` / `x29`) |
| `stack_pointer()` | returns current runtime stack pointer (`RSP` / `sp`) |
| `frame_parent(fp)` | reads caller frame pointer from linked stack frame |
| chain `a + b * c` | left-to-right fold; no precedence, no parentheses |

### Memory writes

| Statement | Behavior |
|-----------|----------|
| `store [p + i] <v>` | write 8-bit low byte of `<v>` at `p + i` |
| `store64 [p + i] <v>` | write 64-bit quadword of `<v>` at `p + i` |
| `store64 [p + i * 8] <v>` | write 64-bit quadword with scaled index at `p + i * 8` |
| `store32 [p + i] <v>` | write 32-bit doubleword of `<v>` at `p + i` |
| `store16 [p + i] <v>` | write 16-bit word of `<v>` at `p + i` |

`file_open`/`file_create`/`file_read`/`file_write`/`file_close` wrap the
platform file APIs with identical syntax on Windows and Linux.

### Assertion macros
The preprocessor transparently expands assertion statements into counting compare-and-report blocks:
- `assert_eq <exprA>, <exprB>`: fails if expressions evaluate to different values.
- `assert_ne <exprA>, <exprB>`: fails if expressions evaluate to identical values.

---

## Part 2 — Standard library

### Calling convention

Every stdlib function follows one convention:

```nex
let nx_i0 = 1071      # 1. set inputs
let nx_i1 = 462
call nx_gcd           # 2. call
print nx_r0           # 3. read the result (prints 21)
```

| Slot | Meaning |
|------|---------|
| `nx_i0`, `nx_i1`, `nx_i2` | inputs, set before the call |
| `nx_r0` | result, read after the call |
| `nx_s0..nx_s7` | internal scratch, clobbered by EVERY stdlib call |

Rules for user programs:

- never name your own variables with the `nx_` prefix
- never assume any `nx_*` variable keeps its value across a stdlib call
- stdlib functions never call each other, so the scratch pool is safe between
  calls and the whole library costs only 12 variable slots

### Math — `stdlib/math.nex`

| Function | Signature | Notes |
|----------|-----------|-------|
| `nx_max` | (a=nx_i0, b=nx_i1) -> nx_r0 | larger of two |
| `nx_min` | (a, b) -> nx_r0 | smaller of two |
| `nx_clamp` | (v=nx_i0, lo=nx_i1, hi=nx_i2) -> nx_r0 | constrain v to closed interval |
| `nx_sign` | (v) -> nx_r0 | -1, 0 or 1 |
| `nx_pow` | (base=nx_i0, exp=nx_i1) -> nx_r0 | integer power; negative exp gives 0; 0^0 = 1 |
| `nx_isqrt` | (v) -> nx_r0 | floor of square root via Newton; negative input gives -1 |
| `nx_gcd` | (a, b) -> nx_r0 | Euclid; absolute values; gcd(0,0)=0 |
| `nx_lcm` | (a, b) -> nx_r0 | absolute values; lcm(0,x)=0 |
| `nx_fib` | (n) -> nx_r0 | iterative; fib(0)=0, fib(1)=1; negative gives -1 |
| `nx_is_prime` | (n) -> nx_r0 | 1 if prime else 0; trial division; n<2 gives 0 |
| `nx_digits` | (n) -> nx_r0 | decimal digit count of the absolute value; 0 has 1 digit |

### Byte arrays — `stdlib/arrays.nex`

An array is an `alloc` buffer; element i lives at `[p + i]` — one byte,
values 0..255, memory is zero-initialized.

| Function | Signature | Notes |
|----------|-----------|-------|
| `nx_arr_fill` | (buf=nx_i0, n=nx_i1, val=nx_i2) | set every element to val |
| `nx_arr_iota` | (buf, n) | fill with 0,1,2,... (n up to 256) |
| `nx_arr_sum` | (buf, n) -> nx_r0 | 64-bit accumulator |
| `nx_arr_max` | (buf, n) -> nx_r0 | empty gives 0 |
| `nx_arr_min` | (buf, n) -> nx_r0 | empty gives 255 |
| `nx_arr_index_of` | (buf, n, val=nx_i2) -> nx_r0 | first index or -1 |
| `nx_arr_reverse` | (buf, n) | in place |
| `nx_arr_count` | (buf, n, val) -> nx_r0 | occurrences of val |

### Strings — `stdlib/strings.nex`

Strings are pointers to null-terminated bytes (string literal variables or
`alloc` buffers both work). Char codes are ASCII: 'A'=65, 'a'=97, '0'=48.

| Function | Signature | Notes |
|----------|-----------|-------|
| `nx_str_len` | (s=nx_i0) -> nx_r0 | byte length excluding terminator |
| `nx_str_copy` | (src=nx_i0, dst=nx_i1) | dst buffer must be large enough |
| `nx_str_cmp` | (a=nx_i0, b=nx_i1) -> nx_r0 | -1 / 0 / 1, byte-wise |
| `nx_str_concat` | (a=nx_i0, b=nx_i1, dst=nx_i2) | dst must hold len(a)+len(b)+1 |
| `nx_str_chr` | (s, ch=nx_i1) -> nx_r0 | first index of ch or -1 |
| `nx_str_starts_with` | (s=nx_i0, prefix=nx_i1) -> nx_r0 | 1 if matched; empty prefix matches |
| `nx_str_ends_with` | (s=nx_i0, suffix=nx_i1) -> nx_r0 | 1 if matched; empty suffix matches |
| `nx_str_trim` | (src=nx_i0, dst=nx_i1) -> nx_r0 | copies without edge ASCII whitespace; returns byte length; dst must hold source length + 1 |
| `nx_str_to_int` | (s) -> nx_r0 | decimal parse; optional leading '-'; non-digits ignored |
| `nx_int_to_str` | (v=nx_i0, buf=nx_i1) | writes decimal text of v into buf (needs 12 bytes) |

### Dynamic Vectors — `stdlib/vec.nex`

A dynamically resizable heap-allocated vector of 64-bit signed integers (`Vec<i64>`).
Implements geometric doubling for amortized O(1) appends.

Layout in memory (24-byte header struct on heap):
- Offset `0`: `data_ptr` (pointer to qword array)
- Offset `8`: `length` (current item count)
- Offset `16`: `capacity` (allocated slot count)

| Function | Signature | Notes |
|----------|-----------|-------|
| `nx_vec_new` | () -> nx_r0 | Allocates new vector with initial capacity 4 |
| `nx_vec_push` | (vec=nx_i0, val=nx_i1) | Appends element to back with automatic geometric growth |
| `nx_vec_get` | (vec=nx_i0, idx=nx_i1) -> nx_r0 | Retrieves 64-bit element at index (bounds checked, returns 0 if OOB) |
| `nx_vec_set` | (vec=nx_i0, idx=nx_i1, val=nx_i2) | Overwrites element at index (bounds checked) |
| `nx_vec_len` | (vec=nx_i0) -> nx_r0 | Current element count |
| `nx_vec_cap` | (vec=nx_i0) -> nx_r0 | Current capacity |
| `nx_vec_pop` | (vec=nx_i0) -> nx_r0 | Removes and returns last element (returns 0 if empty) |
| `nx_vec_clear` | (vec=nx_i0) | Resets length to 0 (preserves allocated buffer) |
| `nx_vec_grow` | (vec=nx_i0, min_cap=nx_i1) | Internal doubling resize subroutine |

### Hash Maps — `stdlib/hashmap.nex`

An open-addressing hash table (`HashMap<i64, i64>`) mapping 64-bit keys to 64-bit values with linear probing.

Layout in memory (32-byte header struct on heap):
- Offset `0`: `keys_ptr` (pointer to key array)
- Offset `8`: `vals_ptr` (pointer to value array)
- Offset `16`: `count` (number of active entries)
- Offset `24`: `capacity` (number of hash slots, initially 16)

| Function | Signature | Notes |
|----------|-----------|-------|
| `nx_hashmap_new` | () -> nx_r0 | Allocates hashmap with initial capacity 16 |
| `nx_hashmap_put` | (map=nx_i0, key=nx_i1, val=nx_i2) | Inserts or updates key/value pair |
| `nx_hashmap_get` | (map=nx_i0, key=nx_i1) -> nx_r0 | Returns value for key, or 0 if not found |
| `nx_hashmap_contains` | (map=nx_i0, key=nx_i1) -> nx_r0 | Returns 1 if key exists, 0 otherwise |
| `nx_hashmap_len` | (map=nx_i0) -> nx_r0 | Number of entries stored |
| `nx_hashmap_cap` | (map=nx_i0) -> nx_r0 | Total bucket capacity |
| `nx_hashmap_clear` | (map=nx_i0) | Clears all buckets and resets count to 0 |
| `nx_hash_int` | (val=nx_i0) -> nx_r0 | Fast integer hash subroutine |

### Bitwise Operations — `stdlib/bitwise.nex`

Software-emulated 64-bit bitwise primitives and shifts. When using operator syntax (`&`, `|`, `^`, `~`, `<<`, `>>`), `nexprep.c` automatically desugars them to these functions:

| Function | Operator | Signature | Notes |
|----------|----------|-----------|-------|
| `nx_band` | `a & b` | (a=nx_i0, b=nx_i1) -> nx_r0 | Bitwise AND across 64 bits |
| `nx_bor` | `a \| b` | (a=nx_i0, b=nx_i1) -> nx_r0 | Bitwise OR across 64 bits |
| `nx_bxor` | `a ^ b` | (a=nx_i0, b=nx_i1) -> nx_r0 | Bitwise XOR across 64 bits |
| `nx_bnot` | `~a` | (a=nx_i0) -> nx_r0 | Bitwise NOT (two's complement: `-a - 1`) |
| `nx_shl` | `a << n` | (a=nx_i0, n=nx_i1) -> nx_r0 | Logical left shift (`a * (2 ** n)`) |
| `nx_shr` | `a >> n` | (a=nx_i0, n=nx_i1) -> nx_r0 | Arithmetic right shift (sign preserving) |
| `nx_bit_set` | - | (a=nx_i0, bit=nx_i1) -> nx_r0 | Returns 1 if bit `bit` is 1, else 0 |

### Asynchronous Event Polling — `stdlib/poll.nex`

Linux kernel `poll(2)` and `epoll(7)` event multiplexing primitives for scalable asynchronous I/O loops.

| Function | Signature | Notes |
|----------|-----------|-------|
| `nx_poll` | (fd=nx_i0, events=nx_i1, timeout_ms=nx_i2) -> nx_r0 | Blocking synchronous poll on single fd |
| `nx_poll_nb` | (fd=nx_i0, events=nx_i1) -> nx_r0 | Non-blocking poll (timeout = 0) |
| `nx_epoll_create` | () -> nx_r0 | Creates epoll instance via `sys_epoll_create1` |
| `nx_epoll_ctl` | (epoll_fd=nx_i0, op=nx_i1, fd=nx_i2) -> nx_r0 | Modifies epoll interest set with `EPOLLIN` |
| `nx_epoll_ctl_full` | (epoll_fd=nx_i0, op=nx_i1, fd=nx_i2) -> nx_r0 | Uses custom mask set in global `nx_ev_mask` |
| `nx_epoll_wait` | (epoll_fd=nx_i0, max_ev=nx_i1, timeout_ms=nx_i2) -> nx_r0 | Waits for events, returns ready count |
| `nx_epoll_event_fd` | (idx=nx_i1) -> nx_r0 | Extracts fd from event index `idx` |
| `nx_epoll_event_events` | (idx=nx_i1) -> nx_r0 | Extracts events bitmask from event index `idx` |
| `nx_eventfd` | (initval=nx_i0) -> nx_r0 | Creates Linux eventfd inter-process counter |
| `nx_pipe` | () -> (nx_pipe_read, nx_pipe_write) | Creates pipe via `sys_pipe2` |
| `nx_close` | (fd=nx_i0) -> nx_r0 | Closes file descriptor |

### Additional Modules Directory

| Module | Primary Functionality |
|--------|-----------------------|
| `stdlib/assert.nex` | Unit testing primitives: `assert_eq`, `assert_ne`, `assert_gt`, `assert_ge`, `assert_lt`, `assert_le` |
| `stdlib/complex.nex` | Complex number arithmetic: `nx_complex_add`, `nx_complex_mul`, polar/magnitude operations |
| `stdlib/linalg.nex` | Matrix operations: 2D matrix multiplication, determinants, vector dot products |
| `stdlib/number_theory.nex` | Extended Euclidean GCD, modular inverse, Euler totient, Miller-Rabin primality |
| `stdlib/calculus.nex` | Numerical differentiation (central difference) and Simpson's rule numerical integration |
| `stdlib/combinatorics.nex` | Factorials, permutations ($P(n, k)$), combinations ($C(n, k)$), Catalan numbers |
| `stdlib/sets.nex` | Bitset and dynamic integer set operations (union, intersection, difference) |
| `stdlib/statistics.nex` | Mean, variance, standard deviation, median, quantile analysis |
| `stdlib/math_trig.nex` | Fixed-point Taylor approximations for $\sin$, $\cos$, $\tan$, $\arctan$ |
| `stdlib/x11.nex` & `framebuffer.nex` | Low-level graphics: X11 socket protocol client and raw framebuffer blitting |

### Worked example

```nex
include "stdlib/nstdlib.nex"
include "stdlib/vec.nex"

let who = "prime count below 50:\n"
print who

let buf = alloc 50
let nx_i0 = buf
let nx_i1 = 50
call nx_arr_iota

# Collect primes in dynamic Vec
call nx_vec_new
let prime_vec = nx_r0

let scan = 2
while scan < 50 {
    let nx_i0 = scan
    call nx_is_prime
    if nx_r0 == 1 {
        let nx_i0 = prime_vec
        let nx_i1 = scan
        call nx_vec_push
    }
    let scan = scan + 1
}

let nx_i0 = prime_vec
call nx_vec_len
print "Total primes found: "
print nx_r0
print "\n"
```

Prints 15 — the primes 2,3,5,7,11,13,17,19,23,29,31,37,41,43,47.

---

## Part 3 — Verification

Everything above is machine-checked:

- `tests/stdlib_test.nex` — 108 assertions across math, byte arrays, strings
- `tests/test_vec.nex` — Dynamic vector reallocation, push, pop, indexing
- `tests/test_hashmap.nex` — Open-addressing hashmap put, get, collision probing
- `tests/test_bitwise_ops.nex` — Bitwise operations desugared and verified
- `tests/test_poll_events.nex` — Async epoll and eventfd multiplexing
- `tests/test_string_escapes.nex` — Universal string escape sequences across Linux/Win/macOS
- `examples/stdlib_demo.nex` — Runnable tour of standard library modules
- `./nexus test` runs all 41 test suites and verifies 100% pass rate
