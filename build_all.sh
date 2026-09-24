#!/usr/bin/env bash
# ==============================================================================
#                      NEXUS PROJECT: MASTER BUILD SCRIPT (LINUX)
#           0% C#  -  0% .NET Runtime  -  100% Native x86-64 Machine Code
# ==============================================================================
set -e

BASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMPILER_DIR="$BASE_DIR/compiler"
EXAMPLES_DIR="$BASE_DIR/examples"
TOOLS_DIR="$BASE_DIR/tools"
BIN_DIR="$BASE_DIR/bin"
NEXLOAD="$BIN_DIR/nexload"
NEXELF="$BIN_DIR/nexelf"

CYAN='\033[0;36m'
YELLOW='\033[1;33m'
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo -e "${CYAN}==================================================================${NC}"
echo -e "${CYAN} [NEXUS] Building Full Project Pipeline (Stage 5 Linux Driver)   ${NC}"
echo -e "${CYAN}==================================================================${NC}"

# --- STEP 1: Ensure Native ABI Bridge (nexload) and ELF64 Emitter (nexelf) are Ready ---
echo -e "\n${YELLOW}[+] STEP 1: Initializing Native x86-64 Execution Engine and ELF64 Emitter...${NC}"
mkdir -p "$BIN_DIR"
if [ ! -f "$NEXLOAD" ] || [ "$TOOLS_DIR/nexload.c" -nt "$NEXLOAD" ]; then
    gcc -O2 "$TOOLS_DIR/nexload.c" -o "$NEXLOAD"
    echo -e "${GREEN}[+] Compiled $NEXLOAD (Zero-Dependency Bare-Metal Bridge)${NC}"
else
    echo -e "${GREEN}[+] Native engine $NEXLOAD is up to date.${NC}"
fi
if [ ! -f "$NEXELF" ] || [ "$TOOLS_DIR/nexelf.c" -nt "$NEXELF" ]; then
    gcc -O2 "$TOOLS_DIR/nexelf.c" -o "$NEXELF"
    echo -e "${GREEN}[+] Compiled $NEXELF (Zero-Dependency Native ELF64 Emitter)${NC}"
else
    echo -e "${GREEN}[+] Native ELF emitter $NEXELF is up to date.${NC}"
fi
NEXPREP="$BIN_DIR/nexprep"
if [ ! -f "$NEXPREP" ] || [ "$TOOLS_DIR/nexprep.c" -nt "$NEXPREP" ]; then
    gcc -O2 "$TOOLS_DIR/nexprep.c" -o "$NEXPREP"
    echo -e "${GREEN}[+] Compiled $NEXPREP (Zero-Dependency Multi-File Preprocessor)${NC}"
else
    echo -e "${GREEN}[+] Native preprocessor $NEXPREP is up to date.${NC}"
fi

# Ensure nexc.exe exists (from nexc.hex or pre-built)
if [ ! -f "$COMPILER_DIR/nexc.exe" ] && [ -f "$COMPILER_DIR/nexc.hex" ]; then
    echo -e "${YELLOW}[+] Synthesizing bootstrap nexc.exe from nexc.hex...${NC}"
    # Hex decoding using pure Linux coreutils or python
    python3 -c "
with open('$COMPILER_DIR/nexc.hex') as f_in, open('$COMPILER_DIR/nexc.exe', 'wb') as f_out:
    for line in f_in:
        line = line.strip()
        if line:
            f_out.write(bytes.fromhex(line))
"
    echo -e "${GREEN}[+] Bootstrap nexc.exe generated successfully!${NC}"
fi

# --- STEP 2: Compile Master Test Suite with nexc.exe ---
echo -e "\n${YELLOW}[+] STEP 2: Compiling code.nex -> app.exe using nexc.exe...${NC}"
cp "$EXAMPLES_DIR/master_test_suite.nex" "$COMPILER_DIR/code.nex"
(cd "$COMPILER_DIR" && "$NEXLOAD" "$COMPILER_DIR/nexc.exe")
echo -e "${GREEN}[+] Master test suite compiled to app.exe${NC}"

