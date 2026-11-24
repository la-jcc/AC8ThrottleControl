$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$build = Join-Path $root 'build'
$dll = Join-Path $build 'ac8_throttle_control.dll'
$exportsPath = Join-Path $build 'exports.txt'
$dependentsPath = Join-Path $build 'dependents.txt'
$assertions = 0

function Assert-True([bool]$condition, [string]$message) {
    $script:assertions++
    if (-not $condition) { throw $message }
}

Assert-True (Test-Path -LiteralPath $dll -PathType Leaf) 'Native DLL is missing.'
Assert-True (Test-Path -LiteralPath $exportsPath -PathType Leaf) 'dumpbin exports output is missing.'
Assert-True (Test-Path -LiteralPath $dependentsPath -PathType Leaf) 'dumpbin dependents output is missing.'

$bytes = [IO.File]::ReadAllBytes($dll)
Assert-True ($bytes.Length -gt 4096) 'Native DLL is unexpectedly small.'
$peOffset = [BitConverter]::ToInt32($bytes, 0x3c)
Assert-True ($peOffset -gt 0 -and $peOffset + 6 -lt $bytes.Length) 'Invalid PE header offset.'
$machine = [BitConverter]::ToUInt16($bytes, $peOffset + 4)
Assert-True ($machine -eq 0x8664) 'Native DLL is not x64.'

$exports = [IO.File]::ReadAllText($exportsPath)
$exportRows = @($exports -split "`r?`n" | Where-Object { $_ -match '^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+\S+' })
Assert-True ($exports -match '\bac8_throttle_start\b') 'Required ac8_throttle_start export is missing.'
Assert-True ($exportRows.Count -eq 1) "Expected one export, found $($exportRows.Count)."

$dependents = [IO.File]::ReadAllText($dependentsPath)
Assert-True ($dependents -match '(?im)^\s+KERNEL32\.dll\s*$') 'KERNEL32.dll dependency is missing.'
Assert-True ($dependents -match '(?im)^\s+USER32\.dll\s*$') 'USER32.dll dependency is missing.'
Assert-True ($dependents -notmatch '(?i)UE4SS|lua5|xinput1_4') 'Unexpected runtime dependency found.'

$hash = (Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash
Write-Output "PASS throttle native artifact tests ($assertions assertions, SHA256 $hash)"
