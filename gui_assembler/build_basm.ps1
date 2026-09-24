# ==============================================================================
#                  BASM v2.0: THE NATIVE WIN32 ASSEMBLER BUILDER
#   Creates basm.hex (0% C#). Decoded with certutil -> basm.exe.
# ==============================================================================

$winTemplate = [System.IO.File]::ReadAllBytes((Join-Path $PSScriptRoot "test_win.exe"))
Write-Host "Template size: $($winTemplate.Length) bytes"

# File Size: 0x1800 (6,144 bytes)
# 0x0000 .. 0x01FF : Headers (0x200)
# 0x0200 .. 0x05FF : .text (Code, 0x400 bytes, RVA 0x1000)
# 0x0600 .. 0x09FF : .rdata (Imports & Strings, 0x400 bytes, RVA 0x2000)
# 0x0A00 .. 0x17FF : .data (PE Template & Buffer, 0xE00 bytes, RVA 0x3000)

$fileBytes = New-Object byte[] 0x1800

function Set-C($offset, $bytes) { for ($i = 0; $i -lt $bytes.Length; $i++) { $fileBytes[$offset + $i] = $bytes[$i] } }
function Set-16($offset, [uint16]$v) { Set-C $offset ([BitConverter]::GetBytes($v)) }
function Set-32($offset, [uint32]$v) { Set-C $offset ([BitConverter]::GetBytes($v)) }
function Set-64($offset, [uint64]$v) { Set-C $offset ([BitConverter]::GetBytes($v)) }
function Set-S($offset, [string]$s) { Set-C $offset ([System.Text.Encoding]::ASCII.GetBytes($s)) }

# Headers
Set-C 0x00 @(0x4D, 0x5A); Set-32 0x3C 0x40
Set-C 0x40 @(0x50, 0x45, 0x00, 0x00); Set-16 0x44 0x8664; Set-16 0x46 3; Set-16 0x54 0xF0; Set-16 0x56 0x22

# Opt (PE32+)
Set-16 0x58 0x020B; Set-C 0x5A @(1, 0); Set-32 0x5C 0x400; Set-32 0x60 0x1200
Set-32 0x68 0x1000; Set-32 0x6C 0x1000; Set-64 0x70 0x400000
Set-32 0x78 0x1000; Set-32 0x7C 0x200; Set-16 0x80 6; Set-16 0x88 6; Set-32 0x90 0x5000; Set-32 0x94 0x200
Set-16 0x9C 3 # Console
Set-64 0xA0 0x100000; Set-64 0xA8 0x1000; Set-64 0xB0 0x100000; Set-64 0xB8 0x1000; Set-32 0xC4 16
Set-32 0xD0 0x2000; Set-32 0xD4 0x28 # Import dir

# Section Headers
# .text
Set-S 0x148 ".text"; Set-32 0x150 0x400; Set-32 0x154 0x1000; Set-32 0x158 0x400; Set-32 0x15C 0x200; Set-32 0x16C 0x60000020
# .rdata
Set-S 0x170 ".rdata"; Set-32 0x178 0x400; Set-32 0x17C 0x2000; Set-32 0x180 0x400; Set-32 0x184 0x600; Set-32 0x194 0x40000040
# .data
Set-S 0x198 ".data"; Set-32 0x1A0 0x2000; Set-32 0x1A4 0x3000; Set-32 0x1A8 0xE00; Set-32 0x1AC 0xA00
# Characteristics: READ | WRITE | INITIALIZED_DATA
$fileBytes[0x1BC] = 0x40; $fileBytes[0x1BD] = 0x00; $fileBytes[0x1BE] = 0x00; $fileBytes[0x1BF] = 0xC0

# ------------------------------------------------------------------------------
# .rdata at 0x600 (RVA 0x2000)
# ------------------------------------------------------------------------------
$rd = 0x0600
Set-32 ($rd + 0x00) 0x2060 # ILT RVA
Set-32 ($rd + 0x0C) 0x20A0 # Name RVA ("kernel32.dll")
Set-32 ($rd + 0x10) 0x2028 # IAT RVA

# IAT & ILT (6 functions: GetStdHandle, WriteFile, CreateFileA, ReadFile, CloseHandle, ExitProcess)
$hints = @(0x20B0, 0x20C6, 0x20D8, 0x20EE, 0x2100, 0x2114)
for ($i = 0; $i -lt $hints.Length; $i++) {
    Set-64 ($rd + 0x28 + ($i * 8)) $hints[$i] # IAT
    Set-64 ($rd + 0x60 + ($i * 8)) $hints[$i] # ILT
}