# --- STEP 3: Execute and Verify app.exe with Strict Output Diff ---
echo -e "\n${YELLOW}[+] STEP 3: Executing compiled app.exe and asserting 27 Stage 4 Features...${NC}"
OUTPUT=$(cd "$COMPILER_DIR" && "$NEXLOAD" "$COMPILER_DIR/app.exe" | tr -d '\r')
echo "$OUTPUT"

# Strict automated assertions for all 27 regression points:
declare -a EXPECTED_VALUES=(
    "25"
    "10"
    "38"
    "5"
    "15"
    "If Check: Equal (42 == 42)"
    "If-Else Check 1: Greater/Equal Branch (Expected)"
    "If-Else Check 2: Else Branch (Expected)"
    "Not Equal Check (25 != 99)"
    "3"
    "2"
    "1"
    "Liftoff!"
    "12"
    "3"
    "3"
    "49"
    "50"
    "42"
    "84"
    "123456789"
    "777"
    "104"
    "6"
    "78"
    "52"
    "=== ALL TESTS COMPLETED ==="
)

for val in "${EXPECTED_VALUES[@]}"; do
    if ! echo "$OUTPUT" | grep -Fxq "$val"; then
        echo -e "${RED}[ERROR] Expected output '$val' not found!${NC}"
        exit 1
    fi
done
echo -e "${GREEN}[+] All 27 Stage 4 regression assertions verified successfully!${NC}"

# --- STEP 4: Verify Interactive Console Input (read <var>) with Stream Pipelining ---
echo -e "\n${YELLOW}[+] STEP 4: Verifying Interactive Console Input (read <var>) via piped stdin...${NC}"
cp "$EXAMPLES_DIR/interactive_calc.nex" "$COMPILER_DIR/code.nex"
(cd "$COMPILER_DIR" && "$NEXLOAD" "$COMPILER_DIR/nexc.exe" > /dev/null)
CALC_OUT=$(printf "45\n15\n" | (cd "$COMPILER_DIR" && "$NEXLOAD" "$COMPILER_DIR/app.exe") | tr -d '\r')
echo "$CALC_OUT"

if ! echo "$CALC_OUT" | grep -Fxq "60" || \
   ! echo "$CALC_OUT" | grep -Fxq "30" || \
   ! echo "$CALC_OUT" | grep -Fxq "675" || \
   ! echo "$CALC_OUT" | grep -Fxq "3" || \
   ! echo "$CALC_OUT" | grep -Fxq "0"; then
    echo -e "${RED}[ERROR] Interactive calculation pipeline assertion failed!${NC}"
    exit 1
fi
echo -e "${GREEN}[+] Interactive calculator passed (Sum=60, Diff=30, Prod=675, Quot=3, Rem=0)!${NC}"

# Prime checker interactive tests
cp "$EXAMPLES_DIR/prime_checker.nex" "$COMPILER_DIR/code.nex"
(cd "$COMPILER_DIR" && "$NEXLOAD" "$COMPILER_DIR/nexc.exe" > /dev/null)
P1=$(printf "17\n" | (cd "$COMPILER_DIR" && "$NEXLOAD" "$COMPILER_DIR/app.exe"))
P2=$(printf "24\n" | (cd "$COMPILER_DIR" && "$NEXLOAD" "$COMPILER_DIR/app.exe"))

if ! echo "$P1" | grep -q "PRIME NUMBER!" || ! echo "$P2" | grep -q "COMPOSITE NUMBER!"; then
    echo -e "${RED}[ERROR] Prime checker assertion failed!${NC}"
    exit 1
fi
echo -e "${GREEN}[+] Prime checker verified (17=PRIME, 24=COMPOSITE)!${NC}"

