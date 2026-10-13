# NEXUS Developer & Debugging Toolkit Suite
### Zero-Install Native Diagnostics for Windows Systems & Language Engineering

This directory contains the developer and debugging suite created to support and accelerate future engineering, optimization, and expansion of the **NEXUS Native AOT Compiler**.

All tools in this suite are built using **Windows pre-installed components only** (`C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe`), requiring **zero downloads, zero external SDKs, and zero Visual Studio installations**.

---

## 🛠️ Toolkit Overview

| Tool Binary | Source | Purpose | Key Capabilities |
| :--- | :--- | :--- | :--- |
| **`nexpedump.exe`** | [`nexpedump.cs`](file:///nexpedump.cs) | PE32+ Binary & Header Inspector | Dumps DOS, COFF, Optional Headers, Section Tables, RVAs, and Import Address Table (IAT) function names. |
| **`nexdisasm.exe`** | [`nexdisasm.cs`](file:///nexdisasm.cs) | x86-64 Native Disassembler | Disassembles raw x86-64 machine code from `.exe` files or raw byte offsets. Decodes REX, ModR/M, SIB, and branch targets. |
| **`nexdiff.exe`** | [`nexdiff.cs`](file:///nexdiff.cs) | Visual Side-by-Side Binary Diff | Compares two binaries byte-for-byte, maps differences to PE sections, and renders colored hex side-by-side dumps. |
| **`nexdebug.exe`** | [`nexdebug.cs`](file:///nexdebug.cs) | Win32 Debugger & Crash Interceptor | Spawns target binaries under active native debug monitoring. Intercepts Access Violations & Division-by-Zeros, dumping RIP, registers, and stack memory. |
| **`nexfuzz.exe`** | [`nexfuzz.cs`](file:///nexfuzz.cs) | Automated Grammar & Stress Fuzzer | Generates randomized, nested, chained NEXUS programs, compiling and running them to detect compiler panics and infinite loops. |

---

## ⚡ Quick Start & Usage Examples

### 1. Inspecting PE Headers and Imports (`nexpedump.exe`)
Inspect any compiled executable to verify headers, alignments, and Win32 imported APIs:
```powershell
.\nexpedump.exe ..\compiler\app.exe
```
**Sample Output:**
```
[+] File: app.exe (32,768 bytes)
--- OPTIONAL HEADER (PE32+ 64-bit) ---
  ImageBase:           0x0000000000400000
  AddressOfEntryPoint: 0x00001000 (VA: 0x0000000000401000)
  SectionAlignment:    0x00001000 (4,096 bytes)
--- IMPORT DIRECTORY & FUNCTIONS ---
  [DLL] kernel32.dll
    IAT [0x00008028] API: GetStdHandle
    IAT [0x00008030] API: WriteFile
    IAT [0x00008038] API: ExitProcess
    IAT [0x00008040] API: ReadFile
    IAT [0x00008048] API: VirtualAlloc
    IAT [0x00008050] API: CreateFileA
    IAT [0x00008058] API: CloseHandle
```

---

### 2. Disassembling x86-64 Machine Code (`nexdisasm.exe`)
Disassemble the `.text` section of an executable or inspect specific byte offsets:
```powershell
# Automatically disassembles the PE .text section:
.\nexdisasm.exe ..\compiler\app.exe

# Or specify custom file offset and length:
.\nexdisasm.exe ..\compiler\app.exe 0x200 128
```
**Sample Output:**
```
  Offset     RVA (Virtual)      Bytes                Assembly Instruction
  ---------------------------------------------------------------------------------------
  0x000200   0x000000401000   48 81 EC 28 01 00 00 sub rsp, 0x128
  0x000207   0x000000401007   48 89 E5             mov rbp, rsp
  0x00020A   0x00000040100A   48 8D 7D 30          lea rdi, [rbp + 0x30]
  0x00020E   0x00000040100E   B9 1A 00 00 00       mov ecx, 0x0000001A
  0x000213   0x000000401013   31 C0                xor eax, eax
  0x000215   0x000000401015   F3 48 AB             rep stosq
  0x000218   0x000000401018   B9 F5 FF FF FF       mov ecx, 0xFFFFFFF5
  0x00021D   0x00000040101D   FF 15 05 70 00 00    call qword [rip + 0x7005]
```

---

### 3. Comparing Two Binaries (`nexdiff.exe`)
Compare compiler generations or check for binary regressions with side-by-side hex display:
```powershell
.\nexdiff.exe ..\compiler\nexc3.exe ..\compiler\nexc4.exe
```
**Output for Identical Binaries:**
```
*****************************************************************
 [***] 100% BIT-FOR-BIT IDENTICAL! ZERO DIFFERENCES FOUND! [***] 
       Compared all 32,768 bytes with mathematical precision.
*****************************************************************
```

---

### 4. Catching and Diagnosing Crashes (`nexdebug.exe`)
Launch any executable under active native debugging monitor:
```powershell
.\nexdebug.exe ..\compiler\app.exe
```
If the binary encounters an `ACCESS_VIOLATION` or `DIVIDE_BY_ZERO`, `nexdebug` halts the crash and prints:
- Faulting address and operation (`READ`, `WRITE`, or `EXECUTE`)
- Full 64-bit hardware register dump (`RIP`, `RAX`, `RBX`, `RCX`, `RDX`, `RBP`, `RSP`, `R8`..`R15`)
- Disassembled instruction bytes at the faulting `RIP`
- 64-byte call stack memory dump at `RSP`

---

### 5. Automated Grammar & Stress Fuzzing (`nexfuzz.exe`)
Run randomized stress tests against `nexc.exe` to discover parser crashes, buffer overflows, or compiler hangs:
```powershell
.\nexfuzz.exe ..\compiler\nexc.exe 25
```

---

## 🔨 Rebuilding the Toolkit
To recompile all 5 tools at any time using Windows preinstalled C# compiler:
```powershell
powershell -ExecutionPolicy Bypass -File .\build_tools.ps1
```
