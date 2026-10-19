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
| `print_raw <expr>` | print string or integer without trailing newline |
| `read <var>` | read one signed decimal integer line from stdin |
| `exit <code>` | terminate process immediately with status code `<code>` |

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
| chain `a + b * c` | standard mathematical precedence (`*`, `/`, `%` over `+`, `-`) with parentheses support |

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
| `nx_str_normalize_newlines` | (src=nx_i0, dst=nx_i1) -> nx_r0 | converts CRLF and CR to LF; preserves LF; supports same mutable source/destination buffer |
| `nx_str_escape_literal` | (src=nx_i0, dst=nx_i1) -> nx_r0 | writes a quoted, escaped NEXUS literal; destination needs 4 * source length + 3 bytes; input cannot contain embedded NUL |
| `nx_str_to_int` | (s) -> nx_r0 | decimal parse; optional leading '-'; non-digits ignored |
| `nx_int_to_str` | (v=nx_i0, buf=nx_i1) | writes decimal text of v into buf (needs 12 bytes) |

| `nx_str_split` | (str=nx_i0, delim_byte=nx_i1) -> nx_r0 | Splits at delimiter byte into fresh array: `A[0]`=count, `A[1..N]`=token strings (Feature #133) |
| `nx_str_join` | (arr=nx_i0, sep=nx_i1, count=nx_i2) -> nx_r0 | Joins qword array of strings with separator into one allocation (Feature #134) |

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

### Hash Sets — `stdlib/hashset.nex` (Feature #71)

An open-addressing hash set (`HashSet<T>`) storing unique integer AND string elements with linear probing, tombstone-based removal, and automatic capacity doubling with full rehash when the live load factor reaches 1/2.

Strings are matched by **content** (`nx_str_cmp`), so different literals with identical bytes count as one element; integer `0` is a fully valid element (slots are classified by a per-slot state byte, not a sentinel value).

Layout in memory (32-byte header struct on heap):
- Offset `0`: `vals_ptr` (pointer to element array, 8 bytes per slot)
- Offset `8`: `state_ptr` (pointer to state array, 1 byte per slot: `0`=empty, `1`=integer, `2`=string, `3`=tombstone)
- Offset `16`: `count` (number of live entries)
- Offset `24`: `capacity` (number of hash slots, initially 16)

| Function | Signature | Notes |
|----------|-----------|-------|
| `nx_hashset_new` | () -> nx_r0 | Allocates set with initial capacity 16 |
| `nx_hashset_add` | (set=nx_i0, val=nx_i1) -> nx_r0 | Adds integer element; 1 if inserted, 0 if already present |
| `nx_hashset_add_str` | (set=nx_i0, str=nx_i1) -> nx_r0 | Adds string element (content-matched) |
| `nx_hashset_contains` | (set=nx_i0, val=nx_i1) -> nx_r0 | 1 if integer element exists, 0 otherwise |
| `nx_hashset_contains_str` | (set=nx_i0, str=nx_i1) -> nx_r0 | 1 if string exists (by content) |
| `nx_hashset_remove` | (set=nx_i0, val=nx_i1) -> nx_r0 | Removes element (leaves tombstone); 1 if removed |
| `nx_hashset_remove_str` | (set=nx_i0, str=nx_i1) -> nx_r0 | Removes string element (by content) |
| `nx_hashset_len` | (set=nx_i0) -> nx_r0 | Number of live entries |
| `nx_hashset_cap` | (set=nx_i0) -> nx_r0 | Current slot capacity |
| `nx_hashset_clear` | (set=nx_i0) | Marks every slot empty and resets count |

### Dense BitSet / BitVector — `stdlib/bitset.nex` (Feature #78)

A space-efficient, heap-allocated dense bit vector packed 8 bits per byte (1 byte per 8 flags). Ideal for dirty line markers, selection masks, and large boolean arrays.

Layout in memory (32-byte header struct on heap):
- Offset `0`: `buf_ptr` (pointer to raw byte buffer)
- Offset `8`: `num_bits` (total bit capacity)
- Offset `16`: `num_bytes` (allocated bytes, `(num_bits + 7) / 8`)
- Offset `24`: `count` (number of set bits, cached popcount)

| Function | Signature | Notes |
|----------|-----------|-------|
| `nx_bitset_new` | (num_bits=nx_i0) -> nx_r0 | Allocates bitset initialized to 0 |
| `nx_bitset_len` | (bs=nx_i0) -> nx_r0 | Returns total bit capacity |
| `nx_bitset_bytes` | (bs=nx_i0) -> nx_r0 | Returns number of allocated bytes |
| `nx_bitset_count` | (bs=nx_i0) -> nx_r0 | Returns O(1) count of set bits (cardinality) |
| `nx_bitset_is_empty` | (bs=nx_i0) -> nx_r0 | Returns 1 if count == 0, else 0 |
| `nx_bitset_is_all_set` | (bs=nx_i0) -> nx_r0 | Returns 1 if count == num_bits, else 0 |
| `nx_bitset_test` | (bs=nx_i0, idx=nx_i1) -> nx_r0 | Returns bit at `idx` (1 or 0) |
| `nx_bitset_set` | (bs=nx_i0, idx=nx_i1) -> nx_r0 | Sets bit to 1; returns 1 if changed, 0 otherwise |
| `nx_bitset_clear` | (bs=nx_i0, idx=nx_i1) -> nx_r0 | Clears bit to 0; returns 1 if changed, 0 otherwise |
| `nx_bitset_toggle` | (bs=nx_i0, idx=nx_i1) -> nx_r0 | Flips bit; returns new bit value (1 or 0) |
| `nx_bitset_set_all` | (bs=nx_i0) -> nx_r0 | Sets all bits to 1 (with trailing mask) |
| `nx_bitset_clear_all` | (bs=nx_i0) -> nx_r0 | Clears all bits to 0 |
| `nx_bitset_find_first_set` | (bs=nx_i0) -> nx_r0 | First set bit index, or -1 if none |
| `nx_bitset_find_first_clear` | (bs=nx_i0) -> nx_r0 | First clear bit index, or -1 if all set |
| `nx_bitset_find_next_set` | (bs=nx_i0, start=nx_i1) -> nx_r0 | Next set bit index >= `start`, or -1 |
| `nx_bitset_clone` | (bs=nx_i0) -> nx_r0 | Deep copy of bitset and byte buffer |
| `nx_bitset_equals` | (bs1=nx_i0, bs2=nx_i1) -> nx_r0 | 1 if identical lengths and bits, else 0 |
| `nx_bitset_and` | (dst=nx_i0, src=nx_i1) -> nx_r0 | In-place bitwise AND (`dst = dst & src`) |
| `nx_bitset_or` | (dst=nx_i0, src=nx_i1) -> nx_r0 | In-place bitwise OR (`dst = dst \| src`) |
| `nx_bitset_xor` | (dst=nx_i0, src=nx_i1) -> nx_r0 | In-place bitwise XOR (`dst = dst ^ src`) |
| `nx_bitset_not` | (bs=nx_i0) -> nx_r0 | In-place bitwise NOT (inverts all bits) |
| `nx_bitset_recount` | (bs=nx_i0) -> nx_r0 | Recalculates and caches popcount |

### Circular Ring Buffer / Deque — `stdlib/ring_buffer.nex` (Features #74 & #75)

A high-performance double-ended circular buffer (Deque) storing 64-bit integer elements (values, pointers, object handles). Supports two operating modes:
1. **Dynamic Auto-Grow Deque** (`nx_rb_new`): Automatically doubles allocated capacity when full, providing amortized O(1) push and pop at BOTH ends.
2. **Fixed-Capacity Circular Overwrite Buffer** (`nx_rb_new_fixed`): Bounded memory window where insertions on a full buffer overwrite the oldest elements with dropped eviction count tracking. Ideal for console scrollback, streaming log windows, packet buffers, and audio sample mixing.

Layout in memory (48-byte header struct on heap):
- Offset `0`: `buf_ptr` (pointer to raw element array)
- Offset `8`: `capacity` (total slot count)
- Offset `16`: `head` (physical index of front element)
- Offset `24`: `len` (current live element count)
- Offset `32`: `flags` (0 = dynamic auto-grow, 1 = fixed overwrite mode)
- Offset `40`: `dropped_count` (total items evicted in overwrite mode)

| Function | Signature | Notes |
|----------|-----------|-------|
| `nx_rb_new` | (init_cap=nx_i0) -> nx_r0 | Allocates auto-growing ring buffer (min capacity 4) |
| `nx_rb_new_fixed` | (cap=nx_i0) -> nx_r0 | Allocates fixed-capacity circular buffer with overwrite mode |
| `nx_rb_len` | (rb=nx_i0) -> nx_r0 | Current live element count |
| `nx_rb_cap` | (rb=nx_i0) -> nx_r0 | Total allocated capacity |
| `nx_rb_is_empty` | (rb=nx_i0) -> nx_r0 | 1 if len == 0, else 0 |
| `nx_rb_is_full` | (rb=nx_i0) -> nx_r0 | 1 if len >= cap, else 0 |
| `nx_rb_dropped_count` | (rb=nx_i0) -> nx_r0 | Total elements evicted in overwrite mode |
| `nx_rb_clear` | (rb=nx_i0) -> nx_r0 | Resets buffer to empty state (head=0, len=0, dropped=0) |
| `nx_rb_push_back` | (rb=nx_i0, val=nx_i1) -> nx_r0 | Appends element to back (1=inserted, 2=overwritten oldest) |
| `nx_rb_push_front` | (rb=nx_i0, val=nx_i1) -> nx_r0 | Prepends element to front (1=inserted, 2=overwritten oldest) |
| `nx_rb_pop_front` | (rb=nx_i0) -> nx_r0 | Removes and returns front element (0 if empty) |
| `nx_rb_pop_back` | (rb=nx_i0) -> nx_r0 | Removes and returns back element (0 if empty) |
| `nx_rb_peek_front` | (rb=nx_i0) -> nx_r0 | Returns front element without removing (0 if empty) |
| `nx_rb_peek_back` | (rb=nx_i0) -> nx_r0 | Returns back element without removing (0 if empty) |
| `nx_rb_get` | (rb=nx_i0, idx=nx_i1) -> nx_r0 | Logical index lookup (0=front, len-1=back, bounds checked) |
| `nx_rb_set` | (rb=nx_i0, idx=nx_i1, val=nx_i2) -> nx_r0 | Updates element at logical index; 1 on success, 0 if OOB |
| `nx_rb_contains` | (rb=nx_i0, val=nx_i1) -> nx_r0 | 1 if value exists, 0 otherwise |
| `nx_rb_index_of` | (rb=nx_i0, val=nx_i1) -> nx_r0 | Returns logical index of first occurrence, or -1 if not found |
| `nx_rb_to_array` | (rb=nx_i0) -> nx_r0 | Exports elements in logical order to a contiguous array |
| `nx_rb_clone` | (rb=nx_i0) -> nx_r0 | Deep copy of ring buffer and element buffer |
| `nx_rb_equals` | (rb1=nx_i0, rb2=nx_i1) -> nx_r0 | 1 if identical lengths and elements in order, else 0 |
| `nx_rb_reverse` | (rb=nx_i0) -> nx_r0 | Reverses element order in-place |
| `nx_rb_rotate` | (rb=nx_i0, k=nx_i1) -> nx_r0 | Rotates elements by k positions (positive=right, negative=left) |
| `nx_rb_shrink_to_fit` | (rb=nx_i0) -> nx_r0 | Compacts allocated capacity to live element count (min 4) |

### Memory Primitives — `stdlib/memory.nex` (Features #87 & #88)

Byte-granular bulk memory operations for gap-buffer shifts, framebuffer clears, and buffer resets. `nx_memmove` is overlap-safe: it copies forward when `dst < src` and backward when `dst > src`.

| Function | Signature | Notes |
|----------|-----------|-------|
| `nx_memmove` | (dst=nx_i0, src=nx_i1, n=nx_i2) -> nx_r0 | Overlap-safe byte move; returns dst |
| `nx_memcpy` | (dst=nx_i0, src=nx_i1, n=nx_i2) -> nx_r0 | Forward copy for disjoint buffers; returns dst |
| `nx_memset` | (dst=nx_i0, byte_val=nx_i1, n=nx_i2) -> nx_r0 | Fills n bytes with low byte of value; returns dst |
| `nx_memzero` | (dst=nx_i0, n=nx_i1) -> nx_r0 | Zeroes n bytes; returns dst |

### Growable ByteBuffer — `stdlib/byte_buffer.nex` (Feature #99)

Heap-backed byte buffer (32-byte descriptor: `data_ptr`, `len`, `cap`, reserved) with automatic capacity doubling. Multi-byte values are little-endian. Reads are offset-based and out-of-bounds reads return 0.

| Function | Signature | Notes |
|----------|-----------|-------|
| `nx_buf_new` | (init_cap=nx_i0) -> nx_r0 | New buffer (capacity clamped to >= 16) |
| `nx_buf_write_u8` | (bb=nx_i0, v=nx_i1) -> nx_r0 | Appends low byte; returns new length |
| `nx_buf_write_u16` | (bb=nx_i0, v=nx_i1) -> nx_r0 | Appends 16-bit LE; returns new length |
| `nx_buf_write_u32` | (bb=nx_i0, v=nx_i1) -> nx_r0 | Appends 32-bit LE; returns new length |
| `nx_buf_write_u64` | (bb=nx_i0, v=nx_i1) -> nx_r0 | Appends 64-bit LE; returns new length |
| `nx_buf_write_bytes` | (bb=nx_i0, ptr=nx_i1, n=nx_i2) -> nx_r0 | Appends n bytes from memory; returns new length |
| `nx_buf_read_u8` | (bb=nx_i0, off=nx_i1) -> nx_r0 | Byte at offset (0 if out of bounds) |
| `nx_buf_read_u16` | (bb=nx_i0, off=nx_i1) -> nx_r0 | 16-bit LE at offset (0 if out of bounds) |
| `nx_buf_read_u32` | (bb=nx_i0, off=nx_i1) -> nx_r0 | 32-bit LE at offset (0 if out of bounds) |
| `nx_buf_read_u64` | (bb=nx_i0, off=nx_i1) -> nx_r0 | 64-bit LE at offset (0 if out of bounds) |
| `nx_buf_len` | (bb=nx_i0) -> nx_r0 | Current byte length |
| `nx_buf_cap` | (bb=nx_i0) -> nx_r0 | Allocated capacity |
| `nx_buf_clear` | (bb=nx_i0) -> nx_r0 | Resets length to 0 (capacity preserved) |
| `nx_buf_to_ptr` | (bb=nx_i0) -> nx_r0 | Direct data pointer for raw access |

### Bit Stream Reader & Writer — `stdlib/bit_stream.nex` (Feature #100)

LSB-first bit-level serial I/O (bit 0 of byte 0 is the first bit in the stream), matching DEFLATE-style packing. Bit counts are 1..63 per operation.

| Function | Signature | Notes |
|----------|-----------|-------|
| `nx_bit_writer_new` | (init_bytes=nx_i0) -> nx_r0 | New writer (capacity clamped to >= 8 bytes) |
| `nx_bit_write` | (w=nx_i0, val=nx_i1, count=nx_i2) -> nx_r0 | Appends low `count` bits LSB-first; returns total bits |
| `nx_bit_flush` | (w=nx_i0) -> nx_r0 | Pads with zero bits to byte boundary; returns byte length |
| `nx_bit_writer_bytes` | (w=nx_i0) -> nx_r0 | Whole bytes currently held (ceil of bits/8) |
| `nx_bit_reader_new` | (buf=nx_i0, byte_len=nx_i1) -> nx_r0 | New reader over an existing byte buffer |
| `nx_bit_read` | (r=nx_i0, count=nx_i1) -> nx_r0 | Reads next `count` bits (0 at end of stream) |
| `nx_bit_reader_pos` | (r=nx_i0) -> nx_r0 | Read cursor position in bits |
| `nx_bit_reader_bits_left` | (r=nx_i0) -> nx_r0 | Bits remaining between cursor and end |

### Priority Queue / Binary Heap — `stdlib/priority_queue.nex` (Feature #76)

Binary heap with min-heap or max-heap mode chosen at construction. O(log n) push/pop with iterative sift-up/sift-down, auto-doubling storage.

| Function | Signature | Notes |
|----------|-----------|-------|
| `nx_pq_new` | (init_cap=nx_i0, is_min_heap=nx_i1) -> nx_r0 | New heap (min when flag=1, max when flag=0) |
| `nx_pq_push` | (pq=nx_i0, val=nx_i1) -> nx_r0 | Appends and rebalances; returns new length |
| `nx_pq_pop` | (pq=nx_i0) -> nx_r0 | Removes and returns root (0 if empty) |
| `nx_pq_peek` | (pq=nx_i0) -> nx_r0 | Root value without removal (0 if empty) |
| `nx_pq_len` | (pq=nx_i0) -> nx_r0 | Element count |
| `nx_pq_is_empty` | (pq=nx_i0) -> nx_r0 | 1 if empty, else 0 |

### Doubly-Linked List — `stdlib/linked_list.nex` (Feature #77)

Node-based bidirectional list (24-byte nodes: prev, next, val) with O(1) operations at both ends and O(1) removal by node handle. Node links are directly walkable: next at `[node + 8]`, prev at `[node + 0]`.

| Function | Signature | Notes |
|----------|-----------|-------|
| `nx_list_new` | () -> nx_r0 | New empty list descriptor |
| `nx_list_push_back` | (l=nx_i0, val=nx_i1) -> nx_r0 | Appends; returns node handle |
| `nx_list_push_front` | (l=nx_i0, val=nx_i1) -> nx_r0 | Prepends; returns node handle |
| `nx_list_pop_back` | (l=nx_i0) -> nx_r0 | Removes and returns last value (0 if empty) |
| `nx_list_pop_front` | (l=nx_i0) -> nx_r0 | Removes and returns first value (0 if empty) |
| `nx_list_remove_node` | (l=nx_i0, node=nx_i1) -> nx_r0 | Unlinks node handle (1 ok, 0 null handle) |
| `nx_list_len` | (l=nx_i0) -> nx_r0 | Element count |
| `nx_list_head` | (l=nx_i0) -> nx_r0 | First node handle (0 if empty) |
| `nx_list_tail` | (l=nx_i0) -> nx_r0 | Last node handle (0 if empty) |
| `nx_list_node_val` | (node=nx_i0) -> nx_r0 | Payload stored in a node |

### Trie / Prefix Tree — `stdlib/trie.nex` (Feature #80)

Character prefix tree with parallel branch arrays per node (any byte value 1..255 may branch). Words may share prefixes; the empty string is a valid word.

| Function | Signature | Notes |
|----------|-----------|-------|
| `nx_trie_new` | () -> nx_r0 | New trie (root node handle) |
| `nx_trie_insert` | (trie=nx_i0, str=nx_i1) -> nx_r0 | Inserts word; 1 if newly added, 0 if already present |
| `nx_trie_contains` | (trie=nx_i0, str=nx_i1) -> nx_r0 | 1 if the exact word exists, else 0 |
| `nx_trie_starts_with` | (trie=nx_i0, prefix=nx_i1) -> nx_r0 | 1 if any word begins with prefix (empty prefix always matches) |
| `nx_trie_remove` | (trie=nx_i0, str=nx_i1) -> nx_r0 | Unmarks word terminator; 1 if it existed |

### LRU Cache — `stdlib/lru_cache.nex` (Feature #94)

Fixed-capacity cache combining an open-addressing hash table with an intrusive doubly-linked recency list. O(1) get/put with recency refresh; tail eviction leaves a tombstone that a later put of the same key revives; the table rebuilds from live nodes when tombstones fill it. Keys are non-zero 64-bit values (string pointers match by pointer identity); use `nx_lru_contains` to distinguish a stored 0 value from a missing key.

| Function | Signature | Notes |
|----------|-----------|-------|
| `nx_lru_new` | (cap=nx_i0) -> nx_r0 | New cache holding at most `cap` live entries |
| `nx_lru_get` | (cache=nx_i0, key=nx_i1) -> nx_r0 | Value for key (0 if missing); hit refreshes recency |
| `nx_lru_put` | (cache=nx_i0, key=nx_i1, val=nx_i2) -> nx_r0 | Insert/update; 1=new, 2=updated, 3=inserted after eviction |
| `nx_lru_contains` | (cache=nx_i0, key=nx_i1) -> nx_r0 | 1 if key is live (does not refresh recency) |
| `nx_lru_len` | (cache=nx_i0) -> nx_r0 | Live entry count |
| `nx_lru_cap` | (cache=nx_i0) -> nx_r0 | Maximum live entries |
| `nx_lru_clear` | (cache=nx_i0) -> nx_r0 | Removes all entries and resets the table |

### Arena Bump Allocator — `stdlib/arena.nex` (Feature #96)

Chunked bump allocator: O(1) allocation by pointer bump, 8-byte aligned results, automatic chunk chaining (default or oversized chunks), and a reset pass that is O(number of chunks) regardless of total bytes. Chunks are retained across resets for reuse.

| Function | Signature | Notes |
|----------|-----------|-------|
| `nx_arena_new` | (chunk_sz=nx_i0) -> nx_r0 | New arena with default chunk size (clamped >= 64) |
| `nx_arena_alloc` | (arena=nx_i0, n_bytes=nx_i1) -> nx_r0 | Bump-allocates (rounded up to 8 bytes); 0 for n <= 0 |
| `nx_arena_reset` | (arena=nx_i0) -> nx_r0 | Releases all allocations for reuse; keeps chunks |
| `nx_arena_used` | (arena=nx_i0) -> nx_r0 | Total bytes currently allocated |
| `nx_arena_cap` | (arena=nx_i0) -> nx_r0 | Total reserved bytes across all chunks |
| `nx_arena_chunks` | (arena=nx_i0) -> nx_r0 | Chunk count in the chain |

### UTF-8 Validate / Decode / Encode — `stdlib/unicode.nex` (Features #113-115)

Strict RFC 3629 UTF-8 primitives over raw byte buffers. Validation rejects overlong encodings (C0/C1 leads, E0 80-9F, F0 80-8F), UTF-16 surrogate encodings (ED A0-BF), and out-of-range scalars (F5+, F4 90+).

| Function | Signature | Notes |
|----------|-----------|-------|
| `nx_utf8_validate` | (buf=nx_i0, len=nx_i1) -> nx_r0 | 1 if bytes are strict UTF-8 (empty buffer is valid) |
| `nx_utf8_decode_next` | (buf=nx_i0, offset=nx_i1) -> nx_r0, nx_r1 | Decodes codepoint at offset; `nx_r1` = bytes consumed; (-1, 0) on malformed input |
| `nx_utf8_encode` | (cp=nx_i0, out=nx_i1) -> nx_r0 | Encodes scalar to 1-4 bytes; returns count (0 on invalid scalar) |

### Binary Search Primitives — `stdlib/search.nex` (Feature #93)

Binary searches over ascending sorted 64-bit arrays with overflow-free midpoints (`lo + (hi - lo) / 2`). Duplicate ranges are half-open: `[lower_bound, upper_bound)`.

| Function | Signature | Notes |
|----------|-----------|-------|
| `nx_bsearch_exact` | (arr=nx_i0, len=nx_i1, target=nx_i2) -> nx_r0 | Index of first match, or -1 when absent |
| `nx_bsearch_lower_bound` | (arr=nx_i0, len=nx_i1, target=nx_i2) -> nx_r0 | First index with arr[i] >= target (len if none) |
| `nx_bsearch_upper_bound` | (arr=nx_i0, len=nx_i1, target=nx_i2) -> nx_r0 | First index with arr[i] > target (len if none) |

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

- `tests/stdlib_test.nex` — 122 assertions across math, byte arrays, strings
- `tests/test_vec.nex` — Dynamic vector reallocation, push, pop, indexing
- `tests/test_hashmap.nex` — Open-addressing hashmap put, get, collision probing
- `tests/test_bitwise_ops.nex` — Bitwise operations desugared and verified
- `tests/test_poll_events.nex` — Async epoll and eventfd multiplexing
- `tests/test_string_escapes.nex` — Universal string escape sequences across Linux/Win/macOS
- `examples/stdlib_demo.nex` — Runnable tour of standard library modules
- `tests/test_memory.nex` — memmove overlap directionality, memset patterns, memzero ranges
- `tests/test_byte_buffer.nex` — ByteBuffer width roundtrips, auto-growth, block writes
- `tests/test_bit_stream.nex` — LSB-first bit roundtrips, flush alignment, cursor tracking
- `tests/test_priority_queue.nex` — min/max heap ordering, duplicates, growth, heapsort behavior
- `tests/test_linked_list.nex` — both-end pushes/pops, node removal at every position
- `tests/test_trie.nex` — prefix queries, shared branches, removal semantics, empty word
- `tests/test_lru_cache.nex` — eviction order, recency refresh, tombstone revival, table rebuild
- `tests/test_arena.nex` — alignment, chunk chaining, oversized chunks, reset reuse
- `tests/test_unicode.nex` — strict validation boundaries, decode/encode roundtrips, malformed input
- `tests/test_string_split_join.nex` — delimiter split edge cases, separator joins, roundtrips
- `tests/test_search.nex` — exact/lower/upper bounds, duplicates, negatives, single-element arrays
- `./nexus test` runs all 52 test suites and verifies 100% pass rate
