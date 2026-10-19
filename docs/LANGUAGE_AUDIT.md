# NEXUS Language Stress-Test & Real-World Domain Audit (v1.0.0)

This document logs the real-world domain verification battery and language audit for **NEXUS v1.0.0**. All five domain applications and all previously identified compiler/preprocessor friction points are now **100% COMPLETE & RESOLVED**.

---

## Battery Index (5 / 5 Complete)

| Test # | Domain / Application | Source Artifact | Status | Key Capabilities Verified |
|---|---|---|---|---|
| **Test 1** | **CLI Argument Parser (`grep` & `wc` Utility)** | `examples/cli_tool_grep_wc.nex` | **COMPLETE** | `os_argc`, `os_argv(i)`, `nx_read_file`, `nx_write_file`, `nx_str_slice`, `exit(code)` |
| **Test 2** | **Text Processing & CSV Analytics Engine** | `examples/csv_processor.nex` | **COMPLETE** | CSV file I/O, delimiter slicing, `int(str)`, `nx_hashmap_put_str`/`get_str` grouping |
| **Test 3** | **Data Structures & Graph Shortest-Path Routing** | `examples/data_structures_showcase.nex` | **COMPLETE** | Dynamic `HashMap` auto-resizing (64+ slots), tombstones, `PriorityQueue`, BFS via `RingBuffer` |
| **Test 4** | **HTTP/1.1 Protocol Parser, Router & JSON API** | `examples/http_protocol_demo.nex` | **COMPLETE** | HTTP request frame parsing, query parameter `HashMap`, `match` routing, JSON response framing |
| **Test 5** | **Machine Learning: 2-Layer Neural Net (XOR Solver)** | `examples/neural_network.nex` | **COMPLETE** | IEEE-754 SSE2 backprop, Taylor range reduction, 5,000 epochs in 16 ms |

---

## Domain Verification Details

### Test 1: CLI Argument Parser (`grep` & `wc` Utility)
- **Artifact**: `examples/cli_tool_grep_wc.nex`
- **Capabilities Verified**:
  - Reads `os_argc` inside `if os_argc >= 3` conditions and extracts CLI arguments via `os_argv(i)`.
  - Supports `--wc <file>` (counting lines, words, and bytes) and `--grep <pattern> <file>` (printing matching line numbers and contents), plus an automated 7-assertion self-test mode when invoked without arguments.
  - Exercises variable-path `file_open` and `file_create` via `stdlib/io.nex` (`nx_write_file`, `nx_read_file`, `nx_file_exists`) and process termination via `exit(code)`.

### Test 2: Text Processing & CSV Analytics Processor
- **Artifact**: `examples/csv_processor.nex`
- **Capabilities Verified**:
  - Writes a 6-record corporate dataset (`id,department,employees,revenue_k`) to disk and reads it back into heap memory.
  - Parses comma-separated columns, converts numeric text fields with `int(field)`, and aggregates headcount and revenue by department name using string-keyed hash maps (`nx_hashmap_put_str`, `nx_hashmap_get_str`).
  - Verifies all 14 assertions across row count, total headcount (`165`), total revenue (`$5,100k`), average (`$850k`), min (`$400k`), max (`$1,200k`), and per-department totals.

### Test 3: Data Structures & Graph Shortest-Path Routing
- **Artifact**: `examples/data_structures_showcase.nex`
- **Capabilities Verified**:
  - Inserts 40 integer keys (including key `0`) into `HashMap`, triggering automatic capacity doubling (`16 -> 32 -> 64 -> 128`) and full rehashing, and verifies O(1) tombstone deletion (`nx_hashmap_remove`).
  - Verifies string-keyed dictionary updates and removals (`nx_hashmap_put_str`, `nx_hashmap_get_str`, `nx_hashmap_remove_str`).
  - Exercises `PriorityQueue` min-heap (`nx_pq_new(8, 1)`) and runs Breadth-First Search (BFS) shortest-path routing over a 6-node directed graph using `RingBuffer` (`nx_rb_new(16)`), passing all 27 assertions.

### Test 4: HTTP/1.1 Protocol Parser, Router & JSON Telemetry Builder
- **Artifact**: `examples/http_protocol_demo.nex`
- **Capabilities Verified**:
  - Parses raw HTTP/1.1 request frames (`GET /api/v1/telemetry?node=7&limit=25 HTTP/1.1\r\n...`), splitting method, route path, and URL query parameters.
  - Populates query parameters into a string-keyed `HashMap`, routes the request via `match route_path`, and formats a complete HTTP/1.1 200 OK response with JSON body and exact `Content-Length`, passing all 8 assertions.

### Test 5: Machine Learning (2-Layer Neural Network - XOR Solver)
- **Artifact**: `examples/neural_network.nex`
- **Capabilities Verified**:
  - 2-2-1 neural network with Sigmoid activation trained via batch gradient descent and backpropagation over 5,000 epochs (20,000 forward/backward passes) in ~16 ms wall-clock time, passing all 4 convergence assertions.

---

## Resolution Log of Historical Friction Points

1. **Function Local Scope & Stack Activation Frames [RESOLVED in v1.0.0]**:
   - Parameterized functions (`fn name(p1, p2) { ... }`) automatically save and restore caller local variables on the 1 MB `_nx_call_stack` activation stack (`_nx_sp`, `_nx_fp`), and block-scoped `local` variables are lexically isolated per `{ ... }` block (`_ls_<id>_<var>`).
   - Furthermore, all 250+ standard library functions in `stdlib/*.nex` can now be invoked directly with parameterized call syntax `nx_fn(arg0, arg1, ...)` without clobbering caller frames.
2. **Float Infix Expressions & Parenthesis Precedence [RESOLVED]**:
   - Full Shunting-Yard precedence and parenthesis grouping for floating-point expressions (`+.`, `-.`, `*.`, `/.` and unified `+`, `-`, `*`, `/`).
3. **Immediate Float Literals in `store64` [RESOLVED]**:
   - Automatic hoisting of float literals in `store64 [ptr + off] 1.5` to preserve IEEE-754 64-bit representations.
4. **Console Output Formatting, `print_raw`, & First-Class String Operations [RESOLVED in v1.0.0]**:
   - Added native `print_raw` (no trailing newline) in `compiler/nexc.nex`, full `TY_STR` type inference across variables/parameters/returns/`??`/`?:`, string concatenation (`+`), string content equality (`==`, `!=`), and `str(int)` / `int(str)` conversions.
5. **Variable-Path File I/O & Slot-0 Shadow Space Safety [RESOLVED in v1.0.0]**:
   - Upgraded `file_open` and `file_create` in `compiler/nexc.nex` to accept both string literals and string pointer variables, shifted variable slot 0 from `[rbp + 48]` to `[rbp + 56]` so Windows x64 shadow space (`[rsp + 0x20..0x30]`) never overwrites user variable 0, and added `stdlib/io.nex`.
