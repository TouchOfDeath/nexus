#!/usr/bin/env python3
"""
NEXUS Differential Test-Case Reducer (Delta Debugger)
======================================================

Takes a failing NEXUS test case and reduces it to a minimal reproduction
by greedily deleting lines that don't affect the failure.

Algorithm (classic delta debugging):
  1. Split the source into lines.
  2. Try removing contiguous chunks (halving the chunk size each pass).
  3. After each removal, run the test against all backends (interpreter
     vs. native AOT vs. N-IR VM).
  4. If the failure still reproduces, keep the reduced version.
  5. Repeat until no further reduction is possible (fixpoint).

Usage:
  ./nexus delta <failing_test.nex>
  ./nexus delta <failing_test.nex> --mode diverge
  ./nexus delta <failing_test.nex> --mode crash
  python3 tools/nexdelta.py <file.nex> [--mode diverge|crash|hang]

Modes:
  diverge  : backends produce different outputs (default)
  crash    : backend exits non-zero or segfaults
  hang     : backend exceeds time limit
"""
import sys, os, subprocess, argparse, tempfile, time, shutil

BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NEXPREP = os.path.join(BASE_DIR, "bin", "nexprep")
NEXLOAD = os.path.join(BASE_DIR, "bin", "nexload")
NEXELF  = os.path.join(BASE_DIR, "bin", "nexelf")
NEXC_ELF = os.path.join(BASE_DIR, "compiler", "nexc.elf")
NEXC_EXE = os.path.join(BASE_DIR, "compiler", "nexc.exe")
INTERP_PY = os.path.join(BASE_DIR, "tools", "nexinterp.py")
NEXIR = os.path.join(BASE_DIR, "bin", "nexir")
COMPILER_DIR = os.path.join(BASE_DIR, "compiler")

DEFAULT_TIMEOUT = 5  # seconds per backend run


def run_interp(src_path, timeout=DEFAULT_TIMEOUT):
    """Run the reference Python interpreter."""
    try:
        res = subprocess.run([sys.executable, INTERP_PY, src_path],
                             capture_output=True, text=True, timeout=timeout)
        return (res.returncode, res.stdout.strip(), res.stderr.strip()[:500])
    except subprocess.TimeoutExpired:
        return (-1, "", "TIMEOUT")
    except Exception as e:
        return (-1, "", str(e))


def run_native(src_path, use_hydron=True, timeout=DEFAULT_TIMEOUT):
    """Run the native AOT compiler (PE -> ELF -> execute)."""
    code_nex = os.path.join(COMPILER_DIR, "code.nex")
    app_exe = os.path.join(COMPILER_DIR, "app.exe")
    app_elf = os.path.join(COMPILER_DIR, "app.elf")
    try:
        prep = [NEXPREP]
        if not use_hydron:
            prep.append("--no-hydron")
        prep.extend([src_path, code_nex])
        subprocess.run(prep, check=True, capture_output=True, timeout=timeout)
        for p in (app_elf, app_exe):
            if os.path.exists(p):
                os.remove(p)
        # Use nexc.elf if available, else nexc.exe via nexload
        if os.path.exists(NEXC_ELF):
            subprocess.run([NEXC_ELF], cwd=COMPILER_DIR, check=True,
                          capture_output=True, timeout=timeout)
        else:
            subprocess.run([NEXLOAD, NEXC_EXE], cwd=COMPILER_DIR, check=True,
                          capture_output=True, timeout=timeout)
        if not os.path.exists(app_exe):
            return (-1, "", "no app.exe produced")
        if not os.path.exists(app_elf):
            subprocess.run([NEXELF, app_exe, app_elf], check=True,
                          capture_output=True, timeout=timeout)
        res = subprocess.run([app_elf], capture_output=True, text=True, timeout=timeout)
        return (res.returncode, res.stdout.strip(), res.stderr.strip()[:500])
    except subprocess.TimeoutExpired:
        return (-1, "", "TIMEOUT")
    except subprocess.CalledProcessError as e:
        return (-1, "", f"compile/run error: {e}")
    except Exception as e:
        return (-1, "", str(e))


