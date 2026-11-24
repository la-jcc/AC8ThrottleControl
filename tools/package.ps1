# Builds dist\AC8ThrottleControl-<version>.zip (and a .sha256 file) from this
# source tree. Runs build.cmd and the native artifact checks first; an existing
# ZIP of the same name is never overwritten.
param(
    [switch]$NoBuild
)
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$main = Join-Path $root 'Scripts\main.lua'
$dll = Join-Path $root 'build\ac8_throttle_control.dll'

$line = Select-String -LiteralPath $main -Pattern '^-- AC8ThrottleControl\s+(\S+)' | Select-Object -First 1
if (-not $line) { throw "No version in $main" }
$version = $line.Matches[0].Groups[1].Value

if (-not $NoBuild) {
    & cmd /c (Join-Path $root 'build.cmd')
    if ($LASTEXITCODE -ne 0) { throw 'Build or tests failed: no package built.' }
}
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'tools\tests\native_artifact_tests.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Native artifact checks failed: no package built.' }

$dist = Join-Path $root 'dist'
$zip = Join-Path $dist "AC8ThrottleControl-$version.zip"
if (Test-Path -LiteralPath $zip) { throw "Already exists: $zip" }

$stage = Join-Path $root 'build\package'
if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force -Confirm:$false }
$mod = Join-Path $stage 'AC8ThrottleControl'
New-Item -ItemType Directory -Force (Join-Path $mod 'Scripts'), (Join-Path $mod 'Licenses') | Out-Null

Copy-Item -LiteralPath (Join-Path $root 'config.ini') -Destination $mod
New-Item -ItemType File -Path (Join-Path $mod 'enabled.txt') | Out-Null
Copy-Item -LiteralPath $main -Destination (Join-Path $mod 'Scripts\main.lua')
Copy-Item -LiteralPath $dll -Destination (Join-Path $mod 'Scripts\ac8_throttle_control.dll')
Copy-Item -LiteralPath (Join-Path $root 'README.md') -Destination $mod
Copy-Item -LiteralPath (Join-Path $root 'README.en.md') -Destination $mod
Copy-Item -LiteralPath (Join-Path $root 'LICENSE') -Destination (Join-Path $mod 'LICENSE.txt')
Copy-Item -LiteralPath (Join-Path $root 'THIRD_PARTY_NOTICES.md') -Destination $mod
Copy-Item -LiteralPath (Join-Path $root 'src\third_party\koreanair\LICENSE') -Destination (Join-Path $mod 'Licenses\KoreanAIR-LICENSE.txt')

New-Item -ItemType Directory -Force $dist | Out-Null
Compress-Archive -Path $mod -DestinationPath $zip
$hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash
[IO.File]::WriteAllText("$zip.sha256", "$hash  $(Split-Path -Leaf $zip)`n")

Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::OpenRead($zip)
try {
    $entries = $archive.Entries.Count
    $logs = @($archive.Entries | Where-Object { $_.Name -like '*.log' }).Count
} finally {
    $archive.Dispose()
}
if ($logs -ne 0) { throw 'Package unexpectedly contains logs.' }
"Built $zip ($entries entries, $((Get-Item -LiteralPath $zip).Length) bytes, SHA256 $hash)"
