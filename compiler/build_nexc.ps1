# ==============================================================================
#           NEXUS NATIVE AOT COMPILER BUILDER (STAGE 4: COMPACT MEMORY, ARRAYS & FILE I/O)
#   Arbitrary Block Stack (Nested if/else/while), Functions (fn/call/return),
#   and Interactive Console Input (read <var>)
#  Pure Machine Code Synthesis -> nexc.hex -> nexc.exe (0% C# / 0% .NET Runtime)
# ==============================================================================
$ErrorActionPreference = "Stop"

$nexcExe = Join-Path $PSScriptRoot "nexc.exe"
$nexcHex = Join-Path $PSScriptRoot "nexc.hex"

# ------------------------------------------------------------------------------
# STEP 1: CONSTRUCT THE 8,192-BYTE (8 KB) PE TEMPLATE FOR app.exe
# ------------------------------------------------------------------------------
$appTemplate = New-Object byte[] 0x8000 # 8192 bytes

function T-C($offset, $bytes) { for ($i = 0; $i -lt $bytes.Length; $i++) { $appTemplate[$offset + $i] = $bytes[$i] } }
function T-16($offset, [uint16]$v) { T-C $offset ([BitConverter]::GetBytes($v)) }
function T-32($offset, [uint32]$v) { T-C $offset ([BitConverter]::GetBytes($v)) }
function T-64($offset, [uint64]$v) { T-C $offset ([BitConverter]::GetBytes($v)) }
function T-S($offset, [string]$s) { T-C $offset ([System.Text.Encoding]::ASCII.GetBytes($s)) }

# Headers
T-C 0x00 @(0x4D, 0x5A); T-32 0x3C 0x40
T-C 0x40 @(0x50, 0x45, 0x00, 0x00); T-16 0x44 0x8664; T-16 0x46 2; T-16 0x54 0xF0; T-16 0x56 0x22

# Opt (PE32+)
T-16 0x58 0x020B; T-C 0x5A @(1, 0); T-32 0x5C 0x6E00; T-32 0x60 0x1000
T-32 0x68 0x1000; T-32 0x6C 0x1000; T-64 0x70 0x400000
T-32 0x78 0x1000; T-32 0x7C 0x200; T-16 0x80 6; T-16 0x88 6; T-32 0x90 0x9000; T-32 0x94 0x200
T-16 0x9C 3 # Console
T-16 0x9E 0x8160 # DllCharacteristics: DYNAMIC_BASE | NX_COMPAT | TERMINAL_SERVER_AWARE
T-64 0xA0 0x100000; T-64 0xA8 0x1000; T-64 0xB0 0x100000; T-64 0xB8 0x1000; T-32 0xC4 16
T-32 0xD0 0x8000; T-32 0xD4 0x28 # Import dir

# Section Headers:
# .text (RVA 0x1000, File 0x0200, Size 0x0E00 = 3584 bytes)
T-S 0x148 ".text"; T-32 0x150 0x7000; T-32 0x154 0x1000; T-32 0x158 0x6E00; T-32 0x15C 0x200; T-32 0x16C 0x60000020
# .rdata (RVA 0x2000, File 0x1000, Size 0x1000 = 4096 bytes)
T-S 0x170 ".rdata"; T-32 0x178 0x1000; T-32 0x17C 0x8000; T-32 0x180 0x1000; T-32 0x184 0x7000; T-32 0x194 0x40000040

# .rdata at 0x1000 (RVA 0x2000)
$rd = 0x7000
T-32 ($rd + 0x00) 0x8080 # ILT RVA
T-32 ($rd + 0x0C) 0x8100 # Name RVA ("kernel32.dll")
T-32 ($rd + 0x10) 0x8028 # IAT RVA

# IAT (0x2028 .. 0x2058) & ILT (0x2080 .. 0x20B0) (7 functions)
# 0: GetStdHandle (0x2028) -> Hint 0x2110
# 1: WriteFile    (0x2030) -> Hint 0x2122
# 2: ExitProcess  (0x2038) -> Hint 0x2130
# 3: ReadFile     (0x2040) -> Hint 0x2140
# 4: VirtualAlloc (0x2048) -> Hint 0x2150
# 5: CreateFileA  (0x2050) -> Hint 0x2162
# 6: CloseHandle  (0x2058) -> Hint 0x2172
$hints = @(0x8110, 0x8122, 0x8130, 0x8140, 0x8150, 0x8162, 0x8172)
for ($i = 0; $i -lt $hints.Length; $i++) {
    T-64 ($rd + 0x28 + ($i * 8)) $hints[$i] # IAT
    T-64 ($rd + 0x80 + ($i * 8)) $hints[$i] # ILT
}

T-S ($rd + 0x100) "kernel32.dll`0"
T-16 ($rd + 0x110) 0; T-S ($rd + 0x112) "GetStdHandle`0"
T-16 ($rd + 0x122) 0; T-S ($rd + 0x124) "WriteFile`0"
T-16 ($rd + 0x130) 0; T-S ($rd + 0x132) "ExitProcess`0"
T-16 ($rd + 0x140) 0; T-S ($rd + 0x142) "ReadFile`0"
T-16 ($rd + 0x150) 0; T-S ($rd + 0x152) "VirtualAlloc`0"
T-16 ($rd + 0x162) 0; T-S ($rd + 0x164) "CreateFileA`0"
T-16 ($rd + 0x172) 0; T-S ($rd + 0x174) "CloseHandle`0"

# Static signed print_int routine at offset 0x0E80 (RVA 0x1C80)
$printIntList = New-Object System.Collections.Generic.List[byte]
function P-B([byte[]]$b) { foreach ($x in $b) { $printIntList.Add($x) } }

P-B @(0x55)                                           # push rbp
P-B @(0x57)                                           # push rdi
P-B @(0x48, 0x83, 0xEC, 0x48)                         # sub rsp, 0x48 (aligned)
P-B @(0x48, 0x8D, 0x7C, 0x24, 0x46)                   # lea rdi, [rsp + 0x46]
P-B @(0xC6, 0x07, 0x0A)                               # mov byte ptr [rdi], 0x0A ('\n')
P-B @(0x48, 0xFF, 0xCF)                               # dec rdi
P-B @(0xC6, 0x07, 0x0D)                               # mov byte ptr [rdi], 0x0D ('\r')
P-B @(0x48, 0xFF, 0xCF)                               # dec rdi
P-B @(0x48, 0x89, 0xC8)                               # mov rax, rcx
P-B @(0x45, 0x31, 0xD2)                               # xor r10d, r10d (is_neg = 0)
P-B @(0x48, 0x85, 0xC0)                               # test rax, rax
P-B @(0x79, 0x06)                                     # jns +6 (skip neg)
P-B @(0x48, 0xF7, 0xD8)                               # neg rax
P-B @(0x41, 0xB2, 0x01)                               # mov r10b, 1
# is_pos:
P-B @(0x49, 0xC7, 0xC3, 0x0A, 0x00, 0x00, 0x00)       # mov r11, 10
# itoa_loop:
$loopStart = $printIntList.Count
P-B @(0x31, 0xD2)                                     # xor edx, edx
P-B @(0x49, 0xF7, 0xF3)                               # div r11
P-B @(0x80, 0xC2, 0x30)                               # add dl, '0'
P-B @(0x88, 0x17)                                     # mov [rdi], dl
P-B @(0x48, 0xFF, 0xCF)                               # dec rdi
P-B @(0x48, 0x85, 0xC0)                               # test rax, rax
$loopOffset = ($loopStart) - ($printIntList.Count + 2)
P-B @(0x75, [byte]($loopOffset -band 0xFF))           # jnz itoa_loop

# check neg flag:
P-B @(0x45, 0x84, 0xD2)                               # test r10b, r10b
P-B @(0x74, 0x06)                                     # jz +6 (skip minus)
P-B @(0xC6, 0x07, 0x2D)                               # mov byte ptr [rdi], '-'
P-B @(0x48, 0xFF, 0xCF)                               # dec rdi
# not_neg:
P-B @(0x48, 0x8D, 0x57, 0x01)                         # lea rdx, [rdi + 1]
P-B @(0x48, 0x8D, 0x44, 0x24, 0x47)                   # lea rax, [rsp + 0x47]
P-B @(0x48, 0x29, 0xD0)                               # sub rax, rdx
P-B @(0x41, 0x89, 0xC0)                               # mov r8d, eax (len)
P-B @(0x4C, 0x89, 0xE1)                               # mov rcx, r12 (hStdOut)
P-B @(0x4C, 0x8D, 0x4C, 0x24, 0x28)                   # lea r9, [rsp + 0x28] (&written)
P-B @(0x48, 0xC7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00) # mov qword ptr [rsp+0x20], 0

# call WriteFile: IAT is at RVA 0x2030
$nextRip = 0x7C80 + $printIntList.Count + 6
$callDisp = 0x8030 - $nextRip
P-B @(0xFF, 0x15); P-B ([BitConverter]::GetBytes([int32]$callDisp))

P-B @(0x48, 0x83, 0xC4, 0x48)                         # add rsp, 0x48
P-B @(0x5F)                                           # pop rdi
P-B @(0x5D)                                           # pop rbp
P-B @(0xC3)                                           # ret

T-C 0x6E80 $printIntList.ToArray()

# Static signed read_int routine at offset 0x0F00 (RVA 0x1D00)
# Stream-preserving: reads byte-by-byte and stops immediately at the end of the integer!
$readIntList = New-Object System.Collections.Generic.List[byte]
function R-B([byte[]]$b) { foreach ($x in $b) { $readIntList.Add($x) } }

R-B @(0x55)                                           # push rbp
R-B @(0x57)                                           # push rdi
R-B @(0x56)                                           # push rsi
R-B @(0x53)                                           # push rbx
R-B @(0x48, 0x83, 0xEC, 0x48)                         # sub rsp, 0x48 (16-byte aligned before call)

R-B @(0x31, 0xFF)                                     # xor edi, edi (accumulated number = 0)
R-B @(0x31, 0xF6)                                     # xor esi, esi (is_neg = 0)

# skip_ws_loop:
$skipWsStart = $readIntList.Count
# ReadFile(hStdIn, &ch, 1, &bytesRead, 0)
R-B @(0x4C, 0x89, 0xE9)                               # mov rcx, r13 (hStdIn)
R-B @(0x48, 0x8D, 0x54, 0x24, 0x28)                   # lea rdx, [rsp + 0x28] (char buffer)
R-B @(0x41, 0xB8, 0x01, 0x00, 0x00, 0x00)             # mov r8d, 1
R-B @(0x4C, 0x8D, 0x4C, 0x24, 0x30)                   # lea r9, [rsp + 0x30] (&bytesRead)
R-B @(0x48, 0xC7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00) # mov qword ptr [rsp+0x20], 0

$nextRipRead = 0x7D00 + $readIntList.Count + 6
$callDispRead = 0x8040 - $nextRipRead
R-B @(0xFF, 0x15); R-B ([BitConverter]::GetBytes([int32]$callDispRead))

# Check bytesRead == 1:
R-B @(0x83, 0x7C, 0x24, 0x30, 0x01)                   # cmp dword ptr [rsp + 0x30], 1
$jneFinishWs = $readIntList.Count; R-B @(0x75, 0x00)

R-B @(0x8A, 0x4C, 0x24, 0x28)                         # mov cl, [rsp + 0x28]
R-B @(0x80, 0xF9, 0x20); $jeWs1 = $readIntList.Count; R-B @(0x74, 0x00) # ' '
R-B @(0x80, 0xF9, 0x09); $jeWs2 = $readIntList.Count; R-B @(0x74, 0x00) # '\t'
R-B @(0x80, 0xF9, 0x0D); $jeWs3 = $readIntList.Count; R-B @(0x74, 0x00) # '\r'
R-B @(0x80, 0xF9, 0x0A); $jeWs4 = $readIntList.Count; R-B @(0x74, 0x00) # '\n'

$jmpNotWs = $readIntList.Count; R-B @(0xEB, 0x00)

# Advance whitespace and loop:
$wsLoopTarget = $readIntList.Count
$readIntList[$jeWs1 + 1] = [byte]($wsLoopTarget - ($jeWs1 + 2))
$readIntList[$jeWs2 + 1] = [byte]($wsLoopTarget - ($jeWs2 + 2))
$readIntList[$jeWs3 + 1] = [byte]($wsLoopTarget - ($jeWs3 + 2))
$readIntList[$jeWs4 + 1] = [byte]($wsLoopTarget - ($jeWs4 + 2))
$readIntList.Add(0xEB)
$readIntList.Add([byte](($skipWsStart - ($readIntList.Count + 1)) -band 0xFF))

# Not whitespace:
$notWsTarget = $readIntList.Count
$readIntList[$jmpNotWs + 1] = [byte]($notWsTarget - ($jmpNotWs + 2))

# Check '-':
R-B @(0x80, 0xF9, 0x2D)                               # cmp cl, '-'
$jneCheckDigit1 = $readIntList.Count; R-B @(0x75, 0x00)
R-B @(0xBE, 0x01, 0x00, 0x00, 0x00)                   # mov esi, 1 (is_neg = 1)

# digit_read_loop:
$digitReadLoopStart = $readIntList.Count
R-B @(0x4C, 0x89, 0xE9)                               # mov rcx, r13
R-B @(0x48, 0x8D, 0x54, 0x24, 0x28)                   # lea rdx, [rsp + 0x28]
R-B @(0x41, 0xB8, 0x01, 0x00, 0x00, 0x00)             # mov r8d, 1
R-B @(0x4C, 0x8D, 0x4C, 0x24, 0x30)                   # lea r9, [rsp + 0x30]
R-B @(0x48, 0xC7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00)
$nextRipRead2 = 0x7D00 + $readIntList.Count + 6
$callDispRead2 = 0x8040 - $nextRipRead2
R-B @(0xFF, 0x15); R-B ([BitConverter]::GetBytes([int32]$callDispRead2))
R-B @(0x83, 0x7C, 0x24, 0x30, 0x01)                   # cmp dword ptr [rsp + 0x30], 1
$jneFinishDigits = $readIntList.Count; R-B @(0x75, 0x00)
R-B @(0x8A, 0x4C, 0x24, 0x28)                         # mov cl, [rsp + 0x28]

# check_digit:
$chkDigitTarget = $readIntList.Count
$readIntList[$jneCheckDigit1 + 1] = [byte]($chkDigitTarget - ($jneCheckDigit1 + 2))
R-B @(0x80, 0xF9, 0x30); $jbDigitDone = $readIntList.Count; R-B @(0x72, 0x00) # '0'
R-B @(0x80, 0xF9, 0x39); $jaDigitDone = $readIntList.Count; R-B @(0x77, 0x00) # '9'

R-B @(0x80, 0xE9, 0x30)                               # sub cl, '0'
R-B @(0x48, 0x6B, 0xFF, 0x0A)                         # imul rdi, rdi, 10
R-B @(0x48, 0x0F, 0xB6, 0xC9)                         # movzx rcx, cl
R-B @(0x48, 0x01, 0xCF)                               # add rdi, rcx
$readIntList.Add(0xEB)
$readIntList.Add([byte](($digitReadLoopStart - ($readIntList.Count + 1)) -band 0xFF))

# finish_read:
$finishTarget = $readIntList.Count
$readIntList[$jneFinishWs + 1] = [byte]($finishTarget - ($jneFinishWs + 2))
$readIntList[$jneFinishDigits + 1] = [byte]($finishTarget - ($jneFinishDigits + 2))
$readIntList[$jbDigitDone + 1] = [byte]($finishTarget - ($jbDigitDone + 2))
$readIntList[$jaDigitDone + 1] = [byte]($finishTarget - ($jaDigitDone + 2))

R-B @(0x48, 0x89, 0xF8)                               # mov rax, rdi
R-B @(0x85, 0xF6)                                     # test esi, esi
$jzNotNegResult = $readIntList.Count; R-B @(0x74, 0x00)
R-B @(0x48, 0xF7, 0xD8)                               # neg rax

$retTarget = $readIntList.Count
$readIntList[$jzNotNegResult + 1] = [byte]($retTarget - ($jzNotNegResult + 2))
R-B @(0x48, 0x83, 0xC4, 0x48)                         # add rsp, 0x48
R-B @(0x5B)                                           # pop rbx
R-B @(0x5E)                                           # pop rsi
R-B @(0x5F)                                           # pop rdi
R-B @(0x5D)                                           # pop rbp
R-B @(0xC3)                                           # ret

T-C 0x6F00 $readIntList.ToArray()

$appPrologue = @(
    0x48, 0x81, 0xEC, 0x28, 0x08, 0x00, 0x00, # sub rsp, 0x828 (2088 bytes)
    0x48, 0x89, 0xE5,                         # mov rbp, rsp
    0x48, 0x8D, 0x7D, 0x30,                   # lea rdi, [rbp + 0x30]
    0xB9, 0x80, 0x00, 0x00, 0x00,             # mov ecx, 128 (zero 128 variables)
    0x31, 0xC0,                               # xor eax, eax
    0xF3, 0x48, 0xAB,                         # rep stosq
    0xB9, 0xF5, 0xFF, 0xFF, 0xFF,             # mov ecx, -11 (STD_OUTPUT_HANDLE)
    0xFF, 0x15, 0x05, 0x70, 0x00, 0x00,       # call [rip + 0x7005] (GetStdHandle at 0x8028)
    0x49, 0x89, 0xC4,                         # mov r12, rax (r12 = hStdOut)
    0xB9, 0xF6, 0xFF, 0xFF, 0xFF,             # mov ecx, -10 (STD_INPUT_HANDLE)
    0xFF, 0x15, 0xF7, 0x6F, 0x00, 0x00,       # call [rip + 0x6FF7] (GetStdHandle at 0x8028)
    0x49, 0x89, 0xC5                          # mov r13, rax (r13 = hStdIn)
)
T-C 0x0200 $appPrologue

$templateBin = Join-Path $PSScriptRoot "template.bin"
[System.IO.File]::WriteAllBytes($templateBin, $appTemplate)
Write-Host "[+] Exported $templateBin (32768 bytes)"

# ------------------------------------------------------------------------------
# STEP 2: CONSTRUCT nexc.exe (24,576 bytes = 0x6000)
# ------------------------------------------------------------------------------
$nexcBytes = New-Object byte[] 0xB000

function N-C($offset, $bytes) { for ($i = 0; $i -lt $bytes.Length; $i++) { $nexcBytes[$offset + $i] = $bytes[$i] } }
function N-16($offset, [uint16]$v) { N-C $offset ([BitConverter]::GetBytes($v)) }
function N-32($offset, [uint32]$v) { N-C $offset ([BitConverter]::GetBytes($v)) }
function N-64($offset, [uint64]$v) { N-C $offset ([BitConverter]::GetBytes($v)) }
function N-S($offset, [string]$s) { N-C $offset ([System.Text.Encoding]::ASCII.GetBytes($s)) }

# Headers
N-C 0x00 @(0x4D, 0x5A); N-32 0x3C 0x40
N-C 0x40 @(0x50, 0x45, 0x00, 0x00); N-16 0x44 0x8664; N-16 0x46 3; N-16 0x54 0xF0; N-16 0x56 0x22

# Opt (PE32+)
N-16 0x58 0x020B; N-C 0x5A @(1, 0); N-32 0x5C 0x1E00; N-32 0x60 0x4000
N-32 0x68 0x1000; N-32 0x6C 0x1000; N-64 0x70 0x400000
N-32 0x78 0x1000; N-32 0x7C 0x200; N-16 0x80 6; N-16 0x88 6; N-32 0x90 0x84000; N-32 0x94 0x200
N-16 0x9C 3 # Console
N-16 0x9E 0x8160 # DllCharacteristics
N-64 0xA0 0x100000; N-64 0xA8 0x1000; N-64 0xB0 0x100000; N-64 0xB8 0x1000; N-32 0xC4 16
N-32 0xD0 0x3000; N-32 0xD4 0x28 # Import dir

# Section Headers:
# .text (RVA 0x1000, File 0x0200, Size 0x1E00 = 7680 bytes)
N-S 0x148 ".text"; N-32 0x150 0x2000; N-32 0x154 0x1000; N-32 0x158 0x1E00; N-32 0x15C 0x200; N-32 0x16C 0x60000020
# .rdata (RVA 0x3000, File 0x2000, Size 0x1000 = 4096 bytes)
N-S 0x170 ".rdata"; N-32 0x178 0x1000; N-32 0x17C 0x3000; N-32 0x180 0x1000; N-32 0x184 0x2000; N-32 0x194 0x40000040
# .data (RVA 0x4000, File 0x3000, Size 0x3000 = 12288 bytes, Virtual Size = 0x40000 = 256 KB)
N-S 0x198 ".data"; N-32 0x1A0 0x80000; N-32 0x1A4 0x4000; N-32 0x1A8 0x8000; N-32 0x1AC 0x3000
$nexcBytes[0x1BC] = 0x40; $nexcBytes[0x1BD] = 0x00; $nexcBytes[0x1BE] = 0x00; $nexcBytes[0x1BF] = 0xC0 # READ | WRITE

# .rdata in nexc.exe (RVA 0x3000, File 0x2000)
$nrd = 0x2000
N-32 ($nrd + 0x00) 0x3060 # ILT RVA
N-32 ($nrd + 0x0C) 0x30A0 # Name RVA ("kernel32.dll")
N-32 ($nrd + 0x10) 0x3028 # IAT RVA

# IAT & ILT (6 functions)
$hintsN = @(0x30B0, 0x30C6, 0x30D8, 0x30EE, 0x3100, 0x3114)
for ($i = 0; $i -lt $hintsN.Length; $i++) {
    N-64 ($nrd + 0x28 + ($i * 8)) $hintsN[$i] # IAT
    N-64 ($nrd + 0x60 + ($i * 8)) $hintsN[$i] # ILT
}

