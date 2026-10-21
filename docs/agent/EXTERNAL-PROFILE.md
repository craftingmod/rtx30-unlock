# v18 외부 프로필 및 통합 폴더

## v20 DEBUG OFF 최소 저장 (실기 미검증)

`-AutoProfile` 공통 EFI는 PROFILE.BIN이 없으면 내부 Probe로 생성한다. DEBUG ON은 기존 상세 수집과 U 승인을 유지한다. DEBUG OFF는 수집·검증을 메모리에서 수행하고 **RTX-UNLOCK/PROFILE.BIN(176바이트)만 저장**한다. DEBUG.ON을 자동 생성하지 않으며, PROBEnn·RUNnn·REPORT·INVENTORY·PROM·ROMIMAGE 파일을 새로 만들지 않는다. 과거 캡처와 로그를 자동 삭제하지 않는다.

| 상태 | DEBUG ON | DEBUG OFF |
| --- | --- | --- |
| PROFILE.BIN 없음 | PROBEnn 캡처와 프로필 생성, U 승인 | 프로필만 생성, 승인 없이 진행 |
| 프로필 유효·PROM 동일 | 기존처럼 상세 로그·U 승인 | 파일 추가 생성 없이 자동 진행 |
| 같은 장치에서 PROM 해시 변경 | PROBEnn 재수집, U 승인 후 교체 | 메모리 재수집, U 승인 후 프로필만 교체 |
| 손상·잘림·잘못된 프로필 / 다른 장치 / 읽기 실패 | 중단 | 중단 |

PROM 변경은 실제 GPU를 읽어 판정한다. 디스크의 PROM.BIN은 사용하지 않는다. 장치 PCI/Subsystem·칩·class/revision·BAR 크기가 기존 프로필과 같아야 변경 경로를 허용한다. 새 캡처는 단일 장치, 칩·PROM 서명, BAR/FB, GFW cold 상태, MMU/WPR2 검사를 통과해야 하며, 기존 적용 전 장치·자산·화면·Windows 검사도 유지한다.

변경된 프로필은 메모리에만 준비하고 사전 검사 후 U를 요구한다. U를 누른 뒤 기존 PROFILE.BIN이 그대로인지 확인하고 교체·Flush·재읽기·Close·로더 검증이 성공해야 적용한다. Esc·30초 시간 초과·입력 오류·사전 검사 실패는 프로필 교체와 적용·Windows 연결 부팅을 중단한다. 승인 후 고정 크기 파일을 제자리에서 갱신하므로 저장 도중 전원이 끊기면 파일이 손상될 수 있으며, 다음 실행은 이를 거부한다. 손상 프로필 복구 시에는 전원을 끄고 원본을 다른 곳에 보관한 뒤 새로 생성한다.

```powershell
./scripts/build-gop-experiment.ps1 -FullUnlock -WindowsHandoff -Production -SsdExperiment -ExternalProfile -AutoProfile
python tools/package_gop_experiment.py --output build/gop-experiment-v20-quiet-profile --zip build/gop-experiment-v20-quiet-profile.zip --assets assets
```

패키지에는 초기 실험용 DEBUG.ON을 포함한다. OFF를 사용하려면 실행 볼륨 RTX-UNLOCK의 DEBUG.ON을 제거한다. 새 패키지 내용물을 FAT32 USB 또는 허용된 GPT EFI System Partition에 복사한다. 위 명령은 실제 디스크·NVRAM을 변경하지 않는다.

DEBUG ON 수집은 비어 있는 PROBE00–PROBE99를 선택해 INVENTORY.TXT, REPORT.TXT, PROM.BIN, 사용 가능한 ROMIMAGE.BIN을 저장·재검증한다. 슬롯이 모두 사용 중이면 중단한다. DEBUG OFF는 슬롯 사용량과 무관하며, REPORT 해시는 메모리에서 작성한 보고서에 대응한다. 저장된 원본이 없어 Python으로 동일 캡처를 재검증·재생성할 수 없다.

```powershell
bun tools/test.ts auto-profile-runtime
```