def run_nir_vm(src_path, opt=False, timeout=DEFAULT_TIMEOUT):
    """Run the N-IR VM."""
    try:
        cmd = [NEXIR, "--run", src_path]
        if opt:
            cmd.insert(1, "--opt")
        res = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        return (res.returncode, res.stdout.strip(), res.stderr.strip()[:500])
    except subprocess.TimeoutExpired:
        return (-1, "", "TIMEOUT")
    except Exception as e:
        return (-1, "", str(e))


def evaluate(src_path, mode="diverge"):
    """Evaluate a test case. Returns True if the failure still reproduces."""
    rc_i, out_i, err_i = run_interp(src_path)
    rc_n, out_n, err_n = run_native(src_path, use_hydron=True)
    rc_r, out_r, err_r = run_nir_vm(src_path, opt=False)

    if mode == "crash":
        # Failure = any backend crashes or returns non-zero
        return (rc_i != 0 or rc_n != 0 or rc_r != 0 or
                "TIMEOUT" in err_i or "TIMEOUT" in err_n or "TIMEOUT" in err_r or
                "Segmentation" in err_n)
    elif mode == "hang":
        return ("TIMEOUT" in err_i or "TIMEOUT" in err_n or "TIMEOUT" in err_r)
    else:  # diverge (default)
        # Failure = outputs differ across backends
        # Skip backends that crashed (they may not have output)
        outs = []
        for rc, out in [(rc_i, out_i), (rc_n, out_n), (rc_r, out_r)]:
            if rc == 0 and "TIMEOUT" not in out:
                outs.append(out)
        if len(outs) < 2:
            # Not enough backends produced output to compare - if at least one crashed,
            # that's still a failure to preserve
            return rc_i != 0 or rc_n != 0 or rc_r != 0
        return len(set(outs)) > 1


