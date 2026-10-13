<#
.SYNOPSIS
    NEXUS Native Toolchain Driver for Windows
.DESCRIPTION
    0% External Dependencies  -  100% Native Windows x86-64 Toolchain
#>
$Command = if ($args.Length -gt 0) { $args[0] } else { "help" }
$Arguments = @($args | Select-Object -Skip 1)

$ErrorActionPreference = "Stop"
$ScriptDir    = Split-Path -Parent $MyInvocation.MyCommand.Path
$CompilerDir  = Join-Path $ScriptDir "compiler"
$BinDir       = Join-Path $ScriptDir "bin"
$ToolsDir     = Join-Path $ScriptDir "tools"
$ExamplesDir  = Join-Path $ScriptDir "examples"
$TestsDir     = Join-Path $ScriptDir "tests"

$NexPrep  = Join-Path $BinDir "nexprep.exe"
$Nexc     = Join-Path $CompilerDir "nexc.exe"
$NexElf   = Join-Path $BinDir "nexelf.exe"
$NexMacho = Join-Path $BinDir "nexmacho.exe"
$NexArm64 = Join-Path $BinDir "nexarm64.exe"
$NexIr    = Join-Path $BinDir "nexir.exe"
$NexFmt   = Join-Path $BinDir "nexfmt.exe"
$NexTest  = Join-Path $BinDir "nextest.exe"
$NexDisasm= Join-Path $BinDir "nexdisasm.exe"
$NexDump  = Join-Path $BinDir "nexpedump.exe"
$NexDiff  = Join-Path $BinDir "nexdiff.exe"
$NexDebug = Join-Path $BinDir "nexdebug.exe"
$NexBench = Join-Path $BinDir "nexbench.exe"
$NexFuzz  = Join-Path $BinDir "nexfuzz.exe"

function Show-Usage {
    Write-Host @"
=============================================================================
           NEXUS Native Toolchain Driver (Windows Native Driver)
         0% C# Runtime  -  0% External SDKs  -  100% Native Win32
=============================================================================
Usage:
  nexus run <source.nex>        Compile and execute a NEXUS program natively
  nexus build [file.nex] [-o out] Build single binary or full master pipeline
  nexus compile [flags] <src>   Compile program to PE32+ (and optional ELF/Mach-O)
  nexus test [options]          Run full automated test harness (bin\nextest.exe)
  nexus self-host               Verify Stage 5 self-hosting bit-for-bit parity
  nexus disasm [binary] [opts]  Disassemble native x86-64 machine code
  nexus dump [binary]           Dump PE headers, sections, and import tables
  nexus diff <bin1> <bin2>      Perform byte-for-byte binary diff across sections
  nexus debug <binary.exe>      Run native Win32 crash & fault interceptor
  nexus bench                   Run native AOT code-generation benchmark
  nexus fuzz [options]          Run differential fuzzing engine
  nexus fmt [--check] <file>    Format a .nex source file
  nexus elf [src.exe] [dst]     Convert PE32+ executable to standalone ELF64
  nexus macho [src.exe] [dst]   Convert PE32+ executable to macOS ARM64 Mach-O
  nexus arm64 [src.nex] [dst]   Compile directly to native ARM64
  nexus ir [flags] <file.nex>   Run N-IR v8 intermediate representation engine
  nexus bootstrap               Run deterministic compiler generation fixed-point

Examples:
  nexus run examples\hello_world.nex
  nexus run examples\rocket_physics.nex
  nexus build examples\fibonacci.nex -o fib.exe
  nexus test --tests tests
  nexus disasm compiler\nexc.exe -n 30
  nexus self-host
"@ -ForegroundColor Cyan
}

function Ensure-Tools {
    if (-not (Test-Path $NexPrep) -or -not (Test-Path $NexTest) -or -not (Test-Path $NexDisasm)) {
        Write-Host "[+] Initializing tools via tools\build_tools.ps1..." -ForegroundColor Yellow
        & "$ToolsDir\build_tools.ps1"
    }
}

