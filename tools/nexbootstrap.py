#!/usr/bin/env python3
"""
NEXUS Official 3-Stage Bootstrap Driver
=======================================

Performs a deterministic, hermetic 3-stage bootstrap verification of the
self-hosted NEXUS compiler. The 3-stage model is the gold standard for
self-hosting language compilers (used by TCC, Go, Rust, etc.):

  Stage 1 (Bootstrap Seed): Pre-built nexc.exe compiles nexc.nex -> Gen1
  Stage 2 (Self-Compile):   Gen1 compiles nexc.nex -> Gen2
  Stage 3 (Fixed Point):    Gen2 compiles nexc.nex -> Gen3

A correct self-hosting compiler reaches a fixed point: Gen2 == Gen3,
bit-for-bit. If they differ, the compiler is non-deterministic or the
bootstrap seed is stale.

Outputs:
  - compiler/nexc_gen1.exe   (Stage 1 output)
  - compiler/nexc_gen2.exe   (Stage 2 output)
  - compiler/nexc_gen3.exe   (Stage 3 output)
  - compiler/nexc.exe        (Promoted from Gen3 on success)
  - compiler/nexc.elf        (Standalone Linux ELF, regenerated)
  - .bootstrap_report.json   (Machine-readable verification report)

Usage:
  ./nexus bootstrap
  ./nexus bootstrap --keep-stages      # don't delete intermediate files
  ./nexus bootstrap --from <seed.exe>  # use custom Stage 0 seed
  python3 tools/nexbootstrap.py [--keep-stages] [--from <seed>]
"""
import sys, os, subprocess, json, time, hashlib, shutil, argparse

BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
COMPILER_DIR = os.path.join(BASE_DIR, "compiler")
BIN_DIR = os.path.join(BASE_DIR, "bin")
NEXLOAD = os.path.join(BIN_DIR, "nexload")
NEXELF = os.path.join(BIN_DIR, "nexelf")
NEXPREP = os.path.join(BIN_DIR, "nexprep")

NEXC_NEX = os.path.join(COMPILER_DIR, "nexc.nex")
NEXC_EXE = os.path.join(COMPILER_DIR, "nexc.exe")
NEXC_ELF = os.path.join(COMPILER_DIR, "nexc.elf")
CODE_NEX = os.path.join(COMPILER_DIR, "code.nex")
APP_EXE = os.path.join(COMPILER_DIR, "app.exe")

COLOR_GREEN = "\033[0;32m"
COLOR_RED = "\033[0;31m"
COLOR_YELLOW = "\033[0;33m"
COLOR_CYAN = "\033[0;36m"
NC = "\033[0m"


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def file_size(path):
    return os.path.getsize(path)


def run_compile(compiler_exe, cwd=COMPILER_DIR, label=""):
    """Run a NEXUS compiler (PE) to produce app.exe from code.nex in cwd."""
    app_path = os.path.join(cwd, "app.exe")
    if os.path.exists(app_path):
        os.remove(app_path)
    if label:
        print(f"  [{label}] {os.path.basename(compiler_exe)} -> app.exe")
    res = subprocess.run([NEXLOAD, compiler_exe], cwd=cwd,
                       capture_output=True, text=True, timeout=60)
    if not os.path.exists(app_path):
        print(f"{COLOR_RED}[-] Compilation failed!{NC}")
        print(f"    stdout: {res.stdout}")
        print(f"    stderr: {res.stderr}")
        return False
    return True


def gen_elf_from_pe(pe_path, elf_path):
    """Convert a PE binary to standalone Linux ELF64."""
    if os.path.exists(elf_path):
        os.remove(elf_path)
    res = subprocess.run([NEXELF, pe_path, elf_path],
                       capture_output=True, text=True, timeout=30)
    return os.path.exists(elf_path), res.stderr


def files_identical(p1, p2):
    """Bit-for-bit comparison of two files."""
    if not (os.path.exists(p1) and os.path.exists(p2)):
        return False
    if file_size(p1) != file_size(p2):
        return False
    h1 = sha256_file(p1)
    h2 = sha256_file(p2)
    return h1 == h2


def count_diff_bytes(p1, p2):
    """Count differing bytes between two files (used for diagnostics)."""
    if file_size(p1) != file_size(p2):
        return max(file_size(p1), file_size(p2))
    diffs = 0
    with open(p1, "rb") as f1, open(p2, "rb") as f2:
        while True:
            b1 = f1.read(65536)
            b2 = f2.read(65536)
            if not b1:
                break
            for x, y in zip(b1, b2):
                if x != y:
                    diffs += 1
    return diffs