호스트 테스트는 C/Python 바이너리 일치, DEBUG OFF 무캡처 저장, 실제 PROM 비교, 승인 전 프로필 보존, 승인 후 교체·재검증, U/Esc/시간 초과·읽기·저장 오류를 검사한다. 자동 생성은 펌웨어 호환성·GPU 안정성 인증이 아니며 실제 GPU 쓰기·Windows 연결 부팅은 실기 확인이 필요하다.

## v18 기존 경로

v18-unified-layout은 공통 EFI 하나와 `RTX-UNLOCK/PROFILE.BIN`을 사용한다. EFI는 JSON을 파싱하지 않는다. PC의 기존 Probe 검증 도구가 원본 REPORT/PROM과 JSON의 일치를 검사한 뒤 176바이트 고정 형식으로 변환한다. v17과 프로필 바이너리 형식은 같다. v18은 새 경로만 읽으므로 새 EFI와 RTX-UNLOCK 폴더를 함께 배치해야 한다.

이 기능은 GA102·GA103·GA104·GA106·GA107의 **실험용 프로필 로딩**이다. 다른 카드의 펌웨어 실행·FLR·영상 복구·Windows/CUDA 성공을 인증하지 않는다. GA100·Turing·Ada는 거부한다. 칩별로 다른 실행 절차가 필요하면 공통 EFI도 수정해야 한다.

## 공통 EFI 빌드와 첫 패키지

저장소 루트에서 실행한다. 공통 빌드에는 `-Profile`을 전달하지 않는다.

```powershell
./scripts/build-gop-experiment.ps1 -FullUnlock -WindowsHandoff -Production -SsdExperiment -ExternalProfile
python tools/package_gop_experiment.py --output build/gop-experiment-v18-unified-layout --zip build/gop-experiment-v18-unified-layout.zip --assets assets --profile profiles/board-refresh-run01.json
```

패키지에는 `EFI/BOOT/BOOTX64.EFI`, 검증된 자산 5개, `RTX-UNLOCK/PROFILE.BIN`, 빈 `DEBUG.ON`, 소스와 빌드 근거가 포함된다. 위 프로필은 기존 RUN01 캡처용이며 다른 카드에 재사용하지 않는다. 출력 폴더/ZIP은 새 경로여야 한다. 실제 SSD/USB·NVRAM·Windows Boot Manager는 빌드 및 패키지 명령이 변경하지 않는다.

## 다른 환경: EFI를 다시 빌드하지 않고 프로필 교체

1. 그 환경에서 읽기 전용 Probe EFI로 새 REPORT/PROM을 수집한다.
2. 원본 캡처를 보존하고 JSON과 실행용 프로필을 만든다.

```powershell
python tools/probe-to-profile.py --report captures/NEW/REPORT.TXT --prom captures/NEW/PROM.BIN --output profiles/new-board.json
python tools/runtime_profile.py profiles/new-board.json --output build/NEW-PROFILE.BIN
```

3. EFI를 실행할 **같은 볼륨**의 `RTX-UNLOCK/PROFILE.BIN`을 새 파일로 교체한다. EFI와 자산은 공통으로 사용한다. 여러 볼륨에서 검색하거나 다른 프로필로 자동 재시도하지 않는다.
4. 첫 시험은 `RTX-UNLOCK/DEBUG.ON`을 둔다. 모든 사전 검사 후 **U**를 한 번 눌러 진행한다. Esc·30초 시간 초과·입력 오류면 적용 및 Windows 자동 부팅을 중단한다. SS0 적용 완료로 스킵할 때도 DEBUG.ON이 있으면 U 승인을 요구한다.
5. `RTX-UNLOCK/RUNnn/GOP.TXT`, `RTX-UNLOCK/RUNnn/UNLOCK.TXT`와 Windows 결과를 수집한다. 그 환경에서 검증 후 DEBUG.ON을 제거하면 무로그·무입력 자동 적용/스킵 및 Windows 연결 부팅을 사용한다.

## 유지하는 검사와 파일의 신뢰 범위