N-S ($nrd + 0xA0) "kernel32.dll`0"
N-16 ($nrd + 0xB0) 0; N-S ($nrd + 0xB2) "GetStdHandle`0"
N-16 ($nrd + 0xC6) 0; N-S ($nrd + 0xC8) "WriteFile`0"
N-16 ($nrd + 0xD8) 0; N-S ($nrd + 0xDA) "CreateFileA`0"
N-16 ($nrd + 0xEE) 0; N-S ($nrd + 0xF0) "ReadFile`0"
N-16 ($nrd + 0x100) 0; N-S ($nrd + 0x102) "CloseHandle`0"
N-16 ($nrd + 0x114) 0; N-S ($nrd + 0x116) "ExitProcess`0"

$banner = "`r`n[+] ====================================================`r`n" +
          "[+] NEXUS Native AOT Compiler v4.0 (Stage 4: Complete Memory, Arrays & File I/O)`r`n" +
          "[+] Compiling code.nex -> app.exe...`r`n"
N-S ($nrd + 0x130) $banner
$bannerLen = [System.Text.Encoding]::ASCII.GetBytes($banner).Length

$success = "[+] Compilation Succeeded! Standalone binary: 32,768 bytes`r`n" +
           "[+] Executable app.exe ready to run.`r`n" +
           "[+] ====================================================`r`n"
N-S ($nrd + 0x200) $success
$successLen = [System.Text.Encoding]::ASCII.GetBytes($success).Length

# Error messages:
$errFile = "[!] Fatal Error: Could not open code.nex`r`n"
N-S ($nrd + 0x2D0) $errFile
$errFileLen = [System.Text.Encoding]::ASCII.GetBytes($errFile).Length

$errSyntax = "[!] Syntax Error: Invalid token, operator or expression in code.nex`r`n"
N-S ($nrd + 0x310) $errSyntax
$errSyntaxLen = [System.Text.Encoding]::ASCII.GetBytes($errSyntax).Length

$errOverflow = "[!] Error: Program size exceeds template capacity limits`r`n"
N-S ($nrd + 0x370) $errOverflow
$errOverflowLen = [System.Text.Encoding]::ASCII.GetBytes($errOverflow).Length
# Diagnostics Strings and Lookup Table for code.nex error reporting:
$sDiagPrefix = "`r`ncode.nex:"
$sDiagInfix  = ": error: "
$sDiagIndent = "`r`n  "
$sDiagCaret  = "^`r`n`r`n"

N-S ($nrd + 0x400) $sDiagPrefix
N-S ($nrd + 0x410) $sDiagInfix
N-S ($nrd + 0x420) $sDiagIndent
N-S ($nrd + 0x430) $sDiagCaret

$errorDefs = @(
    @{ Id = 0;  Msg = "unexpected token or syntax error" },
    @{ Id = 1;  Msg = "unrecognized statement or keyword" },
    @{ Id = 2;  Msg = "invalid variable name in condition (expected a-z)" },
    @{ Id = 3;  Msg = "invalid condition operator (expected ==, !=, <, >, <=, >=)" },
    @{ Id = 4;  Msg = "block nesting depth exceeded (maximum 16 levels)" },
    @{ Id = 5;  Msg = "unmatched '}' (no block was open)" },
    @{ Id = 6;  Msg = "expected '}' to close block (reached end of file)" },
    @{ Id = 7;  Msg = "invalid or missing function name in 'fn'" },
    @{ Id = 8;  Msg = "undefined function or invalid function name in 'call'" },
    @{ Id = 9;  Msg = "expected variable name (a-z) after 'read'" },
    @{ Id = 10; Msg = "invalid variable name (a-z) in 'let'" },
    @{ Id = 11; Msg = "invalid operand in expression (expected variable a-z or number)" },
    @{ Id = 12; Msg = "expected arithmetic operator (+, -, *, /, %)" },
    @{ Id = 13; Msg = "invalid variable name (a-z) in 'print'" },
    @{ Id = 14; Msg = "maximum function definition limit reached (128 functions)" },
    @{ Id = 15; Msg = "expected '=' in 'let' statement" },
    @{ Id = 16; Msg = "invalid memory access syntax (expected '[ptr + idx]')" },
    @{ Id = 17; Msg = "invalid file operation syntax" }
)

$tableBaseOffset = 0x440
$strBaseOffset   = 0x440 + ($errorDefs.Count * 8)
$currStrOffset   = $strBaseOffset

for ($i = 0; $i -lt $errorDefs.Count; $i++) {
    $msg = $errorDefs[$i].Msg
    $msgBytes = [System.Text.Encoding]::ASCII.GetBytes($msg)
    $msgLen = $msgBytes.Length
    N-C ($nrd + $currStrOffset) $msgBytes
    $dispFromTable = $currStrOffset - $tableBaseOffset
    N-32 ($nrd + $tableBaseOffset + ($i * 8)) $dispFromTable
    N-32 ($nrd + $tableBaseOffset + ($i * 8) + 4) $msgLen
    $currStrOffset += $msgLen + 1
}

N-S ($nrd + 0x3D0) "code.nex`0"
N-S ($nrd + 0x3E0) "app.exe`0"

# Copy appTemplate (8,192 bytes) into nexc.exe .data (RVA 0x4000, File offset 0x3000)
N-C 0x3000 $appTemplate

# ------------------------------------------------------------------------------
# STEP 3: ASSEMBLE .text MACHINE CODE FOR nexc.exe
# ------------------------------------------------------------------------------
$code = New-Object System.Collections.Generic.List[byte]

function Emit-B([byte[]]$b) { foreach ($x in $b) { $code.Add($x) } }
function Emit-CallIAT($iatRVA) {
    Emit-B @(0xFF, 0x15)
    $nextRIP = 0x1000 + $code.Count + 4
    $disp = $iatRVA - $nextRIP
    Emit-B ([BitConverter]::GetBytes([int32]$disp))
}
function Emit-LeaRIP($regModRM, $targetRVA) {
    Emit-B @(0x48, 0x8D, $regModRM)
    $nextRIP = 0x1000 + $code.Count + 4
    $disp = $targetRVA - $nextRIP
    Emit-B ([BitConverter]::GetBytes([int32]$disp))
}

function Emit-JmpFwd32 {
    Emit-B @(0xE9)
    $patchPos = $code.Count
    Emit-B @(0x00, 0x00, 0x00, 0x00)
    return $patchPos
}
function Emit-SyntaxErr($errId) {
    Emit-B @(0xBA); Emit-B ([BitConverter]::GetBytes([int32]$errId))
    Emit-JmpBack32 $errSyntaxHandlerPos
}
function Patch-JmpFwd32($patchPos) {
    $targetPos = $code.Count
    $nextRIP = 0x1000 + $patchPos + 4
    $disp = (0x1000 + $targetPos) - $nextRIP
    $bytes = [BitConverter]::GetBytes([int32]$disp)
    for ($i = 0; $i -lt 4; $i++) { $code[$patchPos + $i] = $bytes[$i] }
}

function Emit-JccFwd32($condByte) {
    Emit-B @(0x0F, $condByte)
    $patchPos = $code.Count
    Emit-B @(0x00, 0x00, 0x00, 0x00)
    return $patchPos
}
function Patch-JccFwd32($patchPos) {
    $targetPos = $code.Count
    $nextRIP = 0x1000 + $patchPos + 4
    $disp = (0x1000 + $targetPos) - $nextRIP
    $bytes = [BitConverter]::GetBytes([int32]$disp)
    for ($i = 0; $i -lt 4; $i++) { $code[$patchPos + $i] = $bytes[$i] }
}

function Emit-JmpBack32($targetOffset) {
    Emit-B @(0xE9)
    $nextRIP = 0x1000 + $code.Count + 4
    $disp = (0x1000 + $targetOffset) - $nextRIP
    Emit-B ([BitConverter]::GetBytes([int32]$disp))
}
function Emit-JccBack32($condByte, $targetOffset) {
    Emit-B @(0x0F, $condByte)
    $nextRIP = 0x1000 + $code.Count + 4
    $disp = (0x1000 + $targetOffset) - $nextRIP
    Emit-B ([BitConverter]::GetBytes([int32]$disp))
}

# 1. Entry Point: allocate 0x300 bytes stack frame (aligned)
Emit-B @(0x48, 0x81, 0xEC, 0x00, 0x03, 0x00, 0x00) # sub rsp, 0x300
# Zero out block_depth [rsp + 0x58] and fn_count [rsp + 0x5C]:
Emit-B @(0xC7, 0x44, 0x24, 0x58, 0x00, 0x00, 0x00, 0x00) # mov dword ptr [rsp+0x58], 0
Emit-B @(0xC7, 0x44, 0x24, 0x5C, 0x00, 0x00, 0x00, 0x00) # mov dword ptr [rsp+0x5C], 0

# 2. GetStdHandle(-11) -> IAT 0x3028
Emit-B @(0xB9, 0xF5, 0xFF, 0xFF, 0xFF) # mov ecx, -11
Emit-CallIAT 0x3028
Emit-B @(0x48, 0x89, 0xC3)             # mov rbx, rax (rbx = hStdOut)

# 3. WriteFile(hStdOut, banner, bannerLen, &written, 0) -> IAT 0x3030
Emit-B @(0x48, 0x89, 0xD9)             # mov rcx, rbx
Emit-LeaRIP 0x15 0x3130                # lea rdx, [rip + sBanner]
Emit-B @(0x41, 0xB8); Emit-B ([BitConverter]::GetBytes([int32]$bannerLen))
Emit-B @(0x4C, 0x8D, 0x4C, 0x24, 0x40) # lea r9, [rsp + 0x40]
Emit-B @(0x48, 0xC7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00)
Emit-CallIAT 0x3030

# 4. Open "code.nex": CreateFileA("code.nex", GENERIC_READ, 1, 0, 3, 0x80, 0) -> IAT 0x3038
Emit-LeaRIP 0x0D 0x33D0                # lea rcx, [rip + "code.nex"]
Emit-B @(0xBA, 0x00, 0x00, 0x00, 0x80) # mov edx, 0x80000000
Emit-B @(0x41, 0xB8, 0x01, 0x00, 0x00, 0x00) # mov r8d, 1
Emit-B @(0x45, 0x31, 0xC9)             # xor r9d, r9d
Emit-B @(0x48, 0xC7, 0x44, 0x24, 0x20, 0x03, 0x00, 0x00, 0x00) # 3
Emit-B @(0x48, 0xC7, 0x44, 0x24, 0x28, 0x80, 0x00, 0x00, 0x00) # 0x80
Emit-B @(0x48, 0xC7, 0x44, 0x24, 0x30, 0x00, 0x00, 0x00, 0x00) # 0
Emit-CallIAT 0x3038
Emit-B @(0x48, 0x89, 0xC6)             # mov rsi, rax (rsi = hInput)

# Check if hInput == INVALID_HANDLE_VALUE (-1)
Emit-B @(0x48, 0x83, 0xFE, 0xFF)       # cmp rsi, -1
$jeOpenErr = (Emit-JccFwd32 0x84)       # je error_open_file

# 5. ReadFile(hInput, inputBuf, 131072, &bytesRead, 0) -> IAT 0x3040
Emit-B @(0x48, 0x89, 0xF1)             # mov rcx, rsi
Emit-LeaRIP 0x15 0xE000                # lea rdx, [rip + inputBuf] (RVA 0xE000)
Emit-B @(0x41, 0xB8, 0x00, 0x00, 0x02, 0x00) # mov r8d, 131072 (128 KB)
Emit-B @(0x4C, 0x8D, 0x4C, 0x24, 0x40) # lea r9, [rsp + 0x40]
Emit-B @(0x48, 0xC7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00)
Emit-CallIAT 0x3040

# Null-terminate inputBuf: inputBuf[bytesRead] = 0
Emit-B @(0x8B, 0x44, 0x24, 0x40)       # mov eax, [rsp + 0x40]
Emit-LeaRIP 0x3D 0xE000                # lea rdi, [rip + inputBuf]
Emit-B @(0xC6, 0x04, 0x07, 0x00)       # mov byte ptr [rdi + rax], 0

# CloseHandle(hInput) -> IAT 0x3048
Emit-B @(0x48, 0x89, 0xF1)             # mov rcx, rsi
Emit-CallIAT 0x3048

# ------------------------------------------------------------------------------
# 6. INITIALIZE EMITTER STATE:
# ------------------------------------------------------------------------------
Emit-LeaRIP 0x35 0xE000                # lea rsi, [rip + inputBuf]

# Save peTemplateBase into [rsp + 0x38]
Emit-LeaRIP 0x05 0x4000                # lea rax, [rip + peTemplate] (RVA 0x4000)
Emit-B @(0x48, 0x89, 0x44, 0x24, 0x38) # mov [rsp + 0x38], rax

# rdi = code emit pointer (peTemplate + 0x0200)
Emit-B @(0x48, 0x8B, 0x7C, 0x24, 0x38) # mov rdi, [rsp + 0x38]
Emit-B @(0x48, 0x81, 0xC7, 0x00, 0x02, 0x00, 0x00) # add rdi, 0x0200

# rbp = string pool emit pointer (peTemplate + 0x1200)
Emit-B @(0x48, 0x8B, 0x6C, 0x24, 0x38) # mov rbp, [rsp + 0x38]
Emit-B @(0x48, 0x81, 0xC5, 0x00, 0x72, 0x00, 0x00) # add rbp, 0x7200

# Emit appPrologue:
foreach ($b in $appPrologue) {
    Emit-B @(0xC6, 0x07, $b)           # mov byte ptr [rdi], b
    Emit-B @(0x48, 0xFF, 0xC7)         # inc rdi
}

$jmpToLoop = (Emit-JmpFwd32)

# ------------------------------------------------------------------------------
# ERROR HANDLERS IN nexc.exe:
# ------------------------------------------------------------------------------
Patch-JccFwd32 $jeOpenErr
Emit-B @(0x48, 0x89, 0xD9)             # mov rcx, rbx
Emit-LeaRIP 0x15 0x32D0                # lea rdx, [rip + sErrFile]
Emit-B @(0x41, 0xB8); Emit-B ([BitConverter]::GetBytes([int32]$errFileLen))
Emit-B @(0x4C, 0x8D, 0x4C, 0x24, 0x40)
Emit-B @(0x48, 0xC7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00)
Emit-CallIAT 0x3030
Emit-B @(0xB9, 0x01, 0x00, 0x00, 0x00) # mov ecx, 1
Emit-CallIAT 0x3050

Write-Host "errSyntaxPos: 0x$($code.Count.ToString('X'))"; $errSyntaxPos = $code.Count
Emit-B @(0xBA, 0x00, 0x00, 0x00, 0x00) # mov edx, 0 (default error ID)

$errSyntaxHandlerPos = $code.Count
# 1. Save edx into [rsp + 0x48]
Emit-B @(0x89, 0x54, 0x24, 0x48)       # mov [rsp + 0x48], edx

# 2. Get inputBuf start into r8 (RVA 0xE000)
Emit-LeaRIP 0x05 0xE000                # lea rax, [rip + 0xE000]
Emit-B @(0x49, 0x89, 0xC0)             # mov r8, rax

# 3. Clamp rsi >= r8
Emit-B @(0x49, 0x39, 0xC6)             # cmp rsi, r8
$jaeRsiOk = (Emit-JccFwd32 0x83)
Emit-B @(0x49, 0x89, 0xC6)             # mov rsi, r8
Patch-JccFwd32 $jaeRsiOk

# 4. Scan from r8 to rsi to find line (r12d), col (r13d), line_start (r14)
Emit-B @(0x41, 0xBC, 0x01, 0x00, 0x00, 0x00) # mov r12d, 1 (line = 1)
Emit-B @(0x41, 0xBD, 0x01, 0x00, 0x00, 0x00) # mov r13d, 1 (col = 1)
Emit-B @(0x4D, 0x89, 0xC6)             # mov r14, r8 (line_start = r8)
Emit-B @(0x4C, 0x89, 0xC0)             # mov rax, r8 (curr = r8)

$scanLoopOff = $code.Count
Emit-B @(0x48, 0x39, 0xF0)             # cmp rax, rsi
$jaeScanDone = (Emit-JccFwd32 0x83)      # jae scan_done
Emit-B @(0x8A, 0x08)                   # mov cl, [rax]
Emit-B @(0x80, 0xF9, 0x0A)             # cmp cl, 10 ('\n')
$jneNotLf = (Emit-JccFwd32 0x85)         # jne not_lf
Emit-B @(0x41, 0xFF, 0xC4)             # inc r12d (line++)
Emit-B @(0x41, 0xBD, 0x01, 0x00, 0x00, 0x00) # mov r13d, 1 (col = 1)
Emit-B @(0x4C, 0x8D, 0x70, 0x01)       # lea r14, [rax + 1] (line_start = rax + 1)
$jmpNextChar = (Emit-JmpFwd32)

Patch-JccFwd32 $jneNotLf
Emit-B @(0x80, 0xF9, 0x0D)             # cmp cl, 13 ('\r')
$jeNextChar = (Emit-JccFwd32 0x84)       # je next_char
Emit-B @(0x41, 0xFF, 0xC5)             # inc r13d (col++)

Patch-JmpFwd32 $jmpNextChar
Patch-JccFwd32 $jeNextChar
Emit-B @(0x48, 0xFF, 0xC0)             # inc rax
Emit-JmpBack32 $scanLoopOff

Patch-JccFwd32 $jaeScanDone

# 5. Find end of line starting from r14:
Emit-B @(0x4C, 0x89, 0xF0)             # mov rax, r14
$findEolOff = $code.Count
Emit-B @(0x8A, 0x08)                   # mov cl, [rax]
Emit-B @(0x84, 0xC9); $jzFoundEol1 = (Emit-JccFwd32 0x84) # test cl, cl -> jz
Emit-B @(0x80, 0xF9, 0x0A); $jeFoundEol2 = (Emit-JccFwd32 0x84) # cmp cl, 10 -> je
Emit-B @(0x80, 0xF9, 0x0D); $jeFoundEol3 = (Emit-JccFwd32 0x84) # cmp cl, 13 -> je
Emit-B @(0x48, 0xFF, 0xC0)             # inc rax
Emit-JmpBack32 $findEolOff

Patch-JccFwd32 $jzFoundEol1; Patch-JccFwd32 $jeFoundEol2; Patch-JccFwd32 $jeFoundEol3
Emit-B @(0x49, 0x89, 0xC2)             # mov r10, rax (line_end)
Emit-B @(0x4D, 0x29, 0xF2)             # sub r10, r14 (line_len = line_end - line_start)

# Limit line_len to max 120 chars:
Emit-B @(0x49, 0x83, 0xFA, 0x78)       # cmp r10, 120
$jbeLenOk = (Emit-JccFwd32 0x86)
Emit-B @(0x49, 0xC7, 0xC2, 0x78, 0x00, 0x00, 0x00) # mov r10, 120
Patch-JccFwd32 $jbeLenOk

# 6. Setup output buffer in rdi (RVA 0x30000)
Emit-LeaRIP 0x3D 0x30000                # lea rdi, [rip + 0x30000]
Emit-B @(0x48, 0x89, 0xFD)             # mov rbp, rdi (save outBuf start in rbp)

# 7. Copy prefix "\r\ncode.nex:" (11 bytes from RVA 0x3400)
Emit-LeaRIP 0x35 0x3400                # lea rsi, [rip + 0x3400]
Emit-B @(0xB9, 0x0B, 0x00, 0x00, 0x00) # mov ecx, 11
Emit-B @(0xF3, 0xA4)                   # rep movsb

# 8. Convert r12d (line) to decimal string into [rdi]:
Emit-B @(0x44, 0x89, 0xE0)             # mov eax, r12d
Emit-B @(0x31, 0xC9)                   # xor ecx, ecx
Emit-B @(0x41, 0xB9, 0x0A, 0x00, 0x00, 0x00) # mov r9d, 10
$itoaLineLoop = $code.Count
Emit-B @(0x31, 0xD2)                   # xor edx, edx
Emit-B @(0x41, 0xF7, 0xF1)             # div r9d
Emit-B @(0x52)                         # push rdx
Emit-B @(0xFF, 0xC1)                   # inc ecx
Emit-B @(0x85, 0xC0)                   # test eax, eax
Emit-JccBack32 0x85 $itoaLineLoop      # jnz itoaLineLoop

$popLineLoop = $code.Count
Emit-B @(0x58)                         # pop rax
Emit-B @(0x04, 0x30)                   # add al, '0'
Emit-B @(0x88, 0x07)                   # mov [rdi], al
Emit-B @(0x48, 0xFF, 0xC7)             # inc rdi
Emit-B @(0xFF, 0xC9)                   # dec ecx
Emit-JccBack32 0x85 $popLineLoop       # jnz popLineLoop

# Write ':'
Emit-B @(0xC6, 0x07, 0x3A); Emit-B @(0x48, 0xFF, 0xC7) # mov [rdi], ':'; inc rdi

# 9. Convert r13d (col) to decimal into [rdi]:
Emit-B @(0x44, 0x89, 0xE8)             # mov eax, r13d
Emit-B @(0x31, 0xC9)                   # xor ecx, ecx
Emit-B @(0x41, 0xB9, 0x0A, 0x00, 0x00, 0x00) # mov r9d, 10
$itoaColLoop = $code.Count
Emit-B @(0x31, 0xD2)                   # xor edx, edx
Emit-B @(0x41, 0xF7, 0xF1)             # div r9d
Emit-B @(0x52)                         # push rdx
Emit-B @(0xFF, 0xC1)                   # inc ecx
Emit-B @(0x85, 0xC0)                   # test eax, eax
Emit-JccBack32 0x85 $itoaColLoop       # jnz itoaColLoop

$popColLoop = $code.Count
Emit-B @(0x58)                         # pop rax
Emit-B @(0x04, 0x30)                   # add al, '0'
Emit-B @(0x88, 0x07)                   # mov [rdi], al
Emit-B @(0x48, 0xFF, 0xC7)             # inc rdi
Emit-B @(0xFF, 0xC9)                   # dec ecx
Emit-JccBack32 0x85 $popColLoop        # jnz popColLoop

# 10. Copy infix ": error: " (9 bytes from RVA 0x3410)
Emit-LeaRIP 0x35 0x3410                # lea rsi, [rip + 0x3410]
Emit-B @(0xB9, 0x09, 0x00, 0x00, 0x00) # mov ecx, 9
Emit-B @(0xF3, 0xA4)                   # rep movsb

