# AC8ThrottleControl

[한국어](README.md) | English

A UE4SS mod for ACE COMBAT 8 that turns the accelerate and decelerate keys into **toggle** or **held percentage** throttle control. The throttle stays where you left it when you release the key, and a throttle gauge is shown next to the game's speed indicator.

Do not use it in online play.

## Installation

UE4SS must be installed.

- **Mod manager**: Install the release ZIP with your mod manager.
- **Manual**: Copy the `AC8ThrottleControl` folder from the ZIP into UE4SS's `Mods` folder.

## Controls

### Toggle mode (default)

Each of the accelerate and decelerate keys can be set to toggle or hold. By default, accelerate toggles and decelerate holds.

- **Toggle**: Press once to keep full throttle (+100%) or full brake (-100%).
- **Hold**: Works like the game's default controls.

### Percent mode

The accelerate and decelerate keys set the throttle anywhere from +100% (full throttle) to -100% (full brake). 0% is neutral.

- Holding a key changes the value gradually; releasing it keeps the value.
- Releasing near 0% snaps the value to 0%.
- With a brake floor (`brake_floor`), braking moves gradually only down to the floor. Holding decelerate at the floor applies full brake, and releasing it returns to the floor.

### Both modes

- The accelerate and decelerate keys follow the game's key bindings.
- You can assign a separate neutral key and an air brake key. The air brake applies full brake and returns to the previous throttle when it ends.
- The gauge shows acceleration in green and braking in red. `HG` means a High-G turn is in progress; `BRK`/`A.BRK` means full brake.

## config.ini

Edit `config.ini` in the mod folder, then restart the game. The comments in the file also list each option's choices and description.

| Section | Key | Description |
|---|---|---|
| `[general]` | `enabled` | `false` disables the mod. |
| `[control]` | `mode` | `toggle` (default) or `percent` |
| | `neutral_key` | Key that returns to neutral (0%). `None` to disable |
| | `airbrake_key`, `airbrake_mode` | Air brake key (`None` to disable). `hold` while held, `toggle` on/off per press |
| | `toggle_accelerate`, `toggle_decelerate` | Toggle mode: `true` toggles, `false` uses the game's default hold |
| | `toggle_after_hold` | Toggle mode: after releasing a hold key, `resume` returns to the previous toggle, `neutral` to neutral |
| | `toggle_accelerate_repress` | Toggle mode: pressing accelerate again while accelerating; `neutral` goes to neutral, `keep` keeps accelerating |
| | `sensitivity` | Percent mode: change per second (percentage points). At 50, 0% to +50% takes 1 second |
| | `brake_floor` | Percent mode: brake floor (-100 to 0) |
| | `zero_snap` | Percent mode: releasing within ± this value of 0% snaps to 0%. `0` to disable |
| | `both_keys` | Percent mode: accelerate and decelerate together; `high_g_only` for a High-G turn, `neutral` for neutral |
| `[keys]` | `source` | `game` follows the game's key bindings (changes apply automatically), `custom` uses the keys below |
| | `accelerate`, `decelerate`, `combined` (and each `2`) | Key names for `custom` (Unreal key names, e.g. `W`) |
| `[hud]` | `enabled` | Show the gauge |
| | `x`, `y` | Position as a fraction of the screen. The default sits just right of the game's speed indicator on a 16:9 screen |
| | `height`, `width`, `opacity`, `font_size` | Gauge height, width, opacity and text size |

## References

- [KoreanAIR-AC8-ModPack](https://github.com/KoreanAirline/KoreanAIR-AC8-ModPack): Parts of the hook helper code and key-name handling are used (MIT, [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)).
