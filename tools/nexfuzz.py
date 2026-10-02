#!/usr/bin/env python3
"""
NEXUS Differential Fuzzing Engine (nexfuzz.py)
Generates random valid NEXUS programs and verifies differential equivalence:
    Reference Interpreter == Native AOT (-O0) == Native AOT (HYDRON)

Usage:
    ./nexus fuzz [--iterations N] [--seed S]
    python3 tools/nexfuzz.py --iterations 100
"""

import sys, os, random, subprocess, argparse, tempfile

BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NEXPREP = os.path.join(BASE_DIR, "bin", "nexprep")
NEXLOAD = os.path.join(BASE_DIR, "bin", "nexload")
NEXELF = os.path.join(BASE_DIR, "bin", "nexelf")
COMPILER_DIR = os.path.join(BASE_DIR, "compiler")
NEXC_ELF = os.path.join(COMPILER_DIR, "nexc.elf")
INTERP_PY = os.path.join(BASE_DIR, "tools", "nexinterp.py")
NEXIR = os.path.join(BASE_DIR, "bin", "nexir")

class ProgramGenerator:
    def __init__(self, seed=None):
        self.rng = random.Random(seed)
        self.var_names = ["a", "b", "c", "d", "acc", "sum", "val"]

    def gen_arith_expr(self, depth=0):
        if depth > 2 or self.rng.random() < 0.4:
            if self.rng.random() < 0.6:
                return str(self.rng.randint(1, 100))
            return self.rng.choice(self.var_names)
        op = self.rng.choice(["+", "-", "*", "/"])
        left = self.gen_arith_expr(depth + 1)
        right = self.rng.randint(1, 20) if op in ("/", "*") else self.gen_arith_expr(depth + 1)
        return f"{left} {op} {right}"

    def generate_program(self):
        lines = []
        lines.append("; Auto-generated NEXUS differential fuzzing test")
        
        # Initialize variables
        for v in self.var_names:
            lines.append(f"let {v} = {self.rng.randint(0, 50)}")

        # Add counting loop
        limit = self.rng.randint(10, 200)
        lines.append(f"let i = 0")
        lines.append(f"while i < {limit} {{")
        # In-loop arithmetic
        target = self.rng.choice(self.var_names)
        op = self.rng.choice(["+", "-", "*"])
        step = self.rng.randint(1, 5)
        lines.append(f"    let {target} = {target} {op} {step}")
        if self.rng.random() < 0.5:
            # Add an inner condition
            thr = self.rng.randint(5, limit // 2)
            lines.append(f"    if i > {thr} {{")
            v2 = self.rng.choice(self.var_names)
            lines.append(f"        let {v2} = {v2} + 2")
            lines.append(f"    }}")
        lines.append(f"    let i = i + 1")
        lines.append(f"}}")

        # Memory store and load test
        lines.append(f"let mem = alloc 64")
        lines.append(f"store64 [mem + 0] 12345")
        lines.append(f"store64 [mem + 8] 67890")
        lines.append(f"let m0 = load64 [mem + 0]")
        lines.append(f"let m1 = load64 [mem + 8]")

        # Print outputs
        for v in self.var_names:
            lines.append(f"print {v}")
        lines.append(f"print m0")
        lines.append(f"print m1")

        return "\n".join(lines)

def run_interpreter(src_path):
    cmd = [sys.executable, INTERP_PY, src_path]
    res = subprocess.run(cmd, capture_output=True, text=True, timeout=10)
    if res.returncode != 0:
        raise RuntimeError(f"Interpreter failed: {res.stderr}")
    return res.stdout.strip()

def run_native(src_path, use_hydron=True):
    code_nex = os.path.join(COMPILER_DIR, "code.nex")
    app_elf = os.path.join(COMPILER_DIR, "app.elf")
    app_exe = os.path.join(COMPILER_DIR, "app.exe")

    prep_cmd = [NEXPREP]
    if not use_hydron:
        prep_cmd.append("--no-hydron")
    prep_cmd.extend([src_path, code_nex])

    subprocess.run(prep_cmd, check=True, capture_output=True)

    for p in (app_elf, app_exe):
        if os.path.exists(p): os.remove(p)

    subprocess.run([NEXC_ELF], cwd=COMPILER_DIR, check=True, capture_output=True)

    if not os.path.exists(app_elf):
        if os.path.exists(app_exe):
            subprocess.run([NEXELF, app_exe, app_elf], check=True, capture_output=True)

    if not os.path.exists(app_elf):
        raise RuntimeError("Native compilation did not produce app.elf")

    res = subprocess.run([app_elf], capture_output=True, text=True, timeout=10)
    if res.returncode != 0:
        raise RuntimeError(f"Native binary exited with {res.returncode}")
    return res.stdout.strip()

def run_nir(src_path, opt=False):
    cmd = [NEXIR, "--run", src_path]
    if opt:
        cmd.insert(1, "--opt")
    res = subprocess.run(cmd, capture_output=True, text=True, timeout=10)
    if res.returncode != 0:
        raise RuntimeError(f"N-IR VM failed: {res.stderr}")
    return res.stdout.strip()

def run_nir_elf(src_path, opt=False):
    with tempfile.NamedTemporaryFile(suffix=".elf", delete=False) as ef:
        elf_path = ef.name
    try:
        cmd = [NEXIR, "--emit-elf", src_path, elf_path]
        if opt:
            cmd.insert(1, "--opt")
        res_compile = subprocess.run(cmd, capture_output=True, text=True, timeout=10)
        if res_compile.returncode != 0:
            raise RuntimeError(f"N-IR ELF codegen failed: {res_compile.stderr}")
        res_run = subprocess.run([elf_path], capture_output=True, text=True, timeout=10)
        if res_run.returncode != 0:
            raise RuntimeError(f"N-IR ELF binary exited with {res_run.returncode}")
        return res_run.stdout.strip()
    finally:
        if os.path.exists(elf_path):
            os.remove(elf_path)

def main():
    parser = argparse.ArgumentParser(description="NEXUS Differential Fuzzer")
    parser.add_argument("--iterations", "-n", type=int, default=25, help="Number of random programs to test")
    parser.add_argument("--seed", "-s", type=int, default=None, help="Random seed for reproducibility")
    args = parser.parse_args()

    seed = args.seed if args.seed is not None else random.randint(1, 1000000)
    gen = ProgramGenerator(seed)

    print(f"==================================================================")
    print(f"             NEXUS DIFFERENTIAL FUZZING ENGINE                    ")
    print(f"   7-Way Oracle: Interpreter == nexc (-O0) == nexc (HYDRON)       ")
    print(f"                 == N-IR VM (-O0) == N-IR VM (--opt)              ")
    print(f"                 == N-IR ELF (-O0) == N-IR ELF (--opt)            ")
    print(f"==================================================================")
    print(f"[*] Starting {args.iterations} iterations (Master Seed: {seed})...\n")

    passed = 0
    with tempfile.NamedTemporaryFile(suffix=".nex", delete=False) as f:
        test_file = f.name

    try:
        for it in range(1, args.iterations + 1):
            code = gen.generate_program()
            with open(test_file, "w") as tf:
                tf.write(code)

            try:
                out_interp = run_interpreter(test_file)
                out_unopt = run_native(test_file, use_hydron=False)
                out_opt = run_native(test_file, use_hydron=True)
                out_nir_raw = run_nir(test_file, opt=False)
                out_nir_opt = run_nir(test_file, opt=True)
                out_elf_raw = run_nir_elf(test_file, opt=False)
                out_elf_opt = run_nir_elf(test_file, opt=True)

                if (out_interp == out_unopt == out_opt ==
                    out_nir_raw == out_nir_opt ==
                    out_elf_raw == out_elf_opt):
                    passed += 1
                    sys.stdout.write(f"\r[+] Progress: {passed}/{args.iterations} programs verified (7-way 100% semantic identity)")
                    sys.stdout.flush()
                else:
                    print(f"\n[FAIL] Semantic divergence detected on iteration {it}!")
                    print(f"--- Interpreter Output ---\n{out_interp}")
                    print(f"--- Native -O0 Output ---\n{out_unopt}")
                    print(f"--- Native HYDRON Output ---\n{out_opt}")
                    print(f"--- N-IR VM (-O0) Output ---\n{out_nir_raw}")
                    print(f"--- N-IR VM (--opt) Output ---\n{out_nir_opt}")
                    print(f"--- N-IR ELF (-O0) Output ---\n{out_elf_raw}")
                    print(f"--- N-IR ELF (--opt) Output ---\n{out_elf_opt}")
                    print(f"\nFailed program saved to: {test_file}")
                    sys.exit(1)

            except Exception as e:
                print(f"\n[ERROR] Test iteration {it} crashed: {e}")
                print(f"Problematic source code:\n{code}")
                sys.exit(1)

        print(f"\n\n\033[0;32m[SUCCESS] Differential Fuzzing PASSED: {passed}/{args.iterations} programs verified.\033[0m")
        print(f"\033[0;32m[VERIFIED] Exact 7-way semantic equivalence across all interpreters and AOT engines.\033[0m")
        print(f"==================================================================")

    finally:
        if os.path.exists(test_file):
            os.remove(test_file)

if __name__ == "__main__":
    main()
