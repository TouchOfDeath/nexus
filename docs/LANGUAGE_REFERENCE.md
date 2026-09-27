# NEXUS Language Reference

**Target audience:** anyone writing NEXUS programs.
**Compiler:** self-hosted `nexc` (see `compiler/nexc.nex`) and native ARM64 `nexarm64` (`tools/nexarm64.c`).
**Binaries:** standalone x86-64 & ARM64 — Windows PE32+, Linux ELF64 (x86-64 & aarch64), macOS ARM64 Mach-O, 0% C#, 0% .NET, 0% libc.

> Every example in this document was compiled and executed with the project's
> own self-hosted compiler before publication. The runnable companion suite
> lives in `tests/stdlib_test.nex` (94 assertions).

---

## 1. Program structure

A NEXUS program is a plain UTF-8 (ASCII-safe) text file with the `.nex`
extension. Statements are line-oriented: one statement per line, execution
proceeds top to bottom. There is no `main` function — top-level statements run
in order; functions are declared anywhere and invoked with `call`.

```nex
# comments start with '#' and run to end of line
print "Hello, Bare-Metal x86-64 World!"

fn greet {
    print "Hello from a function!"
}

call greet
```

Multi-file programs use `include` (see section 9). Structs are declared with
`struct` (see section 10).

---

## 2. Lexical rules

| Element     | Rule |
|-------------|------|
| Comments    | `#` to end of line. `;` also starts a comment (legacy). |
| Identifiers | Letters, digits and underscores. Multi-letter and uppercase names are supported. |
| Integers    | Decimal only. Literals are 32-bit immediates: usable range is about -2,147,483,647 .. 2,147,483,647. Negative literals appear via `0 - n` or as the right side of comparisons and in `print`/`abs`. |
| Strings     | Double-quoted `"..."`, stored in the binary's read-only data section, null-terminated. No escape sequences. |
| Whitespace  | Spaces and tabs separate tokens; newlines end statements. |

### 2.1 Case sensitivity
Identifiers are case-sensitive: `Total` and `total` are different variables.

### 2.2 Reserved words
`let print print_str read if else while fn call return struct include import`
plus the built-in operators `alloc load store load64 store64 len abs file_open file_create
file_read file_write file_close`.

---

## 3. Variables and assignment

```
let <name> = <expression>
```

Variables are **global** — every `let` registers a name in a single program-wide
symbol table (maximum **120** variables). Assigning again overwrites the value.

```nex
let counter = 10
let velocity = 7800
let total_sum = counter + velocity
```

### 3.1 Gotcha: reading an undeclared variable
Reading a variable that was never assigned does **not** fail compilation: the
compiler silently registers it with value 0. Typos therefore compile and simply
read zero — pick consistent names.

### 3.2 Expression rules
Expressions are arithmetic chains evaluated **strictly left to right**:

- Operators: `+  -  *  /  %` (signed 64-bit hardware `idiv` for `/` and `%`)
- Operands: integer literals, variables, and the prefix forms below
- **No parentheses, no operator precedence.** `2 + 3 * 4` is `(2 + 3) * 4 = 20`.
- Each operator step must be its own statement when order matters:

```nex
# want: (a + b) / 2  ->  write it as two statements
let t = a + b
let t = t / 2
```

Division truncates toward zero (C semantics): `0 - 7 / 2` is `(0-7)/2 = -3`.

### 3.3 Prefix forms allowed after '='
| Form | Meaning |
|------|---------|
| `alloc <n>` | allocate n zero-initialized bytes; result is a pointer |
| `load [p + i]` | read one byte at address `p + i` (0..255) |
| `load64 [p + i]` | read 64-bit integer at address `p + i` |
| `load64 [p + i * 8]` | read 64-bit integer with scaled index |
| `"text"` | string literal; the variable holds a pointer to its first byte |
| `len <str>` | byte length of a string variable or literal |
| `abs <expr>` | absolute value of a literal, variable, or negative literal |
| `fadd <op1> <op2>` | 64-bit float addition (`addsd xmm0, xmm1`) |
| `fsub <op1> <op2>` | 64-bit float subtraction (`subsd xmm0, xmm1`) |
| `fmul <op1> <op2>` | 64-bit float multiplication (`mulsd xmm0, xmm1`) |
| `fdiv <op1> <op2>` | 64-bit float division (`divsd xmm0, xmm1`) |
| `fsqrt <op>` | 64-bit float square root (`sqrtsd xmm0, xmm0`) |
| `fneg <op>` | in-place 64-bit float negation (`xorpd` + `subsd`) |
| `itof <op>` | convert 64-bit integer to 64-bit float (`cvtsi2sd`) |
| `ftoi <op>` | convert 64-bit float to 64-bit integer (`cvttsd2si`) |

