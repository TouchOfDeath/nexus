# NEXUS Tutorial: From Hello World to the Standard Library

A hands-on introduction to NEXUS — the self-hosting native language.
Every step shows the exact commands to run. Total time: about 30 minutes.

Prerequisites: a Linux x86-64 machine (or WSL2) with `gcc` installed.
That is all — NEXUS needs no runtime, no VM and no interpreter.

---

## Step 0: Verify the toolchain

From the project root:

```bash
./nexus run examples/hello_world.nex
```

You should see:

```
====================================
Hello, Bare-Metal x86-64 World!
Zero Percent C#, Zero Percent .NET!
Compiled Ahead-Of-Time by NEXUS
====================================
```

What just happened: your `.nex` source was preprocessed, compiled by the
self-hosted compiler `nexc.elf` (which is itself written in NEXUS), and the
resulting native binary executed directly by the Linux kernel.

The three commands you will use most:

```bash
./nexus run <file.nex>       # compile + execute, shows program output
./nexus repl                 # interactive playground with stdlib loaded
./nexus test                 # run the full regression suite
```

---

## Step 1: Variables, number bases, and arithmetic

Create `scratch/step1.nex`:

```nex
let price = 120         # Decimal integer (also supports 0x78 hex, 0b1111000 bin, 0o170 octal)
let quantity = 4
let total = price * quantity
total += 20             # Compound assignment: +=, -=, *=, /=, %=
print total
print "total computed\n"

# Arithmetic precedence: * and / bind tighter than + and -
let expr1 = 1 + 2 * 3   # 7 (not 9)
let expr2 = (1 + 2) * 3 # 9 (parentheses supported for grouping)
```

Run it: `./nexus run scratch/step1.nex` → prints `500`.

Key facts:
- `let` declares AND assigns. Re-assigning can be done with `let var = val` or `var = val`.
- `const` declares compile-time immutable values: `const TAX_RATE = 5`.
- Integer literals support Decimal (`120`), Hexadecimal (`0x78`), Binary (`0b1010`), and Octal (`0o755`).
- Arithmetic expressions follow standard operator precedence: `*`, `/`, `%` precede `+`, `-`.
- Parentheses `(...)` are supported for explicit sub-expression grouping.

---

## Step 2: Text output, string escapes, and input

```nex
print "What is your age?\n> "
read age
print "You entered: "
print age
print "\nDone!\tExiting...\n"
print_str "Status: \"OK\"\\processed\n"
```

- `print` handles integers, string *literals*, and string variables.
- Standard C-style escape sequences are fully supported in all string literals:
  - `\n` (newline, ASCII 10)
  - `\t` (horizontal tab, ASCII 9)
  - `\r` (carriage return, ASCII 13)
  - `\\` (literal backslash, ASCII 92)
  - `\"` (literal double quote, ASCII 34)
  - `\0` (null byte, ASCII 0)
  - `\e` (terminal escape, ASCII 27)
  - `\xHH` (arbitrary hexadecimal byte)
- `print_str` prints the null-terminated *contents* of a string pointer/variable without an extra newline.
- `read` reads one integer typed by the user from standard input.

---

## Step 3: Decisions

```nex
let score = 73
if score >= 90 {
    print "grade A"
} else if score >= 60 {
    print "grade B"
} else {
    print "grade C"
}
```

Conditions compare one variable against one variable or integer. Comparisons:
`==  !=  <  <=  >  >=`. For "between" checks, nest two `if`s.

---

## Step 4: Loops

```nex
let i = 1
let acc = 0
while i <= 10 {
    let acc = acc + i
    let i = i + 1
}
print acc
```

Prints 55. Blocks nest up to 16 deep. `break` exits the loop immediately and
`continue` jumps to the next iteration (new in v5.3):

```nex
let found = 0
let probe = 40
while found == 0 {
    let sq = probe * probe
    if sq > 2000 {
        let found = 1
    } else {
        let probe = probe + 1
    }
}
print probe
```

Counted loops use `for`. The separators are **commas**, not semicolons
(`;` starts a line comment in NEXUS):

```nex
let acc = 0
for i = 1, i <= 10, i = i + 1 {
    let acc = acc + i
}
print acc

# break / continue work in for loops too:
for i = 0, i < 100, i = i + 1 {
    if i == 7 {
        break
    }
}
print i
```

