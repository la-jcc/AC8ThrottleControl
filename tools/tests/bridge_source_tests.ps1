$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$bridge = [IO.File]::ReadAllText((Join-Path $root 'src\processor_bridge.asm'))
$contract = [IO.File]::ReadAllText((Join-Path $root 'src\processor_contract.h'))
$assertions = 0
function Assert-True([bool]$condition, [string]$message) {
    $script:assertions++
    if (-not $condition) { throw $message }
}
Assert-True ($contract -match 'kPostHookOffset\s*=\s*0x10') 'Post hook must remain after the verified 16-byte prologue.'
Assert-True ($contract -match 'array<unsigned char,\s*15>\s+kPostHookPrefix') 'Post hook must copy exactly four complete instructions.'
Assert-True ($bridge -match 'pushfq[\s\S]*popfq') 'Bridge must preserve flags.'
foreach ($register in 0..5) {
    Assert-True ($bridge -match "movdqu\s+xmm$register,[^`r`n]*\[rsp") "Bridge must restore XMM$register."
}
Assert-True ($bridge -match 'call\s+ac8_throttle_apply') 'Bridge must call the axis filter.'
Assert-True ($bridge -match 'jmp\s+qword ptr \[ac8_throttle_mid_continue\]') 'Bridge must resume the native continuation.'
Write-Output "PASS throttle post-hook bridge source tests ($assertions assertions)"