# 11. Look up error message in table at RVA 0x3440:
Emit-B @(0x8B, 0x4C, 0x24, 0x48)       # mov ecx, [rsp + 0x48] (error ID)
Emit-B @(0x83, 0xF9, 0x11)             # cmp ecx, 17
$jbeIdOk = (Emit-JccFwd32 0x86)
Emit-B @(0x31, 0xC9)                   # xor ecx, ecx
Patch-JccFwd32 $jbeIdOk
Emit-B @(0xC1, 0xE1, 0x03)             # shl ecx, 3 (* 8)

Emit-LeaRIP 0x05 0x3440                # lea rax, [rip + 0x3440] (table start)
Emit-B @(0x48, 0x01, 0xC8)             # add rax, rcx (rax = entry_ptr)
Emit-B @(0x8B, 0x10)                   # mov edx, [rax] (dispFromTable)
Emit-B @(0x8B, 0x48, 0x04)             # mov ecx, [rax + 4] (msgLen)

Emit-LeaRIP 0x35 0x3440                # lea rsi, [rip + 0x3440] (table start)
Emit-B @(0x48, 0x01, 0xD6)             # add rsi, rdx (rsi = msgPtr)
Emit-B @(0xF3, 0xA4)                   # rep movsb

# 12. Copy indent "\r\n  " (4 bytes from RVA 0x3420)
Emit-LeaRIP 0x35 0x3420                # lea rsi, [rip + 0x3420]
Emit-B @(0xB9, 0x04, 0x00, 0x00, 0x00) # mov ecx, 4
Emit-B @(0xF3, 0xA4)                   # rep movsb

# 13. Copy source line text (r10 bytes from r14)
Emit-B @(0x4D, 0x89, 0xF6)             # mov rsi, r14 (line_start)
Emit-B @(0x4D, 0x89, 0xD1)             # mov rcx, r10 (line_len)
Emit-B @(0xF3, 0xA4)                   # rep movsb

# 14. Copy indent "\r\n  " (4 bytes from RVA 0x3420)
Emit-LeaRIP 0x35 0x3420                # lea rsi, [rip + 0x3420]
Emit-B @(0xB9, 0x04, 0x00, 0x00, 0x00) # mov ecx, 4
Emit-B @(0xF3, 0xA4)                   # rep movsb

# 15. Write (col - 1) spaces
Emit-B @(0x44, 0x89, 0xE9)             # mov ecx, r13d
Emit-B @(0xFF, 0xC9)                   # dec ecx
Emit-B @(0x81, 0xF9, 0x78, 0x00, 0x00, 0x00) # cmp ecx, 120
$jbeSpOk = (Emit-JccFwd32 0x86)
Emit-B @(0xB9, 0x78, 0x00, 0x00, 0x00) # mov ecx, 120
Patch-JccFwd32 $jbeSpOk
Emit-B @(0x85, 0xC9)                   # test ecx, ecx
$jzNoSp = (Emit-JccFwd32 0x84)
Emit-B @(0xB0, 0x20)                   # mov al, ' '
Emit-B @(0xF3, 0xAA)                   # rep stosb
Patch-JccFwd32 $jzNoSp

# 16. Copy caret "^`r`n`r`n" (5 bytes from RVA 0x3430)
Emit-LeaRIP 0x35 0x3430                # lea rsi, [rip + 0x3430]
Emit-B @(0xB9, 0x05, 0x00, 0x00, 0x00) # mov ecx, 5
Emit-B @(0xF3, 0xA4)                   # rep movsb

# 17. WriteFile(hStdOut, rbp, totalLen, &written, 0)
Emit-B @(0x44, 0x89, 0xF8)             # mov r8d, edi
Emit-B @(0x44, 0x29, 0xE8)             # sub r8d, ebp (totalLen = rdi - rbp)
Emit-B @(0x48, 0x89, 0xD9)             # mov rcx, rbx (hStdOut)
Emit-B @(0x48, 0x89, 0xEA)             # mov rdx, rbp (outBuf)
Emit-B @(0x4C, 0x8D, 0x4C, 0x24, 0x40) # lea r9, [rsp + 0x40]
Emit-B @(0x48, 0xC7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00)
Emit-CallIAT 0x3030

# 18. ExitProcess(2)
Emit-B @(0xB9, 0x02, 0x00, 0x00, 0x00) # mov ecx, 2
Emit-CallIAT 0x3050

$errOverflowPos = $code.Count
Emit-B @(0x48, 0x89, 0xD9)             # mov rcx, rbx
Emit-LeaRIP 0x15 0x3370                # lea rdx, [rip + sErrOverflow]
Emit-B @(0x41, 0xB8); Emit-B ([BitConverter]::GetBytes([int32]$errOverflowLen))
Emit-B @(0x4C, 0x8D, 0x4C, 0x24, 0x40)
Emit-B @(0x48, 0xC7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00)
Emit-CallIAT 0x3030
Emit-B @(0xB9, 0x03, 0x00, 0x00, 0x00) # mov ecx, 3
Emit-CallIAT 0x3050

# ------------------------------------------------------------------------------
# HELPER: parse_uint
# ------------------------------------------------------------------------------
$parseUintOff = $code.Count
Emit-B @(0x31, 0xC0)                   # xor eax, eax
$loopUint = $code.Count
Emit-B @(0x0F, 0xB6, 0x0E)             # movzx ecx, byte ptr [rsi]
Emit-B @(0x80, 0xF9, 0x30)             # cmp cl, '0'
$jbUint = (Emit-JccFwd32 0x82)          # jb done
Emit-B @(0x80, 0xF9, 0x39)             # cmp cl, '9'
$jaUint = (Emit-JccFwd32 0x87)          # ja done
Emit-B @(0x80, 0xE9, 0x30)             # sub cl, '0'
Emit-B @(0x6B, 0xC0, 0x0A)             # imul eax, eax, 10
Emit-B @(0x01, 0xC8)                   # add eax, ecx
Emit-B @(0x48, 0xFF, 0xC6)             # inc rsi
Emit-JmpBack32 $loopUint
Patch-JccFwd32 $jbUint
Patch-JccFwd32 $jaUint
Emit-B @(0xC3)                         # ret

# ------------------------------------------------------------------------------
# HELPER: parse_ident_8 (reads up to 8 chars ident into r10)
# Input: rsi
# Output: r10 = 64-bit packed ident, ecx = length (0 if invalid)
# ------------------------------------------------------------------------------
$parseIdentOff = $code.Count
Emit-B @(0x4D, 0x31, 0xD2)             # xor r10, r10
Emit-B @(0x31, 0xC9)                   # xor ecx, ecx
$loopIdent = $code.Count
Emit-B @(0x8A, 0x06)                   # mov al, [rsi]
Emit-B @(0x3C, 0x5F)                   # cmp al, '_'
$jeIdentChar = (Emit-JccFwd32 0x84)
Emit-B @(0x8A, 0xD0)                   # mov dl, al
Emit-B @(0x80, 0xCA, 0x20)             # or dl, 0x20
Emit-B @(0x80, 0xFA, 0x30); $jbNotIdent = (Emit-JccFwd32 0x82) # cmp dl, '0'
Emit-B @(0x80, 0xFA, 0x39); $jbeIdentChar2 = (Emit-JccFwd32 0x86) # cmp dl, '9'
Emit-B @(0x80, 0xFA, 0x61); $jbNotIdent2 = (Emit-JccFwd32 0x82) # cmp dl, 'a'
Emit-B @(0x80, 0xFA, 0x7A); $jaNotIdent3 = (Emit-JccFwd32 0x87) # cmp dl, 'z'

Patch-JccFwd32 $jeIdentChar; Patch-JccFwd32 $jbeIdentChar2
Emit-B @(0x83, 0xF9, 0x08)             # cmp ecx, 8
$jaeSkipPack = (Emit-JccFwd32 0x83)
Emit-B @(0x49, 0xC1, 0xE2, 0x08)       # shl r10, 8
Emit-B @(0x44, 0x8A, 0x16)             # mov r10b, [rsi]
Emit-B @(0xFF, 0xC1)                   # inc ecx
Patch-JccFwd32 $jaeSkipPack
Emit-B @(0x48, 0xFF, 0xC6)             # inc rsi
Emit-JmpBack32 $loopIdent

Patch-JccFwd32 $jbNotIdent; Patch-JccFwd32 $jbNotIdent2; Patch-JccFwd32 $jaNotIdent3
Emit-B @(0xC3)                         # ret

Patch-JmpFwd32 $jmpToLoop

# ------------------------------------------------------------------------------
# 8. PARSER & CODE GENERATOR MAIN LOOP
# ------------------------------------------------------------------------------
$lineLoopOff = $code.Count

# Bounds check: rdi < peTemplateBase + 0x0E00
Emit-B @(0x48, 0x89, 0xF8)             # mov rax, rdi
Emit-B @(0x48, 0x2B, 0x44, 0x24, 0x38) # sub rax, [rsp + 0x38]
Emit-B @(0x3D, 0x00, 0x6E, 0x00, 0x00) # cmp eax, 0x6E00
$jaeCodeOv = (Emit-JccFwd32 0x83)

# Bounds check: rbp < peTemplateBase + 0x1FE0
Emit-B @(0x48, 0x89, 0xE8)             # mov rax, rbp
Emit-B @(0x48, 0x2B, 0x44, 0x24, 0x38) # sub rax, [rsp + 0x38]
Emit-B @(0x3D, 0xE0, 0x7F, 0x00, 0x00) # cmp eax, 0x7FE0
$jaeStrOv = (Emit-JccFwd32 0x83)

$jmpNoOv = (Emit-JmpFwd32)
Patch-JccFwd32 $jaeCodeOv; Patch-JccFwd32 $jaeStrOv
Emit-JmpBack32 $errOverflowPos
Patch-JmpFwd32 $jmpNoOv

# skip_ws:
$skipWsOff = $code.Count
Emit-B @(0x8A, 0x06)                   # mov al, [rsi]
Emit-B @(0x84, 0xC0)                   # test al, al
$jzEof = (Emit-JccFwd32 0x84)           # jz compile_done

Emit-B @(0x3C, 0x20); $jeSp = (Emit-JccFwd32 0x84) # cmp al, ' '
Emit-B @(0x3C, 0x09); $jeTb = (Emit-JccFwd32 0x84) # cmp al, '\t'
Emit-B @(0x3C, 0x0D); $jeCr = (Emit-JccFwd32 0x84) # cmp al, '\r'
Emit-B @(0x3C, 0x0A); $jeLf = (Emit-JccFwd32 0x84) # cmp al, '\n'
Emit-B @(0x3C, 0x23); $jeC1 = (Emit-JccFwd32 0x84) # cmp al, '#'
Emit-B @(0x3C, 0x3B); $jeC2 = (Emit-JccFwd32 0x84) # cmp al, ';'
$jmpCheckKw = (Emit-JmpFwd32)

# advance_ws:
Patch-JccFwd32 $jeSp; Patch-JccFwd32 $jeTb; Patch-JccFwd32 $jeCr; Patch-JccFwd32 $jeLf
Emit-B @(0x48, 0xFF, 0xC6)             # inc rsi
Emit-JmpBack32 $skipWsOff

# skip_comment:
Patch-JccFwd32 $jeC1; Patch-JccFwd32 $jeC2
$skipCommentOff = $code.Count
Emit-B @(0x48, 0xFF, 0xC6)             # inc rsi
Emit-B @(0x8A, 0x06)                   # mov al, [rsi]
Emit-B @(0x84, 0xC0); $jzCmtEof = (Emit-JccFwd32 0x84) # test al, al -> jz eof
Emit-B @(0x3C, 0x0A)                   # cmp al, '\n'
Emit-JccBack32 0x85 $skipCommentOff    # jne skip_comment
Emit-B @(0x48, 0xFF, 0xC6)             # inc rsi
Emit-JmpBack32 $skipWsOff

# check_keyword:
Patch-JmpFwd32 $jmpCheckKw

# --- CHECK '}' (CLOSING BRACE) ---
Emit-B @(0x8A, 0x06)                   # mov al, [rsi]
Emit-B @(0x3C, 0x7D)                   # cmp al, '}'
$jneNotBrace = (Emit-JccFwd32 0x85)
Emit-B @(0x48, 0xFF, 0xC6)             # inc rsi (skip '}')

# Pop Block Stack frame at depth - 1:
Emit-B @(0x8B, 0x4C, 0x24, 0x58)       # mov ecx, [rsp + 0x58] (block_depth)
Emit-B @(0x85, 0xC9)                   # test ecx, ecx
$jzUnmatchedBrace = (Emit-JccFwd32 0x84) # jz err_syntax (block_depth == 0!)
Emit-B @(0xFF, 0xC9)                   # dec ecx
Emit-B @(0x89, 0x4C, 0x24, 0x58)       # mov [rsp + 0x58], ecx (popped depth)

# Compute frame address: r11 = rsp + rcx*32 + 0x60
Emit-B @(0x48, 0xC1, 0xE1, 0x05)       # shl rcx, 5
Emit-B @(0x4C, 0x8D, 0x5C, 0x0C, 0x60) # lea r11, [rsp + rcx + 0x60]

# Check block_type at [r11 + 0x00]:
Emit-B @(0x41, 0x8B, 0x03)             # mov eax, [r11]
Emit-B @(0x83, 0xF8, 0x01); $jeCloseIf = (Emit-JccFwd32 0x84)    # 1 = IF
Emit-B @(0x83, 0xF8, 0x02); $jeCloseElse = (Emit-JccFwd32 0x84)  # 2 = ELSE
Emit-B @(0x83, 0xF8, 0x03); $jeCloseWhile = (Emit-JccFwd32 0x84) # 3 = WHILE
Emit-B @(0x83, 0xF8, 0x04); $jeCloseFunc = (Emit-JccFwd32 0x84)  # 4 = FUNCTION
# Unexpected block type -> syntax error
Patch-JccFwd32 $jzUnmatchedBrace
Emit-JmpBack32 $errSyntaxPos

# --- CLOSE IF BLOCK ---
Patch-JccFwd32 $jeCloseIf
# Skip whitespace to see if next token is 'else':
$skipSpAfterIf = $code.Count
Emit-B @(0x8A, 0x06)
Emit-B @(0x3C, 0x20); $jeSpA1 = (Emit-JccFwd32 0x84)
Emit-B @(0x3C, 0x09); $jeSpA2 = (Emit-JccFwd32 0x84)
Emit-B @(0x3C, 0x0D); $jeSpA3 = (Emit-JccFwd32 0x84)
Emit-B @(0x3C, 0x0A); $jeSpA4 = (Emit-JccFwd32 0x84)
$jmpCheckElseToken = (Emit-JmpFwd32)

Patch-JccFwd32 $jeSpA1; Patch-JccFwd32 $jeSpA2; Patch-JccFwd32 $jeSpA3; Patch-JccFwd32 $jeSpA4
Emit-B @(0x48, 0xFF, 0xC6)             # inc rsi
Emit-JmpBack32 $skipSpAfterIf

Patch-JmpFwd32 $jmpCheckElseToken
# Check 'e','l','s','e':
Emit-B @(0x8A, 0x06); Emit-B @(0x0C, 0x20); Emit-B @(0x3C, 0x65); $jneNotElse1 = (Emit-JccFwd32 0x85) # 'e'
Emit-B @(0x8A, 0x46, 0x01); Emit-B @(0x0C, 0x20); Emit-B @(0x3C, 0x6C); $jneNotElse2 = (Emit-JccFwd32 0x85) # 'l'
Emit-B @(0x8A, 0x46, 0x02); Emit-B @(0x0C, 0x20); Emit-B @(0x3C, 0x73); $jneNotElse3 = (Emit-JccFwd32 0x85) # 's'
Emit-B @(0x8A, 0x46, 0x03); Emit-B @(0x0C, 0x20); Emit-B @(0x3C, 0x65); $jneNotElse4 = (Emit-JccFwd32 0x85) # 'e'

# It is ELSE!
Emit-B @(0x48, 0x83, 0xC6, 0x04)       # add rsi, 4 (skip "else")
# Skip spaces until '{':
$skipSpElseBrace = $code.Count
Emit-B @(0x8A, 0x06)
Emit-B @(0x48, 0xFF, 0xC6)
Emit-B @(0x3C, 0x7B)                   # cmp al, '{'
Emit-JccBack32 0x85 $skipSpElseBrace

# Restore depth: block_depth++ (because ELSE block continues at this depth level)
Emit-B @(0xFF, 0x44, 0x24, 0x58)       # inc dword ptr [rsp + 0x58]
# Set block_type = 2 (ELSE):
Emit-B @(0x41, 0xC7, 0x03, 0x02, 0x00, 0x00, 0x00) # mov dword ptr [r11], 2

# Emit unconditional jump over ELSE block: E9 00 00 00 00
Emit-B @(0xC6, 0x07, 0xE9); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0x49, 0x89, 0x7B, 0x18)       # mov [r11 + 0x18], rdi (else_patch_disp_ptr)
Emit-B @(0x31, 0xC0)                   # xor eax, eax
Emit-B @(0x89, 0x07); Emit-B @(0x48, 0x83, 0xC7, 0x04)

# Patch the IF jump! Target = rdi
# disp = rdi - ([r11 + 0x10] + 4)
Emit-B @(0x4D, 0x8B, 0x43, 0x10)       # mov r8, [r11 + 0x10] (exit_patch_disp_ptr)
Emit-B @(0x48, 0x89, 0xF8)             # mov rax, rdi
Emit-B @(0x49, 0x2B, 0xC0)             # sub rax, r8
Emit-B @(0x83, 0xE8, 0x04)             # sub eax, 4
Emit-B @(0x41, 0x89, 0x00)             # mov [r8], eax
Emit-JmpBack32 $skipWsOff

# Not else: single IF block ending!
Patch-JccFwd32 $jneNotElse1; Patch-JccFwd32 $jneNotElse2
Patch-JccFwd32 $jneNotElse3; Patch-JccFwd32 $jneNotElse4
# Patch IF jump to point right here!
Emit-B @(0x4D, 0x8B, 0x43, 0x10)       # mov r8, [r11 + 0x10]
Emit-B @(0x48, 0x89, 0xF8)             # mov rax, rdi
Emit-B @(0x49, 0x2B, 0xC0)             # sub rax, r8
Emit-B @(0x83, 0xE8, 0x04)             # sub eax, 4
Emit-B @(0x41, 0x89, 0x00)             # mov [r8], eax
Emit-JmpBack32 $skipWsOff

# --- CLOSE ELSE BLOCK ---
Patch-JccFwd32 $jeCloseElse
# Patch ELSE jump to point right here!
Emit-B @(0x4D, 0x8B, 0x43, 0x18)       # mov r8, [r11 + 0x18] (else_patch_disp_ptr)
Emit-B @(0x48, 0x89, 0xF8)             # mov rax, rdi
Emit-B @(0x49, 0x2B, 0xC0)             # sub rax, r8
Emit-B @(0x83, 0xE8, 0x04)             # sub eax, 4
Emit-B @(0x41, 0x89, 0x00)             # mov [r8], eax
Emit-JmpBack32 $skipWsOff

# --- CLOSE WHILE BLOCK ---
Patch-JccFwd32 $jeCloseWhile
# Emit backward jump: jmp [head_ptr] -> E9 <disp32>
Emit-B @(0xC6, 0x07, 0xE9); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0x4D, 0x8B, 0x43, 0x08)       # mov r8, [r11 + 0x08] (head_ptr)
Emit-B @(0x48, 0x89, 0xF8)             # mov rax, rdi
Emit-B @(0x48, 0x83, 0xC0, 0x04)       # add rax, 4 (next RIP)
Emit-B @(0x4C, 0x89, 0xC2)             # mov rdx, r8
Emit-B @(0x48, 0x29, 0xC2)             # sub rdx, rax (rdx = disp32)
Emit-B @(0x89, 0x17); Emit-B @(0x48, 0x83, 0xC7, 0x04)

# Patch WHILE exit jump to point right here!
Emit-B @(0x4D, 0x8B, 0x43, 0x10)       # mov r8, [r11 + 0x10] (exit_patch_disp_ptr)
Emit-B @(0x48, 0x89, 0xF8)             # mov rax, rdi
Emit-B @(0x49, 0x2B, 0xC0)             # sub rax, r8
Emit-B @(0x83, 0xE8, 0x04)             # sub eax, 4
Emit-B @(0x41, 0x89, 0x00)             # mov [r8], eax
Emit-JmpBack32 $skipWsOff

# --- CLOSE FUNCTION BLOCK ---
Patch-JccFwd32 $jeCloseFunc
# Emit ret (C3):
Emit-B @(0xC6, 0x07, 0xC3); Emit-B @(0x48, 0xFF, 0xC7)
# Patch jmp <over_func> to point right after ret:
Emit-B @(0x4D, 0x8B, 0x43, 0x18)       # mov r8, [r11 + 0x18] (jmp_over_func_ptr)
Emit-B @(0x48, 0x89, 0xF8)             # mov rax, rdi
Emit-B @(0x49, 0x2B, 0xC0)             # sub rax, r8
Emit-B @(0x83, 0xE8, 0x04)             # sub eax, 4
Emit-B @(0x41, 0x89, 0x00)             # mov [r8], eax
Emit-JmpBack32 $skipWsOff

# --- CHECK LET KEYWORD ---
Patch-JccFwd32 $jneNotBrace
Emit-B @(0x8A, 0x06); Emit-B @(0x0C, 0x20); Emit-B @(0x3C, 0x6C); $jneNotLet = (Emit-JccFwd32 0x85)
Emit-B @(0x8A, 0x46, 0x01); Emit-B @(0x0C, 0x20); Emit-B @(0x3C, 0x65); $jneNotLet2 = (Emit-JccFwd32 0x85)
Emit-B @(0x8A, 0x46, 0x02); Emit-B @(0x0C, 0x20); Emit-B @(0x3C, 0x74); $jneNotLet3 = (Emit-JccFwd32 0x85)
Emit-B @(0x8A, 0x46, 0x03)
Emit-B @(0x3C, 0x20); $jeLetOk1 = (Emit-JccFwd32 0x84)
Emit-B @(0x3C, 0x09); $jeLetOk2 = (Emit-JccFwd32 0x84)
$jmpNotLet = (Emit-JmpFwd32)