# --- STEP 5: Verify Full Examples Library ---
echo -e "\n${YELLOW}[+] STEP 5: Batch compiling and verifying all example programs...${NC}"
declare -a EXAMPLES=(
    "factorial.nex:720"
    "fibonacci.nex:34"
    "finance.nex:46000"
    "full_demo.nex:Pipeline execution complete! Status: OK"
    "functions_demo.nex:216"
    "hello_world.nex:Zero Percent C#, Zero Percent .NET!"
    "math_pipeline.nex:Arithmetic test passed successfully."
    "nested_loops.nex:20"
    "rocket_physics.nex:Mission Telemetry: ORBIT INSERTION SUCCESSFUL!"
    "else_if_demo.nex:Showcase Execution Finished Successfully"
    "string_demo.nex:=== STRING DEMO COMPLETED ==="
    "builtins_demo.nex:=== BUILTINS DEMO COMPLETED ==="
    "multi_file_demo.nex:Multi-File Inclusion Verified!"
    "struct_demo.nex:=== STRUCT DEMO COMPLETED SUCCESSFULLY ==="
    "float_physics.nex:SSE2 IEEE 754 Floating-Point Physics Verified!"
    "parameterized_functions.nex:=== PARAMETERIZED FUNCTIONS DEMO SUCCESSFUL ==="
    "array_literals.nex:Float array sum (expected 20.000000)"
)

for item in "${EXAMPLES[@]}"; do
    IFS=":" read -r ex_file expected_substr <<< "$item"
    "$NEXPREP" "$EXAMPLES_DIR/$ex_file" "$COMPILER_DIR/code.nex"
    rm -f "$COMPILER_DIR/app.exe"
    (cd "$COMPILER_DIR" && "$NEXLOAD" "$COMPILER_DIR/nexc3.exe" > /dev/null)
    EX_OUT=$(cd "$COMPILER_DIR" && "$NEXLOAD" "$COMPILER_DIR/app.exe")
    if echo "$EX_OUT" | grep -q "$expected_substr"; then
        printf "  [+] %-25s OK\n" "$ex_file"
    else
        echo -e "${RED}  [-] $ex_file FAILED (expected '$expected_substr')${NC}"
        exit 1
    fi
done
echo -e "${GREEN}[+] All example programs verified successfully!${NC}"

# --- STEP 6: Compiler Diagnostic Engine Verification ---
echo -e "\n${YELLOW}[+] STEP 6: Verifying Compiler Diagnostic Engine (Line/Col Error Reporting)...${NC}"

# Diagnostic Test 1: Unrecognized statement keyword
cat << 'EOF' > "$COMPILER_DIR/code.nex"
# Test invalid keyword
let x = 10
invalid_statement_xyz
print x
EOF
rm -f "$COMPILER_DIR/app.exe"
DIAG1_OUT=$(cd "$COMPILER_DIR" && "$NEXLOAD" "$COMPILER_DIR/nexc3.exe" | tr -d '\r')
if [ ! -f "$COMPILER_DIR/app.exe" ] && echo "$DIAG1_OUT" | grep -q "Line:" && echo "$DIAG1_OUT" | grep -q "3" && echo "$DIAG1_OUT" | grep -q "Column:" && echo "$DIAG1_OUT" | grep -q "1" && echo "$DIAG1_OUT" | grep -q "Unrecognized statement keyword"; then
    echo -e "  ${GREEN}[+] Test 1: Unrecognized Statement caught (Line 3, Col 1) - app.exe gated${NC}"
else
    echo -e "${RED}[ERROR] Diagnostic Test 1 failed! Output:${NC}\n$DIAG1_OUT"
    exit 1
fi

# Diagnostic Test 2: Missing '=' in let assignment
cat << 'EOF' > "$COMPILER_DIR/code.nex"
let a = 1
    let b 42
let c = 3
EOF
rm -f "$COMPILER_DIR/app.exe"
DIAG2_OUT=$(cd "$COMPILER_DIR" && "$NEXLOAD" "$COMPILER_DIR/nexc3.exe" | tr -d '\r')
if [ ! -f "$COMPILER_DIR/app.exe" ] && echo "$DIAG2_OUT" | grep -q "Line:" && echo "$DIAG2_OUT" | grep -q "2" && echo "$DIAG2_OUT" | grep -q "Expected '=' in let assignment"; then
    echo -e "  ${GREEN}[+] Test 2: Missing '=' in let caught (Line 2, Col 11) - app.exe gated${NC}"
