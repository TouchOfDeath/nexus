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
| `print "text"` | print string literal + CRLF |
| `print <var>` | print signed 64-bit integer variable + CRLF |
| `print <int>` | print integer literal + CRLF |
| `print_str <var>` | print string variable contents + CRLF |
| `print_str "text"` | print string literal + CRLF |
| `read <var>` | read one signed decimal integer line from stdin |

### Expressions (right side of `let`)

| Form | Result |
|------|--------|
| `alloc <n>` | pointer to n zero-initialized bytes (VirtualAlloc / mmap) |
| `load [p + i]` | one byte (0..255) from address `p + i`; `p`/`i` are variables, `i` may be a literal |
| `"text"` | pointer to a null-terminated string in read-only memory |
| `len s` | byte length of string variable or literal (the terminating byte is not counted) |
| `abs <x>` | absolute value; `<x>` is a variable, integer, or negative literal like `abs -9` |
| chain `a + b * c` | left-to-right fold; no precedence, no parentheses |

### Memory writes

| Statement | Behavior |
|-----------|----------|
| `store [p + i] <v>` | write the LOW BYTE of `<v>` (literal or variable) at `p + i` |

`file_open`/`file_create`/`file_read`/`file_write`/`file_close` wrap the
platform file APIs with identical syntax on Windows and Linux.

### Limits

- Integer literals: 32-bit immediates
- Variables: 120 program-wide
- Block nesting: 16 levels

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
| `nx_str_to_int` | (s) -> nx_r0 | decimal parse; optional leading '-'; non-digits ignored |
| `nx_int_to_str` | (v=nx_i0, buf=nx_i1) | writes decimal text of v into buf (needs 12 bytes) |

### Worked example

```nex
include "stdlib/nstdlib.nex"

let who = "prime count below 50:"
print_str who

let buf = alloc 50
let nx_i0 = buf
let nx_i1 = 50
call nx_arr_iota

let n = 0
let scan = 2
while scan < 50 {
    let nx_i0 = scan
    call nx_is_prime
    if nx_r0 == 1 {
        let n = n + 1
    }
    let scan = scan + 1
}
print n
```

Prints 15 — the primes 2,3,5,7,11,13,17,19,23,29,31,37,41,43,47.

---

## Part 3 — Verification

Everything above is machine-checked:

- `tests/stdlib_test.nex` — 94 assertions across all 26 functions
- `examples/stdlib_demo.nex` — runnable tour of all three modules
- `./nexus test` runs both plus the full example and diagnostic suites