Patch-JccFwd32 $jeLetOk1; Patch-JccFwd32 $jeLetOk2
$jmpParseLet = (Emit-JmpFwd32)

# --- CHECK PRINT KEYWORD ---
Patch-JccFwd32 $jneNotLet; Patch-JccFwd32 $jneNotLet2; Patch-JccFwd32 $jneNotLet3; Patch-JmpFwd32 $jmpNotLet
Emit-B @(0x8A, 0x06); Emit-B @(0x0C, 0x20); Emit-B @(0x3C, 0x70); $jneNotP1 = (Emit-JccFwd32 0x85)
Emit-B @(0x8A, 0x46, 0x01); Emit-B @(0x0C, 0x20); Emit-B @(0x3C, 0x72); $jneNotP2 = (Emit-JccFwd32 0x85)
Emit-B @(0x8A, 0x46, 0x02); Emit-B @(0x0C, 0x20); Emit-B @(0x3C, 0x69); $jneNotP3 = (Emit-JccFwd32 0x85)
Emit-B @(0x8A, 0x46, 0x03); Emit-B @(0x0C, 0x20); Emit-B @(0x3C, 0x6E); $jneNotP4 = (Emit-JccFwd32 0x85)
Emit-B @(0x8A, 0x46, 0x04); Emit-B @(0x0C, 0x20); Emit-B @(0x3C, 0x74); $jneNotP5 = (Emit-JccFwd32 0x85)
Emit-B @(0x8A, 0x46, 0x05)
Emit-B @(0x3C, 0x20); $jePrnOk1 = (Emit-JccFwd32 0x84)
Emit-B @(0x3C, 0x09); $jePrnOk2 = (Emit-JccFwd32 0x84)
Emit-B @(0x3C, 0x22); $jePrnOk3 = (Emit-JccFwd32 0x84)
$jmpNotPrn = (Emit-JmpFwd32)

Patch-JccFwd32 $jePrnOk1; Patch-JccFwd32 $jePrnOk2; Patch-JccFwd32 $jePrnOk3
$jmpParsePrint = (Emit-JmpFwd32)

# --- CHECK IF KEYWORD ---
Patch-JccFwd32 $jneNotP1; Patch-JccFwd32 $jneNotP2; Patch-JccFwd32 $jneNotP3
Patch-JccFwd32 $jneNotP4; Patch-JccFwd32 $jneNotP5; Patch-JmpFwd32 $jmpNotPrn

Emit-B @(0x8A, 0x06); Emit-B @(0x0C, 0x20); Emit-B @(0x3C, 0x69); $jneNotIf1 = (Emit-JccFwd32 0x85) # 'i'
Emit-B @(0x8A, 0x46, 0x01); Emit-B @(0x0C, 0x20); Emit-B @(0x3C, 0x66); $jneNotIf2 = (Emit-JccFwd32 0x85) # 'f'
Emit-B @(0x8A, 0x46, 0x02)
Emit-B @(0x3C, 0x20); $jeIfOk1 = (Emit-JccFwd32 0x84)
Emit-B @(0x3C, 0x09); $jeIfOk2 = (Emit-JccFwd32 0x84)
$jmpNotIf = (Emit-JmpFwd32)

Patch-JccFwd32 $jeIfOk1; Patch-JccFwd32 $jeIfOk2
$jmpParseIf = (Emit-JmpFwd32)

# --- CHECK WHILE KEYWORD ---
Patch-JccFwd32 $jneNotIf1; Patch-JccFwd32 $jneNotIf2; Patch-JmpFwd32 $jmpNotIf
Emit-B @(0x8A, 0x06); Emit-B @(0x0C, 0x20); Emit-B @(0x3C, 0x77); $jneNotWh1 = (Emit-JccFwd32 0x85) # 'w'
Emit-B @(0x8A, 0x46, 0x01); Emit-B @(0x0C, 0x20); Emit-B @(0x3C, 0x68); $jneNotWh2 = (Emit-JccFwd32 0x85) # 'h'
Emit-B @(0x8A, 0x46, 0x02); Emit-B @(0x0C, 0x20); Emit-B @(0x3C, 0x69); $jneNotWh3 = (Emit-JccFwd32 0x85) # 'i'
Emit-B @(0x8A, 0x46, 0x03); Emit-B @(0x0C, 0x20); Emit-B @(0x3C, 0x6C); $jneNotWh4 = (Emit-JccFwd32 0x85) # 'l'
Emit-B @(0x8A, 0x46, 0x04); Emit-B @(0x0C, 0x20); Emit-B @(0x3C, 0x65); $jneNotWh5 = (Emit-JccFwd32 0x85) # 'e'
Emit-B @(0x8A, 0x46, 0x05)
Emit-B @(0x3C, 0x20); $jeWhOk1 = (Emit-JccFwd32 0x84)
Emit-B @(0x3C, 0x09); $jeWhOk2 = (Emit-JccFwd32 0x84)
$jmpNotWh = (Emit-JmpFwd32)

Patch-JccFwd32 $jeWhOk1; Patch-JccFwd32 $jeWhOk2
$jmpParseWhile = (Emit-JmpFwd32)

# --- CHECK FN OR FILE STATEMENT (FIRST LETTER 'F') ---
Patch-JccFwd32 $jneNotWh1; Patch-JccFwd32 $jneNotWh2; Patch-JccFwd32 $jneNotWh3
Patch-JccFwd32 $jneNotWh4; Patch-JccFwd32 $jneNotWh5; Patch-JmpFwd32 $jmpNotWh

Emit-B @(0x8A, 0x06); Emit-B @(0x0C, 0x20); Emit-B @(0x3C, 0x66); $jneNotFWord = (Emit-JccFwd32 0x85) # 'f'

# Check if 2nd char is 'n':
Emit-B @(0x8A, 0x46, 0x01); Emit-B @(0x0C, 0x20); Emit-B @(0x3C, 0x6E); $jneNotFn2 = (Emit-JccFwd32 0x85) # 'n'
Emit-B @(0x8A, 0x46, 0x02)
Emit-B @(0x3C, 0x20); $jeFnOk1 = (Emit-JccFwd32 0x84)
Emit-B @(0x3C, 0x09); $jeFnOk2 = (Emit-JccFwd32 0x84)
$jmpNotFn = (Emit-JmpFwd32)

Patch-JccFwd32 $jeFnOk1; Patch-JccFwd32 $jeFnOk2
$jmpParseFn = (Emit-JmpFwd32)

# 2nd char was not 'n'. Check if it is 'file_write' or 'file_close':
Patch-JccFwd32 $jneNotFn2; Patch-JmpFwd32 $jmpNotFn
Emit-B @(0x8A, 0x46, 0x01); Emit-B @(0x0C, 0x20); Emit-B @(0x3C, 0x69); $jneNotFileStmt = (Emit-JccFwd32 0x85) # 'i'
Emit-B @(0x8A, 0x46, 0x02); Emit-B @(0x0C, 0x20); Emit-B @(0x3C, 0x6C); $jneNotFileStmt2 = (Emit-JccFwd32 0x85) # 'l'
Emit-B @(0x8A, 0x46, 0x03); Emit-B @(0x0C, 0x20); Emit-B @(0x3C, 0x65); $jneNotFileStmt3 = (Emit-JccFwd32 0x85) # 'e'
Emit-B @(0x8A, 0x46, 0x04); Emit-B @(0x3C, 0x5F); $jneNotFileStmt4 = (Emit-JccFwd32 0x85) # '_'

# Check 6th char: 'w' (write) or 'c' (close)
Emit-B @(0x8A, 0x46, 0x05); Emit-B @(0x0C, 0x20)
Emit-B @(0x3C, 0x77); $jeFileWriteStmt = (Emit-JccFwd32 0x84) # 'w'
Emit-B @(0x3C, 0x63); $jeFileCloseStmt = (Emit-JccFwd32 0x84) # 'c'
$jmpNotFileStmt5 = (Emit-JmpFwd32)

# --- PARSE file_write fd buf len ---
Patch-JccFwd32 $jeFileWriteStmt
Emit-B @(0x48, 0x83, 0xC6, 0x0A) # skip "file_write"
Emit-B @(0xC6, 0x44, 0x24, 0x53, 0x00) # file_rw_mode = 0 (WRITE)
$jmpDoFileRwFromStmt = (Emit-JmpFwd32)

# --- PARSE file_close fd ---
Patch-JccFwd32 $jeFileCloseStmt
Emit-B @(0x48, 0x83, 0xC6, 0x0A) # skip "file_close"
$skipSpFc = $code.Count
Emit-B @(0x8A, 0x06); Emit-B @(0x3C, 0x20); $jneGotFc = (Emit-JccFwd32 0x85)
Emit-B @(0x48, 0xFF, 0xC6); Emit-JmpBack32 $skipSpFc
Patch-JccFwd32 $jneGotFc
Emit-B @(0x48, 0xFF, 0xC6); Emit-B @(0x0C, 0x20); Emit-B @(0x2C, 0x61)
Emit-B @(0x0F, 0xB6, 0xC8); Emit-B @(0xC1, 0xE1, 0x03); Emit-B @(0x83, 0xC1, 0x30)
# Emit: mov rcx, [rbp + fd_slot32] (48 8B 8D <disp32>)
Emit-B @(0xC6, 0x07, 0x48); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x8B); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x8D); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0x89, 0x0F); Emit-B @(0x48, 0x83, 0xC7, 0x04)
# Emit: call CloseHandle (FF 15 <disp32>) -> IAT 0x2058
Emit-B @(0x48, 0x89, 0xF8); Emit-B @(0x48, 0x2B, 0x44, 0x24, 0x38)
Emit-B @(0x05, 0x06, 0x0E, 0x00, 0x00)
Emit-B @(0xBA, 0x58, 0x80, 0x00, 0x00) # IAT 0x8058
Emit-B @(0x29, 0xC2)
Emit-B @(0xC6, 0x07, 0xFF); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x15); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0x89, 0x17); Emit-B @(0x48, 0x83, 0xC7, 0x04)
$jmpFcDone = (Emit-JmpFwd32)

# --- CHECK CALL KEYWORD ---
Patch-JccFwd32 $jneNotFWord; Patch-JccFwd32 $jneNotFileStmt; Patch-JccFwd32 $jneNotFileStmt2
Patch-JccFwd32 $jneNotFileStmt3; Patch-JccFwd32 $jneNotFileStmt4; Patch-JmpFwd32 $jmpNotFileStmt5

Emit-B @(0x8A, 0x06); Emit-B @(0x0C, 0x20); Emit-B @(0x3C, 0x63); $jneNotCall1 = (Emit-JccFwd32 0x85) # 'c'
Emit-B @(0x8A, 0x46, 0x01); Emit-B @(0x0C, 0x20); Emit-B @(0x3C, 0x61); $jneNotCall2 = (Emit-JccFwd32 0x85) # 'a'
Emit-B @(0x8A, 0x46, 0x02); Emit-B @(0x0C, 0x20); Emit-B @(0x3C, 0x6C); $jneNotCall3 = (Emit-JccFwd32 0x85) # 'l'
Emit-B @(0x8A, 0x46, 0x03); Emit-B @(0x0C, 0x20); Emit-B @(0x3C, 0x6C); $jneNotCall4 = (Emit-JccFwd32 0x85) # 'l'
Emit-B @(0x8A, 0x46, 0x04)
Emit-B @(0x3C, 0x20); $jeCallOk1 = (Emit-JccFwd32 0x84)
Emit-B @(0x3C, 0x09); $jeCallOk2 = (Emit-JccFwd32 0x84)
$jmpNotCall = (Emit-JmpFwd32)

Patch-JccFwd32 $jeCallOk1; Patch-JccFwd32 $jeCallOk2
$jmpParseCall = (Emit-JmpFwd32)

# --- CHECK RETURN OR READ (FIRST LETTER 'R') ---
Patch-JccFwd32 $jneNotCall1; Patch-JccFwd32 $jneNotCall2; Patch-JccFwd32 $jneNotCall3
Patch-JccFwd32 $jneNotCall4; Patch-JmpFwd32 $jmpNotCall

Emit-B @(0x8A, 0x06); Emit-B @(0x0C, 0x20); Emit-B @(0x3C, 0x72); $jneNotR = (Emit-JccFwd32 0x85) # 'r'
Emit-B @(0x8A, 0x46, 0x01); Emit-B @(0x0C, 0x20); Emit-B @(0x3C, 0x65); $jneNotRe = (Emit-JccFwd32 0x85) # 'e'

# Check 3rd char: 't' (return) or 'a' (read)
Emit-B @(0x8A, 0x46, 0x02); Emit-B @(0x0C, 0x20)
Emit-B @(0x3C, 0x74); $jeCheckRet = (Emit-JccFwd32 0x84) # 't'
Emit-B @(0x3C, 0x61); $jeCheckRead = (Emit-JccFwd32 0x84) # 'a'
$jmpNotRWords = (Emit-JmpFwd32)

# Check 'return':
Patch-JccFwd32 $jeCheckRet
Emit-B @(0x8A, 0x46, 0x03); Emit-B @(0x0C, 0x20); Emit-B @(0x3C, 0x75); $jneNotRet1 = (Emit-JccFwd32 0x85) # 'u'
Emit-B @(0x8A, 0x46, 0x04); Emit-B @(0x0C, 0x20); Emit-B @(0x3C, 0x72); $jneNotRet2 = (Emit-JccFwd32 0x85) # 'r'
Emit-B @(0x8A, 0x46, 0x05); Emit-B @(0x0C, 0x20); Emit-B @(0x3C, 0x6E); $jneNotRet3 = (Emit-JccFwd32 0x85) # 'n'
$jmpParseReturn = (Emit-JmpFwd32)

# Check 'read':
Patch-JccFwd32 $jeCheckRead
Emit-B @(0x8A, 0x46, 0x03); Emit-B @(0x0C, 0x20); Emit-B @(0x3C, 0x64); $jneNotRead1 = (Emit-JccFwd32 0x85) # 'd'
Emit-B @(0x8A, 0x46, 0x04)
Emit-B @(0x3C, 0x20); $jeReadOk1 = (Emit-JccFwd32 0x84)
Emit-B @(0x3C, 0x09); $jeReadOk2 = (Emit-JccFwd32 0x84)
$jmpNotRead = (Emit-JmpFwd32)

Patch-JccFwd32 $jeReadOk1; Patch-JccFwd32 $jeReadOk2
$jmpParseRead = (Emit-JmpFwd32)

# --- CHECK STORE OR STORE64 (FIRST LETTER 'S') ---
Patch-JccFwd32 $jneNotR; Patch-JccFwd32 $jneNotRe; Patch-JmpFwd32 $jmpNotRWords
Patch-JccFwd32 $jneNotRet1; Patch-JccFwd32 $jneNotRet2; Patch-JccFwd32 $jneNotRet3
Patch-JccFwd32 $jneNotRead1; Patch-JmpFwd32 $jmpNotRead

Emit-B @(0x8A, 0x06); Emit-B @(0x0C, 0x20); Emit-B @(0x3C, 0x73); $jneNotStore1 = (Emit-JccFwd32 0x85) # 's'
Emit-B @(0x8A, 0x46, 0x01); Emit-B @(0x0C, 0x20); Emit-B @(0x3C, 0x74); $jneNotStore2 = (Emit-JccFwd32 0x85) # 't'
Emit-B @(0x8A, 0x46, 0x02); Emit-B @(0x0C, 0x20); Emit-B @(0x3C, 0x6F); $jneNotStore3 = (Emit-JccFwd32 0x85) # 'o'
Emit-B @(0x8A, 0x46, 0x03); Emit-B @(0x0C, 0x20); Emit-B @(0x3C, 0x72); $jneNotStore4 = (Emit-JccFwd32 0x85) # 'r'
Emit-B @(0x8A, 0x46, 0x04); Emit-B @(0x0C, 0x20); Emit-B @(0x3C, 0x65); $jneNotStore5 = (Emit-JccFwd32 0x85) # 'e'

# Check if followed by '6','4' (store64):
Emit-B @(0x8A, 0x46, 0x05); Emit-B @(0x3C, 0x36); $jneNotStore64 = (Emit-JccFwd32 0x85)
Emit-B @(0x8A, 0x46, 0x06); Emit-B @(0x3C, 0x34); $jneNotStore64b = (Emit-JccFwd32 0x85)

# store64:
Emit-B @(0xC6, 0x44, 0x24, 0x51, 0x01) # is64 = 1
Emit-B @(0x48, 0x83, 0xC6, 0x07)       # skip "store64"
$jmpParseStoreInner = (Emit-JmpFwd32)

Patch-JccFwd32 $jneNotStore64; Patch-JccFwd32 $jneNotStore64b
# store (byte):
Emit-B @(0xC6, 0x44, 0x24, 0x51, 0x00) # is64 = 0
Emit-B @(0x48, 0x83, 0xC6, 0x05)       # skip "store"

Patch-JmpFwd32 $jmpParseStoreInner
Emit-B @(0xC6, 0x44, 0x24, 0x52, 0x01) # mem_mode = 1 (STORE)
$jmpDoMemFromStore = (Emit-JmpFwd32)

# None matched: syntax error!
Patch-JccFwd32 $jneNotStore1; Patch-JccFwd32 $jneNotStore2; Patch-JccFwd32 $jneNotStore3
Patch-JccFwd32 $jneNotStore4; Patch-JccFwd32 $jneNotStore5
Emit-JmpBack32 $errSyntaxPos

# ==============================================================================
# PARSE IF & WHILE (SHARED CONDITION EVALUATOR + BLOCK STACK PUSH)
# ==============================================================================
Patch-JmpFwd32 $jmpParseIf
Emit-B @(0x48, 0x83, 0xC6, 0x03)       # add rsi, 3 (skip "if ")

# Push Block Stack frame:
Emit-B @(0x8B, 0x4C, 0x24, 0x58)       # mov ecx, [rsp + 0x58] (block_depth)
Emit-B @(0x83, 0xF9, 0x0F); $jaeDepthOv1 = (Emit-JccFwd32 0x83) # max 15
Emit-B @(0x48, 0xC1, 0xE1, 0x05)       # shl rcx, 5
Emit-B @(0x4C, 0x8D, 0x5C, 0x0C, 0x60) # lea r11, [rsp + rcx + 0x60]
Emit-B @(0x41, 0xC7, 0x03, 0x01, 0x00, 0x00, 0x00) # mov dword ptr [r11], 1 (IF)
$jmpCondCommon = (Emit-JmpFwd32)

Patch-JmpFwd32 $jmpParseWhile
Emit-B @(0x48, 0x83, 0xC6, 0x06)       # add rsi, 6 (skip "while ")

# Push Block Stack frame:
Emit-B @(0x8B, 0x4C, 0x24, 0x58)       # mov ecx, [rsp + 0x58] (block_depth)
Emit-B @(0x83, 0xF9, 0x0F); $jaeDepthOv2 = (Emit-JccFwd32 0x83)
Emit-B @(0x48, 0xC1, 0xE1, 0x05)       # shl rcx, 5
Emit-B @(0x4C, 0x8D, 0x5C, 0x0C, 0x60) # lea r11, [rsp + rcx + 0x60]
Emit-B @(0x41, 0xC7, 0x03, 0x03, 0x00, 0x00, 0x00) # mov dword ptr [r11], 3 (WHILE)
Emit-B @(0x49, 0x89, 0x7B, 0x08)       # mov [r11 + 0x08], rdi (head_ptr)

Patch-JmpFwd32 $jmpCondCommon
# 1. Skip spaces before Left Operand
$skipSpCond1 = $code.Count
Emit-B @(0x8A, 0x06)                   # mov al, [rsi]
Emit-B @(0x3C, 0x20)                   # cmp al, ' '
$jneGotCondOp1 = (Emit-JccFwd32 0x85)
Emit-B @(0x48, 0xFF, 0xC6)             # inc rsi
Emit-JmpBack32 $skipSpCond1

Patch-JccFwd32 $jneGotCondOp1
# Parse Left Operand -> emit into RAX:
Emit-B @(0x3C, 0x30); $jbCondOp1Var = (Emit-JccFwd32 0x82)
Emit-B @(0x3C, 0x39); $jaCondOp1Var = (Emit-JccFwd32 0x87)
# Op1 is number:
$dispCall = $parseUintOff - ($code.Count + 5)
Emit-B @(0xE8); Emit-B ([BitConverter]::GetBytes([int32]$dispCall))
# Emit: mov rax, imm32 (48 C7 C0 <imm32>)
Emit-B @(0xC6, 0x07, 0x48); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0xC7); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0xC0); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0x89, 0x07); Emit-B @(0x48, 0x83, 0xC7, 0x04)
$jmpAfterCondOp1 = (Emit-JmpFwd32)

# Op1 is variable:
Patch-JccFwd32 $jbCondOp1Var; Patch-JccFwd32 $jaCondOp1Var
Emit-B @(0x48, 0xFF, 0xC6)             # inc rsi
Emit-B @(0x0C, 0x20)                   # or al, 0x20
# validate:
Emit-B @(0x3C, 0x61); $jbVarBadC1 = (Emit-JccFwd32 0x82)
Emit-B @(0x3C, 0x7A); $jaVarBadC1 = (Emit-JccFwd32 0x87)
$jmpVarOkC1 = (Emit-JmpFwd32)
Patch-JccFwd32 $jbVarBadC1; Patch-JccFwd32 $jaVarBadC1
Patch-JccFwd32 $jaeDepthOv1; Patch-JccFwd32 $jaeDepthOv2
Emit-JmpBack32 $errSyntaxPos
Patch-JmpFwd32 $jmpVarOkC1

Emit-B @(0x2C, 0x61)                   # sub al, 'a'
Emit-B @(0x0F, 0xB6, 0xC8)             # movzx ecx, al
Emit-B @(0xC1, 0xE1, 0x03)             # shl ecx, 3
Emit-B @(0x83, 0xC1, 0x30)             # add ecx, 0x30
# Emit: mov rax, [rbp + slot32] (48 8B 85 <disp32>)
Emit-B @(0xC6, 0x07, 0x48); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x8B); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x85); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0x89, 0x0F); Emit-B @(0x48, 0x83, 0xC7, 0x04)