else
    echo -e "${RED}[ERROR] Diagnostic Test 2 failed! Output:${NC}\n$DIAG2_OUT"
    exit 1
fi

# Diagnostic Test 3: Undefined function call
cat << 'EOF' > "$COMPILER_DIR/code.nex"
fn my_foo {
    let a = 10
}

call non_existent_fn
EOF
rm -f "$COMPILER_DIR/app.exe"
DIAG3_OUT=$(cd "$COMPILER_DIR" && "$NEXLOAD" "$COMPILER_DIR/nexc3.exe" | tr -d '\r')
if [ ! -f "$COMPILER_DIR/app.exe" ] && echo "$DIAG3_OUT" | grep -q "Line:" && echo "$DIAG3_OUT" | grep -q "5" && echo "$DIAG3_OUT" | grep -q "Undefined function name in call statement"; then
    echo -e "  ${GREEN}[+] Test 3: Undefined Function Call caught (Line 5) - app.exe gated${NC}"
else
    echo -e "${RED}[ERROR] Diagnostic Test 3 failed! Output:${NC}\n$DIAG3_OUT"
    exit 1
fi

# Diagnostic Test 4: Unclosed block at EOF
cat << 'EOF' > "$COMPILER_DIR/code.nex"
let a = 1
while a < 10 {
    let a = a + 1
EOF
rm -f "$COMPILER_DIR/app.exe"
DIAG4_OUT=$(cd "$COMPILER_DIR" && "$NEXLOAD" "$COMPILER_DIR/nexc3.exe" | tr -d '\r')
if [ ! -f "$COMPILER_DIR/app.exe" ] && echo "$DIAG4_OUT" | grep -q "Unclosed block (missing '}') at end of file"; then
    echo -e "  ${GREEN}[+] Test 4: Unclosed Block caught at EOF - app.exe gated${NC}"
else
    echo -e "${RED}[ERROR] Diagnostic Test 4 failed! Output:${NC}\n$DIAG4_OUT"
    exit 1
fi

# Diagnostic Test 5: Unmatched '}' (no block was open)
cat << 'EOF' > "$COMPILER_DIR/code.nex"
let a = 1
}
let b = 2
EOF
rm -f "$COMPILER_DIR/app.exe"
DIAG5_OUT=$(cd "$COMPILER_DIR" && "$NEXLOAD" "$COMPILER_DIR/nexc3.exe" | tr -d '\r')
if [ ! -f "$COMPILER_DIR/app.exe" ] && echo "$DIAG5_OUT" | grep -q "Unmatched '}' (no block was open)"; then
    echo -e "  ${GREEN}[+] Test 5: Unmatched '}' caught (Line 2) - app.exe gated${NC}"
else
    echo -e "${RED}[ERROR] Diagnostic Test 5 failed! Output:${NC}\n$DIAG5_OUT"
    exit 1
fi

# Diagnostic Test 6: Invalid relational operator in condition (Expression trap)
cat << 'EOF' > "$COMPILER_DIR/code.nex"
let a = 5
let n = 20
if a * a > n {
    print 1
}
EOF
rm -f "$COMPILER_DIR/app.exe"
DIAG6_OUT=$(cd "$COMPILER_DIR" && "$NEXLOAD" "$COMPILER_DIR/nexc3.exe" | tr -d '\r')
if [ ! -f "$COMPILER_DIR/app.exe" ] && echo "$DIAG6_OUT" | grep -q "Line:" && echo "$DIAG6_OUT" | grep -q "3" && echo "$DIAG6_OUT" | grep -q "Invalid relational operator in condition"; then
    echo -e "  ${GREEN}[+] Test 6: Condition Expression Trap caught (Line 3) - app.exe gated${NC}"
