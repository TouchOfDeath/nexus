# NEXUS Toolchain & Engineering Suite
### Native Emitters, Intermediate Representation, Preprocessors, and Diagnostics

This directory contains the core toolchain utilities, native emitters, optimizers, and diagnostic tooling supporting the **NEXUS Native AOT Compiler**.

---

## 🛠️ Complete Tools Inventory

### 1. Native Emitters & Toolchain Core (C / Native Machine Code)

| Tool | Source | Target | Purpose & Architecture |
| :--- | :--- | :--- | :--- |
| **`nexprep`** | `nexprep.c` | Linux / Windows | **Frontend Preprocessor & AST Transformer**: Performs import/include expansion, struct desugaring, operator precedence partitioning (`prec_rewrite`), statement fusion, and loop induction scaling. |
| **`nexelf`** | `nexelf.c` | Linux x86-64 | **Standalone Linux ELF64 Emitter**: Synthesizes native ELF64 binaries executing directly via raw kernel syscalls with **0% libc and 0% loader**. |
| **`nexarm64`** | `nexarm64.c` | macOS / Linux ARM64 | **Native ARM64 AOT Compiler**: Generates AArch64 machine instructions for Apple Silicon and Linux aarch64. |
| **`nexmacho`** | `nexmacho.c` | macOS ARM64 | **Standalone macOS Mach-O Emitter**: Packages ARM64 code into macOS Mach-O executable binaries. |
| **`nexir`** | `nexir.c` | Tri-Platform | **N-IR Intermediate Representation Engine**: 3-address typed SSA intermediate representation optimizer and execution VM. |
| **`nexfmt`** | `nexfmt.c` | Tri-Platform | **Idempotent Code Formatter**: Standardizes `.nex` source code indentation, whitespace, and bracket alignment. |
| **`nextest`** | `nextest.c` | Tri-Platform | **Native Test Assertion Harness**: Fast native runner validating test assertions and printing pass/fail tallies. |
| **`nexload`** | `nexload.c` | Linux / POSIX | **Zero-Dependency PE32+ Bridge**: Executes PE32+ binaries on Linux for bootstrap generation validation. |

---

### 2. Verification, Interpretation & Fuzzing (Python)

| Tool | Source | Purpose | Key Capabilities |
| :--- | :--- | :--- | :--- |
| **`nexinterp.py`** | `tools/nexinterp.py` | **Reference Semantic Interpreter** | Serves as the mathematical semantic oracle. Evaluates NEXUS programs without compilation to verify compiler output equivalence. |
| **`nexdelta.py`** | `tools/nexdelta.py` | **Automated Delta Debugger** | Minimizes failing test programs into the smallest reproducible test cases via binary line-reduction. |
| **`nexfuzz.py`** | `tools/nexfuzz.py` | **7-Way Differential Fuzzer** | Generates randomized NEXUS code and cross-compares execution results across interpreter, unoptimized native, and HYDRON optimized native. |
| **`nexbootstrap.py`** | `tools/nexbootstrap.py` | **Bootstrap Driver** | Multi-generation compiler self-hosting verification driver. |
| **`nexstudio.py`** | `tools/nexstudio.py` | **Reference Studio IDE** | High-level Python/Tkinter code studio used as the functional specification reference for native IDE development. |

---

### 3. Windows Native Diagnostics Suite (C# / Zero-Dependency)

Built using Windows pre-installed components (`C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe`) with **zero external downloads or SDKs**:

| Tool Binary | Source | Purpose | Key Capabilities |
| :--- | :--- | :--- | :--- |
| **`nexpedump.exe`** | `nexpedump.cs` | PE32+ Header Inspector | Dumps DOS, COFF, Optional Headers, Section Tables, RVAs, and Import Address Table (IAT) function names. |
| **`nexdisasm.exe`** | `nexdisasm.cs` | x86-64 Native Disassembler | Disassembles raw x86-64 machine code from `.exe` files or byte offsets. Decodes REX, ModR/M, SIB, and branches. |
| **`nexdiff.exe`** | `nexdiff.cs` | Visual Binary Diff | Compares two binaries byte-for-byte, maps differences to PE sections, and renders colored hex side-by-side dumps. |
| **`nexdebug.exe`** | `nexdebug.cs` | Win32 Debugger | Spawns binaries under active native debug monitoring. Intercepts Access Violations & Division-by-Zero, dumping registers and call stack. |
| **`nexfuzz.exe`** | `nexfuzz.cs` | Grammar Stress Fuzzer | Generates randomized NEXUS programs to stress-test Windows compilation and execution. |

---

## ⚡ Quick Start & Usage Examples

### Inspecting PE Headers (`nexpedump.exe`)
```powershell
.\tools\nexpedump.exe compiler\nexc.exe
```

### Disassembling Emitted Machine Code (`nexdisasm.exe`)
```powershell
.\tools\nexdisasm.exe compiler\nexc.exe 0x200 128
```

### Verifying Bit-for-Bit Identity (`nexdiff.exe`)
```powershell
.\tools\nexdiff.exe compiler\nexc3.exe compiler\nexc4.exe
```

### Automated Failing Test Minimization (`nexdelta.py`)
```bash
./nexus delta failing_test.nex
```

### Semantic Differential Fuzzing (`nexfuzz.py`)
```bash
./nexus fuzz --iterations 1000
```

### Code Formatting (`nexfmt`)
```bash
./nexus fmt examples/fibonacci.nex
```

---

## 🔨 Rebuilding Tools

### Linux & macOS Emitters:
Built automatically via `./nexus build` or:
```bash
gcc -O2 tools/nexprep.c -o bin/nexprep
gcc -O2 tools/nexelf.c -o bin/nexelf
gcc -O2 tools/nexarm64.c -o bin/nexarm64
gcc -O2 tools/nexmacho.c -o bin/nexmacho
gcc -O2 tools/nexir.c -o bin/nexir
gcc -O2 tools/nexfmt.c -o bin/nexfmt
gcc -O2 tools/nextest.c -o bin/nextest
```

### Windows C# Diagnostic Tools:
```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_tools.ps1
```