Patch-JmpFwd32 $jmpAfterCondOp1
# 2. Skip spaces before Relational Operator
$skipSpRel = $code.Count
Emit-B @(0x8A, 0x06)
Emit-B @(0x3C, 0x20)
$jneGotRel = (Emit-JccFwd32 0x85)
Emit-B @(0x48, 0xFF, 0xC6)
Emit-JmpBack32 $skipSpRel

Patch-JccFwd32 $jneGotRel
# Parse operator: '==', '!=', '<=', '<', '>=', '>'
Emit-B @(0x3C, 0x3D); $jeEqEq = (Emit-JccFwd32 0x84)  # '='
Emit-B @(0x3C, 0x21); $jeNotEq = (Emit-JccFwd32 0x84) # '!'
Emit-B @(0x3C, 0x3C); $jeLt = (Emit-JccFwd32 0x84)    # '<'
Emit-B @(0x3C, 0x3E); $jeGt = (Emit-JccFwd32 0x84)    # '>'
# Invalid relop:
Emit-JmpBack32 $errSyntaxPos

# ==
Patch-JccFwd32 $jeEqEq
Emit-B @(0x48, 0x83, 0xC6, 0x02)       # skip "=="
Emit-B @(0x41, 0xB2, 0x85)             # mov r10b, 0x85 (jne)
$jmpParseRightOp = (Emit-JmpFwd32)

# !=
Patch-JccFwd32 $jeNotEq
Emit-B @(0x48, 0x83, 0xC6, 0x02)       # skip "!="
Emit-B @(0x41, 0xB2, 0x84)             # mov r10b, 0x84 (je)
$jmpParseRightOp2 = (Emit-JmpFwd32)

# < or <=
Patch-JccFwd32 $jeLt
Emit-B @(0x48, 0xFF, 0xC6)             # skip '<'
Emit-B @(0x8A, 0x06); Emit-B @(0x3C, 0x3D); $jeLtEq = (Emit-JccFwd32 0x84) # '<='
Emit-B @(0x41, 0xB2, 0x8D)             # mov r10b, 0x8D (jge)
$jmpParseRightOp3 = (Emit-JmpFwd32)
Patch-JccFwd32 $jeLtEq
Emit-B @(0x48, 0xFF, 0xC6)             # skip '='
Emit-B @(0x41, 0xB2, 0x8F)             # mov r10b, 0x8F (jg)
$jmpParseRightOp4 = (Emit-JmpFwd32)

# > or >=
Patch-JccFwd32 $jeGt
Emit-B @(0x48, 0xFF, 0xC6)             # skip '>'
Emit-B @(0x8A, 0x06); Emit-B @(0x3C, 0x3D); $jeGtEq = (Emit-JccFwd32 0x84) # '>='
Emit-B @(0x41, 0xB2, 0x8E)             # mov r10b, 0x8E (jle)
$jmpParseRightOp5 = (Emit-JmpFwd32)
Patch-JccFwd32 $jeGtEq
Emit-B @(0x48, 0xFF, 0xC6)             # skip '='
Emit-B @(0x41, 0xB2, 0x8C)             # mov r10b, 0x8C (jl)

Patch-JmpFwd32 $jmpParseRightOp; Patch-JmpFwd32 $jmpParseRightOp2
Patch-JmpFwd32 $jmpParseRightOp3; Patch-JmpFwd32 $jmpParseRightOp4; Patch-JmpFwd32 $jmpParseRightOp5

# 3. Skip spaces before Right Operand
$skipSpCond2 = $code.Count
Emit-B @(0x8A, 0x06)
Emit-B @(0x3C, 0x20)
$jneGotCondOp2 = (Emit-JccFwd32 0x85)
Emit-B @(0x48, 0xFF, 0xC6)
Emit-JmpBack32 $skipSpCond2

Patch-JccFwd32 $jneGotCondOp2
# Parse Right Operand -> emit into RBX:
Emit-B @(0x3C, 0x30); $jbCondOp2Var = (Emit-JccFwd32 0x82)
Emit-B @(0x3C, 0x39); $jaCondOp2Var = (Emit-JccFwd32 0x87)

# Op2 is number:
$dispCall = $parseUintOff - ($code.Count + 5)
Emit-B @(0xE8); Emit-B ([BitConverter]::GetBytes([int32]$dispCall))
# Emit: mov rbx, imm32 (48 C7 C3 <imm32>)
Emit-B @(0xC6, 0x07, 0x48); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0xC7); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0xC3); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0x89, 0x07); Emit-B @(0x48, 0x83, 0xC7, 0x04)
$jmpAfterCondOp2 = (Emit-JmpFwd32)

# Op2 is variable:
Patch-JccFwd32 $jbCondOp2Var; Patch-JccFwd32 $jaCondOp2Var
Emit-B @(0x48, 0xFF, 0xC6)             # inc rsi
Emit-B @(0x0C, 0x20)                   # or al, 0x20
Emit-B @(0x3C, 0x61); $jbVarBadC2 = (Emit-JccFwd32 0x82)
Emit-B @(0x3C, 0x7A); $jaVarBadC2 = (Emit-JccFwd32 0x87)
$jmpVarOkC2 = (Emit-JmpFwd32)
Patch-JccFwd32 $jbVarBadC2; Patch-JccFwd32 $jaVarBadC2
Emit-JmpBack32 $errSyntaxPos
Patch-JmpFwd32 $jmpVarOkC2

Emit-B @(0x2C, 0x61)                   # sub al, 'a'
Emit-B @(0x0F, 0xB6, 0xC8)             # movzx ecx, al
Emit-B @(0xC1, 0xE1, 0x03)             # shl ecx, 3
Emit-B @(0x83, 0xC1, 0x30)             # add ecx, 0x30
# Emit: mov rbx, [rbp + slot32] (48 8B 9D <disp32>)
Emit-B @(0xC6, 0x07, 0x48); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x8B); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x9D); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0x89, 0x0F); Emit-B @(0x48, 0x83, 0xC7, 0x04)

Patch-JmpFwd32 $jmpAfterCondOp2
# Skip spaces until '{':
$skipSpBrace = $code.Count
Emit-B @(0x8A, 0x06)
Emit-B @(0x48, 0xFF, 0xC6)
Emit-B @(0x3C, 0x7B)                   # cmp al, '{'
Emit-JccBack32 0x85 $skipSpBrace

# Emit: cmp rax, rbx (48 39 D8)
Emit-B @(0xC6, 0x07, 0x48); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x39); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0xD8); Emit-B @(0x48, 0xFF, 0xC7)

# Emit: 0F <r10b> 00 00 00 00
Emit-B @(0xC6, 0x07, 0x0F); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0x44, 0x88, 0x17); Emit-B @(0x48, 0xFF, 0xC7) # mov [rdi], r10b
Emit-B @(0x31, 0xC0)                                     # placeholder 0
Emit-B @(0x89, 0x07)                                     # mov [rdi], 0

# Save exit_patch_disp_ptr into [r11 + 0x10]:
Emit-B @(0x8B, 0x4C, 0x24, 0x58)       # mov ecx, [rsp + 0x58] (block_depth)
Emit-B @(0x48, 0xC1, 0xE1, 0x05)       # shl rcx, 5
Emit-B @(0x4C, 0x8D, 0x5C, 0x0C, 0x60) # lea r11, [rsp + rcx + 0x60]
Emit-B @(0x49, 0x89, 0x7B, 0x10)       # mov [r11 + 0x10], rdi (exit_patch_disp_ptr)

# Increment block_depth:
Emit-B @(0xFF, 0x44, 0x24, 0x58)       # inc dword ptr [rsp + 0x58]

Emit-B @(0x48, 0x83, 0xC7, 0x04)       # add rdi, 4
Emit-JmpBack32 $skipWsOff

# ==============================================================================
# PARSE FN: fn <name> { ... }
# ==============================================================================
Patch-JmpFwd32 $jmpParseFn
Emit-B @(0x48, 0x83, 0xC6, 0x02)       # add rsi, 2 (skip "fn")

# Skip spaces before function name:
$skipSpFn = $code.Count
Emit-B @(0x8A, 0x06)
Emit-B @(0x3C, 0x20); $jneGotFnName = (Emit-JccFwd32 0x85)
Emit-B @(0x48, 0xFF, 0xC6)
Emit-JmpBack32 $skipSpFn

Patch-JccFwd32 $jneGotFnName
# Parse function name into r10 (up to 8 chars):
$dispCallIdent = $parseIdentOff - ($code.Count + 5)
Emit-B @(0xE8); Emit-B ([BitConverter]::GetBytes([int32]$dispCallIdent))
Emit-B @(0x85, 0xC9)                   # test ecx, ecx
$jzFnNameErr = (Emit-JccFwd32 0x84)     # empty name -> syntax error!

# Skip spaces until '{':
$skipSpFnBrace = $code.Count
Emit-B @(0x8A, 0x06)
Emit-B @(0x48, 0xFF, 0xC6)
Emit-B @(0x3C, 0x7B)                   # cmp al, '{'
Emit-JccBack32 0x85 $skipSpFnBrace

# Emit unconditional jump over function: E9 00 00 00 00
Emit-B @(0xC6, 0x07, 0xE9); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0x49, 0x89, 0xF9)             # mov r9, rdi (r9 = jmp_over_disp_ptr)
Emit-B @(0x31, 0xC0)                   # xor eax, eax
Emit-B @(0x89, 0x07); Emit-B @(0x48, 0x83, 0xC7, 0x04)

# Function body start RVA in app.exe:
# func_rva = (rdi - peTemplateBase) + 0x0E00
Emit-B @(0x48, 0x89, 0xF8)             # mov rax, rdi
Emit-B @(0x48, 0x2B, 0x44, 0x24, 0x38) # sub rax, [rsp + 0x38]
Emit-B @(0x05, 0x00, 0x0E, 0x00, 0x00) # add eax, 0x0E00 (func_rva)

# Register in symbol table at RVA 0x6000:
Emit-B @(0x8B, 0x54, 0x24, 0x5C)       # mov edx, [rsp + 0x5C] (fn_count)
Emit-B @(0x83, 0xFA, 0x20); $jaeFnCountOv = (Emit-JccFwd32 0x83) # max 32 functions
Emit-LeaRIP 0x05 0xC000                # lea rax, [rip + 0xC000]
Emit-B @(0x48, 0x89, 0xC2)             # mov rdx, rax
Emit-B @(0x8B, 0x44, 0x24, 0x5C)       # mov eax, [rsp + 0x5C]
Emit-B @(0x48, 0xC1, 0xE0, 0x04)       # shl rax, 4 (* 16)
Emit-B @(0x48, 0x01, 0xC2)             # add rdx, rax (rdx = entry address)
Emit-B @(0x4C, 0x89, 0x12)             # mov [rdx], r10 (packed name)

# Save func_rva to [rdx + 8]:
Emit-B @(0x48, 0x89, 0xF8)             # mov rax, rdi
Emit-B @(0x48, 0x2B, 0x44, 0x24, 0x38) # sub rax, [rsp + 0x38]
Emit-B @(0x05, 0x00, 0x0E, 0x00, 0x00) # add eax, 0x0E00 (func_rva)
Emit-B @(0x89, 0x42, 0x08)             # mov [rdx + 8], eax
Emit-B @(0xFF, 0x44, 0x24, 0x5C)       # inc dword ptr [rsp + 0x5C] (fn_count++)

# Push FUNCTION frame to Block Stack:
Emit-B @(0x8B, 0x4C, 0x24, 0x58)       # mov ecx, [rsp + 0x58] (block_depth)
Emit-B @(0x83, 0xF9, 0x0F); $jaeDepthOv3 = (Emit-JccFwd32 0x83)
Emit-B @(0x48, 0xC1, 0xE1, 0x05)       # shl rcx, 5
Emit-B @(0x4C, 0x8D, 0x5C, 0x0C, 0x60) # lea r11, [rsp + rcx + 0x60]
Emit-B @(0x41, 0xC7, 0x03, 0x04, 0x00, 0x00, 0x00) # mov dword ptr [r11], 4 (FUNCTION)
Emit-B @(0x4D, 0x89, 0x4B, 0x18)       # mov [r11 + 0x18], r9 (jmp_over_disp_ptr)
Emit-B @(0xFF, 0x44, 0x24, 0x58)       # inc dword ptr [rsp + 0x58] (block_depth++)
Emit-JmpBack32 $skipWsOff

Patch-JccFwd32 $jzFnNameErr; Patch-JccFwd32 $jaeFnCountOv; Patch-JccFwd32 $jaeDepthOv3
Emit-JmpBack32 $errSyntaxPos

# ==============================================================================
# PARSE CALL: call <name>
# ==============================================================================
Patch-JmpFwd32 $jmpParseCall
Emit-B @(0x48, 0x83, 0xC6, 0x04)       # add rsi, 4 (skip "call")

# Skip spaces before function name:
$skipSpCall = $code.Count
Emit-B @(0x8A, 0x06)
Emit-B @(0x3C, 0x20); $jneGotCallName = (Emit-JccFwd32 0x85)
Emit-B @(0x48, 0xFF, 0xC6)
Emit-JmpBack32 $skipSpCall

Patch-JccFwd32 $jneGotCallName
# Parse function name into r10:
$dispCallIdent2 = $parseIdentOff - ($code.Count + 5)
Emit-B @(0xE8); Emit-B ([BitConverter]::GetBytes([int32]$dispCallIdent2))
Emit-B @(0x85, 0xC9); $jzCallNameErr = (Emit-JccFwd32 0x84)

# Look up r10 in symbol table at RVA 0x6000:
Emit-LeaRIP 0x05 0xC000                # lea rax, [rip + 0xC000]
Emit-B @(0x48, 0x89, 0xC2)             # mov rdx, rax
Emit-B @(0x8B, 0x4C, 0x24, 0x5C)       # mov ecx, [rsp + 0x5C] (fn_count)
Emit-B @(0x31, 0xC0)                   # xor eax, eax (index = 0)

$loopFindFn = $code.Count
Emit-B @(0x39, 0xC8); $jaeFnNotFound = (Emit-JccFwd32 0x83) # if index >= fn_count -> not found!
Emit-B @(0x4C, 0x39, 0x12); $jeFoundFn = (Emit-JccFwd32 0x84) # cmp [rdx], r10 -> found!
Emit-B @(0x48, 0x83, 0xC2, 0x10)       # add rdx, 16
Emit-B @(0xFF, 0xC0)                   # inc eax
Emit-JmpBack32 $loopFindFn

Patch-JccFwd32 $jzCallNameErr; Patch-JccFwd32 $jaeFnNotFound
Emit-JmpBack32 $errSyntaxPos

Patch-JccFwd32 $jeFoundFn
# Function found! Target func_rva is at [rdx + 8]:
Emit-B @(0x8B, 0x42, 0x08)             # mov eax, [rdx + 8] (func_rva)

# Emit call <disp32>: E8 <disp32>
# Next RIP = (rdi - peTemplateBase) + 0x0E05
# disp32 = func_rva - Next RIP
Emit-B @(0x48, 0x89, 0xF9)             # mov rcx, rdi
Emit-B @(0x48, 0x2B, 0x4C, 0x24, 0x38) # sub rcx, [rsp + 0x38]
Emit-B @(0x48, 0x81, 0xC1, 0x05, 0x0E, 0x00, 0x00) # add rcx, 0x0E05 (Next RIP)
Emit-B @(0x29, 0xC8)                   # sub eax, ecx (eax = disp32)

Emit-B @(0xC6, 0x07, 0xE8); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0x89, 0x07); Emit-B @(0x48, 0x83, 0xC7, 0x04)

# Skip to end of statement / line:
$skipEolCall = $code.Count
Emit-B @(0x8A, 0x06)
Emit-B @(0x84, 0xC0); $jzCallEof = (Emit-JccFwd32 0x84)
Emit-B @(0x3C, 0x0A); $jeCallLf = (Emit-JccFwd32 0x84)
Emit-B @(0x48, 0xFF, 0xC6)
Emit-JmpBack32 $skipEolCall

Patch-JccFwd32 $jeCallLf
Emit-B @(0x48, 0xFF, 0xC6)
Emit-JmpBack32 $skipWsOff

# ==============================================================================
# PARSE RETURN: return
# ==============================================================================
Patch-JmpFwd32 $jmpParseReturn
Emit-B @(0x48, 0x83, 0xC6, 0x06)       # add rsi, 6 (skip "return")

# Emit ret (C3):
Emit-B @(0xC6, 0x07, 0xC3); Emit-B @(0x48, 0xFF, 0xC7)

# Skip to end of line:
$skipEolRet = $code.Count
Emit-B @(0x8A, 0x06)
Emit-B @(0x84, 0xC0); $jzRetEof = (Emit-JccFwd32 0x84)
Emit-B @(0x3C, 0x0A); $jeRetLf = (Emit-JccFwd32 0x84)
Emit-B @(0x48, 0xFF, 0xC6)
Emit-JmpBack32 $skipEolRet

Patch-JccFwd32 $jeRetLf
Emit-B @(0x48, 0xFF, 0xC6)
Emit-JmpBack32 $skipWsOff

# ==============================================================================
# PARSE READ: read <var>
# ==============================================================================
Patch-JmpFwd32 $jmpParseRead
Emit-B @(0x48, 0x83, 0xC6, 0x04)       # add rsi, 4 (skip "read")

# Skip spaces before var:
$skipSpRead = $code.Count
Emit-B @(0x8A, 0x06)
Emit-B @(0x3C, 0x20); $jneGotReadVar = (Emit-JccFwd32 0x85)
Emit-B @(0x48, 0xFF, 0xC6)
Emit-JmpBack32 $skipSpRead

Patch-JccFwd32 $jneGotReadVar
Emit-B @(0x48, 0xFF, 0xC6)             # inc rsi
Emit-B @(0x0C, 0x20)                   # or al, 0x20
Emit-B @(0x3C, 0x61); $jbReadBad = (Emit-JccFwd32 0x82)
Emit-B @(0x3C, 0x7A); $jaReadBad = (Emit-JccFwd32 0x87)
$jmpReadOk = (Emit-JmpFwd32)
Patch-JccFwd32 $jbReadBad; Patch-JccFwd32 $jaReadBad
Emit-JmpBack32 $errSyntaxPos

Patch-JmpFwd32 $jmpReadOk
Emit-B @(0x2C, 0x61)                   # sub al, 'a'
Emit-B @(0x0F, 0xB6, 0xC0)             # movzx eax, al
Emit-B @(0xC1, 0xE0, 0x03)             # shl eax, 3 (* 8)
Emit-B @(0x83, 0xC0, 0x30)             # add eax, 0x30 (slot)
Emit-B @(0x89, 0x44, 0x24, 0x48)       # mov [rsp + 0x48], eax (save slot)

# Emit call read_int (at RVA 0x1D00 in app.exe):
# Next RIP = (rdi - peTemplateBase) + 0x0E05
# disp32 = 0x1D00 - Next RIP
Emit-B @(0x48, 0x89, 0xF8)             # mov rax, rdi
Emit-B @(0x48, 0x2B, 0x44, 0x24, 0x38) # sub rax, [rsp + 0x38]
Emit-B @(0x05, 0x05, 0x0E, 0x00, 0x00) # add eax, 0x0E05 (Next RIP)
Emit-B @(0xBA, 0x00, 0x7D, 0x00, 0x00) # mov edx, 0x7D00
Emit-B @(0x29, 0xC2)                   # sub edx, eax (edx = disp32)

Emit-B @(0xC6, 0x07, 0xE8); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0x89, 0x17); Emit-B @(0x48, 0x83, 0xC7, 0x04)

# Emit mov [rbp + slot32], rax (48 89 85 <disp32>):
Emit-B @(0xC6, 0x07, 0x48); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x89); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x85); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0x8B, 0x44, 0x24, 0x48)       # mov eax, [rsp + 0x48] (slot)
Emit-B @(0x89, 0x07); Emit-B @(0x48, 0x83, 0xC7, 0x04)

# Skip to end of line:
$skipEolRead = $code.Count
Emit-B @(0x8A, 0x06)
Emit-B @(0x84, 0xC0); $jzReadEof = (Emit-JccFwd32 0x84)
Emit-B @(0x3C, 0x0A); $jeReadLf = (Emit-JccFwd32 0x84)
Emit-B @(0x48, 0xFF, 0xC6)
Emit-JmpBack32 $skipEolRead

Patch-JccFwd32 $jeReadLf
Emit-B @(0x48, 0xFF, 0xC6)
Emit-JmpBack32 $skipWsOff

# ==============================================================================
# PARSE LET:
# ==============================================================================
Patch-JmpFwd32 $jmpParseLet
Emit-B @(0x48, 0x83, 0xC6, 0x04)       # add rsi, 4 (skip "let ")

# Skip spaces before var
$skipSpLet1 = $code.Count
Emit-B @(0x8A, 0x06)                   # mov al, [rsi]
Emit-B @(0x3C, 0x20)                   # cmp al, ' '
$jneGotVar = (Emit-JccFwd32 0x85)
Emit-B @(0x48, 0xFF, 0xC6)             # inc rsi
Emit-JmpBack32 $skipSpLet1

Patch-JccFwd32 $jneGotVar
# al has var letter. Advance rsi.
Emit-B @(0x48, 0xFF, 0xC6)             # inc rsi
Emit-B @(0x0C, 0x20)                   # or al, 0x20

# VALIDATE VAR RANGE: 'a' <= al <= 'z'
Emit-B @(0x3C, 0x61); $jbVarBad1 = (Emit-JccFwd32 0x82)
Emit-B @(0x3C, 0x7A); $jaVarBad1 = (Emit-JccFwd32 0x87)
$jmpVarOk1 = (Emit-JmpFwd32)
Patch-JccFwd32 $jbVarBad1; Patch-JccFwd32 $jaVarBad1
Emit-JmpBack32 $errSyntaxPos
Patch-JmpFwd32 $jmpVarOk1

