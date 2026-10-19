# NEXUS Language Specification — v1.0.0
### Formal Specification of the NEXUS Programming Language & Runtime ABI

> **Document Version**: 1.0.0  
> **Target Release**: NEXUS v1.0.0 Formal Release  
> **Compiler Model**: Self-Hosted Compiler Core (`compiler/nexc.nex`), 0% libc, direct Linux ELF64 syscalls, Native ARM64 Mach-O, and PE32+ executable emission.

---

## 1. Overview & Architectural Principles

NEXUS is a compiled, statically typed systems programming language designed for zero-dependency execution. Programs compile directly to standalone native machine code without linking against C runtime libraries (`libc`), operating system interpreters, or virtual machines.

### Key Tenets
1. **Zero Runtime Dependencies (0% libc)**: Direct execution on the bare Linux kernel via direct x86-64 `syscall` / ARM64 `svc #0` instructions, and Windows via standalone PE32+ kernel imports.
2. **Deterministic Self-Hosting**: The core compiler (`compiler/nexc.nex`) is written 100% in pure NEXUS, proven by bit-for-bit Stage 5 fixed-point convergence (`Gen1 == Gen2 == Gen3 == Gen4`, 0 differences across 49,152 PE32+ bytes and 57,951 Linux ELF64 bytes).
3. **First-Class Systems Ergonomics**: Modern language features including first-class strings, pattern matching, struct methods (`impl`), closures, algebraic optimization passes (HYDRON Ultra Engine), and high-level file I/O.

---

## 2. Lexical Structure

### 2.1 Comments
* Single-line comments begin with `#` or `;` and continue to the end of the line:
  ```nexus
  # This is a comment
  ; This is also a comment
  ```

### 2.2 Identifiers
Identifiers start with a letter (`a-z`, `A-Z`) or underscore `_`, followed by letters, digits (`0-9`), or underscores. Keywords and prefix names are reserved.

### 2.3 Literals
* **Integers**:
  * Decimal: `123`, `0`, `-42`
  * Hexadecimal: `0xFF`, `0x1A2B`
  * Octal: `0o755`, `0o644`
  * Binary: `0b10101010`, `0b1100`
* **Floating-Point**:
  * Standard: `3.14159`, `-0.5`
  * Scientific: `1.5e-3`, `2.5e4`, `1.25e2`
* **Characters**: Single quotes `'A'`, `'\n'`, `'\t'`, `'\0'`, `'\x7F'`
* **Strings**: Double quotes `"Hello, NEXUS!\n"`, supporting escape sequences `\n`, `\r`, `\t`, `\\`, `\"`, `\0`, `\e`, and `\xHH`.
* **Booleans**: `true` (evaluates to integer 1), `false` (evaluates to integer 0).

---

## 3. Type System

NEXUS provides strong compile-time types with multi-pass bidirectional type inference (`TY_INT`, `TY_FLOAT`, `TY_STR`, `TY_PTR`, and user-defined structs/enums).

### 3.1 Primitive Types
* **`int`**: 64-bit two's complement signed integer (`int64`).
* **`float`**: 64-bit IEEE-754 double-precision floating-point number (`float64`).
* **`string` (`TY_STR`)**: Null-terminated byte buffer with compiler-tracked string identity, automatic string concatenation (`+`), and content equality (`==`, `!=`).

### 3.2 Structs (`struct`) & Methods (`impl`)
Structs define composite types with named, typed fields.
```nexus
struct Point {
    x: int,
    y: int
}

impl Point {
    fn new(x: int, y: int) -> Point {
        return Point { x: x, y: y }
    }

    fn length_sq(&self) -> int {
        return self.x * self.x + self.y * self.y
    }
}
```

### 3.3 Enums (`enum`)
Enums declare compile-time integer constants:
```nexus
enum Color {
    Red = 1,
    Green,
    Blue
}
```

---

## 4. Expressions & Operators

### 4.1 Operator Precedence
1. **Grouping**: `(expr)`
2. **Postfix**: `arr[i]`, `obj.field`, `fn(args...)`
3. **Unary**: `!`, `~`, `-`, `++`, `--`
4. **Exponentiation**: `**`
5. **Multiplicative**: `*`, `/`, `%` (and float `*.`, `/.`)
6. **Additive**: `+`, `-` (and float `+.`, `-.`, string `+`)
7. **Bitwise Shifts**: `<<`, `>>`
8. **Relational**: `<`, `<=`, `>`, `>=`
9. **Equality**: `==`, `!=`
10. **Bitwise Logic**: `&`, `^`, `|`
11. **Logical AND**: `&&`, `and`
12. **Logical OR**: `||`, `or`
13. **Null-Coalescing**: `??`
14. **Ternary**: `cond ? expr_true : expr_false`

### 4.2 First-Class String Operations
* **Concatenation (`+`)**: `let msg = "Hello " + "World"` calls `_nx_rt_strcat` on-demand.
* **Content Equality (`==`, `!=`)**: Compares byte contents via `_nx_rt_streq`.
* **Type Conversions**: `str(int_val)` converts integer to string; `int(str_val)` parses decimal string to 64-bit integer.

---

## 5. Control Flow & Statements

### 5.1 Variable Declarations
```nexus
let x = 10
let name = "NEXUS"
const MAX_CAP = 1024
```