### 3.4 Floating-point arithmetic (x86-64 SSE2)

NEXUS provides native 64-bit double-precision floating-point arithmetic (IEEE 754) implemented directly via x86-64 SSE2 scalar instructions (`xmm0`–`xmm3`), with 0% runtime libraries or external dependencies.

#### Float literals
Float literals are written with a decimal point:
```nex
let pi = 3.141592
let half = 0.5
let neg = -2.71828
```

#### Native SSE2 Prefix Primitives
```nex
let sum = fadd a b          # a + b
let diff = fsub a b         # a - b
let prod = fmul a b         # a * b
let quot = fdiv a b         # a / b
let root = fsqrt 16.0       # sqrt(16.0) = 4.0
let inv = fneg x            # -x
let f_val = itof 42         # integer -> float (42.0)
let i_val = ftoi f_val      # float -> integer truncate (42)
```

#### Unified Arithmetic Operators & Basic Type Inference (v6.0)
NEXUS features automatic type inference across lexical scopes, enabling standard arithmetic operators (`+`, `-`, `*`, `/`) without dot notation:
```nex
let a = 10.5 + 4.5                 # automatically inferred as float arithmetic
let b = 20.0 - 5.5
let c = 3.0 * 4.5
let d = 15.0 / 2.0
let e = a + b                      # inferred float variables: uses fadd
let f = (a + 5.0) * 2.0            # parentheses & operator precedence (* before +)
```

#### Mixed-Type Promotion
When an expression mixes float and integer operands, NEXUS promotes integers to 64-bit IEEE floats automatically:
- **Integer literals** in float expressions are promoted to float literals (`2` → `2.0`):
  ```nex
  let h = a + 2                    # 15.0 + 2.0 = 17.0
  let j = 5 * b                    # 5.0 * 14.5 = 72.5
  ```
- **Integer variables** mixed with float expressions are promoted via `itof`:
  ```nex
  let count = 10                   # TY_INT
  let total = a + count            # count promoted via let _f_prom = itof count
  ```

#### Explicit Infix Sugar & Primitives (Backwards Compatible)
The original dotted syntax (`+.`, `-.`, `*.`, `/.`) and prefix primitives (`fadd`, `fsub`, `fmul`, `fdiv`, `fsqrt`, `fneg`, `itof`, `ftoi`) remain 100% supported:
```nex
let c = a +. b                     # desugars to: let c = fadd a b
let d = a -. b                     # desugars to: let d = fsub a b
let e = a *. b                     # desugars to: let e = fmul a b
let f = a /. b                     # desugars to: let f = fdiv a b
let z = a +. b *. c                # precedence: evaluates b *. c first
let w = (a +. b) *. (c -. d)       # parenthesized subexpressions
```

#### Memory Stores with Float Literals
`store64` and `store` automatically accept immediate 64-bit IEEE float literals:
```nex
store64 [ptr + 0] 1.5              # hoists to 64-bit float temp & stores
store64 [arr + idx * 8] -0.25      # preserves IEEE-754 bit pattern
```

### 3.5 Compound Assignment Operators (v5.6)
NEXUS supports compound assignments for concise in-place updates, both with and without `let`:

```nex
# Integer updates:
i += 1                  # desugars to: let i = i + 1
let sum += item_val     # desugars to: let sum = sum + item_val
count -= 1              # desugars to: let count = count - 1
factor *= 2             # desugars to: let factor = factor * 2
val /= 10               # desugars to: let val = val / 10
rem %= 4                # desugars to: let rem = rem % 4

# Floating-point updates:
pos +=. 0.5             # desugars to: let pos = fadd pos 0.5
speed *=. 1.2           # desugars to: let speed = fmul speed 1.2

# Struct member in-place updates:
p.x += 10               # desugars to: p.x = p.x + 10
p.y -= 5                # desugars to: p.y = p.y - 5

# For loop update headers:
for i = 0, i < 10, i += 1 {
    # loop body
}
```