Emit-B @(0x2C, 0x61)                   # sub al, 'a'
Emit-B @(0x0F, 0xB6, 0xC0)             # movzx eax, al
Emit-B @(0xC1, 0xE0, 0x03)             # shl eax, 3 (* 8)
Emit-B @(0x83, 0xC0, 0x30)             # add eax, 0x30
Emit-B @(0x89, 0x44, 0x24, 0x48)       # mov [rsp + 0x48], eax (dest_slot)

# Skip until '='
$skipUntilEq = $code.Count
Emit-B @(0x8A, 0x06)                   # mov al, [rsi]
Emit-B @(0x48, 0xFF, 0xC6)             # inc rsi
Emit-B @(0x3C, 0x3D)                   # cmp al, '='
Emit-JccBack32 0x85 $skipUntilEq

# Skip spaces after '='
$skipSpEq = $code.Count
Emit-B @(0x8A, 0x06)                   # mov al, [rsi]
Emit-B @(0x3C, 0x20)                   # cmp al, ' '
$jneParseOp1 = (Emit-JccFwd32 0x85)
Emit-B @(0x48, 0xFF, 0xC6)             # inc rsi
Emit-JmpBack32 $skipSpEq

# Parse Op1 or Stage 4 Built-ins:
Patch-JccFwd32 $jneParseOp1

# --- CHECK ALLOC ---
Emit-B @(0x8A, 0x06); Emit-B @(0x0C, 0x20); Emit-B @(0x3C, 0x61); $jneNotAlloc1 = (Emit-JccFwd32 0x85) # 'a'
Emit-B @(0x8A, 0x46, 0x01); Emit-B @(0x0C, 0x20); Emit-B @(0x3C, 0x6C); $jneNotAlloc2 = (Emit-JccFwd32 0x85) # 'l'
Emit-B @(0x8A, 0x46, 0x02); Emit-B @(0x0C, 0x20); Emit-B @(0x3C, 0x6C); $jneNotAlloc3 = (Emit-JccFwd32 0x85) # 'l'
Emit-B @(0x8A, 0x46, 0x03); Emit-B @(0x0C, 0x20); Emit-B @(0x3C, 0x6F); $jneNotAlloc4 = (Emit-JccFwd32 0x85) # 'o'
Emit-B @(0x8A, 0x46, 0x04); Emit-B @(0x0C, 0x20); Emit-B @(0x3C, 0x63); $jneNotAlloc5 = (Emit-JccFwd32 0x85) # 'c'
Emit-B @(0x8A, 0x46, 0x05)
Emit-B @(0x3C, 0x20); $jeAllocOk1 = (Emit-JccFwd32 0x84)
Emit-B @(0x3C, 0x09); $jeAllocOk2 = (Emit-JccFwd32 0x84)
$jmpNotAlloc = (Emit-JmpFwd32)

Patch-JccFwd32 $jeAllocOk1; Patch-JccFwd32 $jeAllocOk2
Emit-B @(0x48, 0x83, 0xC6, 0x05) # skip "alloc"
$skipSpAlloc = $code.Count
Emit-B @(0x8A, 0x06); Emit-B @(0x3C, 0x20); $jneGotAllocSize = (Emit-JccFwd32 0x85)
Emit-B @(0x48, 0xFF, 0xC6); Emit-JmpBack32 $skipSpAlloc

Patch-JccFwd32 $jneGotAllocSize
Emit-B @(0x3C, 0x30); $jbAllocVar = (Emit-JccFwd32 0x82)
Emit-B @(0x3C, 0x39); $jaAllocVar = (Emit-JccFwd32 0x87)
# uint size:
$dispCall = $parseUintOff - ($code.Count + 5)
Emit-B @(0xE8); Emit-B ([BitConverter]::GetBytes([int32]$dispCall))
# Emit: mov rdx, imm32 (48 C7 C2 <imm32>)
Emit-B @(0xC6, 0x07, 0x48); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0xC7); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0xC2); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0x89, 0x07); Emit-B @(0x48, 0x83, 0xC7, 0x04)
$jmpAfterAllocSize = (Emit-JmpFwd32)

Patch-JccFwd32 $jbAllocVar; Patch-JccFwd32 $jaAllocVar
# var size:
Emit-B @(0x48, 0xFF, 0xC6); Emit-B @(0x0C, 0x20); Emit-B @(0x2C, 0x61)
Emit-B @(0x0F, 0xB6, 0xC8); Emit-B @(0xC1, 0xE1, 0x03); Emit-B @(0x83, 0xC1, 0x30)
# Emit: mov rdx, [rbp + slot32] (48 8B 95 <disp32>)
Emit-B @(0xC6, 0x07, 0x48); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x8B); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x95); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0x89, 0x0F); Emit-B @(0x48, 0x83, 0xC7, 0x04)

Patch-JmpFwd32 $jmpAfterAllocSize
# Emit: xor ecx, ecx (31 C9)
Emit-B @(0xC6, 0x07, 0x31); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0xC9); Emit-B @(0x48, 0xFF, 0xC7)
# Emit: mov r8d, 0x3000 (41 B8 00 30 00 00)
Emit-B @(0xC6, 0x07, 0x41); Emit-B @(0x48, 0xFF, 0xC7); Emit-B @(0xC6, 0x07, 0xB8); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x00); Emit-B @(0x48, 0xFF, 0xC7); Emit-B @(0xC6, 0x07, 0x30); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x00); Emit-B @(0x48, 0xFF, 0xC7); Emit-B @(0xC6, 0x07, 0x00); Emit-B @(0x48, 0xFF, 0xC7)
# Emit: mov r9d, 4 (41 B9 04 00 00 00)
Emit-B @(0xC6, 0x07, 0x41); Emit-B @(0x48, 0xFF, 0xC7); Emit-B @(0xC6, 0x07, 0xB9); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x04); Emit-B @(0x48, 0xFF, 0xC7); Emit-B @(0xC6, 0x07, 0x00); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x00); Emit-B @(0x48, 0xFF, 0xC7); Emit-B @(0xC6, 0x07, 0x00); Emit-B @(0x48, 0xFF, 0xC7)

# Emit: call VirtualAlloc (FF 15 <disp32>) -> IAT 0x2048
Emit-B @(0x48, 0x89, 0xF8); Emit-B @(0x48, 0x2B, 0x44, 0x24, 0x38)
Emit-B @(0x05, 0x06, 0x0E, 0x00, 0x00)
Emit-B @(0xBA, 0x48, 0x80, 0x00, 0x00) # IAT 0x8048
Emit-B @(0x29, 0xC2)
Emit-B @(0xC6, 0x07, 0xFF); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x15); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0x89, 0x17); Emit-B @(0x48, 0x83, 0xC7, 0x04)
$jmpAllocDone = (Emit-JmpFwd32)

# --- CHECK LOAD / LOAD64 ---
Patch-JccFwd32 $jneNotAlloc1; Patch-JccFwd32 $jneNotAlloc2; Patch-JccFwd32 $jneNotAlloc3
Patch-JccFwd32 $jneNotAlloc4; Patch-JccFwd32 $jneNotAlloc5; Patch-JmpFwd32 $jmpNotAlloc

Emit-B @(0x8A, 0x06); Emit-B @(0x0C, 0x20); Emit-B @(0x3C, 0x6C); $jneNotLoad1 = (Emit-JccFwd32 0x85) # 'l'
Emit-B @(0x8A, 0x46, 0x01); Emit-B @(0x0C, 0x20); Emit-B @(0x3C, 0x6F); $jneNotLoad2 = (Emit-JccFwd32 0x85) # 'o'
Emit-B @(0x8A, 0x46, 0x02); Emit-B @(0x0C, 0x20); Emit-B @(0x3C, 0x61); $jneNotLoad3 = (Emit-JccFwd32 0x85) # 'a'
Emit-B @(0x8A, 0x46, 0x03); Emit-B @(0x0C, 0x20); Emit-B @(0x3C, 0x64); $jneNotLoad4 = (Emit-JccFwd32 0x85) # 'd'

Emit-B @(0x8A, 0x46, 0x04); Emit-B @(0x3C, 0x36); $jneNotLoad64 = (Emit-JccFwd32 0x85)
Emit-B @(0x8A, 0x46, 0x05); Emit-B @(0x3C, 0x34); $jneNotLoad64b = (Emit-JccFwd32 0x85)

# load64:
Emit-B @(0xC6, 0x44, 0x24, 0x51, 0x01) # is64 = 1
Emit-B @(0x48, 0x83, 0xC6, 0x06)       # skip "load64"
$jmpParseLoadInner = (Emit-JmpFwd32)

Patch-JccFwd32 $jneNotLoad64; Patch-JccFwd32 $jneNotLoad64b
# load (byte):
Emit-B @(0xC6, 0x44, 0x24, 0x51, 0x00) # is64 = 0
Emit-B @(0x48, 0x83, 0xC6, 0x04)       # skip "load"

Patch-JmpFwd32 $jmpParseLoadInner
Emit-B @(0xC6, 0x44, 0x24, 0x52, 0x00) # mem_mode = 0 (LOAD)
$jmpDoMemFromLoad = (Emit-JmpFwd32)

# --- CHECK FILE OPERATIONS IN LET (file_open, file_create, file_read) ---
Patch-JccFwd32 $jneNotLoad1; Patch-JccFwd32 $jneNotLoad2
Patch-JccFwd32 $jneNotLoad3; Patch-JccFwd32 $jneNotLoad4

Emit-B @(0x8A, 0x06); Emit-B @(0x0C, 0x20); Emit-B @(0x3C, 0x66); $jneNotFileLet1 = (Emit-JccFwd32 0x85) # 'f'
Emit-B @(0x8A, 0x46, 0x01); Emit-B @(0x0C, 0x20); Emit-B @(0x3C, 0x69); $jneNotFileLet2 = (Emit-JccFwd32 0x85) # 'i'
Emit-B @(0x8A, 0x46, 0x02); Emit-B @(0x0C, 0x20); Emit-B @(0x3C, 0x6C); $jneNotFileLet3 = (Emit-JccFwd32 0x85) # 'l'
Emit-B @(0x8A, 0x46, 0x03); Emit-B @(0x0C, 0x20); Emit-B @(0x3C, 0x65); $jneNotFileLet4 = (Emit-JccFwd32 0x85) # 'e'
Emit-B @(0x8A, 0x46, 0x04); Emit-B @(0x3C, 0x5F); $jneNotFileLet5 = (Emit-JccFwd32 0x85) # '_'

# Check 6th char: 'o' (open), 'c' (create), 'r' (read)
Emit-B @(0x8A, 0x46, 0x05); Emit-B @(0x0C, 0x20)
Emit-B @(0x3C, 0x6F); $jeFileOpen = (Emit-JccFwd32 0x84)   # 'o'
Emit-B @(0x3C, 0x63); $jeFileCreate = (Emit-JccFwd32 0x84) # 'c'
Emit-B @(0x3C, 0x72); $jeFileRead = (Emit-JccFwd32 0x84)   # 'r'
$jmpNotFileLet6 = (Emit-JmpFwd32)

# --- PARSE file_open "filename" ---
Patch-JccFwd32 $jeFileOpen
Emit-B @(0x48, 0x83, 0xC6, 0x09) # skip "file_open"
Emit-B @(0xC6, 0x44, 0x24, 0x51, 0x01) # mode = 1 (OPEN_EXISTING)
$jmpFileCommon = (Emit-JmpFwd32)

# --- PARSE file_create "filename" ---
Patch-JccFwd32 $jeFileCreate
Emit-B @(0x48, 0x83, 0xC6, 0x0B) # skip "file_create"
Emit-B @(0xC6, 0x44, 0x24, 0x51, 0x02) # mode = 2 (CREATE_ALWAYS)

Patch-JmpFwd32 $jmpFileCommon
# Skip spaces until '"':
$skipSpFileStr = $code.Count
Emit-B @(0x8A, 0x06); Emit-B @(0x48, 0xFF, 0xC6)
Emit-B @(0x3C, 0x22); Emit-JccBack32 0x85 $skipSpFileStr

# Calculate string RVA: strRVA = (rbp - peTemplateBase) + 0x1000
Emit-B @(0x48, 0x89, 0xE8); Emit-B @(0x48, 0x2B, 0x44, 0x24, 0x38)
Emit-B @(0x05, 0x00, 0x10, 0x00, 0x00)
Emit-B @(0x89, 0x44, 0x24, 0x54) # save strRVA

# Copy string chars to [rbp] until '"':
$copyFileStrLoop = $code.Count
Emit-B @(0x8A, 0x06); Emit-B @(0x84, 0xC0); $jzFileStrEnd = (Emit-JccFwd32 0x84)
Emit-B @(0x3C, 0x22); $jeFileStrEndQuote = (Emit-JccFwd32 0x84)
Emit-B @(0x3C, 0x0D); $jeFileStrEndCr = (Emit-JccFwd32 0x84)
Emit-B @(0x3C, 0x0A); $jeFileStrEndLf = (Emit-JccFwd32 0x84)
Emit-B @(0x88, 0x45, 0x00); Emit-B @(0x48, 0xFF, 0xC6); Emit-B @(0x48, 0xFF, 0xC5)
Emit-JmpBack32 $copyFileStrLoop

Patch-JccFwd32 $jeFileStrEndQuote; Emit-B @(0x48, 0xFF, 0xC6)
Patch-JccFwd32 $jzFileStrEnd; Patch-JccFwd32 $jeFileStrEndCr; Patch-JccFwd32 $jeFileStrEndLf
# Null-terminate filename:
Emit-B @(0xC6, 0x45, 0x00, 0x00); Emit-B @(0x48, 0xFF, 0xC5)

# Emit CreateFileA call:
# 1. lea rcx, [rip + disp32] (48 8D 0D <disp32>)
Emit-B @(0x48, 0x89, 0xF8); Emit-B @(0x48, 0x2B, 0x44, 0x24, 0x38)
Emit-B @(0x05, 0x07, 0x0E, 0x00, 0x00) # Next RIP
Emit-B @(0x8B, 0x54, 0x24, 0x54)       # strRVA
Emit-B @(0x29, 0xC2)
Emit-B @(0xC6, 0x07, 0x48); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x8D); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x0D); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0x89, 0x17); Emit-B @(0x48, 0x83, 0xC7, 0x04)

# 2. Check mode (1 = open, 2 = create):
Emit-B @(0x80, 0x7C, 0x24, 0x51, 0x01); $jneEmitCreateMode = (Emit-JccFwd32 0x85)

# OPEN: edx = 0x80000000 (GENERIC_READ), r8d = 1 (FILE_SHARE_READ), arg5 = 3 (OPEN_EXISTING)
Emit-B @(0xC6, 0x07, 0xBA); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x00); Emit-B @(0x48, 0xFF, 0xC7); Emit-B @(0xC6, 0x07, 0x00); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x00); Emit-B @(0x48, 0xFF, 0xC7); Emit-B @(0xC6, 0x07, 0x80); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x41); Emit-B @(0x48, 0xFF, 0xC7); Emit-B @(0xC6, 0x07, 0xB8); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x01); Emit-B @(0x48, 0xFF, 0xC7); Emit-B @(0xC6, 0x07, 0x00); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x00); Emit-B @(0x48, 0xFF, 0xC7); Emit-B @(0xC6, 0x07, 0x00); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x45); Emit-B @(0x48, 0xFF, 0xC7); Emit-B @(0xC6, 0x07, 0x31); Emit-B @(0x48, 0xFF, 0xC7); Emit-B @(0xC6, 0x07, 0xC9); Emit-B @(0x48, 0xFF, 0xC7)
$movArg5Open = @(0x48, 0xC7, 0x44, 0x24, 0x20, 0x03, 0x00, 0x00, 0x00)
foreach ($b in $movArg5Open) { Emit-B @(0xC6, 0x07, $b); Emit-B @(0x48, 0xFF, 0xC7) }
$jmpAfterModeSet = (Emit-JmpFwd32)

Patch-JccFwd32 $jneEmitCreateMode
# CREATE: edx = 0x40000000 (GENERIC_WRITE), r8d = 0, arg5 = 2 (CREATE_ALWAYS)
Emit-B @(0xC6, 0x07, 0xBA); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x00); Emit-B @(0x48, 0xFF, 0xC7); Emit-B @(0xC6, 0x07, 0x00); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x00); Emit-B @(0x48, 0xFF, 0xC7); Emit-B @(0xC6, 0x07, 0x40); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x45); Emit-B @(0x48, 0xFF, 0xC7); Emit-B @(0xC6, 0x07, 0x31); Emit-B @(0x48, 0xFF, 0xC7); Emit-B @(0xC6, 0x07, 0xC0); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x45); Emit-B @(0x48, 0xFF, 0xC7); Emit-B @(0xC6, 0x07, 0x31); Emit-B @(0x48, 0xFF, 0xC7); Emit-B @(0xC6, 0x07, 0xC9); Emit-B @(0x48, 0xFF, 0xC7)
$movArg5Create = @(0x48, 0xC7, 0x44, 0x24, 0x20, 0x02, 0x00, 0x00, 0x00)
foreach ($b in $movArg5Create) { Emit-B @(0xC6, 0x07, $b); Emit-B @(0x48, 0xFF, 0xC7) }

Patch-JmpFwd32 $jmpAfterModeSet
$movArg6File = @(0x48, 0xC7, 0x44, 0x24, 0x28, 0x80, 0x00, 0x00, 0x00)
foreach ($b in $movArg6File) { Emit-B @(0xC6, 0x07, $b); Emit-B @(0x48, 0xFF, 0xC7) }
$movArg7File = @(0x48, 0xC7, 0x44, 0x24, 0x30, 0x00, 0x00, 0x00, 0x00)
foreach ($b in $movArg7File) { Emit-B @(0xC6, 0x07, $b); Emit-B @(0x48, 0xFF, 0xC7) }

# call CreateFileA (FF 15 <disp32>) -> IAT 0x2050
Emit-B @(0x48, 0x89, 0xF8); Emit-B @(0x48, 0x2B, 0x44, 0x24, 0x38)
Emit-B @(0x05, 0x06, 0x0E, 0x00, 0x00)
Emit-B @(0xBA, 0x50, 0x80, 0x00, 0x00) # IAT 0x8050
Emit-B @(0x29, 0xC2)
Emit-B @(0xC6, 0x07, 0xFF); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x15); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0x89, 0x17); Emit-B @(0x48, 0x83, 0xC7, 0x04)
$jmpFileOpenDone = (Emit-JmpFwd32)

# --- PARSE file_read fd buf max_len ---
Patch-JccFwd32 $jeFileRead
Emit-B @(0x48, 0x83, 0xC6, 0x09) # skip "file_read"
Emit-B @(0xC6, 0x44, 0x24, 0x53, 0x01) # file_rw_mode = 1 (READ)

# --- UNIFIED FILE READ/WRITE ARGUMENT PARSER & CALL EMITTER ---
Patch-JmpFwd32 $jmpDoFileRwFromStmt
$skipSpFr1 = $code.Count
Emit-B @(0x8A, 0x06); Emit-B @(0x3C, 0x20); $jneGotFrFd = (Emit-JccFwd32 0x85)
Emit-B @(0x48, 0xFF, 0xC6); Emit-JmpBack32 $skipSpFr1
Patch-JccFwd32 $jneGotFrFd
Emit-B @(0x48, 0xFF, 0xC6); Emit-B @(0x0C, 0x20); Emit-B @(0x2C, 0x61)
Emit-B @(0x0F, 0xB6, 0xC8); Emit-B @(0xC1, 0xE1, 0x03); Emit-B @(0x83, 0xC1, 0x30)
# Emit: mov rcx, [rbp + fd_slot32] (48 8B 8D <disp32>)
Emit-B @(0xC6, 0x07, 0x48); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x8B); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x8D); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0x89, 0x0F); Emit-B @(0x48, 0x83, 0xC7, 0x04)

$skipSpFr2 = $code.Count
Emit-B @(0x8A, 0x06); Emit-B @(0x3C, 0x20); $jneGotFrBuf = (Emit-JccFwd32 0x85)
Emit-B @(0x48, 0xFF, 0xC6); Emit-JmpBack32 $skipSpFr2
Patch-JccFwd32 $jneGotFrBuf
Emit-B @(0x48, 0xFF, 0xC6); Emit-B @(0x0C, 0x20); Emit-B @(0x2C, 0x61)
Emit-B @(0x0F, 0xB6, 0xC8); Emit-B @(0xC1, 0xE1, 0x03); Emit-B @(0x83, 0xC1, 0x30)
# Emit: mov rdx, [rbp + buf_slot32] (48 8B 95 <disp32>)
Emit-B @(0xC6, 0x07, 0x48); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x8B); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x95); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0x89, 0x0F); Emit-B @(0x48, 0x83, 0xC7, 0x04)

$skipSpFr3 = $code.Count
Emit-B @(0x8A, 0x06); Emit-B @(0x3C, 0x20); $jneGotFrLen = (Emit-JccFwd32 0x85)
Emit-B @(0x48, 0xFF, 0xC6); Emit-JmpBack32 $skipSpFr3
Patch-JccFwd32 $jneGotFrLen
Emit-B @(0x3C, 0x30); $jbFrLenVar = (Emit-JccFwd32 0x82)
Emit-B @(0x3C, 0x39); $jaFrLenVar = (Emit-JccFwd32 0x87)
# uint:
$dispCall = $parseUintOff - ($code.Count + 5)
Emit-B @(0xE8); Emit-B ([BitConverter]::GetBytes([int32]$dispCall))
# Emit: mov r8d, imm32 (41 B8 <imm32>)
Emit-B @(0xC6, 0x07, 0x41); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0xB8); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0x89, 0x07); Emit-B @(0x48, 0x83, 0xC7, 0x04)
$jmpAfterFrLen = (Emit-JmpFwd32)

Patch-JccFwd32 $jbFrLenVar; Patch-JccFwd32 $jaFrLenVar
Emit-B @(0x48, 0xFF, 0xC6); Emit-B @(0x0C, 0x20); Emit-B @(0x2C, 0x61)
Emit-B @(0x0F, 0xB6, 0xC8); Emit-B @(0xC1, 0xE1, 0x03); Emit-B @(0x83, 0xC1, 0x30)
# Emit: mov r8d, [rbp + slot32] (44 8B 85 <disp32>)
Emit-B @(0xC6, 0x07, 0x44); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x8B); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x85); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0x89, 0x0F); Emit-B @(0x48, 0x83, 0xC7, 0x04)