def main():
    parser = argparse.ArgumentParser(
        description="NEXUS Official 3-Stage Bootstrap Driver")
    parser.add_argument("--keep-stages", action="store_true",
                        help="Keep intermediate Stage 1/2/3 .exe files")
    parser.add_argument("--from", dest="seed", default=NEXC_EXE,
                        help=f"Stage 0 seed compiler (default: {NEXC_EXE})")
    parser.add_argument("--no-promote", action="store_true",
                        help="Don't promote Gen3 to nexc.exe / regenerate nexc.elf")
    args = parser.parse_args()

    seed = os.path.abspath(args.seed)
    if not os.path.exists(seed):
        print(f"{COLOR_RED}[-] Stage 0 seed not found: {seed}{NC}")
        sys.exit(1)

    print(f"{COLOR_CYAN}{'=' * 70}{NC}")
    print(f"{COLOR_CYAN}     NEXUS Official 3-Stage Bootstrap Verification Driver     {NC}")
    print(f"{COLOR_CYAN}{'=' * 70}{NC}")
    print(f"  Stage 0 seed : {seed}")
    print(f"  Source       : compiler/nexc.nex")
    print(f"  Bootstrap    : 3-stage deterministic fixed-point convergence")
    print()

    report = {
        "tool": "nexbootstrap",
        "started_at": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "seed": seed,
        "seed_sha256": sha256_file(seed),
        "seed_size": file_size(seed),
        "stages": [],
        "fixed_point": False,
        "promoted": False,
    }

    # Copy nexc.nex -> code.nex (the file the compiler reads)
    print(f"{COLOR_YELLOW}[1] Preparing source: nexc.nex -> code.nex{NC}")
    with open(NEXC_NEX, "rb") as f:
        nexc_src = f.read()
    with open(CODE_NEX, "wb") as f:
        f.write(nexc_src)
    src_sha = hashlib.sha256(nexc_src).hexdigest()
    print(f"    Source SHA-256: {src_sha[:16]}...")
    print(f"    Source size: {len(nexc_src)} bytes")
    report["source_sha256"] = src_sha
    report["source_size"] = len(nexc_src)
    print()

    # Stage 1: Seed (Stage 0) compiles nexc.nex -> Gen1
    print(f"{COLOR_YELLOW}[2] Stage 1: Seed -> Gen1{NC}")
    if not run_compile(seed, label="Stage 1"):
        report["error"] = "Stage 1 compilation failed"
        _write_report(report, fail=True)
        sys.exit(1)
    gen1_path = os.path.join(COMPILER_DIR, "nexc_gen1.exe")
    shutil.copy(APP_EXE, gen1_path)
    gen1_sha = sha256_file(gen1_path)
    print(f"    Gen1 size: {file_size(gen1_path)} bytes")
    print(f"    Gen1 SHA-256: {gen1_sha[:16]}...")
    report["stages"].append({
        "name": "Gen1",
        "path": gen1_path,
        "sha256": gen1_sha,
        "size": file_size(gen1_path),
    })
    print()

    # Stage 2: Gen1 compiles nexc.nex -> Gen2
    print(f"{COLOR_YELLOW}[3] Stage 2: Gen1 -> Gen2 (first self-compile){NC}")
    if not run_compile(gen1_path, label="Stage 2"):
        report["error"] = "Stage 2 compilation failed"
        _write_report(report, fail=True)
        sys.exit(1)
    gen2_path = os.path.join(COMPILER_DIR, "nexc_gen2.exe")
    shutil.copy(APP_EXE, gen2_path)
    gen2_sha = sha256_file(gen2_path)
    print(f"    Gen2 size: {file_size(gen2_path)} bytes")
    print(f"    Gen2 SHA-256: {gen2_sha[:16]}...")
    report["stages"].append({
        "name": "Gen2",
        "path": gen2_path,
        "sha256": gen2_sha,
        "size": file_size(gen2_path),
    })
    print()

    # Stage 3: Gen2 compiles nexc.nex -> Gen3 (fixed-point check)
    print(f"{COLOR_YELLOW}[4] Stage 3: Gen2 -> Gen3 (fixed-point verification){NC}")
    if not run_compile(gen2_path, label="Stage 3"):
        report["error"] = "Stage 3 compilation failed"
        _write_report(report, fail=True)
        sys.exit(1)
    gen3_path = os.path.join(COMPILER_DIR, "nexc_gen3.exe")
    shutil.copy(APP_EXE, gen3_path)
    gen3_sha = sha256_file(gen3_path)
    print(f"    Gen3 size: {file_size(gen3_path)} bytes")
    print(f"    Gen3 SHA-256: {gen3_sha[:16]}...")
    report["stages"].append({
        "name": "Gen3",
        "path": gen3_path,
        "sha256": gen3_sha,
        "size": file_size(gen3_path),
    })
    print()

    # Fixed-point check: Gen2 == Gen3 ?
    print(f"{COLOR_YELLOW}[5] Fixed-point check: Gen2 == Gen3 ?{NC}")
    fp_pass = files_identical(gen2_path, gen3_path)
    report["fixed_point"] = fp_pass
    if fp_pass:
        print(f"    {COLOR_GREEN}[PASS] Bit-for-bit identical: Gen2 == Gen3{NC}")
        print(f"    {COLOR_GREEN}    Compiler has reached deterministic fixed point!{NC}")
    else:
        diff_bytes = count_diff_bytes(gen2_path, gen3_path)
        print(f"    {COLOR_RED}[FAIL] Gen2 != Gen3 ({diff_bytes} differing bytes){NC}")
        print(f"    {COLOR_RED}    Compiler is non-deterministic or seed is stale.{NC}")
        _write_report(report, fail=True)
        sys.exit(1)
    print()

    # Bonus: Gen1 == Gen2 ? (true self-hosting from Stage 1)
    print(f"{COLOR_YELLOW}[6] Bonus: Gen1 == Gen2 (transitive convergence)?{NC}")
    g1g2_pass = files_identical(gen1_path, gen2_path)
    if g1g2_pass:
        print(f"    {COLOR_GREEN}[PASS] Gen1 == Gen2 (immediate convergence from seed){NC}")
    else:
        diff_bytes = count_diff_bytes(gen1_path, gen2_path)
        print(f"    {COLOR_YELLOW}[INFO] Gen1 != Gen2 ({diff_bytes} differing bytes){NC}")
        print(f"    {COLOR_YELLOW}     (This is OK - Gen1 may carry seed artifacts){NC}")
    report["gen1_equals_gen2"] = g1g2_pass
    print()

    # Promote Gen3 -> nexc.exe, regenerate nexc.elf
    if not args.no_promote:
        print(f"{COLOR_YELLOW}[7] Promoting Gen3 to active compiler (nexc.exe + nexc.elf){NC}")
        shutil.copy(gen3_path, NEXC_EXE)
        print(f"    {COLOR_GREEN}[+] nexc.exe <- Gen3 ({file_size(NEXC_EXE)} bytes){NC}")
        ok, err = gen_elf_from_pe(NEXC_EXE, NEXC_ELF)
        if ok:
            print(f"    {COLOR_GREEN}[+] nexc.elf regenerated ({file_size(NEXC_ELF)} bytes){NC}")
        else:
            print(f"    {COLOR_RED}[-] nexc.elf regeneration failed: {err}{NC}")
            report["error"] = f"ELF regeneration failed: {err}"
            _write_report(report, fail=True)
            sys.exit(1)
        report["promoted"] = True
        report["nexc_exe_sha256"] = sha256_file(NEXC_EXE)
        report["nexc_elf_sha256"] = sha256_file(NEXC_ELF)
    print()

    # Capture sizes before cleanup
    gen1_size = file_size(gen1_path)
    gen2_size = file_size(gen2_path)
    gen3_size = file_size(gen3_path)

    # Cleanup intermediates unless --keep-stages
    if not args.keep_stages:
        for p in [gen1_path, gen2_path, gen3_path, CODE_NEX, APP_EXE]:
            if os.path.exists(p):
                os.remove(p)

    report["completed_at"] = time.strftime("%Y-%m-%dT%H:%M:%S")
    report["success"] = True
    _write_report(report, fail=False)

    print(f"{COLOR_GREEN}{'=' * 70}{NC}")
    print(f"{COLOR_GREEN} [SUCCESS] 3-STAGE BOOTSTRAP FIXED-POINT VERIFIED!       {NC}")
    print(f"{COLOR_GREEN} 100% Deterministic Self-Hosting Convergence            {NC}")
    print(f"{COLOR_GREEN} Stage 1 (Seed)  -> Gen1 ({gen1_size} bytes)            {NC}")
    print(f"{COLOR_GREEN} Stage 2 (Self)  -> Gen2 ({gen2_size} bytes)            {NC}")
    print(f"{COLOR_GREEN} Stage 3 (FixPt) -> Gen3 ({gen3_size} bytes)            {NC}")
    print(f"{COLOR_GREEN} Gen2 == Gen3 : {fp_pass}                                  {NC}")
    print(f"{COLOR_GREEN} Report      : .bootstrap_report.json                       {NC}")
    print(f"{COLOR_GREEN}{'=' * 70}{NC}")


def _write_report(report, fail=False):
    """Write JSON report and exit if failure."""
    report_path = os.path.join(BASE_DIR, ".bootstrap_report.json")
    with open(report_path, "w") as f:
        json.dump(report, f, indent=2)
    if fail:
        print(f"\n{COLOR_RED}[FAIL] Bootstrap verification failed.{NC}")
        print(f"{COLOR_RED}       Report: {report_path}{NC}")


if __name__ == "__main__":
    main()