### 3.6 Array Literals & Bracket Indexing (v5.7)

Arrays are heap-allocated contiguous 64-bit integer slots. The preprocessor
(`nexprep`) desugars all bracket syntax transparently.

#### Array declaration
```nex
let primes = [2, 3, 5, 7, 11, 13]   # inline literal – allocates 6 × 8 bytes

# Multi-line literals are also accepted:
let coords = [
    100,   # x
    200,   # y
    300,   # z
]
```

#### Indexed read
```nex
let first = primes[0]        # constant index  → load64 [primes + 0]
let kth   = primes[k]        # variable index  → load64 [primes + k * 8]
```

#### Indexed write
```nex
primes[0] = 42               # constant index  → store64 [primes + 0] 42
primes[i] = val              # variable index  → store64 [primes + i * 8] val
```

#### Compound update on elements
```nex
primes[0] += 1               # read-modify-write (uses internal temp)
primes[i] *= 2
```

#### Arrays in expressions
When an indexed read appears inside a larger expression, `nexprep` hoists it
to a temporary automatically:
```nex
let dot = arr[0] * brr[0]   # hoisted: _a_arr_0, _a_arr_brr_0
if arr[i] > 5 {              # hoisted: _a_arr_i
    ...
}
```

#### Float arrays
Float literals in an array literal are stored as raw IEEE-754 64-bit doubles:
```nex
let weights = [0.5, 1.5, 2.0]
let w0 = weights[0]          # w0 is the float bit-pattern of 0.5
let s  = fadd w0 weights[1]  # use float ops to compute with elements
```

> **Limits (current release):** max 128 elements per literal; element
> expressions may be integer sub-expressions or float literals.
> Nested brackets in the same statement are not supported.

---

## 4. Output and input

```nex
print "text"      # string literal, newline appended
print 42          # signed integer, newline appended
print counter     # variable holding an integer, newline appended
print_float val   # 64-bit float (6 decimal places), newline appended
print_str msg     # string VARIABLE or literal, newline appended, no extra bytes
read a            # read one integer line from stdin into variable a
```

Notes:
- `print` automatically adapts to inferred variable types: if a variable is inferred as a float, or an argument is a float literal, `print` automatically formats it as a 6-decimal-place float. Explicit `print_float` is also supported.
- `print` on a string variable prints the pointer value; use `print_str` for
  string contents.
- Output line endings are CRLF (`\r\n`) on both targets.
- `read` parses one signed decimal integer per line.

---

## 5. Control flow

### 5.1 Conditions
A condition is exactly:

```
<variable> <op> <variable-or-integer> {
```

Operators: `==  !=  <  <=  >  >=` (signed). The left side must be a variable;
load memory or compute expressions into a temporary variable first. There is no boolean type — use 0/1
integers, and no `and`/`or` — use nested `if`.

### 5.1.1 Floating-point conditions (SSE2 / NEON)

#### Unified Standard Conditions (v6.0)
With type inference, standard relational operators (`<`, `<=`, `>`, `>=`, `==`, `!=`) work directly in `if`, `else if`, and `while` blocks without requiring dot notation:
```nex
if a > 10.0 {
    print "above threshold"
} else if a <= 5.5 {
    print "low"
}

while iter <= 4.0 {
    print iter
    iter += 1.0
}
```
If an operand is an integer, it is automatically promoted to float before comparison.

#### Explicit Float Conditions & Infix Sugar (Backwards Compatible)
Floating-point comparisons can also use `if_f`, `while_f`, and `else if_f` or dotted operators:
```nex
if_f a > b {
    print 1
} else if_f a == b {
    print 2
} else {
    print 3
}

while_f dist < 100.0 {
    let dist = fadd dist 1.5
}

# Dotted relational syntax:
if a <. b { ... }              # desugars to: if_f a < b {
while dist >=. 100.0 { ... }   # desugars to: while_f dist >= 100.0 {
```

