# NEXUS Documentation Hub

Welcome to the **NEXUS** documentation directory. NEXUS is a standalone, self-hosted Ahead-Of-Time (AOT) programming language and native compiler toolchain targeting Linux ELF64 (x86-64 and aarch64), Windows PE32+, and macOS ARM64 Mach-O with **0% libc, 0% runtime, and direct kernel syscalls**.

This directory contains complete technical guides, language specifications, tutorials, standard library references, and compiler architectural blueprints.

---

## 📚 Documentation Index

| Document | Description | Target Audience |
| :--- | :--- | :--- |
| **[Language Reference](LANGUAGE_REFERENCE.md)** | Complete specification of syntax, lexical rules, types, operator precedence, memory operations, control flow, functions, closures, and stdlib modules. | All NEXUS Developers |
| **[Hands-On Tutorial](TUTORIAL.md)** | Step-by-step introduction from Hello World to memory management, data structures, and native systems programming. | Beginners & New Users |
| **[Built-ins & Stdlib Reference](BUILTINS.md)** | Detailed API documentation for all compiler built-ins, memory primitives, and 30+ standard library modules. | Systems Programmers |
| **[N-IR Intermediate Representation](NEXUS_IR_SPECIFICATION.md)** | Specification of the typed, 3-address, SSA-style control-flow intermediate representation and multi-pass optimizer. | Compiler Engineers |
| **[Technical Audit & Direction](TECHNICAL_AUDIT.md)** | Independent technical audit, benchmark methodology analysis, and architectural engineering priorities for NEXUS. | Architecture & Core Devs |
| **[500 Features Needed Roadmap](500_FEATURES_NEEDED.md)** | Exhaustive 500-feature gap analysis and architectural specification — **500 / 500 Completed & Verified**. | Contributors & Maintainers |
| **[Remaining Features Needed Inventory](REMAINING_FEATURES_NEEDED.md)** | Active capability tracking inventory confirming **0 / 500 remaining** across Phases A–F. | Architecture & Core Devs |
| **[Benchmark Results](BENCHMARK_RESULTS.txt)** | Scientific two-category benchmark report: Raw AOT physical loop throughput vs HYDRON recurrence-attractor elimination. | Performance Researchers |
| **[Interactive Web Portal](index.html)** | Standalone HTML documentation and interactive showcase page. | General Audience |
| **[Historical Archive](archive/)** | Early development handoffs (v5.0 Stage 5), ecosystem roadmaps, and initial bootstrap verification logs. | Historical Reference |

---

## 🚀 Quick Reference: Core Toolchain Commands

```bash
# Compile and run any NEXUS program natively on Linux kernel
./nexus run examples/hello_world.nex

# Run full regression test suite (27 examples, 13 diagnostics, 50 foundation, 26 stdlib)
./nexus test

# Verify Stage 5 bit-for-bit self-hosting parity (PE32+ and ELF64)
./nexus self-host

# Run Raw AOT code generation benchmark (50M un-eliminated physical iters)
./nexus bench

# Run HYDRON recurrence-attractor semantic optimization benchmark
./nexus hydron

# Format source code idempotently
./nexus fmt examples/fibonacci.nex

# Launch the interactive REPL with standard library preloaded
./nexus repl

# Launch the Pure Native NEXUS IDE (100% Machine Code)
./nexus ide
```

---

## 🏛️ Compiler Ecosystem Architecture

NEXUS features a **self-hosted compiler core**: [`compiler/nexc.nex`](../compiler/nexc.nex) is written in pure NEXUS source code and compiles itself with zero external runtime dependencies. Emitted binaries are 100% standalone and directly executable on the host kernel without any C runtime libraries (`libc.so`, `msvcrt.dll`, etc.).

```
nexus_project/
├── README.md                  <- Main project landing page & quick start
├── PROJECT_STATE.md           <- Detailed living status, metrics, and milestones
├── CHANGES.md                 <- Ecosystem release changelog
├── LLMS.txt                   <- AI assistant prompt & syntax guide
├── nexus                      <- Master toolchain CLI driver (Linux/macOS)
├── nexus.cmd / nexus.ps1      <- Zero-dependency Windows CLI drivers
│
├── compiler/                  <- Self-hosting compiler source (nexc.nex) & stages
├── stdlib/                    <- 30+ Standard library modules in pure NEXUS
├── examples/                  <- Demos, games, raycaster, GUI flappy, web server
├── tests/                     <- Comprehensive regression & foundation test suites
├── tools/                     <- Native emitters (nexelf, nexmacho, nexarm64), N-IR, fuzzer
└── docs/                      <- Documentation hub (this directory)
```
