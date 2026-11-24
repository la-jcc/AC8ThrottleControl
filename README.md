# AC8ThrottleControl

한국어 | [English](README.en.md)

ACE COMBAT 8의 가속·감속 키를 **토글** 또는 **유지형 퍼센트** 조작으로 바꾸는 UE4SS 모드입니다. 키에서 손을 떼도 스로틀이 유지되며, 게임 속도계 옆에 현재 스로틀 게이지를 표시합니다.

온라인 플레이에서는 사용하지 마세요.

## 설치

UE4SS가 설치되어 있어야 합니다.

- **모드 매니저 설치**: 릴리스 ZIP을 모드 매니저로 설치하세요.
- **수동 설치**: ZIP 안의 `AC8ThrottleControl` 폴더를 UE4SS의 `Mods` 폴더에 넣으세요.

## 조작

### toggle 모드 (기본값)

가속 키와 감속 키를 각각 토글 또는 홀드로 쓸 수 있습니다. 기본값은 가속 키 토글, 감속 키 홀드입니다.

- **토글**: 한 번 누르면 최대 가속(+100%) 또는 최대 감속(-100%)이 유지됩니다.
- **홀드**: 게임 기본 조작과 같습니다.

### percent 모드

가속·감속 키로 스로틀을 +100%(최대 가속)에서 -100%(최대 감속)까지 조절합니다. 0%는 중립입니다.

- 키를 누르고 있으면 값이 서서히 바뀌고, 손을 떼면 그 값이 유지됩니다.
- 0% 근처에서 손을 떼면 0%로 맞춰집니다.
- 감속 하한(`brake_floor`)을 정하면 하한까지만 서서히 감속합니다. 하한에서 감속 키를 계속 누르면 최대 감속이 걸리고, 손을 떼면 하한으로 돌아옵니다.

### 공통

- 가속·감속 키는 게임 설정의 키 배치를 그대로 따릅니다.
- 중립 키와 에어브레이크 키를 따로 지정할 수 있습니다. 에어브레이크는 최대 감속을 건 뒤, 끝나면 이전 스로틀로 돌아갑니다.
- 게이지는 가속을 녹색, 감속을 적색으로 표시합니다. `HG`는 High-G 선회 중, `BRK`/`A.BRK`는 최대 감속 중이라는 뜻입니다.

## config.ini

모드 폴더의 `config.ini`를 수정한 뒤 게임을 다시 시작하세요. 각 항목의 선택지와 설명은 파일 안의 주석에도 있습니다.

| 섹션 | 키 | 설명 |
|---|---|---|
| `[general]` | `enabled` | `false`로 두면 모드를 끕니다. |
| `[control]` | `mode` | `toggle`(기본값) 또는 `percent` |
| | `neutral_key` | 누르면 중립(0%)으로 돌아가는 키. `None`이면 사용하지 않음 |
| | `airbrake_key`, `airbrake_mode` | 에어브레이크 키(`None`이면 사용하지 않음). `hold`는 누르는 동안, `toggle`은 누를 때마다 켜고 끔 |
| | `toggle_accelerate`, `toggle_decelerate` | toggle 모드: `true`는 토글, `false`는 게임 기본 홀드 |
| | `toggle_after_hold` | toggle 모드: 홀드 키를 뗀 뒤 `resume`은 이전 토글로, `neutral`은 중립으로 |
| | `toggle_accelerate_repress` | toggle 모드: 가속 중 가속 키를 다시 누르면 `neutral`은 중립으로, `keep`은 가속 유지 |
| | `sensitivity` | percent 모드: 초당 변화량(%p). 50이면 0%에서 +50%까지 1초 |
| | `brake_floor` | percent 모드: 감속 하한(-100~0) |
| | `zero_snap` | percent 모드: 0% 기준 ±이 값 안에서 손을 떼면 0%. `0`이면 끔 |
| | `both_keys` | percent 모드: 가속·감속 키를 함께 누를 때 `high_g_only`는 High-G 선회, `neutral`은 중립 |
| `[keys]` | `source` | `game`은 게임 키 배치를 따름(바꾸면 자동 반영), `custom`은 아래 키를 사용 |
| | `accelerate`, `decelerate`, `combined` (각 `2` 포함) | `custom`일 때 쓸 키 이름(Unreal 키 이름, 예: `W`) |
| `[hud]` | `enabled` | 게이지 표시 여부 |
| | `x`, `y` | 화면 비율 기준 위치. 기본값은 16:9 화면에서 게임 속도계 바로 오른쪽 |
| | `height`, `width`, `opacity`, `font_size` | 게이지 높이·너비·투명도·글자 크기 |

## 참고

- [KoreanAIR-AC8-ModPack](https://github.com/KoreanAirline/KoreanAIR-AC8-ModPack): 훅 도우미 코드와 키 이름 처리 일부를 사용했습니다(MIT, [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)).