**Strict Validation & Compiler Diagnostics:**
- **No compound expressions in condition headers:** Writing `if a * a > n {` is caught at compile time with `[!] Error: Invalid relational operator in condition`.
- **Assignment `=` rejected:** Writing `if a = 5 {` produces an invalid relational operator error.
- **Strict `{` termination required:** Condition headers must terminate with `{` on the same statement line (e.g. `if a == 5` without `{` produces `[!] Error: Expected '{' after condition`).
- In all condition failure cases, compilation is immediately gated (`app.exe` is not emitted) and exact line and column numbers are reported.

### 5.2 if / else if / else

```nex
let val = 25
if val == 10 {
    print 1
} else if val == 25 {
    print 2
} else {
    print 3
}
```

`}` must stay on the same line as `else` / `else if`.

### 5.3 while

```nex
let loop_idx = 0
let running_total = 0
while loop_idx < 5 {
    let running_total = running_total + loop_idx
    let loop_idx = loop_idx + 1
}
```

Blocks may nest up to **16 levels** deep.

### 5.4 break / continue (new in v5.3)

`break` and `continue` are bare statements on their own line. Inside `while`
and `for` bodies they jump to the loop exit and the next iteration step
respectively. They always bind to the **innermost enclosing loop** and never
cross a function boundary (a `break` inside `fn` bodies cannot target an
outer loop; the compiler reports it).

```nex
let i = 0
while i < 100 {
    let i = i + 10
    if i == 40 {
        break              # jump past the closing brace of this while
    }
}
print i                   # 40

let n = 0
let seen = 0
while n < 6 {
    let n = n + 1
    if n == 3 {
        continue           # jump back to the condition test
    }
    let seen = seen + 1
}
print seen                # 5
```

Diagnostics: a `break`/`continue` outside any enclosing loop is rejected with
`[!] Error: 'break' outside of a loop` (or `'continue' ...`) and gated output.

### 5.5 for (new in v5.3)

C-style counted loops. **Separators are commas** (`;` is a comment-to-end-of-line
character in NEXUS and cannot be used inside a header):

```nex
for i = 0, i < 10, i = i + 1 {
    print i
}
```

- **init** runs once: `let`-style assignment; the loop variable is registered
  if new, reused if it exists.
- **cond** is a standard condition (5.1): `<var> <op> <var-or-int>`; evaluated
  before every iteration; the loop exits when it is false.
- **update** runs after every body iteration (before the re-test). Supported
  forms: `<var> = <operand>` and `<var> = <operand> +|- <operand>` where an
  operand is a variable or an integer. Use `while` for richer updates.
- `break` exits immediately; `continue` jumps to the **update** step (so the
  counter still advances).
- The header must fit on one line; the body starts on the following lines.

```nex
let acc = 1
for i = 1, i <= 5, i = i + 1 {
    let acc = acc * i
}
print acc                 # 120
```

Diagnostics: a missing comma produces `[!] Error: Expected ',' in for header`.

### 5.6 Test assertions (new in v5.3, preprocessor)

Inside any test program the preprocessor (`bin/nexprep`, run automatically by
`nexus run/build/test`) expands:

```nex
assert_eq <exprA>, <exprB>
assert_ne <exprA>, <exprB>
```

into a counting compare-and-report block. Counters live in
`nx_assert_passes` / `nx_assert_fails`; `call nx_assert_summary` (stdlib) prints
`ASSERTIONS: <n> passed, <m> failed` — the line `bin/nextest` validates. Arguments
are NEXUS expressions (literals, variables, arithmetic); to assert on a stdlib
result, call the function first and assert on `nx_r0`.

---

## 6. Functions

NEXUS supports parameterized functions, return values, call expressions, and legacy subroutines.

### 6.1 Function Definition with Parameters
Functions declare parameters inside parentheses following the function name:
```nex
fn add(a, b) {
    return a + b
}

fn clamp(val, min_v, max_v) {
    if val < min_v {
        return min_v
    }
    if val > max_v {
        return max_v
    }
    return val
}
```

### 6.2 Return Values (`return <expr>`)
The `return` statement supports returning values and expressions:
- `return <expr>` (e.g. `return 42`, `return a + b`, `return a +. b`)
- Early returns inside conditional blocks (`if`, `else`, `while`) immediately return execution to the caller.
- Bare `return` exits the function early without assigning a return value.
- Reaching the closing `}` at the end of the function body automatically returns.