### 5.2 Functions & Multiple Return Values
Functions support parameter lists, local variable activation isolation on `_nx_call_stack`, and multiple return values:
```nexus
fn divmod(a: int, b: int) {
    let q = a / b
    let r = a % b
    return q, r
}

let q, r = divmod(17, 5)
```

### 5.3 Conditionals
```nexus
if x > 100 {
    print "large"
} else if x > 50 {
    print "medium"
} else {
    print "small"
}
```

### 5.4 Loops
```nexus
# While loop
while i < 10 {
    i += 1
}

# Range loop (exclusive .. or inclusive ..=)
for i in 0..10 {
    total += i
}
```

### 5.5 Pattern Matching (`match`)
```nexus
match status_code {
    200 => { print "OK" }
    404 => { print "Not Found" }
    500 | 502 => { print "Server Error" }
    _ => { print "Unknown" }
}
```

### 5.6 Process Termination
```nexus
exit(0)      # Clean exit with code 0
exit(1)      # Exit with error code 1
```

---

## 6. Built-in Compiler Primitives

### 6.1 Low-Level Memory Management
* `alloc <size>`: Dynamically allocates `<size>` bytes on the heap.
* `load [ptr + off]`: Loads 8-bit unsigned byte.
* `load64 [ptr + off]`: Loads 64-bit integer.
* `store [ptr + off] val`: Stores 8-bit byte.
* `store64 [ptr + off] val`: Stores 64-bit integer.
* `arr[i]`: Array indexing (desugared to `load64 [arr + i * 8]`).
* `arr[i] = val`: Array element assignment (desugared to `store64 [arr + i * 8] val`).

### 6.2 Standard Console I/O
* `print <expr>`: Prints value followed by newline (`\r\n`).
* `print_raw <expr>`: Prints string or integer without trailing newline.
* `read <var>`: Reads a line of signed integer input from standard input.

### 6.3 Low-Level File I/O Builtins
* `let fd = file_open(path)`: Opens file at `path` (literal string or string variable).
* `let fd = file_create(path)`: Creates or truncates file at `path`.
* `let n = file_read(fd, buf, max_bytes)`: Reads up to `max_bytes` into `buf`.
* `file_write(fd, buf, byte_count)`: Writes `byte_count` bytes from `buf`.
* `file_close(fd)`: Closes file descriptor `fd`.

### 6.4 Process Arguments
* `os_argc`: Total count of command-line arguments.
* `os_argv(i)`: Returns the $i$-th command-line argument as a string.

---

## 7. Standard Library Architecture (`stdlib/`)

NEXUS includes 35+ zero-dependency modules bundled via `include "stdlib/nstdlib.nex"`.

### 7.1 Unified Stdlib Calling ABI
All standard library functions can be called directly via parameterized function call syntax:
```nexus
let m = nx_max(10, 20)
let text = nx_read_file("/etc/hosts")
```

### 7.2 Primary Modules
* **`stdlib/io.nex`**: High-level file and stream I/O (`nx_read_file`, `nx_write_file`, `nx_append_file`, `nx_file_exists`, `nx_read_line`, `nx_eprint`).
* **`stdlib/strings.nex`**: Allocating and non-allocating string utilities (`nx_str_slice`, `nx_str_concat_new`, `nx_str_trim_new`, `nx_str_index_of`, `nx_str_contains`, `nx_str_replace`, `nx_str_levenshtein`).
* **`stdlib/hashmap.nex`**: Dynamic open-addressing hash table with automatic 50% load factor doubling, tombstone deletions, and string keys (`nx_hashmap_put_str`, `nx_hashmap_get_str`, `nx_hashmap_remove_str`).
* **`stdlib/hashset.nex`**: Set collection with string and integer membership testing.
* **`stdlib/vec.nex`**: Dynamic growable vector.
* **`stdlib/priority_queue.nex`**: Min-heap and max-heap priority queue.
* **`stdlib/ring_buffer.nex`**: Circular ring buffer / double-ended queue.
* **`stdlib/piece_table.nex`**: Professional text editor data structure with multi-level undo/redo.
* **`stdlib/gap_buffer.nex`**: Contiguous sliding text gap buffer.
* **`stdlib/bloom_filter.nex`**: Probabilistic set membership filter.
* **`stdlib/pattern_search.nex`**: Knuth-Morris-Pratt and Boyer-Moore-Horspool search.
* **`stdlib/btree.nex`**: Ordered map and set (`BTreeMap`, `BTreeSet`).
* **`stdlib/fenwick.nex`**: Binary indexed tree.
* **`stdlib/union_find.nex`**: Disjoint set partition forest.
* **`stdlib/base64.nex`**: RFC 4648 Base64 encoding and decoding.
* **`stdlib/x11.nex`**: Pure NEXUS wire-protocol X11 client with headless fallback.

---

## 8. Compiler Driver & CLI Reference

The `nexus` shell driver provides the unified interface:
```bash
nexus run <file.nex> [args...]    # Compile and run natively on bare-metal kernel
nexus check <file.nex>            # Type and syntax checking without execution
nexus compile <file.nex>          # Produce standalone native binary
nexus test                        # Run complete test and regression battery
nexus bench                       # Run HYDRON Ultra performance benchmarks
nexus version                     # Display version info
```
