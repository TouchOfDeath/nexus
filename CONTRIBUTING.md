# Contributing to NEXUS

Thank you for your interest in contributing to **NEXUS**!

NEXUS is an autonomous, self-hosting programming language and native AOT compiler toolchain targeting Linux ELF64, Windows PE32+, and macOS ARM64 with direct kernel syscalls and **0% libc runtime**.

---

## 🏛️ Core Principles & Invariants

When writing code or contributing features to NEXUS, maintain these project invariants:

1. **Self-Hosting Parity**: The compiler core (`compiler/nexc.nex`) is written in pure NEXUS and compiles itself. Any change to compiler semantics must preserve **bit-for-bit self-hosting convergence** (`./nexus self-host`).
2. **Zero Runtime / Zero Libc**: Native binaries emitted by NEXUS must remain 100% standalone, executing directly via operating system kernel syscalls without linking to `libc.so`, `msvcrt.dll`, or external runtimes.
3. **Tri-Platform Compatibility**: Code and standard library modules should work portably across Linux x86-64, Windows x86-64, and Apple Silicon / Linux ARM64.
4. **Idempotent Code Formatting**: Run `./nexus fmt <file.nex>` to normalize indentation, operator spacing, and statement formatting before committing.

---

## 🛠️ Setting Up Your Environment

### Linux (Ubuntu / Debian / Fedora / Arch)
Prerequisites: A 64-bit Linux distribution with `gcc` (for building native tools) and `python3`:
```bash
# Clone the repository
git clone https://github.com/TouchOfDeath/nexus.git
cd nexus

# Run the master verification pipeline
./build_all.sh

# Run the full regression test suite
./nexus test

# Verify bit-for-bit self-hosting parity
./nexus self-host
```

### Windows (Zero-Dependency)
On Windows, NEXUS requires **zero third-party toolchains** (no Visual Studio or Python needed):
```powershell
# In PowerShell:
.\build_all.ps1

# Run tests natively
nexus test

# Verify self-hosting
nexus self-host
```

---

## 🧪 Testing Guidelines

Before opening a pull request, ensure all tests pass:

```bash
# 1. Run all regression test suites (examples, diagnostics, foundation, stdlib)
./nexus test

# 2. Test a specific file
./nexus test --file tests/test_ring_buffer.nex

# 3. Run benchmarks
./nexus bench
./nexus hydron

# 4. Verify 3-stage bootstrap convergence
./nexus bootstrap
```

### Adding New Language Features or Standard Library Modules
1. **Source Code**: Place new standard library modules in `stdlib/<feature_name>.nex`.
2. **Regression Test**: Add a comprehensive companion test suite in `tests/test_<feature_name>.nex` using `assert_eq` / `assert_ne`.
3. **Test Runner Integration**: Wire the test suite into the `./nexus` test driver.
4. **Documentation**: Document the new API in [`docs/BUILTINS.md`](docs/BUILTINS.md) and [`docs/LANGUAGE_REFERENCE.md`](docs/LANGUAGE_REFERENCE.md).

---

## 📬 Pull Request Workflow

1. Fork the repository and create a feature branch (`git checkout -b feat/my-feature`).
2. Make your changes adhering to existing code conventions.
3. Verify all tests pass with `./nexus test` and `./nexus self-host`.
4. Commit with descriptive messages (e.g. `feat(stdlib): add PriorityQueue min/max binary heap`).
5. Open a Pull Request against the `main` branch.