### 6.3 Function Invocations & Call Expressions
Functions are called with arguments passed inside parentheses:
```nex
# Call with return assignment:
let sum = call add(15, 27)

# Floating-point parameters and return values:
let mid = call lerp(10.0, 20.0, 0.5)

# Void call (side-effect subroutine):
call print_coords(100, 200)
```

### 6.4 Nested Function Calls
Function calls can be nested arbitrarily within argument lists:
```nex
let result = call square(call add(1, 2))
let quad = call add(call add(10, 20), call add(30, 40))
```
Nested calls are topologically hoisted into safe sequential evaluation steps before invocation, preventing parameter clobbering.

### 6.5 Parameterless Subroutines (Legacy Compatibility)
Parameterless subroutines without parentheses remain 100% supported for complete backwards compatibility:
```nex
fn greet {
    print "Hello from NEXUS!"
}
call greet
```

---

## 7. Memory: alloc / load / store / load64 / store64

`alloc` returns a pointer to **zero-initialized** memory (guaranteed on both targets).

### 7.1 Byte Operations (`load` / `store`)
- `store [<ptr> + <idx>] <value>`: writes the low 8-bit byte of a literal or variable. Emits `41 88 00` (`mov byte ptr [r8], al`).
- `load [<ptr> + <idx>]`: zero-extends 1 byte from memory into a variable. Emits `49 0F B6 00` (`movzx rax, byte ptr [r8]`).

```nex
let buf = alloc 64          # 64 zeroed bytes
store [buf + 0] 72          # 'H'
store [buf + 1] 105         # 'i'
let c = load [buf + 0]      # 72
```

### 7.2 Native 64-Bit Operations (`load64` / `store64`)
- `store64 [<ptr> + <idx>] <value>`: stores a full 64-bit integer quadword into memory. Emits `49 89 00` (`mov qword ptr [r8], rax`).
- `load64 [<ptr> + <idx>]`: reads a 64-bit integer quadword from memory into a variable. Emits `49 8B 00` (`mov rax, qword ptr [r8]`).

```nex
let p = alloc 64
store64 [p + 0] 123456789
let val = load64 [p + 0]    # 123456789
```

### 7.3 Multi-Width Operations (`load16` / `store16`, `load32` / `store32`)
For graphics, network buffers, audio, and OS structures, NEXUS provides 16-bit (word) and 32-bit (doubleword) operations:
- `store16 [<ptr> + <idx>] <value>` / `load16 [<ptr> + <idx>]`: 16-bit memory access (x86-64 `mov word ptr`, ARM64 `strh` / `ldrh`).
- `store32 [<ptr> + <idx>] <value>` / `load32 [<ptr> + <idx>]`: 32-bit memory access (x86-64 `mov dword ptr`, ARM64 `str w` / `ldr w`).

```nex
let buf = alloc 16
store16 [buf + 0] 4660      # 0x1234
let w = load16 [buf + 0]    # 4660

store32 [buf + 4] 305419896 # 0x12345678
let dw = load32 [buf + 4]   # 305419896
```

### 7.4 Scaled Indexing (`* 8`, `* 4`, `* 2`)
Memory addressing natively supports scaled variable offsets (`* 8`) in both `load`/`load64` and `store`/`store64`:
```nex
let idx = 2
store64 [p + idx * 8] 777
let v = load64 [p + idx * 8]  # 777
```
Emits `shl r9, 3` (`49 C1 E1 03`) followed by `add r8, r9` (`4D 01 C8`).

### 7.4 Chained Load Expressions
Memory loads (`load` and `load64`) directly integrate with the expression evaluation engine and can be chained with subsequent arithmetic operators:
```nex
let k = load [p + 0] + 10 * 2  # reads byte, adds 10, multiplies by 2
```

Strings are pointers: indexing a string variable with `load` reads its bytes.

---

## 8. File I/O

Win32 API on Windows, direct syscalls on Linux; identical language surface.

```nex
let fd = file_create "out.txt"     # create/truncate for writing
let fd = file_open "in.txt"        # open existing for reading

file_read <fd> <buffer> <bytes>    # read into memory from alloc
file_write <fd> <buffer> <bytes>   # write memory to file
file_close <fd>
```