Set-S ($rd + 0xA0) "kernel32.dll`0"
Set-16 ($rd + 0xB0) 0; Set-S ($rd + 0xB2) "GetStdHandle`0"
Set-16 ($rd + 0xC6) 0; Set-S ($rd + 0xC8) "WriteFile`0"
Set-16 ($rd + 0xD8) 0; Set-S ($rd + 0xDA) "CreateFileA`0"
Set-16 ($rd + 0xEE) 0; Set-S ($rd + 0xF0) "ReadFile`0"
Set-16 ($rd + 0x100) 0; Set-S ($rd + 0x102) "CloseHandle`0"
Set-16 ($rd + 0x114) 0; Set-S ($rd + 0x116) "ExitProcess`0"

$banner = "[+] ==================================================`r`n" +
          "[+] BASM v2.0: Native Win32 Assembler & GUI Engine`r`n" +
          "[+] Reading input.txt...`r`n"
Set-S ($rd + 0x130) $banner
$bannerLen = [System.Text.Encoding]::ASCII.GetBytes($banner).Length

$success = "[+] Compiled input.txt -> output.exe (2,560 bytes)!`r`n" +
           "[+] Standalone Native Win32 Window built with 0% C#!`r`n" +
           "[+] ==================================================`r`n"
Set-S ($rd + 0x1C0) $success
$successLen = [System.Text.Encoding]::ASCII.GetBytes($success).Length

Set-S ($rd + 0x280) "input.txt`0"
Set-S ($rd + 0x290) "output.exe`0"

# Copy WinTemplate to .data (RVA 0x3000, File offset 0x0A00)
Set-C 0x0A00 $winTemplate

# ------------------------------------------------------------------------------
# .text Code Generation (Dynamic RIP offsets)
# ------------------------------------------------------------------------------
$code = New-Object System.Collections.Generic.List[byte]

function Emit-B($b) { foreach ($x in $b) { $code.Add($x) } }
function Emit-Call($targetRVA) {
    Emit-B @(0xFF, 0x15)
    $nextRIP = 0x1000 + $code.Count + 4
    $disp = $targetRVA - $nextRIP
    Emit-B ([BitConverter]::GetBytes([int]$disp))
}
function Emit-Lea($regByte, $targetRVA) {
    Emit-B @(0x48, 0x8D, $regByte)
    $nextRIP = 0x1000 + $code.Count + 4
    $disp = $targetRVA - $nextRIP
    Emit-B ([BitConverter]::GetBytes([int]$disp))
}

# 1. sub rsp, 0x58 (16-byte aligned)
Emit-B @(0x48, 0x83, 0xEC, 0x58)

# 2. GetStdHandle(-11) -> IAT 0x2028
Emit-B @(0xB9, 0xF5, 0xFF, 0xFF, 0xFF) # mov ecx, -11
Emit-Call 0x2028                       # call GetStdHandle
Emit-B @(0x48, 0x89, 0xC3)             # mov rbx, rax (rbx = hStdOut)

# 3. WriteFile(hStdOut, banner, bannerLen, &written, 0) -> IAT 0x2030
Emit-B @(0x48, 0x89, 0xD9)             # mov rcx, rbx
Emit-Lea 0x15 0x2130                   # lea rdx, [rip + sBanner]
Emit-B @(0x41, 0xB8)                   # mov r8d, bannerLen
Emit-B ([BitConverter]::GetBytes([int32]$bannerLen))
Emit-B @(0x4C, 0x8D, 0x4C, 0x24, 0x40) # lea r9, [rsp + 0x40]
Emit-B @(0x48, 0xC7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00) # mov [rsp+0x20], 0
Emit-Call 0x2030                       # call WriteFile

# 4. Open input.txt: CreateFileA("input.txt", GENERIC_READ, 1, 0, OPEN_EXISTING=3, 0x80, 0) -> IAT 0x2038
Emit-Lea 0x0D 0x2280                   # lea rcx, [rip + "input.txt"]
Emit-B @(0xBA, 0x00, 0x00, 0x00, 0x80) # mov edx, 0x80000000
Emit-B @(0x41, 0xB8, 0x01, 0x00, 0x00, 0x00) # mov r8d, 1
Emit-B @(0x45, 0x31, 0xC9)             # xor r9d, r9d
Emit-B @(0x48, 0xC7, 0x44, 0x24, 0x20, 0x03, 0x00, 0x00, 0x00) # mov [rsp+0x20], 3
Emit-B @(0x48, 0xC7, 0x44, 0x24, 0x28, 0x80, 0x00, 0x00, 0x00) # mov [rsp+0x28], 0x80
Emit-B @(0x48, 0xC7, 0x44, 0x24, 0x30, 0x00, 0x00, 0x00, 0x00) # mov [rsp+0x30], 0
Emit-Call 0x2038                       # call CreateFileA
Emit-B @(0x48, 0x89, 0xC6)             # mov rsi, rax (rsi = hInput)

