# AC8ThrottleControl 설계

## 입력 의미

모드는 속도·추력·`InputThrottle` 프로퍼티를 사후 변경하지 않습니다. native processor가 소비하기 직전의 raw packet 축 3/4를 쓰므로 그 뒤의 바닐라 입력·엔진·비행 로직은 그대로 실행됩니다. 입력값과 실제 추력/가속의 대응은 기체별 내부 로직에 따라 비선형일 수 있습니다.

| 부호형 표시값 | raw 출력 |
|---:|---|
| -100% | throttle 0, brake 1 |
| -50% | throttle 0, brake 0.5 |
| 0% | throttle 0, brake 0 |
| +50% | throttle 0.5, brake 0 |
| +100% | throttle 1, brake 0 |

`mode=percent`의 저장값은 감속 하한 `brake_floor`(B, -100~0)까지 내려가며 위 표대로 출력합니다(`src/control_state.h`). B > -100이면 B에 도달한 뒤 감속 키를 누르는 동안 raw 출력을 게임에 넘겨 바닐라 brake를 씁니다(HUD는 -100% 위치, 숫자 없이 `BRK`). B=0이면 음수 저장값이 없는 가속 전용 동작입니다. 알 수 없는 `mode` 값은 `percent`로 읽습니다.

`zero_snap`은 중립 데드존입니다. 누르는 동안의 저장값은 멈추지 않고 움직이며, 출력은 |저장값| ≤ `zero_snap`이면 0%입니다. 키가 모두 놓였을 때 저장값이 범위 안이면 0으로 정리합니다.

에어브레이크(`airbrake_key`, `airbrake_mode=hold|toggle`)는 모든 모드에서 활성 동안 throttle 0, brake 1을 쓰고 저장값을 유지합니다. 상태 플래그 16, HUD는 숫자 없이 `A.BRK`.

`mode=toggle`에서 토글이 꺼진 방향(바닐라 홀드)을 누르는 동안 raw 축은 게임에 양보합니다. 놓은 뒤 `toggle_after_hold=resume`이면 반대 방향 래치를 이어가고, `neutral`이면 홀드가 그 래치를 끄므로 중립 0%가 됩니다. `toggle_accelerate_repress=keep`이면 가속 래치 중 가속키를 다시 눌러도 끄는 동작(뗄 때 중립)을 걸지 않습니다.

## 후단 훅

지원 빌드의 processor 시작은 `0x4D2C040`입니다. ThrottleControl은 entry가 아닌 `0x4D2C050`의 다음 15바이트를 검증하고 교체합니다.

```text
4D2C040  native 16-byte prologue
4D2C050  post hook: 15 bytes, four complete instructions
4D2C05F  continuation
```

`processor_bridge.asm`은 교체한 네 명령을 먼저 실행하고 flags, RAX/RCX/RDX/R8-R11, XMM0-XMM5와 x64 shadow space를 보존한 뒤 `ac8_throttle_apply`를 호출합니다. C++ helper는 player/context 계약을 통과한 callback에서만 축 3/4를 씁니다. 설치 중 해당 범위에 있던 thread는 안전한 복제 trampoline으로 재배치됩니다.

KoreanAIR-AC8-ModPack 같은 다른 모드가 먼저 또는 나중에 entry를 훅해도 `original` trampoline이 native body로 복귀하면 후단 훅을 통과합니다. ThrottleControl은 다른 모드의 모듈 이름·export·설정을 검사하지 않습니다.

## 제어와 소유권

worker는 10ms 주기로 물리 키 상태와 경과시간을 상태기계에 전달합니다. 경과시간은 최대 100ms로 제한되어 포커스 정지 후 큰 점프를 방지합니다. player 교체나 포커스 상실은 출력과 래치를 중립으로 초기화합니다.

raw callback은 이전에 자신이 쓴 3/4 값이 그대로 남았을 때만 저장한 native 값으로 되돌립니다. 새 출력이 활성일 때 게임 매핑 키로 설명되지 않는 nonzero/비정상 축은 물리 컨트롤러 등의 수동 입력으로 보고 그 callback을 양보합니다. 다만 worker의 키 표본은 10ms마다 갱신되므로, 설명되지 않은 nonzero 축이 들어온 callback에서만 훅이 현재 바인딩 키를 직접 확인하고, 그 축 방향 키가 눌려 있으면 양보하지 않고 저장 명령값을 씁니다(방금 누른 복귀키의 한 callback overshoot 방지). 키를 뗄 때는 반대로 게임 축이 한두 프레임 늦게 0이 되므로, worker는 방향별로 마지막 눌림 뒤 80ms 동안도 그 축을 예상 입력으로 공개해 남은 1.0을 명령값으로 덮습니다. 축 0..2와 5는 읽어 판정하거나 쓰지 않습니다.

## 키와 HUD

키 파서는 `System.sav`의 길이 접두 FString property와 `EOptionsKeys::` 값을 최대 16MiB의 read-only snapshot에서 찾습니다. unknown/unpollable 키는 설정 fallback을 유지합니다. 결합키가 방향키와 중복되면 결합키만 무시합니다.

native는 두 개의 32바이트 체크섬 레코드를 mapped file에 번갈아 게시합니다. Lua는 최신 유효 레코드만 사용하므로 동시 읽기의 부분 레코드는 화면에 반영되지 않습니다. 동일한 단일 recurring callback이 3 frames마다 상태를 읽고 30 frames마다 player를 확인합니다.

HUD는 게임의 1인칭 gauge widget을 수정하지 않고 별도 UMG Image/TextBlock을 생성합니다. 기본 좌표는 바닐라 속도/스로틀 눈금 바로 오른쪽이며 설정 좌표로 겹칠 수 있습니다. 검은 배경과 rail 위에 양수 녹색·음수 적색 fill을 그리며, 저장 명령값 위치에는 주황 tick과 숫자를 함께 이동시킵니다. `HG/BRK/A.BRK` 표시는 바닐라 게이지가 저장값과 다르게 움직이는 상태를 구분합니다. 최대 감속(`BRK`/`A.BRK`)에서는 숫자를 숨기고, 수동 입력 양보(상태 플래그 4)는 표시하지 않습니다.

새 UMG TextBlock에는 폰트가 없으므로 활성 게임 TextBlock의 `FontObject/TypefaceFontName`을 복사합니다. 최초 스캔에서 폰트를 찾지 못해도 매 HUD 갱신마다 재검색하지 않고 30 tick 간격으로만 재시도합니다. 따라서 카메라 모드와 무관하게 같은 Canvas 계층에서 유지되며 GuidedWeapons와는 런타임 의존성 없이 검증된 폰트 복사 개념과 위젯/Z-order만 공유합니다.

## 실패-폐쇄 조건

- 일반/EAC 실행 또는 다른 EXE build
- 후단 15바이트 불일치/이미 패치됨
- 단일 player 미확보, context owner/settings 불일치
- 게임 포커스 상실 또는 player 교체
- 설명되지 않는 물리 throttle/brake 입력
- 상태 레코드 checksum 불일치: HUD만 이전 유효 상태 유지/숨김

게임에서 아직 확인하지 않은 항목은 후단 훅 실행, KoreanAIR-AC8-ModPack 동시 실행, UMG HUD 배치, 기체별 중간축 반응, 결합키/High-G, 포커스·기체교체 복구입니다.
