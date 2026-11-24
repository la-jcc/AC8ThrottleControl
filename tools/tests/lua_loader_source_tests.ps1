$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$main = Join-Path $root 'Scripts\main.lua'
$source = [IO.File]::ReadAllText($main)
$assertions = 0

function Assert-True([bool]$condition, [string]$message) {
    $script:assertions++
    if (-not $condition) { throw $message }
}

$registrations = [regex]::Matches($source, '(?m)^\s*LoopInGameThreadAfterFrames\s*\(')
Assert-True ($registrations.Count -eq 1) "Expected one recurring game-thread registration, found $($registrations.Count)."
Assert-True ($source -match 'LoopInGameThreadAfterFrames\(3,\s*function\(\)\s*if mine ~= generation then return true end\s*tick\(\)') 'One generation-guarded recurring registration runs tick.'
Assert-True ($source -match 'os\.clock\(\) - last_tick > 5') 'Async watchdog re-registers a lost game-thread loop.'
Assert-True ($source -match 'ticks\s*%\s*10\s*==\s*1') 'Player scanning must remain lower-rate than HUD refresh.'
Assert-True ($source -match "local hud_x = number\('hud', 'x', 0\.392") 'HUD must default beside the vanilla speed/throttle scale.'
Assert-True ($source -match "pcall\(FindAllOf, 'TextBlock'\)") 'Numeric HUD must borrow a live game font.'
Assert-True ($source -match 'next_font_scan\s*=\s*ticks\s*\+\s*30') 'Missing-font retries must remain rate-limited.'
Assert-True ($source -match 'gauge\.value_tick_slot:SetPosition\(\{X=0,Y=marker_y\}\)') 'HUD must move an immediate-value tick with the controlled percentage.'
Assert-True ($source -match "words\s*=\s*words\s*\.\.\s*' HG'") 'HUD must identify native High-G yield without discarding the stored percentage.'
Assert-True ($source -match 'SetBrushTintColor\(\{SpecifiedColor=colour,ColorUseRule=0\}\)') 'Signed HUD colours must also tint the brush for HDR paths.'

Write-Output "PASS throttle Lua loader source tests ($assertions assertions)"
