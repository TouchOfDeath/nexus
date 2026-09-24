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

## Step 1: Variables and arithmetic

Create `scratch/step1.nex`:

```nex
let price = 120
let quantity = 4
let total = price * quantity
print total
print "total computed"
```

Run it: `./nexus run scratch/step1.nex` → prints `480`.

Key facts:
- `let` declares AND assigns. Re-assigning with another `let` overwrites.
- Arithmetic chains run left to right: `2 + 3 * 4` is 20, not 14.
- There are no parentheses — split complex math into intermediate `let`s.

---

## Step 2: Text output and input

```nex
print "What is your age?"
read age
print age
print_str "age stored, bye"
```

- `print` handles integers and string *literals*.
- `print_str` prints the *contents* of a string variable.
- `read` reads one integer typed by the user.

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

## Step 5: Functions — the globals convention

NEXUS functions have no parameters and no return values. Data flows through
variables:

```nex
fn power_of_two {
    # input: exp   output: result
    let result = 1
    let k = 0
    while k < exp {
        let result = result * 2
        let k = k + 1
    }
}

let exp = 8
call power_of_two
print result
```

This is the same convention the standard library formalizes:
set `nx_i0..nx_i2`, `call`, read `nx_r0`.

---

## Step 6: Memory and strings

Strings are pointers to null-terminated bytes; `alloc` gives zeroed memory:

```nex
let word = "NEXUS"
let first = load [word + 0]
print first                # 78 = 'N'

let buffer = alloc 32
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

Memory from `alloc` is always zero-initialized. `load`/`store` move one byte.

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
| byte arrays | `nx_arr_fill`, `nx_arr_sum`, `nx_arr_max`, `nx_arr_reverse`... |
| strings | `nx_str_len`, `nx_str_copy`, `nx_str_cmp`, `nx_str_concat`, `nx_str_chr` |
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

## Step 10: Daily workflow

```bash
./nexus repl                    # try ideas with instant feedback
./nexus run scratch/idea.nex    # compile + run a file
./nexus build scratch/idea.nex  # incremental build (skips if nothing changed)
./nexus fmt idea.nex            # normalize formatting (idempotent)
./nexus new myproject           # scaffold a project with vendored stdlib
./nexus test --file tests/x.nex # run one test file (assert_eq macros)
./nexus test                    # examples + diagnostics + stdlib suite
./nexus build                   # the full 10-stage self-hosting pipeline
```

---

## Where to go next

- `docs/LANGUAGE_REFERENCE.md` — the complete grammar and every gotcha
- `docs/BUILTINS.md` — compiler built-ins and the full stdlib API
- `examples/` — 15 runnable programs, from fibonacci to prime sieves
- `compiler/nexc.nex` — the compiler source, written in NEXUS itself
- `tests/stdlib_test.nex` — 94 executable assertions on the stdlib