> **Warning:** conditions accept exactly one comparison — the left side must
> be a plain variable. `if probe * probe > 2000` is rejected at compile time
> (the compiler reports "Invalid relational operator in condition"); compute
> `let sq = probe * probe` first.

> **Testing your code:** the preprocessor expands `assert_eq <A>, <B>` /
> `assert_ne <A>, <B>` into counting checks. Finish a test with
> `call nx_assert_summary` (from the stdlib) and run it with
> `nexus test --file tests/your_test.nex` — the runner fails the file when
> any assertion fails.

---

## Step 5: Functions — Parameters and Return Values

NEXUS supports modern parameterized functions with `return` statements and expressions, as well as fast subroutines:

### Parameterized Functions (Modern)

```nex
fn add(a, b) {
    return a + b
}

fn power_of_two(exp) {
    let result = 1
    let k = 0
    while k < exp {
        let result = result * 2
        let k = k + 1
    }
    return result
}

let sum = call add(15, 27)
print sum                       # 42

let p = call power_of_two(8)
print p                         # 256
```

Functions can return values, expressions, or exit early from conditional branches:

```nex
fn clamp_val(val, min_v, max_v) {
    if val < min_v {
        return min_v
    }
    if val > max_v {
        return max_v
    }
    return val
}
```

### The Standard Library Calling Convention

The low-level standard library and self-hosted compiler internals often use a zero-overhead globals convention to avoid stack frame allocation:
- Inputs: set `nx_i0`, `nx_i1`, `nx_i2`
- Invocation: `call <function_name>`
- Output: read `nx_r0`
- Scratch: `s0..s7` (or `nx_s0..nx_s7`) are clobbered across calls

```nex
let nx_i0 = 1071
let nx_i1 = 462
call nx_gcd
print nx_r0                     # 21
```

---

## Step 6: Arrays and Low-Level Memory

### Array Literals & Bracket Indexing

NEXUS provides ergonomic array literals and bracket-based index access:

```nex
let arr = [10, 20, 30]
let v0 = arr[0]             # Read element 0 (10)
arr[1] = 99                 # Modify element 1 in-place
arr[0] += 5                 # Compound operation on array element

# Iterate through array
let sum = 0
for i = 0, i < 3, i = i + 1 {
    sum += arr[i]
}
print sum                   # 15 + 99 + 30 = 144
```

### Raw Heap Buffers: `alloc`, `load`, and `store`

For systems programming and raw buffers, NEXUS provides direct bare-metal pointer operations:

```nex
let word = "NEXUS"
let first = load [word + 0]
print first                 # 78 = 'N'

let buffer = alloc 32       # Zero-initialized heap buffer
let idx = 0
let ch = load [word + 0]
while ch != 0 {
    store [buffer + idx] ch
    let idx = idx + 1
    let ch = load [word + idx]
}
store [buffer + idx] 0
print_str buffer
```

Memory from `alloc` is always zero-initialized. Multi-width memory builtins provide complete control:
- `load` / `store`: 1-byte (8-bit) access
- `load16` / `store16`: 2-byte (16-bit) access
- `load32` / `store32`: 4-byte (32-bit) access
- `load64` / `store64`: 8-byte (64-bit) access

---

## Step 7: Structs

```nex
struct Player {
    hp
    score
}

let hero = alloc Player.size
hero.hp = 100
hero.score = 0

let damage = 30
hero.hp = hero.hp - damage
print hero.hp
print Player.score
```

`Player.size` is 16 (two 8-byte fields), `Player.score` is the offset 8.
The preprocessor lowers every `hero.hp` to a `load`/`store` on raw memory.

---

## Step 8: The standard library

One include line loads 26 utility functions:

```nex
include "stdlib/nstdlib.nex"

let nx_i0 = 1071
let nx_i1 = 462
call nx_gcd
print nx_r0
```

Highlights:

| Want | Use |
|------|-----|
| max / min / clamp | `nx_max`, `nx_min`, `nx_clamp` |
| powers, roots | `nx_pow`, `nx_isqrt` |
| gcd, lcm | `nx_gcd`, `nx_lcm` |
| primality, fibonacci | `nx_is_prime`, `nx_fib` |
| dynamic vectors | `stdlib/vec.nex`: `nx_vec_new`, `nx_vec_push`, `nx_vec_get`, `nx_vec_pop` |
| hash maps | `stdlib/hashmap.nex`: `nx_hashmap_new`, `nx_hashmap_put`, `nx_hashmap_get` |
| hash sets | `stdlib/hashset.nex`: `nx_hashset_new`, `nx_hashset_add`, `nx_hashset_contains` |
| ring buffer & deque | `stdlib/ring_buffer.nex`: `nx_rb_new`, `nx_rb_push_back`, `nx_rb_pop_front` |
| priority queue | `stdlib/priority_queue.nex`: `nx_pq_new`, `nx_pq_push`, `nx_pq_pop` |
| byte buffer | `stdlib/byte_buffer.nex`: `nx_buf_new`, `nx_buf_write_u32`, `nx_buf_read_u32` |
| bit stream | `stdlib/bit_stream.nex`: `nx_bit_writer_new`, `nx_bit_write`, `nx_bit_flush` |
| bump arena allocator | `stdlib/arena.nex`: `nx_arena_new`, `nx_arena_alloc`, `nx_arena_reset` |
| UTF-8 codec | `stdlib/unicode.nex`: `nx_utf8_validate`, `nx_utf8_decode_next`, `nx_utf8_encode` |
| binary search | `stdlib/search.nex`: `nx_bsearch_exact`, `nx_bsearch_lower_bound` |
| piece table | `stdlib/piece_table.nex`: `nx_pt_new`, `nx_pt_insert`, `nx_pt_delete` |
| fast memory | `stdlib/memory.nex`: `nx_memmove`, `nx_memcpy`, `nx_memset`, `nx_memzero` |
| bitwise ops | `stdlib/bitwise.nex`: `&`, `\|`, `^`, `~`, `<<`, `>>` |
| async I/O / epoll | `stdlib/poll.nex`: `nx_poll`, `nx_epoll_create`, `nx_epoll_wait` |
| byte arrays | `nx_arr_fill`, `nx_arr_sum`, `nx_arr_max`, `nx_arr_reverse`... |
| strings | `nx_str_split`, `nx_str_join`, `nx_str_trim`, `nx_str_starts_with`, `nx_str_ends_with`, `nx_str_normalize_newlines`, `nx_str_escape_literal` |
| conversions | `nx_str_to_int`, `nx_int_to_str` |

Rule: never name your own variables with the `nx_` prefix — those slots belong
to the library.

---

## Step 9: Multi-file programs

Split code into modules and include them:

```nex
include "modules/math_utils.nex"
include "stdlib/nstdlib.nex"

let in_val = 9
call calc_square
print sq_res
```

See `examples/multi_file_demo.nex` and `examples/modules/` for the pattern.

---

## Step 10: Writing tests & daily workflow

Write tests using the built-in assertion macros:

```nex
# tests/my_test.nex
include "stdlib/assert.nex"

let val = 10 + 32
assert_eq val, 42
assert_ne val, 0
assert_gt val, 40
assert_le val, 50

print "All assertions passed!\n"
```

Workflow commands:

```bash
./nexus repl                    # try ideas with instant feedback
./nexus run scratch/idea.nex    # compile + run a file
./nexus build scratch/idea.nex  # incremental build (skips if nothing changed)
./nexus fmt idea.nex            # normalize formatting (idempotent)
./nexus new myproject           # scaffold a project with vendored stdlib
./nexus test --file tests/x.nex # run one test file (assert_eq macros)
./nexus test                    # runs all 53 regression test suites
./nexus self-host               # verifies bit-for-bit self-hosting parity
./nexus bench                   # raw AOT code-generation benchmark (50M physical iters)
./nexus hydron                  # semantic recurrence-attractor elimination benchmark
```

---

## Where to go next

- `docs/README.md` — central documentation navigation hub
- `docs/LANGUAGE_REFERENCE.md` — complete grammar, semantics, operator precedence, and memory ops
- `docs/BUILTINS.md` — compiler built-ins, multi-width memory ops, and stdlib API reference
- `docs/NEXUS_IR_SPECIFICATION.md` — N-IR intermediate representation architecture
- `docs/TECHNICAL_AUDIT.md` — technical audit and engineering priorities
- `examples/` — 27 runnable programs, from fibonacci and raycasters to GUI Flappy Bird
- `compiler/nexc.nex` — the self-hosted compiler source, written in NEXUS itself
- `tests/` — comprehensive regression test suites validating every language and stdlib feature
