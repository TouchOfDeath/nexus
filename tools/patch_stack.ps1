param([string]$exePath, [long]$stackReserve = 67108864)

$bytes = [System.IO.File]::ReadAllBytes($exePath)
$peOff = [System.BitConverter]::ToInt32($bytes, 0x3C)
$optOff = $peOff + 4 + 20
$old = [System.BitConverter]::ToInt64($bytes, $optOff + 72)
Write-Host "File: $exePath - Old StackReserve: $old"

$newBytes = [System.BitConverter]::GetBytes($stackReserve)
for ($i = 0; $i -lt 8; $i++) {
    $bytes[$optOff + 72 + $i] = $newBytes[$i]
}

# Also set StackCommit to 1MB
$commitBytes = [System.BitConverter]::GetBytes([long]1048576)
for ($i = 0; $i -lt 8; $i++) {
    $bytes[$optOff + 80 + $i] = $commitBytes[$i]
}

[System.IO.File]::WriteAllBytes($exePath, $bytes)
$verified = [System.BitConverter]::ToInt64([System.IO.File]::ReadAllBytes($exePath), $optOff + 72)
Write-Host "New StackReserve: $verified bytes ($([math]::Round($verified/1MB)) MB)"