Patch-JmpFwd32 $jmpAfterFrLen
$leaR9Fr = @(0x4C, 0x8D, 0x4C, 0x24, 0x28)
foreach ($b in $leaR9Fr) { Emit-B @(0xC6, 0x07, $b); Emit-B @(0x48, 0xFF, 0xC7) }
$movArg5Fr = @(0x48, 0xC7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00)
foreach ($b in $movArg5Fr) { Emit-B @(0xC6, 0x07, $b); Emit-B @(0x48, 0xFF, 0xC7) }

# Check file_rw_mode:
Emit-B @(0x80, 0x7C, 0x24, 0x53, 0x01); $jeDoReadFileCall = (Emit-JccFwd32 0x84)

# WriteFile (0x2030):
Emit-B @(0x48, 0x89, 0xF8); Emit-B @(0x48, 0x2B, 0x44, 0x24, 0x38)
Emit-B @(0x05, 0x06, 0x0E, 0x00, 0x00)
Emit-B @(0xBA, 0x30, 0x80, 0x00, 0x00) # IAT 0x8030
Emit-B @(0x29, 0xC2)
Emit-B @(0xC6, 0x07, 0xFF); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x15); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0x89, 0x17); Emit-B @(0x48, 0x83, 0xC7, 0x04)
$jmpFwDone = (Emit-JmpFwd32)

Patch-JccFwd32 $jeDoReadFileCall
# ReadFile (0x2040):
Emit-B @(0x48, 0x89, 0xF8); Emit-B @(0x48, 0x2B, 0x44, 0x24, 0x38)
Emit-B @(0x05, 0x06, 0x0E, 0x00, 0x00)
Emit-B @(0xBA, 0x40, 0x80, 0x00, 0x00) # IAT 0x8040
Emit-B @(0x29, 0xC2)
Emit-B @(0xC6, 0x07, 0xFF); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x15); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0x89, 0x17); Emit-B @(0x48, 0x83, 0xC7, 0x04)

# Load bytesRead into rax: mov eax, [rsp + 0x28] (8B 44 24 28); cdqe (48 98)
$loadBytesRead = @(0x8B, 0x44, 0x24, 0x28, 0x48, 0x98)
foreach ($b in $loadBytesRead) { Emit-B @(0xC6, 0x07, $b); Emit-B @(0x48, 0xFF, 0xC7) }
$jmpFrDone = (Emit-JmpFwd32)

# --- UNIFIED MEMORY ADDRESS PARSER & LOAD/STORE CODE EMITTER ---
Patch-JmpFwd32 $jmpDoMemFromStore
Patch-JmpFwd32 $jmpDoMemFromLoad

# Skip spaces until '[':
$skipSpMemBrk = $code.Count
Emit-B @(0x8A, 0x06); Emit-B @(0x48, 0xFF, 0xC6)
Emit-B @(0x3C, 0x5B); Emit-JccBack32 0x85 $skipSpMemBrk

# Skip spaces before ptr var:
$skipSpMemPtr = $code.Count
Emit-B @(0x8A, 0x06); Emit-B @(0x3C, 0x20); $jneGotMemPtr = (Emit-JccFwd32 0x85)
Emit-B @(0x48, 0xFF, 0xC6); Emit-JmpBack32 $skipSpMemPtr

Patch-JccFwd32 $jneGotMemPtr
Emit-B @(0x48, 0xFF, 0xC6); Emit-B @(0x0C, 0x20); Emit-B @(0x2C, 0x61)
Emit-B @(0x0F, 0xB6, 0xC8); Emit-B @(0xC1, 0xE1, 0x03); Emit-B @(0x83, 0xC1, 0x30)
# Emit: mov r8, [rbp + ptr_slot32] (4C 8B 85 <disp32>)
Emit-B @(0xC6, 0x07, 0x4C); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x8B); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x85); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0x89, 0x0F); Emit-B @(0x48, 0x83, 0xC7, 0x04)

# Skip spaces to check '+' or ']':
$skipSpMemIdx = $code.Count
Emit-B @(0x8A, 0x06); Emit-B @(0x3C, 0x20); $jneGotMemPlus = (Emit-JccFwd32 0x85)
Emit-B @(0x48, 0xFF, 0xC6); Emit-JmpBack32 $skipSpMemIdx

Patch-JccFwd32 $jneGotMemPlus
Emit-B @(0x3C, 0x2B); $jneNoMemIdx = (Emit-JccFwd32 0x85) # '+'
Emit-B @(0x48, 0xFF, 0xC6) # skip '+'

# Skip spaces before index:
$skipSpMemIdx2 = $code.Count
Emit-B @(0x8A, 0x06); Emit-B @(0x3C, 0x20); $jneGotMemIdxOp = (Emit-JccFwd32 0x85)
Emit-B @(0x48, 0xFF, 0xC6); Emit-JmpBack32 $skipSpMemIdx2

Patch-JccFwd32 $jneGotMemIdxOp
Emit-B @(0x3C, 0x30); $jbMemIdxVar = (Emit-JccFwd32 0x82)
Emit-B @(0x3C, 0x39); $jaMemIdxVar = (Emit-JccFwd32 0x87)

# Uint index:
$dispCall = $parseUintOff - ($code.Count + 5)
Emit-B @(0xE8); Emit-B ([BitConverter]::GetBytes([int32]$dispCall))
Emit-B @(0x80, 0x7C, 0x24, 0x51, 0x01); $jneNotScaleImm = (Emit-JccFwd32 0x85)
Emit-B @(0xC1, 0xE0, 0x03) # shl eax, 3
Patch-JccFwd32 $jneNotScaleImm
# Emit: add r8, imm32 (49 81 C0 <imm32>)
Emit-B @(0xC6, 0x07, 0x49); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x81); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0xC0); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0x89, 0x07); Emit-B @(0x48, 0x83, 0xC7, 0x04)
$jmpAfterMemIdx = (Emit-JmpFwd32)

Patch-JccFwd32 $jbMemIdxVar; Patch-JccFwd32 $jaMemIdxVar
# Variable index:
Emit-B @(0x48, 0xFF, 0xC6); Emit-B @(0x0C, 0x20); Emit-B @(0x2C, 0x61)
Emit-B @(0x0F, 0xB6, 0xC8); Emit-B @(0xC1, 0xE1, 0x03); Emit-B @(0x83, 0xC1, 0x30)
# Emit: mov r9, [rbp + idx_slot32] (4C 8B 8D <disp32>)
Emit-B @(0xC6, 0x07, 0x4C); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x8B); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x8D); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0x89, 0x0F); Emit-B @(0x48, 0x83, 0xC7, 0x04)

# Check for optional "* 8" or is64:
$skipSpMemScale = $code.Count
Emit-B @(0x8A, 0x06); Emit-B @(0x3C, 0x20); $jneCheckStar = (Emit-JccFwd32 0x85)
Emit-B @(0x48, 0xFF, 0xC6); Emit-JmpBack32 $skipSpMemScale

Patch-JccFwd32 $jneCheckStar
Emit-B @(0x3C, 0x2A); $jeGotExplicitStar = (Emit-JccFwd32 0x84)
Emit-B @(0x80, 0x7C, 0x24, 0x51, 0x01); $jneNoScaleVar = (Emit-JccFwd32 0x85)
$jmpDoScaleVar = (Emit-JmpFwd32)

Patch-JccFwd32 $jeGotExplicitStar
Emit-B @(0x48, 0xFF, 0xC6) # skip '*'
$skipSpScaleNum = $code.Count
Emit-B @(0x8A, 0x06); Emit-B @(0x3C, 0x20); $jneGotScaleNum = (Emit-JccFwd32 0x85)
Emit-B @(0x48, 0xFF, 0xC6); Emit-JmpBack32 $skipSpScaleNum
Patch-JccFwd32 $jneGotScaleNum
Emit-B @(0x48, 0xFF, 0xC6) # skip '8'

Patch-JmpFwd32 $jmpDoScaleVar
# Emit: shl r9, 3 (49 C1 E1 03)
Emit-B @(0xC6, 0x07, 0x49); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0xC1); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0xE1); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x03); Emit-B @(0x48, 0xFF, 0xC7)

Patch-JccFwd32 $jneNoScaleVar
# Emit: add r8, r9 (4D 01 C8)
Emit-B @(0xC6, 0x07, 0x4D); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x01); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0xC8); Emit-B @(0x48, 0xFF, 0xC7)

Patch-JccFwd32 $jneNoMemIdx
Patch-JmpFwd32 $jmpAfterMemIdx

# Skip until ']' and skip ']':
$skipUntilCloseBrk = $code.Count
Emit-B @(0x8A, 0x06); Emit-B @(0x48, 0xFF, 0xC6)
Emit-B @(0x3C, 0x5D); Emit-JccBack32 0x85 $skipUntilCloseBrk

# Check mem_mode (0 = LOAD, 1 = STORE):
Emit-B @(0x80, 0x7C, 0x24, 0x52, 0x01); $jeDoStoreAction = (Emit-JccFwd32 0x84)

# --- ACTION FOR LOAD / LOAD64 ---
Emit-B @(0x80, 0x7C, 0x24, 0x51, 0x01); $jeEmitLoad64 = (Emit-JccFwd32 0x84)
# load byte: movzx rax, byte ptr [r8] (49 0F B6 00)
Emit-B @(0xC6, 0x07, 0x49); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x0F); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0xB6); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x00); Emit-B @(0x48, 0xFF, 0xC7)
$jmpLoadedByte = (Emit-JmpFwd32)

Patch-JccFwd32 $jeEmitLoad64
# load64: mov rax, [r8] (49 8B 00)
Emit-B @(0xC6, 0x07, 0x49); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x8B); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x00); Emit-B @(0x48, 0xFF, 0xC7)

Patch-JmpFwd32 $jmpLoadedByte
$jmpLoadedIntoRax = (Emit-JmpFwd32)

# --- ACTION FOR STORE / STORE64 ---
Patch-JccFwd32 $jeDoStoreAction
# Skip spaces before value:
$skipSpStoreVal = $code.Count
Emit-B @(0x8A, 0x06); Emit-B @(0x3C, 0x20); $jneGotStoreVal = (Emit-JccFwd32 0x85)
Emit-B @(0x48, 0xFF, 0xC6); Emit-JmpBack32 $skipSpStoreVal

Patch-JccFwd32 $jneGotStoreVal
# Parse value (uint or variable):
Emit-B @(0x3C, 0x30); $jbStoreValVar = (Emit-JccFwd32 0x82)
Emit-B @(0x3C, 0x39); $jaStoreValVar = (Emit-JccFwd32 0x87)

# Uint value:
$dispCall = $parseUintOff - ($code.Count + 5)
Emit-B @(0xE8); Emit-B ([BitConverter]::GetBytes([int32]$dispCall))
# Emit: mov rax, imm32 (48 C7 C0 <imm32>)
Emit-B @(0xC6, 0x07, 0x48); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0xC7); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0xC0); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0x89, 0x07); Emit-B @(0x48, 0x83, 0xC7, 0x04)
$jmpAfterStoreVal = (Emit-JmpFwd32)

Patch-JccFwd32 $jbStoreValVar; Patch-JccFwd32 $jaStoreValVar
# Variable value:
Emit-B @(0x48, 0xFF, 0xC6); Emit-B @(0x0C, 0x20); Emit-B @(0x2C, 0x61)
Emit-B @(0x0F, 0xB6, 0xC8); Emit-B @(0xC1, 0xE1, 0x03); Emit-B @(0x83, 0xC1, 0x30)
# Emit: mov rax, [rbp + val_slot32] (48 8B 85 <disp32>)
Emit-B @(0xC6, 0x07, 0x48); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x8B); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x85); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0x89, 0x0F); Emit-B @(0x48, 0x83, 0xC7, 0x04)

Patch-JmpFwd32 $jmpAfterStoreVal
# Emit write to memory:
Emit-B @(0x80, 0x7C, 0x24, 0x51, 0x01); $jeEmitStore64 = (Emit-JccFwd32 0x84)
# store byte: mov [r8], al (41 88 00)
Emit-B @(0xC6, 0x07, 0x41); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x88); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x00); Emit-B @(0x48, 0xFF, 0xC7)
$jmpStoreDone = (Emit-JmpFwd32)

Patch-JccFwd32 $jeEmitStore64
# store64: mov [r8], rax (49 89 00)
Emit-B @(0xC6, 0x07, 0x49); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x89); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x00); Emit-B @(0x48, 0xFF, 0xC7)

Patch-JmpFwd32 $jmpStoreDone
$jmpStoreFin = (Emit-JmpFwd32)

# None matched: fall through to existing expression parser!
Patch-JccFwd32 $jneNotFileLet1; Patch-JccFwd32 $jneNotFileLet2; Patch-JccFwd32 $jneNotFileLet3
Patch-JccFwd32 $jneNotFileLet4; Patch-JccFwd32 $jneNotFileLet5; Patch-JmpFwd32 $jmpNotFileLet6

Emit-B @(0x3C, 0x30); $jbOp1Var = (Emit-JccFwd32 0x82) # cmp al, '0'
Emit-B @(0x3C, 0x39); $jaOp1Var = (Emit-JccFwd32 0x87) # cmp al, '9'

# Op1 is number:
$dispCall = $parseUintOff - ($code.Count + 5)
Emit-B @(0xE8); Emit-B ([BitConverter]::GetBytes([int32]$dispCall))
# Emit: mov rax, imm32 (48 C7 C0 <imm32>)
Emit-B @(0xC6, 0x07, 0x48); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0xC7); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0xC0); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0x89, 0x07); Emit-B @(0x48, 0x83, 0xC7, 0x04)
$jmpAfterOp1 = (Emit-JmpFwd32)

# Op1 is variable:
Patch-JccFwd32 $jbOp1Var; Patch-JccFwd32 $jaOp1Var
Emit-B @(0x48, 0xFF, 0xC6)             # inc rsi
Emit-B @(0x0C, 0x20)                   # or al, 0x20

# VALIDATE VAR RANGE:
Emit-B @(0x3C, 0x61); $jbVarBad2 = (Emit-JccFwd32 0x82)
Emit-B @(0x3C, 0x7A); $jaVarBad2 = (Emit-JccFwd32 0x87)
$jmpVarOk2 = (Emit-JmpFwd32)
Patch-JccFwd32 $jbVarBad2; Patch-JccFwd32 $jaVarBad2
Emit-JmpBack32 $errSyntaxPos
Patch-JmpFwd32 $jmpVarOk2

Emit-B @(0x2C, 0x61)                   # sub al, 'a'
Emit-B @(0x0F, 0xB6, 0xC8)             # movzx ecx, al
Emit-B @(0xC1, 0xE1, 0x03)             # shl ecx, 3
Emit-B @(0x83, 0xC1, 0x30)             # add ecx, 0x30 (src1_slot)
# Emit: mov rax, [rbp + slot32] (48 8B 85 <disp32>)
Emit-B @(0xC6, 0x07, 0x48); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x8B); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x85); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0x89, 0x0F); Emit-B @(0x48, 0x83, 0xC7, 0x04)

# after_op1 / CHAIN ACCUMULATOR LOOP:
Patch-JmpFwd32 $jmpAfterOp1
Patch-JmpFwd32 $jmpLoadedIntoRax
$skipSpOp1 = $code.Count
Emit-B @(0x8A, 0x06)                   # mov al, [rsi]
Emit-B @(0x3C, 0x20)                   # cmp al, ' '
$jneCheckOp = (Emit-JccFwd32 0x85)
Emit-B @(0x48, 0xFF, 0xC6)             # inc rsi
Emit-JmpBack32 $skipSpOp1

# check_operator:
Patch-JccFwd32 $jneCheckOp
# If al is newline, comment, or EOF -> finished expression! Store and exit!
Emit-B @(0x84, 0xC0); $jzStoreLet1 = (Emit-JccFwd32 0x84)
Emit-B @(0x3C, 0x0D); $jeStoreLet2 = (Emit-JccFwd32 0x84)
Emit-B @(0x3C, 0x0A); $jeStoreLet3 = (Emit-JccFwd32 0x84)
Emit-B @(0x3C, 0x23); $jeStoreLet4 = (Emit-JccFwd32 0x84)
Emit-B @(0x3C, 0x3B); $jeStoreLet5 = (Emit-JccFwd32 0x84)

# Operator must be '+', '-', '*', '/', or '%'
Emit-B @(0x3C, 0x2B); $jeValidOp1 = (Emit-JccFwd32 0x84) # '+'
Emit-B @(0x3C, 0x2D); $jeValidOp2 = (Emit-JccFwd32 0x84) # '-'
Emit-B @(0x3C, 0x2A); $jeValidOp3 = (Emit-JccFwd32 0x84) # '*'
Emit-B @(0x3C, 0x2F); $jeValidOp4 = (Emit-JccFwd32 0x84) # '/'
Emit-B @(0x3C, 0x25); $jeValidOp5 = (Emit-JccFwd32 0x84) # '%'
# Not a valid operator -> Syntax Error!
Emit-JmpBack32 $errSyntaxPos

Patch-JccFwd32 $jeValidOp1; Patch-JccFwd32 $jeValidOp2; Patch-JccFwd32 $jeValidOp3
Patch-JccFwd32 $jeValidOp4; Patch-JccFwd32 $jeValidOp5
# It's an operator! Save in [rsp + 0x50]
Emit-B @(0x88, 0x44, 0x24, 0x50)       # mov [rsp + 0x50], al
Emit-B @(0x48, 0xFF, 0xC6)             # inc rsi

# Skip spaces after op
$skipSpOp2 = $code.Count
Emit-B @(0x8A, 0x06)
Emit-B @(0x3C, 0x20)
$jneParseOp2 = (Emit-JccFwd32 0x85)
Emit-B @(0x48, 0xFF, 0xC6)
Emit-JmpBack32 $skipSpOp2

# Parse Op2:
Patch-JccFwd32 $jneParseOp2
Emit-B @(0x3C, 0x30); $jbOp2Var = (Emit-JccFwd32 0x82)
Emit-B @(0x3C, 0x39); $jaOp2Var = (Emit-JccFwd32 0x87)

# Op2 is immediate number:
$dispCall = $parseUintOff - ($code.Count + 5)
Emit-B @(0xE8); Emit-B ([BitConverter]::GetBytes([int32]$dispCall))
# Check op in [rsp + 0x50]:
Emit-B @(0x80, 0x7C, 0x24, 0x50, 0x2B); $jeAddImm = (Emit-JccFwd32 0x84) # '+'
Emit-B @(0x80, 0x7C, 0x24, 0x50, 0x2D); $jeSubImm = (Emit-JccFwd32 0x84) # '-'
Emit-B @(0x80, 0x7C, 0x24, 0x50, 0x2A); $jeMulImm = (Emit-JccFwd32 0x84) # '*'
Emit-B @(0x80, 0x7C, 0x24, 0x50, 0x2F); $jeDivImm = (Emit-JccFwd32 0x84) # '/'
Emit-B @(0x80, 0x7C, 0x24, 0x50, 0x25); $jeModImm = (Emit-JccFwd32 0x84) # '%'
Emit-JmpBack32 $errSyntaxPos

# emit_add_imm: add rax, imm32 (48 05 <imm32>)
Patch-JccFwd32 $jeAddImm
Emit-B @(0xC6, 0x07, 0x48); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x05); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0x89, 0x07); Emit-B @(0x48, 0x83, 0xC7, 0x04)
Emit-JmpBack32 $skipSpOp1 # Chain!

# emit_sub_imm: sub rax, imm32 (48 2D <imm32>)
Patch-JccFwd32 $jeSubImm
Emit-B @(0xC6, 0x07, 0x48); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x2D); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0x89, 0x07); Emit-B @(0x48, 0x83, 0xC7, 0x04)
Emit-JmpBack32 $skipSpOp1 # Chain!

# emit_mul_imm: imul rax, rax, imm32 (48 69 C0 <imm32>)
Patch-JccFwd32 $jeMulImm
Emit-B @(0xC6, 0x07, 0x48); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x69); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0xC0); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0x89, 0x07); Emit-B @(0x48, 0x83, 0xC7, 0x04)
Emit-JmpBack32 $skipSpOp1 # Chain!

# emit_div_imm: mov rbx, imm32; cqo; idiv rbx
Patch-JccFwd32 $jeDivImm
Emit-B @(0xC6, 0x07, 0x48); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0xC7); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0xC3); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0x89, 0x07); Emit-B @(0x48, 0x83, 0xC7, 0x04) # mov rbx, imm32
Emit-B @(0xC6, 0x07, 0x48); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x99); Emit-B @(0x48, 0xFF, 0xC7) # cqo
Emit-B @(0xC6, 0x07, 0x48); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0xF7); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0xFB); Emit-B @(0x48, 0xFF, 0xC7) # idiv rbx
Emit-JmpBack32 $skipSpOp1 # Chain!

# emit_mod_imm: mov rbx, imm32; cqo; idiv rbx; mov rax, rdx
Patch-JccFwd32 $jeModImm
Emit-B @(0xC6, 0x07, 0x48); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0xC7); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0xC3); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0x89, 0x07); Emit-B @(0x48, 0x83, 0xC7, 0x04) # mov rbx, imm32
Emit-B @(0xC6, 0x07, 0x48); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x99); Emit-B @(0x48, 0xFF, 0xC7) # cqo
Emit-B @(0xC6, 0x07, 0x48); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0xF7); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0xFB); Emit-B @(0x48, 0xFF, 0xC7) # idiv rbx
Emit-B @(0xC6, 0x07, 0x48); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x89); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0xD0); Emit-B @(0x48, 0xFF, 0xC7) # mov rax, rdx
Emit-JmpBack32 $skipSpOp1 # Chain!

# Op2 is variable:
Patch-JccFwd32 $jbOp2Var; Patch-JccFwd32 $jaOp2Var
Emit-B @(0x48, 0xFF, 0xC6)             # inc rsi
Emit-B @(0x0C, 0x20)                   # or al, 0x20