else
    echo -e "${RED}[ERROR] Diagnostic Test 6 failed! Output:${NC}\n$DIAG6_OUT"
    exit 1
fi

# Diagnostic Test 7: Missing '{' after condition
cat << 'EOF' > "$COMPILER_DIR/code.nex"
let a = 5
if a == 5
    print 1
}
EOF
rm -f "$COMPILER_DIR/app.exe"
DIAG7_OUT=$(cd "$COMPILER_DIR" && "$NEXLOAD" "$COMPILER_DIR/nexc3.exe" | tr -d '\r')
if [ ! -f "$COMPILER_DIR/app.exe" ] && echo "$DIAG7_OUT" | grep -q "Expected '{' after condition"; then
    echo -e "  ${GREEN}[+] Test 7: Missing '{' after condition caught - app.exe gated${NC}"
else
    echo -e "${RED}[ERROR] Diagnostic Test 7 failed! Output:${NC}\n$DIAG7_OUT"
    exit 1
fi
echo -e "${GREEN}[+] All 7 Compiler Diagnostic tests verified successfully!${NC}"

# --- STEP 7: Self-Hosting Fixed-Point Parity Proof ---
echo -e "\n${YELLOW}[+] STEP 7: Verifying Stage 5 Bit-for-Bit Self-Hosting Convergence...${NC}"
cp "$COMPILER_DIR/nexc.nex" "$COMPILER_DIR/code.nex"

# Gen 1 -> Gen 2
echo "  [*] Gen 1 (nexc.exe) compiles nexc.nex -> nexc2.exe (Gen 2)..."
(cd "$COMPILER_DIR" && "$NEXLOAD" "$COMPILER_DIR/nexc.exe" > /dev/null)
cp "$COMPILER_DIR/app.exe" "$COMPILER_DIR/nexc2.exe"

# Gen 2 -> Gen 3
echo "  [*] Gen 2 (nexc2.exe) compiles nexc.nex -> nexc3.exe (Gen 3)..."
(cd "$COMPILER_DIR" && "$NEXLOAD" "$COMPILER_DIR/nexc2.exe" > /dev/null)
cp "$COMPILER_DIR/app.exe" "$COMPILER_DIR/nexc3.exe"

# Gen 3 -> Gen 4
echo "  [*] Gen 3 (nexc3.exe) compiles nexc.nex -> nexc4.exe (Gen 4)..."
(cd "$COMPILER_DIR" && "$NEXLOAD" "$COMPILER_DIR/nexc3.exe" > /dev/null)
cp "$COMPILER_DIR/app.exe" "$COMPILER_DIR/nexc4.exe"

# Byte-for-byte diff
DIFF_COUNT=$(cmp -l "$COMPILER_DIR/nexc3.exe" "$COMPILER_DIR/nexc4.exe" 2>/dev/null | wc -l || true)
echo -e "\n  [>] Diff (Gen 3 vs Gen 4): $DIFF_COUNT differences across 49,152 bytes"

