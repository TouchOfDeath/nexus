# NEXUS Language Stress-Test & Weakness Audit

This document systematically logs the findings, friction points, edge cases, and design limitations discovered during intensive, real-world domain stress-testing of the NEXUS programming language.

---

## Battery Index

| Test # | Domain / Test Name | Status | Key Focus |
|---|---|---|---|
| **Test 1** | CLI Argument Parser (`grep` / `wc`) | *Pending* | `argc`, `argv`, string slice/matching, OS exit codes |
| **Test 2** | Text Processing & CSV Parser | *Pending* | Dynamic reallocation, delimiters, parsing strings to ints/floats |
| **Test 3** | Data Structures (Hash Map / Dynamic Array) | *Pending* | Hash functions, collision resolution, pointer arithmetic |
| **Test 4** | OS Syscall / Networking (HTTP client or socket) | *Pending* | Direct kernel syscalls (`socket`, `connect`, `send`, `recv`) |
| **Test 5** | **Machine Learning: 2-Layer Neural Net (XOR Solver)** | **COMPLETE** | IEEE-754 SSE2 backprop, Taylor range reduction, matrix weight ops |

---

## Test 5: Machine Learning (2-Layer Neural Network - XOR Solver)

- **Artifacts Created**:
  - Source: [`examples/neural_network.nex`](file:///home/lifelonglearner/nexus_project/examples/neural_network.nex)
  - Prototype / Verification: [`scratch/test_nn_full.nex`](file:///home/lifelonglearner/nexus_project/scratch/test_nn_full.nex)
- **Execution Target**: Native ELF64 Linux (x86-64), 0% libc, 0% runtime, zero dependencies.
- **Model Topology**:
  - Input layer: 2 inputs ($x_1, x_2$)
  - Hidden layer: 2 units ($h_1, h_2$) with Sigmoid activation
  - Output layer: 1 unit ($\hat{y}$) with Sigmoid activation
  - Total parameters: 9 (6 weights, 3 biases: $w_{11}, w_{21}, b_1, w_{12}, w_{22}, b_2, v_1, v_2, b_o$)
  - Optimization: Batch Gradient Descent with Backpropagation, 5,000 epochs, learning rate $\eta = 3.0$
- **Convergence Verification**:
  - $(0, 0) \to 0.008753$ ($< 0.05$ target 0)
  - $(0, 1) \to 0.990894$ ($> 0.95$ target 1)
  - $(1, 0) \to 0.990832$ ($> 0.95$ target 1)
  - $(1, 1) \to 0.011236$ ($< 0.05$ target 0)
  - Automated assertions: 4/4 cases pass (`assert_eq verified_cases, 4`)
- **Performance**:
  - Compilation + Linking + Execution of 5,000 epochs (20,000 forward/backward passes): **16 ms total wall-clock time**.

---

### Friction Points & Weaknesses Cataloged

#### 1. Flat Global Variable Scope & Silent Variable Clobbering (Critical Risk)
- **Severity**: High (Silent Logic Corruption)
- **Symptom**:
  Functions do not create stack-isolated local activation frames. While function parameters are desugared to unique symbols (`_arg_<fn>_<idx>`), variable declarations (`let var = ...`) inside function bodies write directly to the compiler's flat `.bss` symbol table.
- **Real Incident in Test 5**:
  Inside `fn sigmoid(x)`, an intermediate variable was named `let y = x /. 16.0`. In the outer training loop, `let y = Y[i]` held the current sample's target label. Calling `sigmoid` silently overwritten the outer `y` with the inner value ($z / 16.0$), causing the target to drift to negative values (e.g. $-0.687345$) and completely corrupting loss computation.
- **Workaround**:
  Manually prefix all function-local variables (e.g., `sx`, `sx2`, `sres`) or maintain strict global naming conventions.
- **Proposed Compiler Enhancement**:
  - *Short-term*: In `nexprep`, automatically mangle function-local `let` variables to `_local_<fn>_<var>`.
  - *Long-term*: Emit proper stack frame pointer offsets (`[rbp - offset]`) for function-scoped variables.

---

#### 2. Float Infix Expressions Lack Parenthesis Precedence
- **Severity**: Medium (Ergonomic / Readability Friction)
- **Symptom**:
  Floating-point infix expressions (`+.`, `-.`, `*.`, `/.`) in `nexprep` do not support parenthetical grouping `(...)`. The desugarer splits tokens on whitespace, causing expressions like:
  ```nex
  let v1 = v1 -. (lr *. d_zo *. ah1)
  ```
  to fail compilation due to tokens `(lr` and `ah1)`.
- **Workaround**:
  Decompose all composite formulas into individual sequential binary operations:
  ```nex
  let delta_v1 = lr *. d_zo *. ah1
  let v1 = v1 -. delta_v1
  ```
- **Proposed Enhancement**:
  Extend `desugar_float` in `nexprep` with a standard shunting-yard or tree rewrite to support arbitrary parenthesis nesting.

---

#### 3. Immediate Float Literals in `store64` Parsed as Integers
- **Severity**: Medium (Silent Data Corruption)
- **Symptom**:
  Writing `store64 [ptr + 0] 1.0` parses `1` as an integer literal and emits `mov qword [ptr], 1`. The integer `1` loaded into an SSE register represents a denormalized/subnormal float ($0.00000000000...$), and the trailing `.0` is either dropped or rejected.
- **Workaround**:
  Always bind float literals to a variable first before storing:
  ```nex
  let val = 1.0
  store64 [ptr] val
  ```
  Or use array literal syntax (`let Y = [0.0, 1.0]`), which generates intermediate `_arr_el_N` variables.
- **Proposed Enhancement**:
  Have the compiler or preprocessor detect floating-point constants in `store64` arguments and allocate 64-bit IEEE-754 constant pool entries or load via XMM register.

---

#### 4. Console Output Formatting (No Inline / Unbuffered Print)
- **Severity**: Low (Ergonomics)
- **Symptom**:
  Both `print` and `print_float` unconditionally append `\n`. There is no `print_raw` or format string functionality (`print "XOR(%f, %f) = %f"`).
- **Workaround**:
  Printing a vector or key-value pair requires 4–6 lines of console output.
- **Proposed Enhancement**:
  Introduce `print_raw` (or `print_str_no_newline`) and basic format printing.

---

#### 5. Multidimensional Array / Tensor Indexing
- **Severity**: Low (Ergonomics)
- **Symptom**:
  NEXUS provides 1D array indexing (`A[i]`). 2D indexing for weight matrices requires manual pointer arithmetic (`A[row * cols + col]`).
- **Workaround**:
  For small networks, scalar weights (`w11`, `w12`, `v1`, etc.) or flat 1D stride arithmetic is practical.
- **Proposed Enhancement**:
  Future syntax sugar for 2D arrays: `A[row, col]` desugaring to `A[row * stride + col]`.

---

## Summary & Verification Status
- **Test 5 Status**: **PASSED**
- All 27 project example suites, 39 foundation tests, 13 diagnostics, stdlib (94 assertions), and optimizer (52 assertions) pass with zero errors.
- Test 5 has been codified as a permanent regression test in [`examples/neural_network.nex`](file:///home/lifelonglearner/nexus_project/examples/neural_network.nex).