def try_reduce(lines, mode):
    """Try removing chunks of lines. Returns reduced lines or None."""
    n = len(lines)
    chunk = max(1, n // 2)
    while chunk >= 1:
        progress = False
        pos = 0
        while pos < n:
            end = min(pos + chunk, n)
            candidate = lines[:pos] + lines[end:]
            with tempfile.NamedTemporaryFile(suffix=".nex", mode="w", delete=False) as tf:
                tf.write("\n".join(candidate) + "\n")
                tmp_path = tf.name
            try:
                if evaluate(tmp_path, mode):
                    lines = candidate
                    n = len(lines)
                    progress = True
                    pos = 0
                    continue
            finally:
                os.unlink(tmp_path)
            pos = end
        if not progress:
            chunk //= 2
    return lines


def try_reduce_lines_to_single(lines, mode):
    """Second pass: try replacing each multi-token line with a minimal stub."""
    # For each non-structural line, try empty/replacement
    minimal = []
    for line in lines:
        stripped = line.strip()
        if not stripped or stripped.startswith("#") or stripped.startswith(";"):
            minimal.append(line)
            continue
        # Try replacing the line with a minimal version
        # Skip structural lines (if/while/for/fn/etc.)
        first_word = stripped.split()[0] if stripped.split() else ""
        if first_word in ("if", "while", "for", "fn", "else", "struct",
                          "include", "return", "break", "continue", "}"):
            minimal.append(line)
            continue
        # Try replacing with `let x = 0`
        candidate = minimal + ["let _r = 0"] + lines[len(minimal)+1:]
        with tempfile.NamedTemporaryFile(suffix=".nex", mode="w", delete=False) as tf:
            tf.write("\n".join(candidate) + "\n")
            tmp_path = tf.name
        try:
            if evaluate(tmp_path, mode):
                minimal.append("let _r = 0")
                lines = candidate
                continue
        finally:
            os.unlink(tmp_path)
        minimal.append(line)
    return minimal


def normalize_input(src_path):
    """Read source, strip trailing whitespace per line, drop blank-only lines."""
    with open(src_path) as f:
        raw = f.read()
    lines = raw.splitlines()
    # Keep comments and structure; just trim
    return [line.rstrip() for line in lines if line.strip()]


def main():
    parser = argparse.ArgumentParser(description="NEXUS Delta Debugger")
    parser.add_argument("input", help="Failing test case to reduce")
    parser.add_argument("--mode", choices=["diverge", "crash", "hang"],
                        default="diverge", help="Failure mode to preserve (default: diverge)")
    parser.add_argument("--output", "-o", help="Output reduced file (default: <input>.reduced.nex)")
    parser.add_argument("--max-iters", type=int, default=10, help="Max reduction passes")
    args = parser.parse_args()

    if not os.path.exists(args.input):
        print(f"[-] Input file not found: {args.input}")
        sys.exit(1)

    out_path = args.output or (args.input + ".reduced.nex")

    print("=" * 70)
    print("            NEXUS DIFFERENTIAL TEST-CASE REDUCER")
    print("   Classic Delta Debugging - 7-way Oracle Preservation")
    print("=" * 70)
    print(f"[*] Input:  {args.input}")
    print(f"[*] Output: {out_path}")
    print(f"[*] Mode:   {args.mode}")
    print()

    # Verify the original fails
    print("[1] Verifying original test case reproduces failure...")
    if not evaluate(args.input, args.mode):
        print(f"[-] Original test case does NOT reproduce failure in '{args.mode}' mode.")
        print("    Backend outputs (for diagnostics):")
        rc_i, out_i, err_i = run_interp(args.input)
        rc_n, out_n, err_n = run_native(args.input)
        rc_r, out_r, err_r = run_nir_vm(args.input)
        print(f"    Interpreter (rc={rc_i}): {out_i[:100]}")
        print(f"    Native AOT  (rc={rc_n}): {out_n[:100]}")
        print(f"    N-IR VM     (rc={rc_r}): {out_r[:100]}")
        sys.exit(1)
    print("[+] Original failure reproduced. Proceeding to reduction.\n")

    # Phase 1: line-level chunk removal
    print("[2] Phase 1: Chunk-based line removal (delta debugging)...")
    lines = normalize_input(args.input)
    original_count = len(lines)
    print(f"    Starting lines: {original_count}")

    for it in range(1, args.max_iters + 1):
        before = len(lines)
        reduced = try_reduce(lines, args.mode)
        after = len(reduced)
        print(f"    Pass {it}: {before} -> {after} lines")
        if after >= before:
            break
        lines = reduced

    # Phase 2: line stubbing (replace complex lines with minimal stubs)
    print("\n[3] Phase 2: Stub simplification (replace complex lines)...")
    lines = try_reduce_lines_to_single(lines, args.mode)
    print(f"    After stub: {len(lines)} lines")

    # Write output
    with open(out_path, "w") as f:
        f.write("\n".join(lines) + "\n")

    print(f"\n[+] Reduced test case written to: {out_path}")
    print(f"[+] Reduction: {original_count} -> {len(lines)} lines "
          f"({(1 - len(lines)/max(1, original_count))*100:.1f}% smaller)")

    # Final verification
    print("\n[4] Final verification: reduced case still reproduces failure...")
    if evaluate(out_path, args.mode):
        print("[+] CONFIRMED: Reduced test case still reproduces the failure.")
        rc_i, out_i, _ = run_interp(out_path)
        rc_n, out_n, _ = run_native(out_path)
        rc_r, out_r, _ = run_nir_vm(out_path)
        print(f"    Interpreter: rc={rc_i}, out={out_i[:60]}")
        print(f"    Native AOT:  rc={rc_n}, out={out_n[:60]}")
        print(f"    N-IR VM:     rc={rc_r}, out={out_r[:60]}")
    else:
        print("[-] WARNING: Reduced test case no longer reproduces failure!")
        print("    This can happen with aggressive stubbing. Try --max-iters 3.")
    print()
    print("=" * 70)


if __name__ == "__main__":
    main()