# VALIDATE VAR RANGE:
Emit-B @(0x3C, 0x61); $jbVarBad3 = (Emit-JccFwd32 0x82)
Emit-B @(0x3C, 0x7A); $jaVarBad3 = (Emit-JccFwd32 0x87)
$jmpVarOk3 = (Emit-JmpFwd32)
Patch-JccFwd32 $jbVarBad3; Patch-JccFwd32 $jaVarBad3
Emit-JmpBack32 $errSyntaxPos
Patch-JmpFwd32 $jmpVarOk3

Emit-B @(0x2C, 0x61)                   # sub al, 'a'
Emit-B @(0x0F, 0xB6, 0xC8)             # movzx ecx, al
Emit-B @(0xC1, 0xE1, 0x03)             # shl ecx, 3
Emit-B @(0x83, 0xC1, 0x30)             # add ecx, 0x30 (src2_slot)

# Check op in [rsp + 0x50]:
Emit-B @(0x80, 0x7C, 0x24, 0x50, 0x2B); $jeAddVar = (Emit-JccFwd32 0x84) # '+'
Emit-B @(0x80, 0x7C, 0x24, 0x50, 0x2D); $jeSubVar = (Emit-JccFwd32 0x84) # '-'
Emit-B @(0x80, 0x7C, 0x24, 0x50, 0x2A); $jeMulVar = (Emit-JccFwd32 0x84) # '*'
Emit-B @(0x80, 0x7C, 0x24, 0x50, 0x2F); $jeDivVar = (Emit-JccFwd32 0x84) # '/'
Emit-B @(0x80, 0x7C, 0x24, 0x50, 0x25); $jeModVar = (Emit-JccFwd32 0x84) # '%'
Emit-JmpBack32 $errSyntaxPos

# emit_add_var: add rax, [rbp + slot32] (48 03 85 <disp32>)
Patch-JccFwd32 $jeAddVar
Emit-B @(0xC6, 0x07, 0x48); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x03); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x85); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0x89, 0x0F); Emit-B @(0x48, 0x83, 0xC7, 0x04)
Emit-JmpBack32 $skipSpOp1 # Chain!

# emit_sub_var: sub rax, [rbp + slot32] (48 2B 85 <disp32>)
Patch-JccFwd32 $jeSubVar
Emit-B @(0xC6, 0x07, 0x48); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x2B); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x85); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0x89, 0x0F); Emit-B @(0x48, 0x83, 0xC7, 0x04)
Emit-JmpBack32 $skipSpOp1 # Chain!

# emit_mul_var: imul rax, [rbp + slot32] (48 0F AF 85 <disp32>)
Patch-JccFwd32 $jeMulVar
Emit-B @(0xC6, 0x07, 0x48); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x0F); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0xAF); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x85); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0x89, 0x0F); Emit-B @(0x48, 0x83, 0xC7, 0x04)
Emit-JmpBack32 $skipSpOp1 # Chain!

# emit_div_var: mov rbx, [rbp + slot32]; cqo; idiv rbx
Patch-JccFwd32 $jeDivVar
Emit-B @(0xC6, 0x07, 0x48); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x8B); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x9D); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0x89, 0x0F); Emit-B @(0x48, 0x83, 0xC7, 0x04) # mov rbx, [rbp + slot32]
Emit-B @(0xC6, 0x07, 0x48); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x99); Emit-B @(0x48, 0xFF, 0xC7) # cqo
Emit-B @(0xC6, 0x07, 0x48); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0xF7); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0xFB); Emit-B @(0x48, 0xFF, 0xC7) # idiv rbx
Emit-JmpBack32 $skipSpOp1 # Chain!

# emit_mod_var: mov rbx, [rbp + slot32]; cqo; idiv rbx; mov rax, rdx
Patch-JccFwd32 $jeModVar
Emit-B @(0xC6, 0x07, 0x48); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x8B); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x9D); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0x89, 0x0F); Emit-B @(0x48, 0x83, 0xC7, 0x04) # mov rbx, [rbp + slot32]
Emit-B @(0xC6, 0x07, 0x48); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x99); Emit-B @(0x48, 0xFF, 0xC7) # cqo
Emit-B @(0xC6, 0x07, 0x48); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0xF7); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0xFB); Emit-B @(0x48, 0xFF, 0xC7) # idiv rbx
Emit-B @(0xC6, 0x07, 0x48); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x89); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0xD0); Emit-B @(0x48, 0xFF, 0xC7) # mov rax, rdx
Emit-JmpBack32 $skipSpOp1 # Chain!

# store_and_finish_let:
Patch-JccFwd32 $jzStoreLet1; Patch-JccFwd32 $jeStoreLet2; Patch-JccFwd32 $jeStoreLet3
Patch-JccFwd32 $jeStoreLet4; Patch-JccFwd32 $jeStoreLet5
Patch-JmpFwd32 $jmpAllocDone
Patch-JmpFwd32 $jmpFileOpenDone
Patch-JmpFwd32 $jmpFrDone

# Store result to dest_slot from [rsp + 0x48]: mov [rbp + dest_slot32], rax (48 89 85 <disp32>)
Emit-B @(0xC6, 0x07, 0x48); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x89); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x85); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0x8B, 0x44, 0x24, 0x48)       # mov eax, [rsp + 0x48] (dest_slot)
Emit-B @(0x89, 0x07); Emit-B @(0x48, 0x83, 0xC7, 0x04) # mov [rdi], eax; add rdi, 4

# Skip to end of line
Patch-JmpFwd32 $jmpFcDone
Patch-JmpFwd32 $jmpFwDone
Patch-JmpFwd32 $jmpStoreFin
$skipEolLet = $code.Count
Emit-B @(0x8A, 0x06)
Emit-B @(0x84, 0xC0); $jzLetEof = (Emit-JccFwd32 0x84)
Emit-B @(0x3C, 0x0A); $jeLetLf = (Emit-JccFwd32 0x84)
Emit-B @(0x48, 0xFF, 0xC6)
Emit-JmpBack32 $skipEolLet

Patch-JccFwd32 $jeLetLf
Emit-B @(0x48, 0xFF, 0xC6)             # inc rsi
Emit-JmpBack32 $skipWsOff

# ==============================================================================
# PARSE PRINT:
# ==============================================================================
Patch-JmpFwd32 $jmpParsePrint
Emit-B @(0x48, 0x83, 0xC6, 0x05)       # add rsi, 5 (skip "print")

# Skip spaces
$skipSpPrint = $code.Count
Emit-B @(0x8A, 0x06)
Emit-B @(0x3C, 0x20)
$jneCheckArg = (Emit-JccFwd32 0x85)
Emit-B @(0x48, 0xFF, 0xC6)
Emit-JmpBack32 $skipSpPrint

Patch-JccFwd32 $jneCheckArg
Emit-B @(0x3C, 0x22); $jePrintStr = (Emit-JccFwd32 0x84) # '"'

# ------------------------------------------------------------------------------
# PRINT INTEGER (VAR OR NUMBER):
# ------------------------------------------------------------------------------
Emit-B @(0x3C, 0x30); $jbPrnVar = (Emit-JccFwd32 0x82)
Emit-B @(0x3C, 0x39); $jaPrnVar = (Emit-JccFwd32 0x87)

# Print immediate number:
$dispCall = $parseUintOff - ($code.Count + 5)
Emit-B @(0xE8); Emit-B ([BitConverter]::GetBytes([int32]$dispCall))
# Emit: mov rcx, imm32 (48 C7 C1 <imm32>)
Emit-B @(0xC6, 0x07, 0x48); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0xC7); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0xC1); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0x89, 0x07); Emit-B @(0x48, 0x83, 0xC7, 0x04)
$jmpEmitPrintCall = (Emit-JmpFwd32)

# Print variable:
Patch-JccFwd32 $jbPrnVar; Patch-JccFwd32 $jaPrnVar
Emit-B @(0x48, 0xFF, 0xC6)             # inc rsi
Emit-B @(0x0C, 0x20)                   # or al, 0x20

# VALIDATE VAR RANGE:
Emit-B @(0x3C, 0x61); $jbVarBad4 = (Emit-JccFwd32 0x82)
Emit-B @(0x3C, 0x7A); $jaVarBad4 = (Emit-JccFwd32 0x87)
$jmpVarOk4 = (Emit-JmpFwd32)
Patch-JccFwd32 $jbVarBad4; Patch-JccFwd32 $jaVarBad4
Emit-JmpBack32 $errSyntaxPos
Patch-JmpFwd32 $jmpVarOk4

Emit-B @(0x2C, 0x61)                   # sub al, 'a'
Emit-B @(0x0F, 0xB6, 0xC8)             # movzx ecx, al
Emit-B @(0xC1, 0xE1, 0x03)             # shl ecx, 3
Emit-B @(0x83, 0xC1, 0x30)             # add ecx, 0x30 (slot)
# Emit: mov rcx, [rbp + slot32] (48 8B 8D <disp32>)
Emit-B @(0xC6, 0x07, 0x48); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x8B); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x8D); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0x89, 0x0F); Emit-B @(0x48, 0x83, 0xC7, 0x04)

# emit_print_int_call:
Patch-JmpFwd32 $jmpEmitPrintCall
# Emit: call print_int (E8 <disp32>)
# print_int is at RVA 0x1C80
# Next RIP = (rdi - peTemplateBase) + 0x0E05
# disp32 = 0x1C80 - Next RIP
Emit-B @(0x48, 0x89, 0xF8)             # mov rax, rdi
Emit-B @(0x48, 0x2B, 0x44, 0x24, 0x38) # sub rax, [rsp + 0x38]
Emit-B @(0x05, 0x05, 0x0E, 0x00, 0x00) # add eax, 0x0E05 (Next RIP)
Emit-B @(0xBA, 0x80, 0x7C, 0x00, 0x00) # mov edx, 0x7C80
Emit-B @(0x29, 0xC2)                   # sub edx, eax (edx = disp32)
Emit-B @(0xC6, 0x07, 0xE8); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0x89, 0x17); Emit-B @(0x48, 0x83, 0xC7, 0x04)
$jmpSkipEolPrint = (Emit-JmpFwd32)

# ------------------------------------------------------------------------------
# PRINT STRING:
# ------------------------------------------------------------------------------
Patch-JccFwd32 $jePrintStr
Emit-B @(0x48, 0xFF, 0xC6)             # inc rsi (skip opening '"')

# Calculate string RVA in app.exe:
# strRVA = (rbp - peTemplateBase) + 0x1000
Emit-B @(0x48, 0x89, 0xE8)             # mov rax, rbp
Emit-B @(0x48, 0x2B, 0x44, 0x24, 0x38) # sub rax, [rsp + 0x38]
Emit-B @(0x05, 0x00, 0x10, 0x00, 0x00) # add eax, 0x1000
Emit-B @(0x89, 0x44, 0x24, 0x54)       # mov [rsp + 0x54], eax (strRVA)

# Copy string chars to [rbp]
Emit-B @(0x31, 0xC9)                   # xor ecx, ecx (len = 0)
$copyStrLoop = $code.Count
Emit-B @(0x8A, 0x06)                   # mov al, [rsi]
Emit-B @(0x84, 0xC0); $jzStrEnd = (Emit-JccFwd32 0x84)
Emit-B @(0x3C, 0x22); $jeStrEndQuote = (Emit-JccFwd32 0x84) # '"'
Emit-B @(0x3C, 0x0D); $jeStrEndCr = (Emit-JccFwd32 0x84)
Emit-B @(0x3C, 0x0A); $jeStrEndLf = (Emit-JccFwd32 0x84)
Emit-B @(0x88, 0x45, 0x00)             # mov [rbp + 0], al
Emit-B @(0x48, 0xFF, 0xC6)             # inc rsi
Emit-B @(0x48, 0xFF, 0xC5)             # inc rbp
Emit-B @(0xFF, 0xC1)                   # inc ecx
Emit-JmpBack32 $copyStrLoop

Patch-JccFwd32 $jeStrEndQuote
Emit-B @(0x48, 0xFF, 0xC6)             # inc rsi (skip closing quote)

Patch-JccFwd32 $jzStrEnd; Patch-JccFwd32 $jeStrEndCr; Patch-JccFwd32 $jeStrEndLf
# Append \r \n \0
Emit-B @(0xC6, 0x45, 0x00, 0x0D); Emit-B @(0x48, 0xFF, 0xC5); Emit-B @(0xFF, 0xC1) # \r
Emit-B @(0xC6, 0x45, 0x00, 0x0A); Emit-B @(0x48, 0xFF, 0xC5); Emit-B @(0xFF, 0xC1) # \n
Emit-B @(0xC6, 0x45, 0x00, 0x00); Emit-B @(0x48, 0xFF, 0xC5)                         # \0

# Emit WriteFile to app.exe at [rdi]:
# 1. mov rcx, r12 (4C 89 E1)
Emit-B @(0xC6, 0x07, 0x4C); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x89); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0xE1); Emit-B @(0x48, 0xFF, 0xC7)

# 2. lea rdx, [rip + disp32] (48 8D 15 <disp32>)
# Next RIP = (rdi - peTemplateBase) + 0x0E07
# disp32 = strRVA - Next RIP
Emit-B @(0x48, 0x89, 0xF8)             # mov rax, rdi
Emit-B @(0x48, 0x2B, 0x44, 0x24, 0x38) # sub rax, [rsp + 0x38]
Emit-B @(0x05, 0x07, 0x0E, 0x00, 0x00) # add eax, 0x0E07 (Next RIP)
Emit-B @(0x8B, 0x54, 0x24, 0x54)       # mov edx, [rsp + 0x54] (strRVA)
Emit-B @(0x29, 0xC2)                   # sub edx, eax (disp32)
Emit-B @(0xC6, 0x07, 0x48); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x8D); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x15); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0x89, 0x17); Emit-B @(0x48, 0x83, 0xC7, 0x04)

# 3. mov r8d, len (41 B8 <len32>)
Emit-B @(0xC6, 0x07, 0x41); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0xB8); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0x89, 0x0F); Emit-B @(0x48, 0x83, 0xC7, 0x04)

# 4. lea r9, [rsp + 0x28] (4C 8D 4C 24 28)
$leaR9 = @(0x4C, 0x8D, 0x4C, 0x24, 0x28)
foreach ($b in $leaR9) { Emit-B @(0xC6, 0x07, $b); Emit-B @(0x48, 0xFF, 0xC7) }

# 5. mov qword ptr [rsp + 0x20], 0 (48 C7 44 24 20 00 00 00 00)
$movArg5 = @(0x48, 0xC7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00)
foreach ($b in $movArg5) { Emit-B @(0xC6, 0x07, $b); Emit-B @(0x48, 0xFF, 0xC7) }

# 6. call WriteFile (FF 15 <disp32>) -> IAT 0x2030
# Next RIP = (rdi - peTemplateBase) + 0x0E06
# disp32 = 0x2030 - Next RIP
Emit-B @(0x48, 0x89, 0xF8)             # mov rax, rdi
Emit-B @(0x48, 0x2B, 0x44, 0x24, 0x38) # sub rax, [rsp + 0x38]
Emit-B @(0x05, 0x06, 0x0E, 0x00, 0x00) # add eax, 0x0E06 (Next RIP)
Emit-B @(0xBA, 0x30, 0x80, 0x00, 0x00) # mov edx, 0x8030
Emit-B @(0x29, 0xC2)                   # sub edx, eax (edx = disp32)
Emit-B @(0xC6, 0x07, 0xFF); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x15); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0x89, 0x17); Emit-B @(0x48, 0x83, 0xC7, 0x04)

# Skip to end of line after print
Patch-JmpFwd32 $jmpSkipEolPrint
$skipEolPrint = $code.Count
Emit-B @(0x8A, 0x06)
Emit-B @(0x84, 0xC0); $jzPrnEof = (Emit-JccFwd32 0x84)
Emit-B @(0x3C, 0x0A); $jePrnLf = (Emit-JccFwd32 0x84)
Emit-B @(0x48, 0xFF, 0xC6)
Emit-JmpBack32 $skipEolPrint

Patch-JccFwd32 $jePrnLf
Emit-B @(0x48, 0xFF, 0xC6)             # inc rsi
Emit-JmpBack32 $skipWsOff

# ==============================================================================
# 9. COMPILATION FINISH & WRITE app.exe
# ==============================================================================
Patch-JccFwd32 $jzEof; Patch-JccFwd32 $jzCmtEof
Patch-JccFwd32 $jzLetEof; Patch-JccFwd32 $jzPrnEof
Patch-JccFwd32 $jzCallEof; Patch-JccFwd32 $jzRetEof; Patch-JccFwd32 $jzReadEof

# Check that block_depth == 0 (all blocks closed!)
Emit-B @(0x8B, 0x44, 0x24, 0x58)       # mov eax, [rsp + 0x58]
Emit-B @(0x85, 0xC0)                   # test eax, eax
$jnzUnclosedBlocks = (Emit-JccFwd32 0x85)
$jmpAllBlocksClosed = (Emit-JmpFwd32)

Patch-JccFwd32 $jnzUnclosedBlocks
Emit-JmpBack32 $errSyntaxPos

Patch-JmpFwd32 $jmpAllBlocksClosed

# Emit ExitProcess(0) to app.exe:
# xor ecx, ecx (31 C9)
Emit-B @(0xC6, 0x07, 0x31); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0xC9); Emit-B @(0x48, 0xFF, 0xC7)
# call ExitProcess (FF 15 <disp32>) -> IAT 0x2038
# Next RIP = (rdi - peTemplateBase) + 0x0E06
# disp32 = 0x2038 - Next RIP
Emit-B @(0x48, 0x89, 0xF8)             # mov rax, rdi
Emit-B @(0x48, 0x2B, 0x44, 0x24, 0x38) # sub rax, [rsp + 0x38]
Emit-B @(0x05, 0x06, 0x0E, 0x00, 0x00) # add eax, 0x0E06
Emit-B @(0xBA, 0x38, 0x80, 0x00, 0x00) # mov edx, 0x8038
Emit-B @(0x29, 0xC2)                   # sub edx, eax
Emit-B @(0xC6, 0x07, 0xFF); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0xC6, 0x07, 0x15); Emit-B @(0x48, 0xFF, 0xC7)
Emit-B @(0x89, 0x17); Emit-B @(0x48, 0x83, 0xC7, 0x04)

# CreateFileA("app.exe", GENERIC_WRITE, 0, 0, CREATE_ALWAYS=2, 0x80, 0) -> IAT 0x3038
Emit-LeaRIP 0x0D 0x33E0                # lea rcx, [rip + "app.exe"]
Emit-B @(0xBA, 0x00, 0x00, 0x00, 0x40) # mov edx, 0x40000000
Emit-B @(0x45, 0x31, 0xC0)             # xor r8d, r8d
Emit-B @(0x45, 0x31, 0xC9)             # xor r9d, r9d
Emit-B @(0x48, 0xC7, 0x44, 0x24, 0x20, 0x02, 0x00, 0x00, 0x00) # 2
Emit-B @(0x48, 0xC7, 0x44, 0x24, 0x28, 0x80, 0x00, 0x00, 0x00) # 0x80
Emit-B @(0x48, 0xC7, 0x44, 0x24, 0x30, 0x00, 0x00, 0x00, 0x00) # 0
Emit-CallIAT 0x3038
Emit-B @(0x48, 0x89, 0xC6)             # mov rsi, rax (rsi = hApp)

# WriteFile(hApp, peTemplate, 8192 = 0x2000, &written, 0) -> IAT 0x3030
Emit-B @(0x48, 0x89, 0xF1)             # mov rcx, rsi
Emit-B @(0x48, 0x8B, 0x54, 0x24, 0x38) # mov rdx, [rsp + 0x38] (peTemplate base)
Emit-B @(0x41, 0xB8, 0x00, 0x80, 0x00, 0x00) # mov r8d, 32768 (0x8000)
Emit-B @(0x4C, 0x8D, 0x4C, 0x24, 0x40) # lea r9, [rsp + 0x40]
Emit-B @(0x48, 0xC7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00)
Emit-CallIAT 0x3030

# CloseHandle(hApp) -> IAT 0x3048
Emit-B @(0x48, 0x89, 0xF1)             # mov rcx, rsi
Emit-CallIAT 0x3048

# WriteFile(hStdOut, sSuccess, successLen, &written, 0) -> IAT 0x3030
Emit-B @(0x48, 0x89, 0xD9)             # mov rcx, rbx
Emit-LeaRIP 0x15 0x3200                # lea rdx, [rip + sSuccess]
Emit-B @(0x41, 0xB8); Emit-B ([BitConverter]::GetBytes([int32]$successLen))
Emit-B @(0x4C, 0x8D, 0x4C, 0x24, 0x40)
Emit-B @(0x48, 0xC7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00)
Emit-CallIAT 0x3030

# ExitProcess(0) -> IAT 0x3050
Emit-B @(0x31, 0xC9)
Emit-CallIAT 0x3050

Write-Host "[+] Assembled nexc.exe code ($($code.Count) bytes, allocated 0x1E00 = 7680 bytes)"
N-C 0x0200 $code.ToArray()

# ------------------------------------------------------------------------------
# STEP 4: WRITE nexc.hex AND DECODE WITH certutil
# ------------------------------------------------------------------------------
$hexLines = @()
for ($i = 0; $i -lt $nexcBytes.Length; $i += 16) {
    $chunk = $nexcBytes[$i..([Math]::Min($i + 15, $nexcBytes.Length - 1))]
    $hexStr = ($chunk | ForEach-Object { $_.ToString("X2") }) -join " "
    $hexLines += $hexStr
}
[System.IO.File]::WriteAllLines($nexcHex, $hexLines)
Write-Host "[+] Generated $nexcHex ($($hexLines.Count) lines, $($nexcBytes.Length) bytes)"

# Run certutil -decodehex to create nexc.exe (0% C#)
Write-Host "[+] Decoding $nexcHex -> $nexcExe via certutil..."
$res = & certutil -f -decodehex $nexcHex $nexcExe
Write-Host $res
Write-Host "[+] Build complete!"