### 8.1 Direct Kernel Syscalls (`syscall0` .. `syscall6`)
NEXUS allows direct kernel system calls without libc across Linux (x86-64 / aarch64) and macOS ARM64:
- `syscall0 <nr>`: 0-argument syscall (e.g. `sys_getpid`)
- `syscall1 <nr>, <arg1>` .. `syscall6 <nr>, <a1>, <a2>, <a3>, <a4>, <a5>, <a6>`
- Return value is provided in `x0` / `rax` and assigned directly:
```nex
let pid = syscall0 39              # Linux sys_getpid
let nw = syscall3 1, 1, msg, 20    # Linux sys_write(stdout, msg, 20)
```

### 8.2 Command-Line Arguments (`os_argc`, `os_argv`)
Process argument count and vector are accessible directly from the entry stack:
```nex
let count = os_argc
let first_arg_str = os_argv 1
```

---

## 9. Multi-file programs: include / import

```
include "path/file.nex"
import "path/file.nex"     # exact same behavior
```

Included files are inlined at the position of the directive (functions,
variables and all). Resolution order:

1. relative to the directory of the file containing the directive
2. relative to the current working directory
3. relative to each ancestor directory of the including file — this is why
   `include "stdlib/nstdlib.nex"` works from `examples/`, `tests/` and
   project roots alike

Circular includes are detected and skipped (with a comment in the output).

---

## 10. Structs

Declared at source level; the preprocessor lowers them to byte offsets:

```nex
struct Point {
    x
    y
}

let pt1 = alloc Point.size    # 16 bytes (8 per field)
pt1.x = 25
pt1.y = 50
print pt1.x                   # 25
pt1.x = pt1.x + 15            # in-place mutation

print Point.y                 # 8 — field offset constant
```

Rules:
- Each field occupies 8 bytes; `Name.size` is the total.
- Member reads in any statement are hoisted into temporaries
  (`let _s_obj_field = load [obj + off]`) by the preprocessor.
- Member writes accept numbers, variables, members, or full expressions.
- Object-to-struct typing is tracked from `let o = alloc Name.size`; if the
  type is unknown, the first declaration of a field name wins.

---

## 11. Limits and known gotchas

| Limit / Gotcha | Detail |
|----------------|--------|
| 120 variables  | Program-wide symbol table (including stdlib names, which use only 12 slots) |
| 16 block depth | `{ }` nesting limit |
| 32-bit literals| Integer literals are 32-bit immediates |
| No parentheses | Expressions fold strictly left to right |
| No precedence  | `*` does not bind tighter than `+` |
| Memory ops     | `load`/`store` (8-bit bytes); `load64`/`store64` (64-bit quadwords with `* 8` scaled indexing) |
| Global-only vars | No locals, no parameters, no recursion |
| Auto-vivify    | Reading an undeclared variable silently creates it (value 0) |
| store value    | Right side of `store [p+i] v` must be a literal or variable, not an expression |
| Condition syntax| Conditions must be `<var> <relop> <var|num> {`; expressions, `=` or missing `{` are error-gated |
| String escapes | None — strings are raw bytes between quotes |
| CRLF output    | `print`/`print_str` emit `\r\n` |

---

## 12. Complete example

A prime sieve using `alloc`, nested loops, byte memory and the stdlib
(this exact program is shipped as `examples/prime_sieve.nex` and prints 25):

```nex
include "stdlib/nstdlib.nex"

let n = 100
let sieve = alloc n          # 0 = prime candidate, 1 = composite
store [sieve + 0] 1
store [sieve + 1] 1

let i = 2
while i < n {
    let is_p = load [sieve + i]
    if is_p == 0 {
        let m = i * i
        while m < n {
            store [sieve + m] 1
            let m = m + i
        }
    }
    let i = i + 1
}

let count = 0
let i = 2
while i < n {
    let is_p = load [sieve + i]
    if is_p == 0 {
        let count = count + 1
    }
    let i = i + 1
}
print "primes below 100:"
print count
```

For a fully working sieve see `examples/`, for the full statement grammar see
`compiler/nexc.nex` (the compiler is written in NEXUS itself — the ultimate
reference).