# 5. ReadFile(hInput, inputBuf, 1024, &bytesRead, 0) -> IAT 0x2040
# inputBuf is at RVA 0x3A00
Emit-B @(0x48, 0x89, 0xF1)             # mov rcx, rsi
Emit-Lea 0x15 0x3A00                   # lea rdx, [rip + inputBuf]
Emit-B @(0x41, 0xB8, 0x00, 0x04, 0x00, 0x00) # mov r8d, 1024
Emit-B @(0x4C, 0x8D, 0x4C, 0x24, 0x40) # lea r9, [rsp + 0x40]
Emit-B @(0x48, 0xC7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00)
Emit-Call 0x2040                       # call ReadFile

# CloseHandle(hInput) -> IAT 0x2048
Emit-B @(0x48, 0x89, 0xF1)             # mov rcx, rsi
Emit-Call 0x2048                       # call CloseHandle

# ==============================================================================
# 6. PARSER & DYNAMIC PE PATCHING
# ==============================================================================
# rsi = inputBuf (0x3A00)
# Line 1 -> Window Title (template_title at RVA 0x3920)
Emit-Lea 0x35 0x3A00                   # lea rsi, [rip + inputBuf]
Emit-Lea 0x3D 0x3920                   # lea rdi, [rip + template_title] (0x3920)
Emit-B @(0xB9, 0x40, 0x00, 0x00, 0x00) # mov ecx, 64 (max title length)

# copy_title_loop:
$titleLoopOff = $code.Count
Emit-B @(0xAC)                         # lodsb
Emit-B @(0x3C, 0x0D)                   # cmp al, '\r'
$jeTitle1 = $code.Count; Emit-B @(0x74, 0x00)
Emit-B @(0x3C, 0x0A)                   # cmp al, '\n'
$jeTitle2 = $code.Count; Emit-B @(0x74, 0x00)
Emit-B @(0xAA)                         # stosb
$disp = $titleLoopOff - ($code.Count + 2)
Emit-B @(0xE2, [byte]($disp -band 0xFF)) # loop copy_title_loop

# done_title:
$code[$jeTitle1 + 1] = [byte]($code.Count - ($jeTitle1 + 2))
$code[$jeTitle2 + 1] = [byte]($code.Count - ($jeTitle2 + 2))
Emit-B @(0xC6, 0x07, 0x00)             # mov byte ptr [rdi], 0

# Skip newline characters in rsi:
# skip_nl_loop:
$skipNlOff = $code.Count
Emit-B @(0x8A, 0x06)                   # mov al, [rsi]
Emit-B @(0x3C, 0x0D)                   # cmp al, '\r'
$jeSkip1 = $code.Count; Emit-B @(0x74, 0x00)
Emit-B @(0x3C, 0x0A)                   # cmp al, '\n'
$jeSkip2 = $code.Count; Emit-B @(0x74, 0x00)
# jmp start_text
$jmpStartText = $code.Count; Emit-B @(0xEB, 0x00)

# advance_nl:
$code[$jeSkip1 + 1] = [byte]($code.Count - ($jeSkip1 + 2))
$code[$jeSkip2 + 1] = [byte]($code.Count - ($jeSkip2 + 2))
Emit-B @(0x48, 0xFF, 0xC6)             # inc rsi
$disp = $skipNlOff - ($code.Count + 2)
Emit-B @(0xEB, [byte]($disp -band 0xFF))

# start_text:
$code[$jmpStartText + 1] = [byte]($code.Count - ($jmpStartText + 2))

# Line 2 -> Window Centered Text (template_text at RVA 0x3960)
Emit-Lea 0x3D 0x3960                   # lea rdi, [rip + template_text] (0x3960)
Emit-B @(0xB9, 0x80, 0x00, 0x00, 0x00) # mov ecx, 128 (max text length)

# copy_text_loop:
$textLoopOff = $code.Count
Emit-B @(0xAC)                         # lodsb
Emit-B @(0x3C, 0x0D)                   # cmp al, '\r'
$jeText1 = $code.Count; Emit-B @(0x74, 0x00)
Emit-B @(0x3C, 0x0A)                   # cmp al, '\n'
$jeText2 = $code.Count; Emit-B @(0x74, 0x00)
Emit-B @(0xAA)                         # stosb
$disp = $textLoopOff - ($code.Count + 2)
Emit-B @(0xE2, [byte]($disp -band 0xFF)) # loop copy_text_loop

