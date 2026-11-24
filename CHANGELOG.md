# Changelog

## 0.1.0 - 2026-11-24

First public release.

- Held signed throttle command (`mode = percent`): +100% full throttle, 0% neutral, -100% full brake, changed at `sensitivity` percentage points per second and kept on release. `brake_floor` (-100 to 0) limits the gradual brake; decelerate held at the floor gives the game's full brake until released.
- Per-direction toggles (`mode = toggle`): `toggle_accelerate`, `toggle_decelerate`, `toggle_after_hold = resume | neutral`, `toggle_accelerate_repress = neutral | keep`. Turning on acts on press and turning off on release, so the order of W and S does not change the result.
- Neutral deadzone (`zero_snap`), `both_keys = high_g_only | neutral`, optional `neutral_key`, and an optional air brake key (`airbrake_mode = hold | toggle`).
- Reads the game's throttle key bindings from `System.sav` and follows changes about once per second; falls back to configured W/S keys.
- Independent UMG gauge beside the vanilla speed scale in first and third person, with `HG`, `BRK` and `A.BRK` states.
- Writes only raw input axes 3/4 from a guarded post-prologue hook that is designed to coexist with entry-point hooks such as KoreanAIR-AC8-ModPack. Unsupported executables, a patched hook site, lost focus, aircraft changes and unexplained analog input all fail closed to vanilla input.
- Unit tests for the state machine, raw-axis ownership, key parser, HUD status channel and hook contract run on every `build.cmd`.