파일은 GOP 해제 전에 한 번 읽고 닫는다. 크기·버전·형식·SHA256·GA10x 후보/칩·BAR/FB 범위·MMU/WPR2 조건을 확인한 뒤 메모리의 기대값으로 사용한다. 누락·잘림·추가 데이터·디렉터리·읽기/닫기 오류·손상은 내장 프로필로 대체하지 않고 중단한다.

기존 PCI/Subsystem/class·실제 칩·BAR 크기·PROM 해시·PLM·메모리 배치·GFW 완료·WPR2 미사용 검사, 자산 5개의 크기/SHA256, PCI capability/FLR, GOP 소유권/공유 콘솔 보호, DMA 정리와 영상 복구 검사는 유지한다. BDF와 BAR 주소는 실행 환경에서 조회한다. v16의 초기 SS0/SS1 완화와 SS0=88888888 스킵 정책도 유지한다. 스킵은 기존 v16처럼 콜드 상태/자산 로드를 생략하지만 장치·BAR·칩·PROM 검사 후에만 가능하다.

SHA256은 **손상 검출**이며 서명이 아니다. EFI는 PC에서의 캡처 검증 자체를 인증하지 않는다. EFI와 함께 신뢰하는 프로필을 배치해야 한다. 프로필에는 임의 주소·쓰기 명령·자산 경로·검사 우회 옵션이 없고, 메모리 배치 레지스터 주소와 실행 절차는 코드에 고정한다. 프로필에 포함하는 PROM 해시는 하드웨어 대조용이며 REPORT 해시는 캡처 추적용이다.

## 형식 v1

모든 정수는 little-endian이다. 총 176바이트만 허용한다.

| 오프셋 | 내용 |
| --- | --- |
| 0 | 8바이트 `GUPROF17` |
| 8, 12 | u32 버전 1, 전체 길이 176 |
| 16–44 | u32 PCI ID, Subsystem ID, chip, class/revision, PLM, SS0, SS1, FB MiB |
| 48 | u64 BAR0 크기 |
| 56–76 | u32 레이아웃 값 6개: 625f04, 1fa7c8, 1fa82c, 1fa830, 1fa824, 1fa828 |
| 80 | 32바이트 PROM SHA256 |
| 112 | 32바이트 원본 REPORT SHA256 |
| 144 | 앞 144바이트의 SHA256 |

## 검증

```powershell
bun tools/test.ts python
bun tools/test.ts gop-external-profile
bun tools/test.ts gop-production
bun tools/test.ts gop-ssd
```

이 검사는 모의 펌웨어와 캡처 fixture를 사용하며 실제 GPU 쓰기·FLR·Windows 실행은 하지 않는다. v17의 현재 환경 적용은 사용자 성공 보고와 로그로 확인했다. v18의 통합 경로·스킵·무로그 실행·다른 카드 지원·반복 안정성은 실기 미검증이다.

## 실행 볼륨의 폴더 구조

```text
EFI/BOOT/BOOTX64.EFI
RTX-UNLOCK/
  ASSETS/          (검증된 자산 5개)
  PROFILE.BIN      (v17과 같은 바이너리 형식)
  DEBUG.ON         (선택 사항, 첫 패키지에는 포함)
  RUN00/
    GOP.TXT
    UNLOCK.TXT
  RUN01/
    GOP.TXT
    UNLOCK.TXT
```

상세 로그를 켰을 때 GOP가 먼저 비어 있는 RUN 번호를 고르고 Unlock이 같은 번호의 폴더를 연다. 기존 RUN은 건너뛰며 GOP.TXT나 UNLOCK.TXT가 이미 있으면 덮어쓰지 않는다. RUN00–RUN99가 모두 있으면 GPU 해제 전에 중단한다. DEBUG.ON이 없으면 RUN/로그 파일을 만들지 않는다. Unlock 사전 검사에 도달하기 전에 실패하면 GOP.TXT만 남을 수 있다.

새 패키지를 EFI 실행 볼륨에 배치하고 BOOTX64.EFI도 교체한다. 기존 두 폴더는 자동으로 이동하거나 삭제하지 않는다. 과거 로그는 보존할 수 있지만 새 EFI는 이전 경로로 돌아가서 읽지 않는다.