# done_text:
$code[$jeText1 + 1] = [byte]($code.Count - ($jeText1 + 2))
$code[$jeText2 + 1] = [byte]($code.Count - ($jeText2 + 2))
Emit-B @(0xC6, 0x07, 0x00)             # mov byte ptr [rdi], 0

# ==============================================================================
# 7. EMIT OUTPUT.EXE (2,560 BYTES)
# ==============================================================================
# CreateFileA("output.exe", GENERIC_WRITE, 0, 0, CREATE_ALWAYS=2, 0x80, 0) -> IAT 0x2038
Emit-Lea 0x0D 0x2290                   # lea rcx, [rip + "output.exe"]
Emit-B @(0xBA, 0x00, 0x00, 0x00, 0x40) # mov edx, 0x40000000 (GENERIC_WRITE)
Emit-B @(0x45, 0x31, 0xC0)             # xor r8d, r8d
Emit-B @(0x45, 0x31, 0xC9)             # xor r9d, r9d
Emit-B @(0x48, 0xC7, 0x44, 0x24, 0x20, 0x02, 0x00, 0x00, 0x00) # mov [rsp+0x20], 2
Emit-B @(0x48, 0xC7, 0x44, 0x24, 0x28, 0x80, 0x00, 0x00, 0x00) # mov [rsp+0x28], 0x80
Emit-B @(0x48, 0xC7, 0x44, 0x24, 0x30, 0x00, 0x00, 0x00, 0x00) # mov [rsp+0x30], 0
Emit-Call 0x2038                       # call CreateFileA
Emit-B @(0x48, 0x89, 0xC7)             # mov rdi, rax (rdi = hOutput)

# 8. WriteFile(hOutput, peTemplate, 2560, &written, 0) -> IAT 0x2030
Emit-B @(0x48, 0x89, 0xF9)             # mov rcx, rdi
Emit-Lea 0x15 0x3000                   # lea rdx, [rip + peTemplate]
Emit-B @(0x41, 0xB8, 0x00, 0x0A, 0x00, 0x00) # mov r8d, 2560 (0x0A00)
Emit-B @(0x4C, 0x8D, 0x4C, 0x24, 0x40) # lea r9, [rsp + 0x40]
Emit-B @(0x48, 0xC7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00)
Emit-Call 0x2030                       # call WriteFile

# CloseHandle(hOutput) -> IAT 0x2048
Emit-B @(0x48, 0x89, 0xF9)             # mov rcx, rdi
Emit-Call 0x2048                       # call CloseHandle

# 9. Print Success Message -> IAT 0x2030
Emit-B @(0x48, 0x89, 0xD9)             # mov rcx, rbx (hStdOut)
Emit-Lea 0x15 0x21C0                   # lea rdx, [rip + sSuccess]
Emit-B @(0x41, 0xB8)                   # mov r8d, successLen
Emit-B ([BitConverter]::GetBytes([int32]$successLen))
Emit-B @(0x4C, 0x8D, 0x4C, 0x24, 0x40) # lea r9, [rsp + 0x40]
Emit-B @(0x48, 0xC7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00)
Emit-Call 0x2030                       # call WriteFile

# 10. ExitProcess(0) -> IAT 0x2050
Emit-B @(0x31, 0xC9)                   # xor ecx, ecx
Emit-Call 0x2050                       # call ExitProcess

Write-Host "Total code bytes: $($code.Count) (allocated: 0x400 = 1024)"
Set-C 0x0200 $code.ToArray()

# Save basm.exe
$basmExe = Join-Path $PSScriptRoot "basm.exe"
$basmHex = Join-Path $PSScriptRoot "basm.hex"

[System.IO.File]::WriteAllBytes($basmExe, $fileBytes)

# Generate basm.hex
$hexLines = @()
for ($i = 0; $i -lt $fileBytes.Length; $i += 16) {
    $chunk = $fileBytes[$i..([Math]::Min($i + 15, $fileBytes.Length - 1))]
    $hexStr = ($chunk | ForEach-Object { $_.ToString("X2") }) -join " "
    $hexLines += $hexStr
}
[System.IO.File]::WriteAllLines($basmHex, $hexLines)

Write-Host "[+] Generated $basmExe ($($fileBytes.Length) bytes)"
Write-Host "[+] Generated $basmHex ($($hexLines.Count) lines)"