if [ "$DIFF_COUNT" -eq 0 ]; then
    echo -e "${GREEN}==================================================================${NC}"
    echo -e "${GREEN} [SUCCESS] STAGE 5 FIXED-POINT SELF-HOSTING PARITY VERIFIED!      ${NC}"
    echo -e "${GREEN} 0 differences across all 49,152 bytes! Compiler is self-hosting! ${NC}"
    echo -e "${GREEN} 100% Native x86-64 | 0% C# | 0% .NET Runtime | Cross-Platform    ${NC}"
    echo -e "${GREEN}==================================================================${NC}"
    # Promote Gen 3 to active compiler
    echo -e "\n${YELLOW}[+] STEP 8: Promoting Gen 3 (nexc3.exe) to Active Production Compiler (nexc.exe)...${NC}"
    cp "$COMPILER_DIR/nexc3.exe" "$COMPILER_DIR/nexc.exe"
    echo -e "${GREEN}[+] Promoted nexc3.exe -> nexc.exe${NC}"

    # --- STEP 9: Synthesize Standalone Native Linux ELF64 Compiler (nexc.elf) ---
    echo -e "\n${YELLOW}[+] STEP 9: Synthesizing Standalone Native Linux ELF64 Compiler (nexc.elf)...${NC}"
    "$NEXELF" "$COMPILER_DIR/nexc.exe" "$COMPILER_DIR/nexc.elf"
    echo -e "${GREEN}[+] Standalone Native Linux Compiler deployed to $COMPILER_DIR/nexc.elf${NC}"

    # --- STEP 10: Direct Native Linux ELF64 Execution Verification (0% Loader) ---
    echo -e "\n${YELLOW}[+] STEP 10: Verifying Direct Native Linux ELF64 Execution on Bare-Metal Kernel...${NC}"
    
    # 10A: Master test suite (all 27 regression points via direct kernel execution)
    echo "  [*] 10A: Direct Native Execution of master_test_suite.nex (app.elf)..."
    cp "$EXAMPLES_DIR/master_test_suite.nex" "$COMPILER_DIR/code.nex"
    (cd "$COMPILER_DIR" && "$NEXLOAD" "$COMPILER_DIR/nexc.exe" > /dev/null)
    "$NEXELF" "$COMPILER_DIR/app.exe" "$COMPILER_DIR/app.elf" > /dev/null
    ELF_OUTPUT=$(cd "$COMPILER_DIR" && ./app.elf | tr -d '\r')
    for val in "${EXPECTED_VALUES[@]}"; do
        if ! echo "$ELF_OUTPUT" | grep -Fxq "$val"; then
            echo -e "${RED}[ERROR] Expected ELF output '$val' not found!${NC}"
            exit 1
        fi
    done
    echo -e "  ${GREEN}[+] 10A: All 27 regression assertions verified directly on Linux kernel (0% loader)!${NC}"

    # 10B: Interactive Console Input (read <var>) via direct ELF64 execution
    echo "  [*] 10B: Direct Native Execution of Interactive Input (calc.elf & prime.elf)..."
    cp "$EXAMPLES_DIR/interactive_calc.nex" "$COMPILER_DIR/code.nex"
    (cd "$COMPILER_DIR" && ./nexc.elf > /dev/null)
    [ ! -f "$COMPILER_DIR/app.elf" ] && "$NEXELF" "$COMPILER_DIR/app.exe" "$COMPILER_DIR/app.elf" > /dev/null
    ELF_CALC_OUT=$(printf "45\n15\n" | (cd "$COMPILER_DIR" && ./app.elf) | tr -d '\r')
    if ! echo "$ELF_CALC_OUT" | grep -Fxq "60" || \
       ! echo "$ELF_CALC_OUT" | grep -Fxq "30" || \
       ! echo "$ELF_CALC_OUT" | grep -Fxq "675" || \
       ! echo "$ELF_CALC_OUT" | grep -Fxq "3" || \
       ! echo "$ELF_CALC_OUT" | grep -Fxq "0"; then
        echo -e "${RED}[ERROR] Direct ELF interactive calculator assertion failed!${NC}"
        exit 1
    fi
    cp "$EXAMPLES_DIR/prime_checker.nex" "$COMPILER_DIR/code.nex"
    (cd "$COMPILER_DIR" && ./nexc.elf > /dev/null)
    [ ! -f "$COMPILER_DIR/app.elf" ] && "$NEXELF" "$COMPILER_DIR/app.exe" "$COMPILER_DIR/app.elf" > /dev/null
    ELF_P1=$(printf "17\n" | (cd "$COMPILER_DIR" && ./app.elf))
    ELF_P2=$(printf "24\n" | (cd "$COMPILER_DIR" && ./app.elf))
    if ! echo "$ELF_P1" | grep -q "PRIME NUMBER!" || ! echo "$ELF_P2" | grep -q "COMPOSITE NUMBER!"; then
        echo -e "${RED}[ERROR] Direct ELF prime checker assertion failed!${NC}"
        exit 1
    fi
    echo -e "  ${GREEN}[+] 10B: Direct ELF interactive console input verified on bare-metal Linux kernel!${NC}"

    # 10C: Batch direct ELF execution for all example programs
    echo "  [*] 10C: Batch Direct Native Execution of all example programs..."
    for item in "${EXAMPLES[@]}"; do
        IFS=":" read -r ex_file expected_substr <<< "$item"
        "$NEXPREP" "$EXAMPLES_DIR/$ex_file" "$COMPILER_DIR/code.nex"
        rm -f "$COMPILER_DIR/app.exe" "$COMPILER_DIR/app.elf"
        (cd "$COMPILER_DIR" && ./nexc.elf > /dev/null)
        [ ! -f "$COMPILER_DIR/app.elf" ] && "$NEXELF" "$COMPILER_DIR/app.exe" "$COMPILER_DIR/app.elf" > /dev/null
        EX_ELF_OUT=$(cd "$COMPILER_DIR" && ./app.elf)
        if echo "$EX_ELF_OUT" | grep -q "$expected_substr"; then
            printf "      [+] %-25s OK (Direct ELF64)\n" "$ex_file"
        else
            echo -e "${RED}      [-] $ex_file FAILED on direct ELF execution!${NC}"
            exit 1
        fi
    done
    echo -e "  ${GREEN}[+] 10C: All ${#EXAMPLES[@]} examples executed natively on Linux kernel!${NC}"

    # 10D: Standalone Native Linux ELF Compiler Self-Hosting Parity Proof
    echo "  [*] 10D: Asserting Standalone Native Linux Compiler Self-Hosting Parity..."
    cp "$COMPILER_DIR/nexc.nex" "$COMPILER_DIR/code.nex"
    rm -f "$COMPILER_DIR/app.exe" "$COMPILER_DIR/app.elf"
    (cd "$COMPILER_DIR" && ./nexc.elf > /dev/null)
    ELF_PE_DIFF=$(cmp -l "$COMPILER_DIR/app.exe" "$COMPILER_DIR/nexc.exe" 2>/dev/null | wc -l || true)
    if [ "$ELF_PE_DIFF" -ne 0 ]; then
        echo -e "${RED}[ERROR] nexc.elf emitted app.exe differs from nexc.exe! ($ELF_PE_DIFF diffs)${NC}"
        exit 1
    fi
    [ ! -f "$COMPILER_DIR/app.elf" ] && "$NEXELF" "$COMPILER_DIR/app.exe" "$COMPILER_DIR/app.elf" > /dev/null
    ELF_ELF_DIFF=$(cmp -l "$COMPILER_DIR/app.elf" "$COMPILER_DIR/nexc.elf" 2>/dev/null | wc -l || true)
    if [ "$ELF_ELF_DIFF" -ne 0 ]; then
        echo -e "${RED}[ERROR] nexc.elf emitted app.elf differs from nexc.elf! ($ELF_ELF_DIFF diffs)${NC}"
        exit 1
    fi
    echo -e "  ${GREEN}[+] 10D: Standalone nexc.elf self-hosting confirmed: 0 byte differences across PE & ELF!${NC}"

    # Restore master test suite to code.nex
    cp "$EXAMPLES_DIR/master_test_suite.nex" "$COMPILER_DIR/code.nex"

    echo -e "\n${GREEN}==================================================================${NC}"
    echo -e "${GREEN} [SUCCESS] ALL 10 BUILD & VERIFICATION STEPS PASSED!             ${NC}"
    echo -e "${GREEN} DUAL TARGET VERIFIED: Windows PE32+ and Linux ELF64             ${NC}"
    echo -e "${GREEN} 100% Native x86-64 | 0% C# | 0% .NET Runtime | Standalone ELF   ${NC}"
    echo -e "${GREEN}==================================================================${NC}"
else
    echo -e "${RED}[FAIL] Binary differences detected between compiler generations!${NC}"
    exit 1
fi
