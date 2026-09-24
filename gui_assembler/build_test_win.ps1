# Test generator for standalone Win32 Window in pure x86-64 machine code
$outFile = Join-Path $PSScriptRoot "test_win.exe"

function Build-PE {
    $headerSize = 0x0200    # 512
    $textFileSize = 0x0400  # 1024 bytes for code
    $rdataFileSize = 0x0400 # 1024 bytes for imports & data
    $totalFileSize = $headerSize + $textFileSize + $rdataFileSize # 2048 bytes

    $fileBytes = New-Object byte[] $totalFileSize

    # RVAs:
    # Headers: 0..0x0FFF
    # .text: RVA 0x1000 (File offset 0x0200, size 0x0400)
    # .rdata: RVA 0x2000 (File offset 0x0600, size 0x0400)

    # 1. DOS Header
    $fileBytes[0] = 0x4D; $fileBytes[1] = 0x5A # MZ
    $e_lfanew = 0x80
    [BitConverter]::GetBytes([uint32]$e_lfanew).CopyTo($fileBytes, 0x3C)

    # 2. PE Signature
    $fileBytes[$e_lfanew] = 0x50; $fileBytes[$e_lfanew + 1] = 0x45 # PE\0\0

    # 3. File Header
    $fh = $e_lfanew + 4
    [BitConverter]::GetBytes([uint16]0x8664).CopyTo($fileBytes, $fh + 0) # Machine: AMD64
    [BitConverter]::GetBytes([uint16]2).CopyTo($fileBytes, $fh + 2)      # NumberOfSections: 2
    [BitConverter]::GetBytes([uint32]0).CopyTo($fileBytes, $fh + 4)      # TimeDateStamp
    [BitConverter]::GetBytes([uint32]0).CopyTo($fileBytes, $fh + 8)      # PointerToSymbolTable
    [BitConverter]::GetBytes([uint32]0).CopyTo($fileBytes, $fh + 12)     # NumberOfSymbols
    [BitConverter]::GetBytes([uint16]0x00F0).CopyTo($fileBytes, $fh + 16) # SizeOfOptionalHeader: 240 bytes
    [BitConverter]::GetBytes([uint16]0x0022).CopyTo($fileBytes, $fh + 18) # Characteristics: EXECUTABLE_IMAGE | LARGE_ADDRESS_AWARE

    # 4. Optional Header (PE32+ / 64-bit)
    $opt = $fh + 20
    [BitConverter]::GetBytes([uint16]0x020B).CopyTo($fileBytes, $opt + 0) # Magic: PE32+
    $fileBytes[$opt + 2] = 1 # MajorLinkerVersion
    $fileBytes[$opt + 3] = 0 # MinorLinkerVersion
    [BitConverter]::GetBytes([uint32]$textFileSize).CopyTo($fileBytes, $opt + 4)  # SizeOfCode
    [BitConverter]::GetBytes([uint32]$rdataFileSize).CopyTo($fileBytes, $opt + 8) # SizeOfInitializedData
    [BitConverter]::GetBytes([uint32]0).CopyTo($fileBytes, $opt + 12)             # SizeOfUninitializedData
    [BitConverter]::GetBytes([uint32]0x1000).CopyTo($fileBytes, $opt + 16)        # AddressOfEntryPoint (RVA 0x1000)
    [BitConverter]::GetBytes([uint32]0x1000).CopyTo($fileBytes, $opt + 20)        # BaseOfCode

    # Windows-specific fields
    [BitConverter]::GetBytes([uint64]0x0000000140000000L).CopyTo($fileBytes, $opt + 24) # ImageBase
    [BitConverter]::GetBytes([uint32]0x1000).CopyTo($fileBytes, $opt + 32)        # SectionAlignment
    [BitConverter]::GetBytes([uint32]0x0200).CopyTo($fileBytes, $opt + 36)        # FileAlignment
    [BitConverter]::GetBytes([uint16]6).CopyTo($fileBytes, $opt + 40)             # MajorOperatingSystemVersion
    [BitConverter]::GetBytes([uint16]0).CopyTo($fileBytes, $opt + 42)             # MinorOperatingSystemVersion
    [BitConverter]::GetBytes([uint16]6).CopyTo($fileBytes, $opt + 48)             # MajorSubsystemVersion
    [BitConverter]::GetBytes([uint16]0).CopyTo($fileBytes, $opt + 50)             # MinorSubsystemVersion
    [BitConverter]::GetBytes([uint32]0x3000).CopyTo($fileBytes, $opt + 56)        # SizeOfImage (3 pages: headers, .text, .rdata)
    [BitConverter]::GetBytes([uint32]$headerSize).CopyTo($fileBytes, $opt + 60)   # SizeOfHeaders
    [BitConverter]::GetBytes([uint32]0).CopyTo($fileBytes, $opt + 64)             # CheckSum
    [BitConverter]::GetBytes([uint16]2).CopyTo($fileBytes, $opt + 68)             # Subsystem: IMAGE_SUBSYSTEM_WINDOWS_GUI (2)
    [BitConverter]::GetBytes([uint16]0x8160).CopyTo($fileBytes, $opt + 70)         # DllCharacteristics: DYNAMIC_BASE | NX_COMPAT | TERMINAL_SERVER_AWARE
    [BitConverter]::GetBytes([uint64]0x100000L).CopyTo($fileBytes, $opt + 72)     # SizeOfStackReserve
    [BitConverter]::GetBytes([uint64]0x1000L).CopyTo($fileBytes, $opt + 80)       # SizeOfStackCommit
    [BitConverter]::GetBytes([uint64]0x100000L).CopyTo($fileBytes, $opt + 88)     # SizeOfHeapReserve
    [BitConverter]::GetBytes([uint64]0x1000L).CopyTo($fileBytes, $opt + 96)       # SizeOfHeapCommit
    [BitConverter]::GetBytes([uint32]16).CopyTo($fileBytes, $opt + 108)           # NumberOfRvaAndSizes

    # Data Directories (Import Table at entry 1)
    $dirImport = $opt + 112 + (1 * 8)
    [BitConverter]::GetBytes([uint32]0x2000).CopyTo($fileBytes, $dirImport + 0)   # Import Table RVA
    [BitConverter]::GetBytes([uint32]0x0400).CopyTo($fileBytes, $dirImport + 4)   # Import Table Size

    # Section Headers
    $sec1 = $opt + 240
    # .text
    [System.Text.Encoding]::ASCII.GetBytes(".text`0`0`0").CopyTo($fileBytes, $sec1)
    [BitConverter]::GetBytes([uint32]$textFileSize).CopyTo($fileBytes, $sec1 + 8)  # VirtualSize
    [BitConverter]::GetBytes([uint32]0x1000).CopyTo($fileBytes, $sec1 + 12)        # VirtualAddress
    [BitConverter]::GetBytes([uint32]$textFileSize).CopyTo($fileBytes, $sec1 + 16) # SizeOfRawData
    [BitConverter]::GetBytes([uint32]0x0200).CopyTo($fileBytes, $sec1 + 20)        # PointerToRawData
    [BitConverter]::GetBytes([uint32]0x60000020).CopyTo($fileBytes, $sec1 + 36)    # Characteristics: CODE | EXECUTE | READ

    # .rdata
    $sec2 = $sec1 + 40
    [System.Text.Encoding]::ASCII.GetBytes(".rdata`0`0").CopyTo($fileBytes, $sec2)
    [BitConverter]::GetBytes([uint32]$rdataFileSize).CopyTo($fileBytes, $sec2 + 8) # VirtualSize
    [BitConverter]::GetBytes([uint32]0x2000).CopyTo($fileBytes, $sec2 + 12)        # VirtualAddress
    [BitConverter]::GetBytes([uint32]$rdataFileSize).CopyTo($fileBytes, $sec2 + 16)# SizeOfRawData
    [BitConverter]::GetBytes([uint32]0x0600).CopyTo($fileBytes, $sec2 + 20)        # PointerToRawData (0x200 + 0x400 = 0x0600)
    [BitConverter]::GetBytes([uint32]0x40000040).CopyTo($fileBytes, $sec2 + 36)    # Characteristics: INITIALIZED_DATA | READ

    # =========================================================================
    # .RDATA SECTION LAYOUT (RVA 0x2000, File Offset 0x0600)
    # =========================================================================
    $rdataOffset = 0x0600
    $rdataRVA = 0x2000

    $u32Funcs = @(
        "RegisterClassExA",   # 0
        "CreateWindowExA",     # 1
        "ShowWindow",          # 2
        "UpdateWindow",        # 3
        "GetMessageA",         # 4
        "TranslateMessage",    # 5
        "DispatchMessageA",    # 6
        "DefWindowProcA",      # 7
        "PostQuitMessage",     # 8
        "LoadCursorA",         # 9
        "BeginPaint",          # 10
        "EndPaint",            # 11
        "GetClientRect",       # 12
        "DrawTextA"            # 13
    )

    $k32Funcs = @(
        "GetModuleHandleA",    # 0
        "ExitProcess"          # 1
    )

    $descUser32   = $rdataOffset + 0x00
    $descKernel32 = $rdataOffset + 0x14

    $u32INT_Offset = $rdataOffset + 0x40
    $u32INT_RVA    = $rdataRVA + 0x40

    $k32INT_Offset = $rdataOffset + 0xB8
    $k32INT_RVA    = $rdataRVA + 0xB8

    $u32IAT_Offset = $rdataOffset + 0xD0
    $u32IAT_RVA    = $rdataRVA + 0xD0

    $k32IAT_Offset = $rdataOffset + 0x148
    $k32IAT_RVA    = $rdataRVA + 0x148

    $dllNamesOffset = $rdataOffset + 0x160
    $dllNamesRVA    = $rdataRVA + 0x160

    $strU32 = [System.Text.Encoding]::ASCII.GetBytes("user32.dll`0")
    $strU32.CopyTo($fileBytes, $dllNamesOffset)
    $rvaU32Dll = $dllNamesRVA

    $strK32 = [System.Text.Encoding]::ASCII.GetBytes("kernel32.dll`0")
    $strK32.CopyTo($fileBytes, $dllNamesOffset + 16)
    $rvaK32Dll = $dllNamesRVA + 16

    $hintNameOffset = $rdataOffset + 0x180
    $hintNameRVA    = $rdataRVA + 0x180

    $currHintOffset = $hintNameOffset
    $currHintRVA    = $hintNameRVA

    for ($i = 0; $i -lt $u32Funcs.Count; $i++) {
        $name = $u32Funcs[$i]
        $nameBytes = [System.Text.Encoding]::ASCII.GetBytes($name + "`0")
        
        $fileBytes[$currHintOffset] = 0
        $fileBytes[$currHintOffset + 1] = 0
        $nameBytes.CopyTo($fileBytes, $currHintOffset + 2)

        [BitConverter]::GetBytes([uint64]$currHintRVA).CopyTo($fileBytes, $u32INT_Offset + ($i * 8))
        [BitConverter]::GetBytes([uint64]$currHintRVA).CopyTo($fileBytes, $u32IAT_Offset + ($i * 8))

        $entryLen = 2 + $nameBytes.Length
        if ($entryLen % 2 -ne 0) { $entryLen++ }
        $currHintOffset += $entryLen
        $currHintRVA += $entryLen
    }

    for ($i = 0; $i -lt $k32Funcs.Count; $i++) {
        $name = $k32Funcs[$i]
        $nameBytes = [System.Text.Encoding]::ASCII.GetBytes($name + "`0")

        $fileBytes[$currHintOffset] = 0
        $fileBytes[$currHintOffset + 1] = 0
        $nameBytes.CopyTo($fileBytes, $currHintOffset + 2)

        [BitConverter]::GetBytes([uint64]$currHintRVA).CopyTo($fileBytes, $k32INT_Offset + ($i * 8))
        [BitConverter]::GetBytes([uint64]$currHintRVA).CopyTo($fileBytes, $k32IAT_Offset + ($i * 8))

        $entryLen = 2 + $nameBytes.Length
        if ($entryLen % 2 -ne 0) { $entryLen++ }
        $currHintOffset += $entryLen
        $currHintRVA += $entryLen
    }

    [BitConverter]::GetBytes([uint32]$u32INT_RVA).CopyTo($fileBytes, $descUser32 + 0)
    [BitConverter]::GetBytes([uint32]$rvaU32Dll).CopyTo($fileBytes, $descUser32 + 12)
    [BitConverter]::GetBytes([uint32]$u32IAT_RVA).CopyTo($fileBytes, $descUser32 + 16)

    [BitConverter]::GetBytes([uint32]$k32INT_RVA).CopyTo($fileBytes, $descKernel32 + 0)
    [BitConverter]::GetBytes([uint32]$rvaK32Dll).CopyTo($fileBytes, $descKernel32 + 12)
    [BitConverter]::GetBytes([uint32]$k32IAT_RVA).CopyTo($fileBytes, $descKernel32 + 16)

    $strOffset = $rdataOffset + 0x300
    $strRVA = $rdataRVA + 0x300

    $clsName = "PureBinWinClass`0"
    $titleName = "0% C# Native Win32 Window`0"
    $textMsg = "Hello World! Rendered with 100% Machine Code!`0"

    [System.Text.Encoding]::ASCII.GetBytes($clsName).CopyTo($fileBytes, $strOffset)
    $rvaCls = $strRVA

    $titleOffset = $strOffset + 32
    $rvaTitle = $strRVA + 32
    [System.Text.Encoding]::ASCII.GetBytes($titleName).CopyTo($fileBytes, $titleOffset)

    $textOffset = $titleOffset + 64
    $rvaText = $rvaTitle + 64
    [System.Text.Encoding]::ASCII.GetBytes($textMsg).CopyTo($fileBytes, $textOffset)

    # =========================================================================
    # .TEXT SECTION (RVA 0x1000, File Offset 0x0200)
    # =========================================================================
    $code = New-Object System.Collections.Generic.List[byte]
    function AddB([byte[]]$b) { foreach($x in $b) { $code.Add($x) } }

    # sub rsp, 0xE8 (232 bytes, 16-byte aligned)
    AddB @(0x48, 0x81, 0xEC, 0xE8, 0x00, 0x00, 0x00)

    # 1. GetModuleHandleA(0)
    # xor ecx, ecx
    AddB @(0x31, 0xC9)
    # call [__imp_GetModuleHandleA] (0x2148)
    $disp = 0x2148 - (0x1000 + $code.Count + 6)
    AddB @(0xFF, 0x15)
    AddB ([BitConverter]::GetBytes([int32]$disp))
    # mov r12, rax
    AddB @(0x49, 0x89, 0xC4)

    # 2. LoadCursorA(0, IDC_ARROW = 0x7F00)
    # xor ecx, ecx
    AddB @(0x31, 0xC9)
    # mov edx, 0x7F00
    AddB @(0xBA, 0x00, 0x7F, 0x00, 0x00)
    # call [__imp_LoadCursorA] (0x2118)
    $disp = 0x2118 - (0x1000 + $code.Count + 6)
    AddB @(0xFF, 0x15)
    AddB ([BitConverter]::GetBytes([int32]$disp))
    # mov r13, rax
    AddB @(0x49, 0x89, 0xC5)

    # 3. Populate WNDCLASSEXA at [rsp + 0x90]
    # lea rdi, [rsp + 0x90]
    AddB @(0x48, 0x8D, 0xBC, 0x24, 0x90, 0x00, 0x00, 0x00)
    # xor eax, eax
    AddB @(0x31, 0xC0)
    # mov ecx, 10
    AddB @(0xB9, 0x0A, 0x00, 0x00, 0x00)
    # rep stosq
    AddB @(0xF3, 0x48, 0xAB)

    # cbSize = 80
    AddB @(0xC7, 0x84, 0x24, 0x90, 0x00, 0x00, 0x00, 0x50, 0x00, 0x00, 0x00)
    # style = CS_HREDRAW | CS_VREDRAW (3)
    AddB @(0xC7, 0x84, 0x24, 0x94, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00)
    # lea rax, [rip + WndProc] (0x1200)
    $disp = 0x1200 - (0x1000 + $code.Count + 7)
    AddB @(0x48, 0x8D, 0x05)
    AddB ([BitConverter]::GetBytes([int32]$disp))
    # mov [rsp + 0x98], rax (lpfnWndProc)
    AddB @(0x48, 0x89, 0x84, 0x24, 0x98, 0x00, 0x00, 0x00)
    # mov [rsp + 0xA8], r12 (hInstance)
    AddB @(0x4C, 0x89, 0xA4, 0x24, 0xA8, 0x00, 0x00, 0x00)
    # mov [rsp + 0xB8], r13 (hCursor)
    AddB @(0x4C, 0x89, 0xAC, 0x24, 0xB8, 0x00, 0x00, 0x00)
    # mov qword ptr [rsp + 0xC0], 6 (hbrBackground = COLOR_WINDOW + 1)
    AddB @(0x48, 0xC7, 0x84, 0x24, 0xC0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00)
    # lea rax, [rip + szClassName] (0x2300)
    $disp = 0x2300 - (0x1000 + $code.Count + 7)
    AddB @(0x48, 0x8D, 0x05)
    AddB ([BitConverter]::GetBytes([int32]$disp))
    # mov [rsp + 0xD0], rax (lpszClassName)
    AddB @(0x48, 0x89, 0x84, 0x24, 0xD0, 0x00, 0x00, 0x00)

    # 4. RegisterClassExA(&wcex)
    # lea rcx, [rsp + 0x90]
    AddB @(0x48, 0x8D, 0x8C, 0x24, 0x90, 0x00, 0x00, 0x00)
    # call [__imp_RegisterClassExA] (0x20D0)
    $disp = 0x20D0 - (0x1000 + $code.Count + 6)
    AddB @(0xFF, 0x15)
    AddB ([BitConverter]::GetBytes([int32]$disp))

    # 5. CreateWindowExA(0, szClass, szTitle, WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 640, 400, 0, 0, hInstance, 0)
    # xor ecx, ecx (dwExStyle)
    AddB @(0x31, 0xC9)
    # lea rdx, [rip + szClassName] (0x2300)
    $disp = 0x2300 - (0x1000 + $code.Count + 7)
    AddB @(0x48, 0x8D, 0x15)
    AddB ([BitConverter]::GetBytes([int32]$disp))
    # lea r8, [rip + szTitle] (0x2320)
    $disp = 0x2320 - (0x1000 + $code.Count + 7)
    AddB @(0x4C, 0x8D, 0x05)
    AddB ([BitConverter]::GetBytes([int32]$disp))
    # mov r9d, 0x10CF0000 (WS_OVERLAPPEDWINDOW | WS_VISIBLE)
    AddB @(0x41, 0xB9, 0x00, 0x00, 0xCF, 0x10)

    # Stack arguments for CreateWindowExA (all 64-bit slots):
    # [rsp + 0x20] = X = 100 (0x64)
    AddB @(0x48, 0xC7, 0x44, 0x24, 0x20, 0x64, 0x00, 0x00, 0x00)
    # [rsp + 0x28] = Y = 100 (0x64)
    AddB @(0x48, 0xC7, 0x44, 0x24, 0x28, 0x64, 0x00, 0x00, 0x00)
    # [rsp + 0x30] = 640 (nWidth: 0x0280)
    AddB @(0x48, 0xC7, 0x44, 0x24, 0x30, 0x80, 0x02, 0x00, 0x00)
    # [rsp + 0x38] = 400 (nHeight: 0x0190)
    AddB @(0x48, 0xC7, 0x44, 0x24, 0x38, 0x90, 0x01, 0x00, 0x00)
    # [rsp + 0x40] = 0 (hWndParent)
    AddB @(0x48, 0xC7, 0x44, 0x24, 0x40, 0x00, 0x00, 0x00, 0x00)
    # [rsp + 0x48] = 0 (hMenu)
    AddB @(0x48, 0xC7, 0x44, 0x24, 0x48, 0x00, 0x00, 0x00, 0x00)
    # [rsp + 0x50] = r12 (hInstance)
    AddB @(0x4C, 0x89, 0x64, 0x24, 0x50)
    # [rsp + 0x58] = 0 (lpParam)
    AddB @(0x48, 0xC7, 0x44, 0x24, 0x58, 0x00, 0x00, 0x00, 0x00)

    # call [__imp_CreateWindowExA] (0x20D8)
    $disp = 0x20D8 - (0x1000 + $code.Count + 6)
    AddB @(0xFF, 0x15)
    AddB ([BitConverter]::GetBytes([int32]$disp))

    # mov rbx, rax (hwnd)
    AddB @(0x48, 0x89, 0xC3)

    # 6. ShowWindow(hwnd, SW_SHOW = 5)
    # mov rcx, rbx
    AddB @(0x48, 0x89, 0xD9)
    # mov edx, 5
    AddB @(0xBA, 0x05, 0x00, 0x00, 0x00)
    # call [__imp_ShowWindow] (0x20E0)
    $disp = 0x20E0 - (0x1000 + $code.Count + 6)
    AddB @(0xFF, 0x15)
    AddB ([BitConverter]::GetBytes([int32]$disp))

    # 7. UpdateWindow(hwnd)
    # mov rcx, rbx
    AddB @(0x48, 0x89, 0xD9)
    # call [__imp_UpdateWindow] (0x20E8)
    $disp = 0x20E8 - (0x1000 + $code.Count + 6)
    AddB @(0xFF, 0x15)
    AddB ([BitConverter]::GetBytes([int32]$disp))

    # 8. Message Loop
    $loopRVA = 0x1000 + $code.Count
    # lea rcx, [rsp + 0x60] (&msg)
    AddB @(0x48, 0x8D, 0x4C, 0x24, 0x60)
    # xor edx, edx (hWnd = NULL)
    AddB @(0x31, 0xD2)
    # xor r8d, r8d
    AddB @(0x45, 0x31, 0xC0)
    # xor r9d, r9d
    AddB @(0x45, 0x31, 0xC9)
    # call [__imp_GetMessageA] (0x20F0)
    $disp = 0x20F0 - (0x1000 + $code.Count + 6)
    AddB @(0xFF, 0x15)
    AddB ([BitConverter]::GetBytes([int32]$disp))

    # test eax, eax
    AddB @(0x85, 0xC0)
    # jle exit_app
    $jleOffset = $code.Count
    AddB @(0x7E, 0x00)

    # lea rcx, [rsp + 0x60]
    AddB @(0x48, 0x8D, 0x4C, 0x24, 0x60)
    # call [__imp_TranslateMessage] (0x20F8)
    $disp = 0x20F8 - (0x1000 + $code.Count + 6)
    AddB @(0xFF, 0x15)
    AddB ([BitConverter]::GetBytes([int32]$disp))

    # lea rcx, [rsp + 0x60]
    AddB @(0x48, 0x8D, 0x4C, 0x24, 0x60)
    # call [__imp_DispatchMessageA] (0x2100)
    $disp = 0x2100 - (0x1000 + $code.Count + 6)
    AddB @(0xFF, 0x15)
    AddB ([BitConverter]::GetBytes([int32]$disp))

    # jmp msg_loop
    $currRVA = 0x1000 + $code.Count + 2
    $jmpDisp = $loopRVA - $currRVA
    AddB @(0xEB, [byte]($jmpDisp -band 0xFF))

    # exit_app:
    $exitOffset = $code.Count
    $code[$jleOffset + 1] = [byte]($exitOffset - ($jleOffset + 2))

    # xor ecx, ecx
    AddB @(0x31, 0xC9)
    # call [__imp_ExitProcess] (0x2150)
    $disp = 0x2150 - (0x1000 + $code.Count + 6)
    AddB @(0xFF, 0x15)
    AddB ([BitConverter]::GetBytes([int32]$disp))

    Write-Host "Code count before pad: 0x$($code.Count.ToString('X4'))"
    # Pad until 0x1200 for WndProc
    while ($code.Count -lt 0x200) {
        $code.Add(0x90)
    }
    Write-Host "WndProc actually at: 0x$((0x1000 + $code.Count).ToString('X4'))"

    # =========================================================================
    # WndProc Callback (RVA 0x1100, File Offset 0x0300)
    # =========================================================================
    # Check edx == WM_PAINT (0x000F)
    AddB @(0x83, 0xFA, 0x0F)
    $jneDestroy = $code.Count
    AddB @(0x75, 0x00)

    # WM_PAINT:
    # push rbx
    $code.Add(0x53)
    # sub rsp, 0x90
    AddB @(0x48, 0x81, 0xEC, 0x90, 0x00, 0x00, 0x00)
    # mov rbx, rcx (hwnd)
    AddB @(0x48, 0x89, 0xCB)

    # BeginPaint(hwnd, &ps)
    # rcx = rbx
    AddB @(0x48, 0x89, 0xD9)
    # lea rdx, [rsp + 0x30]
    AddB @(0x48, 0x8D, 0x54, 0x24, 0x30)
    # call [__imp_BeginPaint] (0x2120)
    $disp = 0x2120 - (0x1000 + $code.Count + 6)
    AddB @(0xFF, 0x15)
    AddB ([BitConverter]::GetBytes([int32]$disp))
    # mov r12, rax (hdc)
    AddB @(0x49, 0x89, 0xC4)

    # GetClientRect(hwnd, &rc)
    # rcx = rbx
    AddB @(0x48, 0x89, 0xD9)
    # lea rdx, [rsp + 0x20]
    AddB @(0x48, 0x8D, 0x54, 0x24, 0x20)
    # call [__imp_GetClientRect] (0x2130)
    $disp = 0x2130 - (0x1000 + $code.Count + 6)
    AddB @(0xFF, 0x15)
    AddB ([BitConverter]::GetBytes([int32]$disp))

    # DrawTextA(hdc, szText, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE = 0x25)
    # rcx = r12 (hdc)
    AddB @(0x4C, 0x89, 0xE1)
    # lea rdx, [rip + szText] (0x2380: 0x2300 + 32 + 64 = 0x2360)
    $disp = 0x2360 - (0x1000 + $code.Count + 7)
    AddB @(0x48, 0x8D, 0x15)
    AddB ([BitConverter]::GetBytes([int32]$disp))
    # mov r8d, -1
    AddB @(0x41, 0xB8, 0xFF, 0xFF, 0xFF, 0xFF)
    # lea r9, [rsp + 0x20]
    AddB @(0x4C, 0x8D, 0x4C, 0x24, 0x20)
    # mov dword ptr [rsp + 0x20], 0x25
    AddB @(0xC7, 0x44, 0x24, 0x20, 0x25, 0x00, 0x00, 0x00)
    # call [__imp_DrawTextA] (0x2138)
    $disp = 0x2138 - (0x1000 + $code.Count + 6)
    AddB @(0xFF, 0x15)
    AddB ([BitConverter]::GetBytes([int32]$disp))

    # EndPaint(hwnd, &ps)
    # rcx = rbx
    AddB @(0x48, 0x89, 0xD9)
    # lea rdx, [rsp + 0x30]
    AddB @(0x48, 0x8D, 0x54, 0x24, 0x30)
    # call [__imp_EndPaint] (0x2128)
    $disp = 0x2128 - (0x1000 + $code.Count + 6)
    AddB @(0xFF, 0x15)
    AddB ([BitConverter]::GetBytes([int32]$disp))

    # Return 0
    AddB @(0x31, 0xC0)
    # add rsp, 0x90
    AddB @(0x48, 0x81, 0xC4, 0x90, 0x00, 0x00, 0x00)
    # pop rbx
    $code.Add(0x5B)
    # ret
    $code.Add(0xC3)

    # Patch jne check_destroy
    $code[$jneDestroy + 1] = [byte]($code.Count - ($jneDestroy + 2))

    # check_destroy:
    # cmp edx, 2 (WM_DESTROY)
    AddB @(0x83, 0xFA, 0x02)
    # jne default_proc
    $jneDef = $code.Count
    AddB @(0x75, 0x00)

    # WM_DESTROY:
    # sub rsp, 0x28
    AddB @(0x48, 0x83, 0xEC, 0x28)
    # xor ecx, ecx (exitCode = 0)
    AddB @(0x31, 0xC9)
    # call [__imp_PostQuitMessage] (0x2110)
    $disp = 0x2110 - (0x1000 + $code.Count + 6)
    AddB @(0xFF, 0x15)
    AddB ([BitConverter]::GetBytes([int32]$disp))
    # xor eax, eax
    AddB @(0x31, 0xC0)
    # add rsp, 0x28
    AddB @(0x48, 0x83, 0xC4, 0x28)
    # ret
    $code.Add(0xC3)

    # Patch jne default_proc
    $code[$jneDef + 1] = [byte]($code.Count - ($jneDef + 2))

    # default_proc:
    # jmp [__imp_DefWindowProcA] (0x2108)
    $disp = 0x2108 - (0x1000 + $code.Count + 6)
    AddB @(0xFF, 0x25)
    AddB ([BitConverter]::GetBytes([int32]$disp))

    $codeBytes = $code.ToArray()
    Write-Host "Total .text size: $($codeBytes.Length) bytes (allocated: $textFileSize)"
    $codeBytes.CopyTo($fileBytes, 0x0200)

    [System.IO.File]::WriteAllBytes($outFile, $fileBytes)
    Write-Host "[+] Generated $outFile ($($fileBytes.Length) bytes) successfully!"
}

Build-PE
