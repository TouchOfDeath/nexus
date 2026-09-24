# ==============================================================================
#                      NEXUS PROJECT: DEMO RUNNER
# ==============================================================================
$ErrorActionPreference = "Stop"

$baseDir = $PSScriptRoot
$compilerDir = Join-Path $baseDir "compiler"
$guiDir = Join-Path $baseDir "gui_assembler"

Write-Host "`n>>> [1/2] Running NEXUS AOT Output (app.exe):" -ForegroundColor Cyan
Push-Location $compilerDir
try {
    & .\app.exe
} finally {
    Pop-Location
}

Write-Host "`n>>> [2/2] Launching BASM Native Win32 Window (output.exe)..." -ForegroundColor Cyan
Push-Location $guiDir
try {
    Start-Process -FilePath ".\output.exe"
    Write-Host "Launched output.exe! A native Win32 desktop window should appear." -ForegroundColor Green
} finally {
    Pop-Location
}