switch ($Command.ToLower()) {
    "run" {
        if (-not $Arguments -or $Arguments.Length -eq 0) {
            Write-Host "[-] Error: Missing source file. Usage: nexus run <source.nex>" -ForegroundColor Red
            exit 1
        }
        Ensure-Tools
        $srcPath = Resolve-Path $Arguments[0]
        $codeNex = Join-Path $CompilerDir "code.nex"
        $appExe  = Join-Path $CompilerDir "app.exe"

        & $NexPrep $srcPath $codeNex
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

        Push-Location $CompilerDir
        try {
            & $Nexc
            if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        }
        finally {
            Pop-Location
        }

        & $appExe
        exit $LASTEXITCODE
    }

    "build" {
        if ($Arguments -and $Arguments.Length -gt 0 -and $Arguments[0] -match '\.nex$') {
            Ensure-Tools
            $srcPath = Resolve-Path $Arguments[0]
            $outName = [System.IO.Path]::ChangeExtension($srcPath, ".exe")
            for ($i = 1; $i -lt $Arguments.Length; $i++) {
                if ($Arguments[$i] -eq "-o" -and ($i + 1) -lt $Arguments.Length) {
                    $outName = [System.IO.Path]::GetFullPath($Arguments[$i + 1])
                    break
                }
            }

            $codeNex = Join-Path $CompilerDir "code.nex"
            $appExe  = Join-Path $CompilerDir "app.exe"

            & $NexPrep $srcPath $codeNex
            if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

            Push-Location $CompilerDir
            try {
                & $Nexc
                if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
            }
            finally {
                Pop-Location
            }

            Copy-Item $appExe $outName -Force
            Write-Host "[+] Built standalone executable: $outName" -ForegroundColor Green
            exit 0
        } else {
            & "$ScriptDir\build_all.ps1" @Arguments
            exit $LASTEXITCODE
        }
    }

    "compile" {
        if (-not $Arguments -or $Arguments.Length -eq 0) {
            Write-Host "[-] Usage: nexus compile <source.nex> [--elf] [--macho]" -ForegroundColor Red
            exit 1
        }
        Ensure-Tools
        $srcPath = Resolve-Path $Arguments[0]
        $codeNex = Join-Path $CompilerDir "code.nex"
        $appExe  = Join-Path $CompilerDir "app.exe"

        & $NexPrep $srcPath $codeNex
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

        Push-Location $CompilerDir
        try {
            & $Nexc
            if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        }
        finally {
            Pop-Location
        }

        $baseName = [System.IO.Path]::ChangeExtension($srcPath, "")
        if ($baseName.EndsWith(".")) { $baseName = $baseName.Substring(0, $baseName.Length - 1) }

        $outExe   = "$baseName.exe"
        $outElf   = "$baseName.elf"
        $outMacho = "$baseName.macho"

        Copy-Item $appExe $outExe -Force
        Write-Host "[+] PE32+ binary: $outExe" -ForegroundColor Green

        if ($Arguments -contains "--elf" -or $Arguments -contains "--all") {
            & $NexElf $outExe $outElf
            Write-Host "[+] ELF64 binary: $outElf" -ForegroundColor Green
        }
        if ($Arguments -contains "--macho" -or $Arguments -contains "--all") {
            & $NexMacho $outExe $outMacho
            Write-Host "[+] Mach-O binary: $outMacho" -ForegroundColor Green
        }
        exit 0
    }

    "test" {
        Ensure-Tools
        if (-not $Arguments -or $Arguments.Length -eq 0) {
            & $NexTest $Nexc --tests $TestsDir
        } elseif ($Arguments[0] -match '\.nex$') {
            $testName = [System.IO.Path]::GetFileNameWithoutExtension($Arguments[0])
            & $NexTest $Nexc --tests $TestsDir --filter $testName
        } else {
            & $NexTest $Nexc @Arguments
        }
        exit $LASTEXITCODE
    }

    "self-host" {
        Ensure-Tools
        $codeNex = Join-Path $CompilerDir "code.nex"
        $nexcNex = Join-Path $CompilerDir "nexc.nex"
        Copy-Item $nexcNex $codeNex -Force

        Push-Location $CompilerDir
        try {
            Write-Host "[+] Compiling Generation 1 (nexc.exe -> app_gen1.exe)..." -ForegroundColor Yellow
            & $Nexc
            Move-Item "app.exe" "app_gen1.exe" -Force

            Write-Host "[+] Compiling Generation 2 (app_gen1.exe -> app_gen2.exe)..." -ForegroundColor Yellow
            & ".\app_gen1.exe"
            Move-Item "app.exe" "app_gen2.exe" -Force

            $diff = fc.exe /b app_gen1.exe app_gen2.exe
            if ($LASTEXITCODE -eq 0) {
                Write-Host "[+] SUCCESS: Stage 5 Bit-for-Bit Self-Hosting Parity Verified (0 diffs)!" -ForegroundColor Green
                Copy-Item "app_gen2.exe" "nexc.exe" -Force
            } else {
                Write-Host "[-] Parity mismatch:" -ForegroundColor Red
                Write-Host ($diff -join "`r`n")
                exit 1
            }
        }
        finally {
            Remove-Item "app_gen1.exe", "app_gen2.exe" -ErrorAction SilentlyContinue
            Pop-Location
        }
        exit 0
    }

    "bootstrap" {
        Ensure-Tools
        & "$ScriptDir\nexus.ps1" self-host
        exit $LASTEXITCODE
    }

    "disasm" {
        Ensure-Tools
        $target = if ($Arguments -and $Arguments.Length -gt 0) { $Arguments[0] } else { Join-Path $CompilerDir "app.exe" }
        $rest = if ($Arguments.Length -gt 1) { $Arguments[1..($Arguments.Length - 1)] } else { @() }
        & $NexDisasm $target @rest
        exit $LASTEXITCODE
    }

    "dump" {
        Ensure-Tools
        $target = if ($Arguments -and $Arguments.Length -gt 0) { $Arguments[0] } else { Join-Path $CompilerDir "app.exe" }
        & $NexDump $target
        exit $LASTEXITCODE
    }

    "diff" {
        Ensure-Tools
        & $NexDiff @Arguments
        exit $LASTEXITCODE
    }

    "debug" {
        Ensure-Tools
        $target = if ($Arguments -and $Arguments.Length -gt 0) { $Arguments[0] } else { Join-Path $CompilerDir "app.exe" }
        $rest = if ($Arguments.Length -gt 1) { $Arguments[1..($Arguments.Length - 1)] } else { @() }
        & $NexDebug $target @rest
        exit $LASTEXITCODE
    }

    "bench" {
        Ensure-Tools
        if ($Arguments -and $Arguments.Length -gt 0) {
            & $NexBench @Arguments
        } else {
            Write-Host "Running NEXUS raw code-generation benchmark (50M physical iters)..." -ForegroundColor Yellow
            $src = Join-Path $ExamplesDir "bench_arith.nex"
            $codeNex = Join-Path $CompilerDir "code.nex"
            $appExe  = Join-Path $CompilerDir "app.exe"
            & $NexPrep "--no-hydron" $src $codeNex
            Push-Location $CompilerDir
            try { & $Nexc } finally { Pop-Location }
            & $NexBench $appExe --iter 5
        }
        exit $LASTEXITCODE
    }

    "hydron" {
        Ensure-Tools
        Write-Host "==================================================================" -ForegroundColor Cyan
        Write-Host "                 NEXUS HYDRON ACCELERATION ENGINE                 " -ForegroundColor Cyan
        Write-Host "         Semantic Optimization & Recurrence Attractor Solver      " -ForegroundColor Cyan
        Write-Host "==================================================================" -ForegroundColor Cyan
        Write-Host "Benchmarking HYDRON Semantic Loop Elimination (50M logical iterations)..." -ForegroundColor Yellow
        $src = Join-Path $ExamplesDir "bench_arith.nex"
        $codeNex = Join-Path $CompilerDir "code.nex"
        $appExe  = Join-Path $CompilerDir "app.exe"
        & $NexPrep $src $codeNex
        Push-Location $CompilerDir
        try { & $Nexc } finally { Pop-Location }
        & $NexBench $appExe --iter 5
        exit $LASTEXITCODE
    }

    "fuzz" {
        Ensure-Tools
        & $NexFuzz @Arguments
        exit $LASTEXITCODE
    }

    "fmt" {
        Ensure-Tools
        & $NexFmt @Arguments
        exit $LASTEXITCODE
    }

    "elf" {
        Ensure-Tools
        & $NexElf @Arguments
        exit $LASTEXITCODE
    }

    "macho" {
        Ensure-Tools
        & $NexMacho @Arguments
        exit $LASTEXITCODE
    }

    "arm64" {
        Ensure-Tools
        & $NexArm64 @Arguments
        exit $LASTEXITCODE
    }

    "ir" {
        Ensure-Tools
        & $NexIr @Arguments
        exit $LASTEXITCODE
    }

    default {
        Show-Usage
        exit 0
    }
}
