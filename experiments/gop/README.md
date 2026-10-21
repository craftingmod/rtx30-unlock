# NVIDIA GOP 해제·복구 실험 1

## 현재 실험: v18-unified-layout

실행 폴더를 `RTX-UNLOCK`으로 통일했다. `ASSETS/`, `PROFILE.BIN`, `DEBUG.ON`은 그 바로 아래에 둔다. 상세 로그를 켜면 GOP가 고른 새 `RUN00`–`RUN99` 하나에 `GOP.TXT`와 `UNLOCK.TXT`를 함께 저장한다. 기존 RUN은 건너뛰고 기존 로그는 덮어쓰지 않는다. DEBUG.ON이 없으면 RUN을 만들지 않는다. 사전 검사 중 일찍 거부되면 GOP.TXT만 생길 수 있다.

외부 프로필 형식과 DEBUG.ON의 U 승인, 언락·화면 복구 절차는 유지한다. v17의 현재 환경 적용 경로는 사용자 성공 보고와 로그로 확인했으며, v18의 새 폴더 구조는 실기 미검증이다. 새 EFI와 `RTX-UNLOCK` 폴더 전체를 함께 배치한다. 이전 폴더의 자동 검색·이동·삭제는 하지 않는다. 아래 v17 이하의 경로는 당시 배포 파일의 기록이다. [현재 사용법](../../docs/EXTERNAL-PROFILE.md)을 따른다.

## 이전 실험: v17-external-profile (적용 경로 실기 성공)

공통 EFI가 실행 볼륨의 `RTX30-GOP-TEST/PROFILE.BIN`을 읽는 구현을 추가했다. JSON은 PC에서 원본 Probe REPORT/PROM과 대조한 뒤 고정 길이 파일로 변환한다. 기존 v16 EFI는 외부 파일을 읽지 못하므로 v17로 한 번 교체한다. 이후 기존 GA10x 공통 절차 안에서는 EFI 재빌드 없이 프로필을 교체할 수 있다. [빌드·패키지·프로필 교체·검증 범위](../../docs/EXTERNAL-PROFILE.md)를 참고한다.

새 소스에서 빌드하는 production 모드는 `DEBUG.ON`이 있으면 상세 로그와 **U 키 승인**을 요구한다. Esc·30초 시간 초과·입력 오류는 적용/스킵 및 Windows 자동 부팅을 중단한다. DEBUG.ON이 없으면 기존처럼 자동 진행한다. 기존에 배포된 v16 파일의 동작은 변경되지 않는다. 아래 v16 기록의 무조건 자동 실행 설명은 당시 배포 파일을 가리킨다.

## 이전 실험: v16-ssd-selectors (적용 경로 실기 성공)

### SSD 실행 피드백: 예상 외 초기 SS0에서 적용·복구·Windows 부팅 성공

2026-10-21 사용자는 정상 부팅과 적용을 확인했고, 제공한 두 로그도 완료 상태다. `SELECTOR_PREFLIGHT SS0=24700051 SS1=00000006 skip=0 relaxed_initial_selectors=1 status=0`에서 기존 캡처와 다른 초기 SS0를 거부하지 않고 실제 적용 경로를 탔다. PROM·다섯 자산 해시 검사를 통과한 뒤 SS0 `24700051→88888888`, SS1 `6→8` 쓰기와 읽기 확인이 성공했다.

`PATCH_TRANSACTION result=0 containment=0 started=1 quiesced=1 fault=0 retained=0 maps=0 FLR=1 safe=1`로 단일 FLR·DMA 정리를 확인했다. GOP 자식 2개와 모든 역할 복구, mode 1(640×480)→원래 mode 0 복귀, 녹색 9,216픽셀 읽기 불일치 0도 성공했다. before-gop부터 after-output까지 모든 단계에서 셀렉터 `88888888 / 8`이 유지됐고, `FULL_FINAL status=0 touched=1 restored=1`, `PATCH_LOG_CLOSE status=0`, `END`, 마지막 `WINDOWS_START_INTENT ... reboot=0`까지 기록됐다. Windows 진입은 사용자 확인이며 로그 자체는 StartImage 직전까지만 남긴다.

상세 로그를 켠 상태에서 RTC 기준 preflight-begin 0ms→report-complete 9000ms로 **약 9초**, auto-start 1000ms 이후로는 약 8초다. 자산 시작/끝은 모두 1000ms로 같은 RTC 초 안에 기록돼, 정확한 읽기·해시 소요 시간을 0ms라고 해석하지 않는다. 이전 v14 USB에서는 자산 구간 102초, U 이후 보고 완료 227초였다. 이 실행은 SSD에서도 상세 로그를 정상 저장하며 완료 시간이 크게 줄어든 결과다. 세부 속도 향상의 원인별 비율이나 반복 실행 시간은 아직 측정하지 않았다.

이번에는 `skip=0`인 적용 경로를 검증했다. SS0가 이미 `88888888`일 때의 실기 스킵 경로, 디버그 파일 없는 기본 무로그 실행, v16 이후 CUDA 성능 및 반복 안정성은 별도 검증 대상이다. 원본 두 파일은 `captures/v16-ssd-result/GOP.TXT`, `REPORT.TXT`에 보존하고 사용자 파일과 SHA256 일치를 확인했다.

### 빌드 동작 및 시험 방법

사용자 요청에 따라 **SSD EFI 지원**과 **초기 SS0 기반 스킵/시도** 정책을 별도 `-SsdExperiment` 빌드에 적용한다. 이 옵션은 `-FullUnlock -WindowsHandoff -Production`과 함께 사용한다. 기본 자동 실행·DEBUG.ON 선택 로깅을 유지한다. 기존 v15 빌드의 USB 전용·초기 셀렉터 기대값 검사는 옵션을 생략하면 그대로다.

- 실행 매체는 기존 USB 또는 PartitionInfo 프로토콜에서 GPT EFI System Partition GUID가 확인된 볼륨만 허용한다. 일반 SSD 데이터 파티션, MBR 또는 PartitionInfo가 없는 SSD는 거부한다. GOP 로그와 UNLOCK 자산/로그가 같은 검사 함수를 사용한다. 파일은 항상 실제 EFI를 실행한 볼륨에서 찾는다.
- PCI/칩/BAR 검사를 거친 초기 SS0가 `0x88888888`이면 이미 적용된 것으로 취급한다. SS1 값은 기록하지만 스킵 조건에 추가하지 않는다. PROM 해시는 검사하고 자산 읽기·GOP 계획/해제·펌웨어 실행·FLR·SetMode는 모두 건너뛴 뒤 Windows로 이어진다. 화면에 `SS0 already 88888888. Unlock skipped.`를 표시한다. 이는 SS1이나 실제 연산 효과까지 검증했다는 뜻은 아니다.
- SS0가 다른 값이면 기존 초기 SS0/SS1 기대값과 달라도 그 이유만으로 거부하지 않는다. 0/0도 이 값 비교만으로 거부하지 않는다. 다만 PROM·장치/칩·BAR·레이아웃·GFW·WPR·PLM·DMA/소유권 및 읽기 오류 검사는 유지한다. 따라서 다른 상태 검사가 실패하면 적용하지 않는다. 접근 불가를 나타내는 all-ones 값 등에 대한 하위 펌웨어 경로의 오류 처리도 유지한다.
- 실제 적용 후에는 두 셀렉터가 `88888888 / 8`인지, 단일 FLR·DMA 정리·GOP 복구·모드 왕복·색상 읽기가 성공했는지 기존대로 확인한다. 실패한 적용 뒤 Windows로 넘어가지 않는다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build-gop-experiment.ps1 -FullUnlock -WindowsHandoff -Production -SsdExperiment -Profile profiles/board-refresh-run01.json
experiments\gop\test-ssd.cmd
python tools/package_gop_experiment.py --output build/gop-experiment-v16-ssd-selectors --zip build/gop-experiment-v16-ssd-selectors.zip --assets assets
```

I: SSD EFI 시험에서는 패키지의 `EFI/BOOT/BOOTX64.EFI`를 해당 경로에 교체하고 `RTX30-GOP-TEST`, `RTX3080-UNLOCK/ASSETS`는 EFI를 실행할 볼륨의 루트에 둔다. 기존 `RTX30-GOP-TEST/DEBUG.ON`과 과거 RUN은 보존한다. 첫 시험은 DEBUG.ON을 켠 상태로 진행한다. 부팅 화면에서 **v16-ssd-selectors**, `Boot source accepted. Logging ON`을 확인한다. 키 입력 없이 적용/스킵 뒤 Windows로 이어진다. 새 두 REPORT와 Windows 수집 결과로 어느 경로가 실행됐는지 확인한다. 새 NVRAM 항목 등록이나 Windows Boot Manager 파일 변경은 이 빌드/패키지 생성에 포함하지 않는다.

빌드 준비 시에는 I:에 파일을 설치하거나 재부팅하지 않았다. 이후 사용자 실행에서 SSD 부팅·예상 외 초기값의 적용 성공과 실행 시간을 위와 같이 확인했다. 중단 시 전원 종료 후 로그를 회수하며 같은 부팅에서 재시도하지 않는다.

## 이전 빌드: v15-auto-windows (production 옵션, 실기 미검증)

`-FullUnlock -WindowsHandoff -Production`은 기존 보드·초기값·PROM·자산 해시·독점 접근·단일 FLR·DMA 정리·GOP 복구·모드 왕복·색상 읽기·셀렉터 검사를 유지하고 **U 입력 없이 자동 진행**한다. Windows 후보가 하나인지도 먼저 검증한다. 성공한 경우에만 Windows로 이어진다. `-Production`을 생략하면 기존 v14 수동 U/상세 기록 동작이다. production이라는 옵션명은 실기 안정성 인증을 뜻하지 않는다.

기본적으로 로그 파일과 RUN 폴더를 생성하지 않고 로그 Write/Flush를 호출하지 않는다. 아래의 빈 파일이 부팅 시 있으면 해당 실행에서만 기존 상세 로그를 활성화한다.

```text
USB:/RTX30-GOP-TEST/DEBUG.ON
```

두 보고서 계통(GOP 및 UNLOCK)에 같은 설정을 적용한다. 파일 내용은 읽지 않고 존재 여부로 선택한다. 진단을 끝내면 DEBUG.ON을 제거해 추가 로그 누적을 막는다. 디버그 활성 상태에서는 기존 RUN00~RUN99 보존 방식과 공간 부족 시 중단 처리를 유지하며 자동 삭제하지 않는다. 기본 모드도 자산은 매 부팅 다시 읽고 해시를 검사하므로 약 84MB 읽기 시간 자체는 남는다. 실기 소요 시간은 아직 측정하지 않았다.

검사 실패 시 Windows를 시작하지 않는다. 하드웨어를 건드리기 전 실패는 화면에 단계/상태를 표시하고 정지한다. 복구가 불확실한 상태에서는 기존처럼 콘솔 접근을 피하고 정지하므로 화면이 꺼진 채 멈출 수 있다. 로그를 켜려면 전원 종료 후 DEBUG.ON을 만들어 다음 부팅을 진단한다. 실패 후 같은 부팅에서 재시도하지 않는다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build-gop-experiment.ps1 -FullUnlock -WindowsHandoff -Production -Profile profiles/board-refresh-run01.json
experiments\gop\test-production.cmd
python tools/package_gop_experiment.py --output build/gop-experiment-usb-v15-auto-windows --zip build/gop-experiment-usb-v15-auto-windows.zip --assets assets
```

### DEBUG.ON 미생성 신고 점검 (2026-10-21)

사용자가 지정한 I:를 관리자 권한으로 읽기 전용 확인했다. `I:\RTX30-GOP-TEST\DEBUG.ON`은 실제 이름이 맞는 0바이트 일반 파일이었고, `I:\EFI\BOOT\BOOTX64.EFI` SHA256도 v15 배포본 `aafd2f9d16b38601cfdd0ce3390dce8f842c92fa12b4176444c97415ba170584`와 일치했다. 그러나 I:의 장치는 USB가 아니라 NVMe Seagate FireCuda 520 SSD이며, FAT32 EFI 파티션이다.

현재 `OpenLog`는 LoadedImage의 DeviceHandle이 USB 경로인지 먼저 확인하고, 그 뒤에야 DEBUG.ON을 조회한다. 자산 로더의 GuIoOpen도 USB 출처를 검사한다. 따라서 이 SSD에서 v15를 실행하면 디버그 트리거를 검사하기 전에 EFI_UNSUPPORTED로 거부되며 RUN/REPORT가 생기지 않는 것이 코드상 예상 결과다. 실제 어떤 UEFI 부팅 항목을 선택했는지는 이 파일 검사만으로 확인하지 않았다. 파일 확장자나 빈 파일 처리의 오류는 재현되지 않았고, 모의 검사도 DEBUG.ON의 정확한 이름·접근 오류 처리까지 보강해 통과했다. SSD 실행은 별도의 저장장치 허용 정책을 구현한 빌드가 필요하며, 이번 점검에서는 SSD 파일·파티션·NVRAM을 변경하지 않았다.

현재는 **USB 시험용**이다. 앱과 자산 로더의 USB 출처 검사를 유지하므로 SSD EFI에 복사만 하면 실행을 거부한다. SSD 지원/설치와 NVRAM 등록은 별도 작업이다. SSD 설치 시 전용 경로(예: `\EFI\NvidiaUnlock\BOOTX64.EFI`)와 `NVIDIA Unlock + Windows` 같은 UEFI Boot#### Description을 사용할 수 있다. EFI 파일명과 메뉴 표시 이름은 별개이며 기존 Windows Boot Manager를 덮어쓰지 않고 새 항목으로 등록할 수 있다. 이번 작업에서는 디스크 파티션이나 부팅 항목을 변경하지 않았다.

시험 순서: 완전 종료 → 새 USB EFI의 **v15-auto-windows** 확인 → 키 입력 없이 Windows 진입 대기 → 동일 벤치마크로 성능 확인. 기본 모드에서는 새 RUN 폴더/REPORT가 없어야 한다. 10분간 Windows가 시작되지 않으면 물리 종료 후 DEBUG.ON을 넣어 진단한다. 기존 USB의 과거 RUN은 삭제하지 않으며 이전 로그가 남아 있어도 새 로그 생성과 구분한다.

## 이전 실험: diagnostic v14-windows-handoff

### 최초 실기 결과: Windows 장치·드라이버 정상 수집, 연산은 미측정

2026-10-21 사용자가 제공한 `V14RESULT`에서 언락·단일 FLR·DMA 정리, 자식 2개와 GOP 복구, mode 1(640×480) → 원래 mode 0 복귀가 모두 성공했다. 녹색 9,216픽셀의 직접 읽기는 불일치 0이며, 모든 관측 단계의 셀렉터는 `88888888 / 8`을 유지했다. `FULL_FINAL status=0 touched=1 restored=1`, `PATCH_LOG_CLOSE status=0`, `END`, `WINDOWS_START_INTENT ... reboot=0`까지 기록됐다. U(149000ms)부터 보고 완료(376000ms)까지 227초(3분 47초)다.

Windows 수집 시각은 `2026-10-21T01:53:43+09:00`, Windows가 보고한 LastBootUpTime은 `2026-10-21T01:52:02.5000000+09:00`이다. NVIDIA 장치는 `Status=OK`, `ConfigManagerErrorCode=0`이며 `nvidia-smi`는 종료 코드 0, Display Attached=Yes, Display Active=Enabled, WDDM, 드라이버 616.56을 보고했다. 이 결과로 Windows에서 장치와 드라이버가 정상 응답한 것을 확인했다. UEFI 로그는 StartImage 직전까지이므로 중간 재부팅 부재를 독립적으로 증명하지는 않는다. 장시간 안정성·드라이버 재시작 여부도 이 한 번의 수집만으로 판정하지 않는다.

Windows 제품명은 `NVIDIA GeForce RTX 3090 Ti`, 보고된 메모리는 24564MiB이다. PCI ID `2203:10DE`, Subsystem `2000:7377`, BDF `0000:01:00.0`는 UEFI 대상과 일치한다. 사용자는 v13과 **같은 카드·VBIOS**라고 확인했다. 이 제품명/메모리 보고만으로 카드 교체나 패치 성능 효과를 추론하지 않는다.

`BenchmarkState=not-run`, BenchmarkExitCode/Sha256=null이며 벤치마크 JSON은 제공되지 않았다. 따라서 CUDA 실행 성공·결과 정확성·성능 향상·Windows 초기화 후 셀렉터 보존은 미검증이다. 이 결과를 수집할 당시 `src/gop/compute-benchmark.cu`와 `windows-check.ps1`은 제품명을 RTX 3080으로 제한했다. 이후 제품명 제한을 제거하고 대상 PCI 위치 `0000:01:00.0`와 중복 대상 거부, 결과 검증은 유지했다. 실제 제품명은 JSON에 그대로 남긴다. 같은 벤치마크 실행 파일·드라이버·조건으로 패치 전후를 비교해야 한다. 이번 `not-run`이 이 제한 때문에 발생했다고 단정하지는 않는다.

원본은 `captures/v14-windows-handoff-result/`에 전체 복사하고 모든 파일의 SHA256 일치를 확인했다. 사용자 제공 `V14RESULT/`는 수정하지 않았다.

### 추가 실기 결과: 같은 Windows 부팅에서 CUDA 3종 실행 성공

2026-10-21 02:02:22(+09:00) 업데이트된 수집 결과는 `BenchmarkState=passed`, BenchmarkExitCode=0, NvidiaSmiExitCode=0이며 두 오류 파일은 비어 있다. 실행 파일 SHA256 `5bc323311c1dd9d7302424cd8a098a84c813af17ac7a371246c112f5b2b76e76`는 배포한 제품명 제한 수정본과 일치한다. LastBootUpTime `2026-10-21T01:52:02.5000000+09:00`는 최초 수집과 같아 두 Windows 수집 사이 재부팅은 관측되지 않는다. NVIDIA 장치 상태도 OK/오류 코드 0이다.

| 연산 | 중앙 시간(ms) | 측정 TFLOPS |
|---|---:|---:|
| FP32 pedantic | 4.640176 | 29.619 |
| FP16 Tensor, FP32 누산 | 1.000960 | 137.307 |
| BF16 Tensor, FP32 누산 | 1.000960 | 137.307 |

각 연산은 4096×4096 GEMM, 2초 워밍업, 30회 표본이며 `reference_checked=true`다. 코드의 검사는 결과 행렬 첫/마지막 원소에 한정한다. 이로써 v14 이후 Windows에서 해당 CUDA 연산들이 정상 완료된 것을 확인했다. 이 시점에는 패치 성능 효과가 미확정이었으며, 이후 기준 비교는 아래에 기록했다. Windows 초기화 후 실제 셀렉터 값과 장시간 안정성은 별도 검증 대상이다.

제품명은 RTX 3090 Ti, PCI 위치는 `0000:01:00.0`, compute capability는 8.6이다. 결과 JSON이 보고한 CUDA runtime/driver API 값은 13040/13040, cuBLAS는 130300이다. 빌드 도구/배포 파일 기록과 런타임 보고값을 구분해 그대로 보존한다. 과거 기준값은 다른 실행 파일·라이브러리·드라이버 조건이므로 이 수치를 단순 비율로 비교해 패치 향상률을 판정하지 않는다.

다음 기준 측정은 완전 종료 뒤 USB 패치를 실행하지 않고 Windows로 부팅해 **동일 벤치마크 패키지와 동일 드라이버**에서 `run.cmd`를 실행한다. 새 결과는 예를 들어 `V14BASELINE`으로 따로 보존하고 PCI 식별자·실행 파일 해시·라이브러리 버전·드라이버를 비교한다. 현재 패치 후 결과는 `captures/v14-windows-benchmark-result/`에 전체 보존했고 사용자 원본과 모든 SHA256이 일치한다. 최초 UEFI/Windows 수집은 `captures/v14-windows-handoff-result/`에 별도로 유지한다.

### 기준 측정과 비교: FP16/BF16 약 1.89배, FP32 차이 작음

사용자가 패치 없는 기준 측정으로 제공한 `V14BaselineResult`와 패치 후 `V14RESULT`를 비교했다. 실행 파일 SHA256, GPU UUID·PCI 위치·Subsystem·VBIOS, NVIDIA 드라이버 616.56, CUDA runtime/driver API 13040/13040, cuBLAS 130300이 일치한다. 두 결과 모두 정상 종료·결과 검사를 통과했고 장치 오류는 없다. Windows LastBootUpTime은 기준 측정 02:05:14.500, 패치 후 측정 01:52:02.500(+09:00)으로 서로 다른 부팅이다.

| 연산 | 기준 TFLOPS | 패치 후 TFLOPS | 처리량 배율 | 처리량 변화 |
|---|---:|---:|---:|---:|
| FP32 pedantic | 29.369 | 29.619 | 1.009× | +0.85% |
| FP16 Tensor, FP32 누산 | 72.672 | 137.307 | 1.889× | +88.94% |
| BF16 Tensor, FP32 누산 | 72.754 | 137.307 | 1.887× | +88.73% |

Tensor 연산 중앙 실행 시간은 FP16 1.891216→1.000960ms, BF16 1.889088→1.000960ms로 약 47% 줄었다. 따라서 이 1회 비교에서는 패치 후 FP16/BF16 Tensor 처리량이 약 1.89배였고 FP32 변화는 작았다. 이는 같은 도구·드라이버 조건에서 관측한 연산 효과이며 모든 작업이나 반복 부팅에서 같은 향상을 보장하지 않는다. 각 상태당 프로세스 실행 1회(내부 표본 30회)이고, 결과 검사는 첫/마지막 행렬 원소에 한정한다. 수집 스크립트는 벤치마크 전에 nvidia-smi를 실행하므로 두 스냅샷의 온도 42°C만으로 측정 중 클록·전력·부하까지 같았다고 판단하지 않는다. Windows 셀렉터 값이나 기준 측정의 cold-state는 직접 수집하지 않았다.

기준 측정 원본은 `captures/v14-baseline-result/`에 복사하고 해시 일치를 확인했다. 계산값·비교 조건·입력 SHA256은 `results/v14-comparison.json`에 기록했다. 사용자 제공 두 결과 폴더는 수정하지 않았다. 다음 남은 검증은 완전 종료 후 패치 전후 반복 측정 및 실제 사용하는 작업의 안정성이다.

### 제품명 제한 수정 후 연산 검사

별도 `build/windows-benchmark-namefix.zip`에는 수정한 벤치마크 EXE, `check.ps1`, 실행용 `run.cmd`, CUDA 13.2의 cuBLAS/cuBLASLt DLL과 라이선스, 소스와 파일별 SHA256을 포함한다. v14 EFI를 교체할 필요 없이, v14로 진입한 Windows의 같은 부팅에서 압축을 풀고 `run.cmd`를 실행한다. 새 `RESULTS-*` 폴더 전체를 회수한다. 위 추가 실기에서 3종 CUDA 연산의 정상 완료를 확인했다.

이 EXE는 CUDA 13.2로 새로 빌드했으므로 과거 CUDA 12.6 실행 파일과 해시·라이브러리가 다르다. 이번 실행의 정상 동작 여부를 먼저 확인하고, 성능 효과 판정은 새 패키지의 동일 실행 파일·DLL·드라이버 조건으로 패치 전후를 다시 측정한다. 기존 v14 USB ZIP의 실행 파일 없는 WINDOWS 폴더와 별도로 배포한다.

### 실험 동작 및 실행 방법

v13에서 성공한 실제 언락·단일 FLR·GOP 복구·다른 해상도 왕복·녹색 프레임버퍼 읽기를 유지한 뒤, **재부팅 없이 Windows Boot Manager를 실행**한다. SSD EFI 파일이나 BootOrder/BootNext 등 NVRAM 변수는 쓰지 않는다. `-WindowsHandoff`를 지정한 별도 빌드이며, 이 옵션 없이 `-FullUnlock`을 빌드하면 v13의 물리 종료 동작을 유지한다. v14의 Windows 장치·드라이버 정상 응답은 위 실기 결과에서 확인했으며, 연산 성능은 아직 미측정이다.

U 전에 최대 128개 파일 시스템을 조회하고, USB 경로를 제외한 단일 인스턴스 디스크 경로에서 `\EFI\Microsoft\Boot\bootmgfw.efi`를 읽기 전용으로 찾는다. 정상 파일이 정확히 하나여야 하며, 없거나 중복되거나 조회가 실패하면 패치를 시작하지 않는다. 해당 전체 Device Path로 `LoadImage`를 수행하고 로드된 이미지의 장치·응용 프로그램 메모리 유형을 검증한다. Windows는 이때 실행하지 않는다. U 취소/실패 시 미리 로드한 이미지를 해제한다. 부팅 항목의 선택 옵션을 복제하지 않는 직접 파일 체인로드이므로 특수한 BCD/멀티부팅 구성은 별도 검증이 필요하다.

패치·DMA 정리·영상 복구·모드 왕복·색상 읽기·셀렉터·콘솔·로그 검사가 모두 성공하고, 언락 로그가 닫히고 안전 상태가 확인돼야 실행한다. `WINDOWS_START_INTENT`를 Flush한 다음 GOP 로그의 Close가 성공한 경우에만 `StartImage`를 한 번 호출한다. 실패하면 정지하며 재시도·재부팅·다른 부팅 후보로의 자동 전환을 하지 않는다. StartImage가 반환하면 성공 상태라도 정지하고 화면에 상태 코드를 표시한다. 이때 로그는 이미 닫혔으므로 반환 상태는 USB에 추가 저장하지 않는다. `WINDOWS_START_INTENT`는 실행 직전 경계이며 Windows 부팅 성공의 증거는 아니다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build-gop-experiment.ps1 -FullUnlock -WindowsHandoff -Profile profiles/board-refresh-run01.json
experiments\gop\test.cmd
experiments\gop\test-patch-state.cmd
experiments\gop\test-visual.cmd
experiments\gop\test-windows.cmd
powershell.exe -NoProfile -ExecutionPolicy Bypass -File experiments/gop/test-windows-check.ps1
python tools/package_gop_experiment.py --output build/gop-experiment-usb-v14-windows-handoff --zip build/gop-experiment-usb-v14-windows-handoff.zip --assets assets
```

1. ZIP을 USB 루트에 풀고 기존 RUN은 보존한다. SSD EFI에는 설치하지 않는다.
2. 완전 종료 후 USB로 부팅해 **v14-windows-handoff**를 확인하고 U를 한 번 누른다. 초기값 `20700011 / 6` 검사는 유지한다.
3. 녹색 표시 및 보고 저장 뒤 Windows 진입을 기다린다. Windows가 실행되면 재부팅·드라이버 설치/교체 없이 아래 결과 수집을 먼저 한다. 10분간 Windows 진입이 없거나 STOP/반환 오류가 표시되면 상태를 기록한 뒤 물리 종료한다.
4. Windows에서 USB의 드라이브 문자로 이동하고 다음 명령을 실행한다. 관리자 권한은 필요하지 않으며, 장치 정보와 `nvidia-smi -q`를 USB의 새 `WINDOWS/RESULTS-*`에 저장한다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\WINDOWS\check.ps1
# 이전 측정에 사용한 동일 실행 파일이 있으면 경로를 지정해 CUDA 측정도 수행:
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\WINDOWS\check.ps1 -Benchmark 'C:\path\compute-benchmark.exe'
```

CUDA 실행 파일은 포함하지 않는다. `WINDOWS`에 기존 벤치마크 소스·빌드 명령·과거 측정 JSON을 함께 제공한다. 소스 빌드 스크립트는 설치된 Visual Studio C++ 도구를 vswhere로 찾고 CUDA_PATH 또는 PATH의 nvcc를 사용한다. 비교 시 동일 실행 파일 해시·드라이버·부하 조건을 맞춘다. 기존 검증은 행렬 결과의 첫/마지막 원소를 확인하며 전체 원소 검사는 아니다. 스크립트의 `BenchmarkState=passed`는 해당 프로그램의 종료 코드와 기본 출력 검사를 통과했다는 뜻으로, 성능 향상이나 Windows에서 셀렉터 보존을 직접 증명하지 않는다. 실행 파일을 지정하지 않으면 `not-run`이다. 과거 측정값은 이 v14 실행의 결과가 아니다.

회수 자료는 두 UEFI REPORT(`RTX30-GOP-TEST/RUNxx`, `RTX3080-UNLOCK/RUNxx`)와 새 `WINDOWS/RESULTS-*` 전체다. 녹색 표시, Windows 바탕화면 진입, NVIDIA 장치 오류(특히 오류 코드 43), 화면 꺼짐/드라이버 재시작 여부도 기록한다. 벤치마크 성능과 같은 부팅에서의 정상 실행 여부를 확인하기 전까지 패치의 Windows 적용 성공으로 판정하지 않는다.

## 이전 실험: diagnostic v13-mode-cycle-readback

### 실기 결과: 모드 왕복·색상 읽기·실제 영상 복구 성공

2026-10-21 제공된 v13 로그와 사용자의 오른쪽 아래 녹색 사각형 표시 확인으로, 이 보드의 이번 실행에서 실제 영상 출력 복구에 성공했다. 초기 셀렉터 `20700011 / 6`, PROM·자산 검증을 통과했고, `PATCH_TRANSACTION result=0 containment=0 started=1 quiesced=1 fault=0 retained=0 maps=0 FLR=1 safe=1`로 실제 언락·단일 FLR·DMA 정리를 완료했다. post-start 자식 2개와 물리 GOP 복구도 확인됐다.

`VISUAL_MODE_CYCLE original=0 current=0 alternate=1 found=1`에 이어 mode 1(640×480) → 원래 mode 0 전환이 모두 `status=0`이었다. 원래 모드의 오른쪽 아래 (3744,2064)에 96×96 녹색 표시를 수행했고, `VISUAL_READBACK_RESULT pixels=9216 mismatches=0 matched=1`로 전 픽셀 읽기가 일치했다. `before-gop`, `after-gop`, `after-alternate-mode`, `after-set-mode`, `after-blt`, `after-readback`, `after-output` 모두 `SS0=88888888 SS1=00000008 matched=1`이다.

`FULL_FINAL status=0 touched=1 restored=1`, `console_output_status=0`, `PATCH_LOG_CLOSE status=0`과 마지막 `END`까지 기록됐다. RTC 경과 시간에서 U는 126000ms, 보고 완료는 274000ms이므로 U 이후 완료까지 **148초(2분 28초)**다. 사용자는 실제 녹색 사각형을 확인했으며, 완료 문구를 봤는지는 별도로 보고하지 않았다.

v12는 동일 모드 설정 후 신호 없음이었고, v13은 다른 해상도 왕복 후 실제 영상을 복구했다. 이는 이 환경에서 모드 왕복의 효과를 뒷받침하지만, 펌웨어의 동일 모드 초기화 생략 여부나 정확한 복구 기전은 아직 확정할 수 없다. 한 번의 실기 성공이며 반복 재현성·Windows 인계·연산 성능은 미검증이다. 원본은 `captures/v13-mode-cycle-readback-result/GOP.TXT`, `UNLOCK.TXT`에 보존했다.

### 실험 동작 및 실행 방법

v12의 해제·언락·단일 FLR·DMA 정리·자식 진단·복구를 유지하고 표시 검사만 바꾼다. 복구된 물리 GOP에서 원래 모드의 QueryMode 정보가 캡처와 같은지 확인한 뒤 최대 128개 지원 모드 중 원래/현재 모드 번호와 다르고 해상도도 원래와 다른 첫 RGB/BGR 모드를 선택한다. 후보의 모드 번호·해상도·조회 상태를 기록하며, 다른 해상도가 없거나 조회가 실패하면 SetMode 없이 STOP한다.

선택한 다른 모드 → 원래 모드 순서로 SetMode를 각각 한 번 호출한다. 매번 모드 정보·프레임버퍼 범위와 `PATCH_STATE`를 검증한다(`after-alternate-mode`, `after-set-mode`). 전환 실패나 셀렉터 불일치 뒤에는 추가 전환을 하지 않으므로 다른 모드에서 중단될 수 있다. 재시도나 추가 FLR은 없다.

원래 모드로 복귀하면 오른쪽 아래 최대 96×96 녹색 사각형을 Blt(VideoFill)로 그리고, `after-blt` 검사 후 **프레임버퍼 주소를 volatile 32비트 읽기로 직접 조회**한다. 사각형 전체 픽셀을 `0x0000ff00`과 비교하며 예약된 상위 8비트는 무시한다. `VISUAL_READBACK_RESULT`에 픽셀 수·불일치 수·첫 불일치 값을 남기고 `after-readback` 셀렉터를 검사한다. 읽기 직전 모드·주소 범위·픽셀 형식을 다시 검증한다. 불일치는 실패로 처리한다. 메모리 읽기 일치는 스캔아웃이나 모니터 링크 복구의 증거가 아니며, 실제 신호 여부는 별도로 관찰해야 한다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build-gop-experiment.ps1 -FullUnlock -Profile profiles/board-refresh-run01.json
experiments\gop\test.cmd
experiments\gop\test-patch-state.cmd
experiments\gop\test-visual.cmd
python tools/package_gop_experiment.py --output build/gop-experiment-usb-v13-mode-cycle-readback --zip build/gop-experiment-usb-v13-mode-cycle-readback.zip --assets assets
```

자산 포함 ZIP을 USB 루트에 풀어 EFI를 교체하고 기존 RUN은 보존한다. 완전 종료 후 부팅해 **v13-mode-cycle-readback** 확인 → U 한 번 → 최대 10분 대기 → 물리 종료한다. 화면이 돌아오지 않아도 같은 시간 뒤 물리 종료하고 두 폴더(`RTX30-GOP-TEST/RUNxx`, `RTX3080-UNLOCK/RUNxx`)의 새 REPORT 전체를 회수한다. 실제 모니터의 신호 없음/영상 표시 여부와 녹색 사각형·완료 문구 표시 여부도 함께 기록한다. v13은 위 실기 1회에서 영상 복구 성공을 확인했다. 반복 재현성과 Windows 인계·연산 성능은 별도 검증 대상이다. 초기값 `20700011 / 6` 검사는 유지한다.

주요 판독값은 `VISUAL_MODE_CYCLE found=1`, 두 `VISUAL_SET_MODE_RESULT status=0`, `VISUAL_READBACK_RESULT matched=1 mismatches=0`, 각 단계 `PATCH_STATE matched=1`, 마지막 `END`다. 이 값이 모두 성공인데도 모니터가 신호 없음이면 다른 해상도 왕복과 메모리 색상 확인으로도 영상이 복구되지 않은 결과다. 출력 엔진/모니터 링크의 구체적인 원인은 여전히 확정할 수 없다.

## 이전 실험: diagnostic v12-child-inspect

### 전원 버튼 종료 후 재실행: 모든 API·셀렉터 검사 완료, 실제 영상은 신호 없음

사용자는 전원 버튼으로 종료 후 다시 켰다고 확인했다(PSU/전원선 차단 확인은 아님). 새 v12 실행은 초기 상태 검사를 통과했고, post-start 양쪽 루트 API가 성공해 자식 2개를 관측했다. 한 자식에는 GOP/EDID/path가, 다른 자식에는 EDID/path가 있고 GOP는 EFI_UNSUPPORTED였다. 기존 자식 검사가 통과했고 역할별 복구도 모두 성공했다. 따라서 이번 실행에서는 v11의 자식 열거 오류가 재현되지 않았다. 추가 진단의 대기 시간 또는 전원 상태가 원인이었다고 확정할 수는 없다.

`VISUAL_QUERY original_match=1`, `VISUAL_SET_MODE_RESULT status=0`(원래 mode=0), `VISUAL_BLT_RESULT status=0`, `console_output_status=0`, `FULL_FINAL status=0 touched=1 restored=1`, `PATCH_LOG_CLOSE status=0`과 마지막 END가 모두 기록됐다. 셀렉터는 before-gop/after-gop/after-set-mode/after-blt/after-output 모두 `88888888 / 8`을 유지했다. 하지만 사용자는 실제 모니터가 **신호 없음** 상태였다고 확인했다. 이는 단순히 완료 문구를 못 본 관측보다 명확한 영상 미복귀이며, API 성공이나 메모리 주소가 화면에 영상이 전달됐음을 증명하지 않는다.

같은 모드의 SetMode 1회로는 이 실행에서 영상을 복구하지 못했다. 다음에는 현재와 다른 지원 모드로 전환했다 원래 모드로 복귀하는 동작, 실제 프레임버퍼 읽기 확인 및 출력 엔진/모니터 링크 쪽을 구분할 자료가 필요하다. 같은 모드 설정이 펌웨어에서 재초기화를 생략했는지는 가설이다. 새 실험 전 구체적인 지원 모드와 각 단계 셀렉터 검사를 준비해야 하며 Windows 인계·연산 성능은 여전히 미검증이다. 로그 원본은 `captures/v12-child-inspect-reboot-result/GOP.TXT`, `UNLOCK.TXT`에 보존했다.

### 첫 실기 피드백: 초기 셀렉터 0/0으로 U 전에 거부

v12의 prepare 진단은 `unique=2`, `FULL_CHILDREN_RESULT phase=prepare status=0`으로 통과했다. 물리 자식의 GOP/EDID/path와 EDID 전용 자식의 EDID/path가 조회됐으며 알려진 두 자식 구조였다. 이후 언락 cold-state 검사에서 `SS0=0 SS1=0`이 관측돼 `PATCH_PREFLIGHT status=0x8000000000000006`, `CANCELLED_OR_REFUSED stage=unlock-preflight ... touched=0`으로 종료했다. 빌드 기대값은 새 Probe에서 확인한 `20700011 / 6`이다. 이번 실행은 U 승인, 자산 읽기, Stop, 언락, FLR 및 post-start 진단에 도달하지 않았다.

이 결과는 자식 진단의 실패나 post-start 오류 재현을 뜻하지 않는다. 0/0으로 바뀐 이유와 전원 상태의 영향은 미확정이다. 먼저 이전 실행의 종료 방식(RESET/전원 종료)을 확인하고, 전원을 완전히 차단했다 다시 켠 상태에서 읽기 전용 Probe REPORT/PROM을 재수집해 초기값을 비교해야 한다. 이번 0/0만 근거로 초기값 검사를 제거하거나 프로필을 0/0으로 바꾸지 않는다. 원본은 로컬 `captures/v12-child-inspect-result/GOP.TXT`, `UNLOCK.TXT`에 보존했다.

v11이 NVIDIA 역할 0 Start 직후 자식 열거 단계에서 EFI_UNSUPPORTED로 중단된 원인을 구분한다. v12는 같은 해제·실제 언락·FLR 한 번·복구 순서를 유지하면서 **prepare와 post-start**에 루트 PCI I/O 및 Device Path의 open 관계를 수집하고, 발견한 자식의 GOP·EDID Discovered·Device Path 인터페이스 조회 상태를 기록한다. 인터페이스 내부나 장치 경로 바이트는 역참조하지 않는다. 역할 0 이후 진단은 `TIME phase=post-start-inspect-begin/end`로 측정한다.

- `CHILD_ROOT_INTENT/RESULT`: 어느 루트 프로토콜의 OpenProtocolInformation이 호출됐고 어떤 상태·항목 수를 반환했는지.
- `CHILD_OPEN`: 모든 항목의 agent, child, attributes, OpenCount와 expected_agent/child_edge/usable/duplicate 플래그. usable은 조회 가능한 비NULL·비루트 자식 핸들이라는 의미이며 실행 승인이 아니다.
- `CHILD_PROTOCOL_INTENT/RESULT`: 자식별 gop/edid/path 조회 상태와 포인터. 인터페이스 부재도 그대로 기록하며 수집 오류로 숨기지 않는다.
- `CHILD_INSPECT_END ... unique=N`: 두 프로토콜에서 중복을 제거한 관측 자식 수. 프로토콜당 128개 open, 전체 16개 자식을 넘으면 진단을 중단한다.
- `FULL_CHILDREN_RESULT phase=post-start`: 기존 두 자식 검사의 실제 결과와 선택된 a/b 핸들. 진단 이후 원래 검사가 새로 열거하므로 두 기록은 동결된 하나의 스냅샷이 아니다.
- `FULL_CHILD_GOP candidate=0/1`: 기존 물리 GOP 선택 단계의 조회 결과. 두 후보 모두 GOP가 없으면 기존대로 EFI_NOT_FOUND로 중단한다.

관측된 자식을 자동 승인하거나 재연결하지 않으며 기존 소유자·정확한 두 자식 조건은 유지한다. 루트 열거 오류·범위 초과·진단 로그 실패 시 추가 복구를 진행하지 않는다. 정상 검사가 통과하면 기존 v11의 SetMode 1회·녹색 표시와 단계별 셀렉터 검사로 이어진다. 실패 시 추가 FLR, 재시도, 범용 ConnectController 또는 Windows 인계는 하지 않는다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build-gop-experiment.ps1 -FullUnlock -Profile profiles/board-refresh-run01.json
experiments\gop\test.cmd
python tools/package_gop_experiment.py --output build/gop-experiment-usb-v12-child-inspect --zip build/gop-experiment-usb-v12-child-inspect.zip --assets B:\firmware
```

자산이 포함된 ZIP 내용물을 USB 루트에 복사해 EFI도 교체하고 기존 RUN은 보존한다. 완전 종료 후 부팅해 **v12-child-inspect** 확인 → U 한 번 → 최대 10분 기다린다. REPORT SAVED 후 물리 종료하고, 10분간 완료 안내가 없거나 화면이 검어도 물리 종료한다. 두 폴더의 새 REPORT 전체와 녹색 사각형·완료 문구의 실제 표시 여부를 전달한다. v12의 실기 결과는 위 기록과 같다. 이 명령은 이전 빌드용 기록이다.

## 이전 실험: diagnostic v11-mode-recovery

### 실기 피드백: NVIDIA Start 뒤 자식 열거 단계에서 중단

사용자 제공 v11 로그는 언락·FLR 1회·DMA 정리와 `before-gop SS0=88888888 SS1=00000008 matched=1`까지 통과했다. NVIDIA 역할 0의 `FULL_START_RESULT status=0 shared_guard=0` 직후 `FULL_FINAL status=0x8000000000000003 touched=1 restored=0`과 STOP이 기록됐다. 현재 코드에서 이 사이 EFI_UNSUPPORTED를 그대로 반환하는 구간은 PCI I/O/Device Path의 두 자식 관계를 확인하는 `Children()`이다. 그 뒤 물리 GOP 조회 실패는 EFI_NOT_FOUND로 변환되므로 이번 오류와 구분된다. 자식 수·각 프로토콜 조회 결과가 기록되지 않아 자식 부족/초과 또는 열거 API 오류 중 어느 조건인지는 미확정이다.

`after-gop`, `VISUAL_QUERY`, `VISUAL_SET_MODE_INTENT`, `VISUAL_BLT_INTENT`가 없으므로 SetMode와 녹색 표시 코드는 실행되지 않았다. 이번 결과로 명시적 SetMode의 효과를 평가할 수 없다. 더 기다려도 이 실행은 STOP에서 복구를 재시도하지 않는다. 수집 원본은 로컬 `captures/v11-mode-recovery-result/GOP.TXT`, `UNLOCK.TXT`에 보존했다.

유효한 RTC 기록은 자산 읽기 102초(U 전), 해제/격리 11초, DMA 준비 3초, firmware 구간 32초, 전체 transaction 63초, DMA 정리 5초를 보여준다. 구간은 서로 포함될 수 있고 USB 로그 비용도 포함한다. U→FLR 시작은 88초, U→GOP 복구 시작은 101초다. FLR 시작/종료는 같은 RTC 초에 기록됐으며 0ms는 실제 무대기를 뜻하지 않는다. 다음 진단은 역할 0 Start 직후의 두 루트 프로토콜별 모든 자식 관계와 각 자식의 GOP/EDID/Device Path 조회 상태를 기록해 실패 조건을 구분해야 한다.

v10에서 GOP 재초기화 후 셀렉터 보존은 통과했지만 실제 화면이 돌아오지 않았다. v11은 같은 실제 언락과 FLR 한 번을 수행한 뒤, 복구된 **새 물리 GOP**에서 원래 모드 번호의 QueryMode 정보가 원래 캡처와 같은지 확인하고 **SetMode를 한 번만 호출**한다. 성공 후 오른쪽 아래에 최대 96×96 녹색 사각형을 Blt(VideoFill)로 한 번 그리고 콘솔 안내를 출력한다. 같은 모드를 설정했을 때 펌웨어가 실제 출력 엔진을 재설정할지는 미검증이다. 다른 모드 전환이나 추가 FLR은 하지 않는다.

`PATCH_STATE`의 `before-gop`, `after-gop`, `after-set-mode`, `after-blt`, `after-output`을 기록한다. SetMode/Blt 실패나 셀렉터 불일치, 로그 실패 시 추가 표시 작업을 하지 않고 STOP한다. 실패한 SetMode/Blt 뒤에도 가능한 셀렉터 조회는 한 번 기록하며, 원래 API 오류를 성공으로 바꾸지 않는다. QueryMode 정보, 모드/프레임버퍼와 공유 콘솔 보호 검사는 유지한다.

시간은 UEFI Runtime GetTime의 RTC를 사용한다. U 전 사용 가능 여부를 검사하며, `TIME phase=... elapsed_ms=... since_last_ms=... clock_valid=1`이 자산 읽기, U, GOP 해제, transaction, DMA, firmware, FLR, DMA 정리, GOP 복구, SetMode, Blt, 콘솔 및 보고 저장 단계를 구분한다. ms는 출력 단위이며 실제 해상도는 펌웨어 시계에 따라 초 단위일 수 있다. 구간에는 USB 로그 Write/Flush 시간도 포함된다. 시계 오류나 역행 후 `clock_valid=0`은 경과 시간으로 해석하지 않는다. 시계 오류 자체로 진행 중인 하드웨어 복구를 중단하지는 않지만 로그 저장 실패는 계속 실패 처리한다. v10의 매 호출 `FULL_TRACE`는 제거하고 기존 중요 기록의 Flush는 유지했다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build-gop-experiment.ps1 -FullUnlock -Profile profiles/board-refresh-run01.json
experiments\gop\test.cmd
experiments\gop\test-patch-state.cmd
experiments\gop\test-visual.cmd
python tools/package_gop_experiment.py --output build/gop-experiment-usb-v11-mode-recovery --zip build/gop-experiment-usb-v11-mode-recovery.zip --assets B:\firmware
```

ZIP 내용물을 FAT32 USB 루트에 복사해 EFI도 교체한다. 기존 RUN은 보존한다. 완전 종료 후 부팅해 화면과 ENTRY의 **v11-mode-recovery**를 확인하고 U를 한 번 누른다. **U 이후 최대 10분**까지 기다린다. 녹색 사각형과 완료 안내가 실제 표시되는지 확인하고 REPORT SAVED 후 물리 전원으로 종료한다. 10분까지 완료 안내가 없거나 화면이 검어도 물리 전원으로 종료하며 Windows로 진행하지 않는다. 이는 사용자 대기 한도이며 멈춘 펌웨어를 자동 중단하는 타이머는 아니다.

두 폴더의 새 REPORT 전체와 함께 U→화면 소실 시간, 소실→복귀 시간, 녹색 사각형/문구/REPORT SAVED 표시 여부를 전달한다. 모든 API 성공과 셀렉터 유지도 실제 영상 복구의 증거를 대신하지 않는다. v11은 실기 미검증이다.

## 이전 실험: diagnostic v10-isolation-trace

### 실기 결과: 셀렉터 보존 통과, 실제 화면 미복귀

사용자 제공 v10 로그는 `PATCH_TRANSACTION result=0 containment=0 started=1 quiesced=1 fault=0 retained=0 maps=0 FLR=1 safe=1`, `FULL_PATCH_CHECK_END call=2 status=0`, `FULL_FINAL status=0 touched=1 restored=1`과 마지막 `END`까지 완료됐다. GOP 복구 전후 모두 `SS0=88888888 SS1=00000008 ... read_ok=1 matched=1`이며, 실제 언락·단일 FLR·DMA 정리 후에도 GOP 재초기화가 이 두 셀렉터 값을 지우지 않았음을 이 보드에서 확인했다. 실제 연산 성능과 Windows 드라이버 인계는 여전히 미검증이다.

모든 역할의 Start와 공유 보호 검사, `console_output_status=0`, `PATCH_LOG_CLOSE status=0`이 기록됐지만 사용자는 약 5분 뒤 화면이 꺼지고 이후 1분간 복귀하지 않아 RESET했다고 보고했다. 따라서 `restored=1`은 UEFI 객체·관계 복구 판정이며 실제 영상·링크 복구 성공을 의미하지 않는다. PCI 전후 64개 항목 중 차이는 `0x80: 00092930 → 00002930` 하나이며 BAR와 Command `0x3`은 일치한다. PLM 관측값 `ffffffff`도 복구 전후 동일하게 기록됐으며 이 값의 의미나 영상 소실과의 인과관계는 이번 기록으로 확정하지 않는다.

v10에는 실제 경과 시간 계측이 없어 약 5분을 자산 읽기·DMA 매핑·펌웨어·로그 Flush·드라이버 호출별로 나눌 수 없다. 매 줄 USB Flush와 2만 개 이상의 DMA 매핑이 지연 후보지만 원인은 미확정이다. 이전의 60초 무응답만으로 영구 정지를 판정할 수 없으며, 위 v9 로그의 중단도 확정된 UEFI 호출 정지로 분류하지 않는다. 아래의 60초 안내는 이 실기에서 관측한 전체 지연을 포괄하지 못한다. 다음 진단은 구간별 경과 시간과 실제 영상 복구를 대상으로 삼아야 한다.

수집 원본은 로컬 `captures/v10-isolation-trace-result/GOP.TXT`, `UNLOCK.TXT`에 보존했다.

v9 실기에서는 GOP 완전 해제, PROM·자산 검사, FWSEC 실행과 `SS0=88888888`, `SS1=8`의 FLR 전 readback까지 확인됐다. 사용자는 U 뒤 60초 이상 대기했고 화면 변화 없이 물리 종료했다. `PCIe FLR begin`은 없고, GOP 로그는 두 번째 격리 검사에서 물리 GOP의 소멸 확인 직후 끝난다. 이 기록만으로 특정 UEFI 호출의 정지와 로그 저장 지연을 확정적으로 구별하지는 못한다. FLR·GOP 복구·복구 후 패치 보존은 미확인이다.

v10은 이 두 번째 검사에 `FULL_TRACE`를 추가한다. 기존 핸들의 HandleProtocol, 가상 GOP의 GOP/경로 조회, ProtocolsPerHandle, OpenProtocolInformation, FreePool, Mode/프레임버퍼 읽기와 공유 콘솔 검사 전후를 USB에 Flush한다. `FULL_PATCH_CHECK_BEGIN/END call=2`가 검사 전체를 감싼다. 마지막 enter와 대응하는 return의 유무로 중단 구간을 좁힌다. 검사 조건, 실제 언락과 FLR 순서는 유지하며 진단 로그가 실패해도 FLR로 진행하지 않는다. 추가 Flush는 실행 시간을 늘릴 수 있으며, 마지막 enter만 있다는 사실이 그 API 자체의 결함을 증명하지는 않는다.

현재 보드의 새 Probe RUN01과 PROM은 동일한 PROM 해시, `SS0=20700011`을 확인했다. 원래 `20740011`과 다른 이유는 미확인이다. 현재 빌드는 이 캡처에서 생성한 `profiles/board-refresh-run01.json`을 사용한다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build-gop-experiment.ps1 -FullUnlock -Profile profiles/board-refresh-run01.json
experiments\gop\test.cmd
python tools/package_gop_experiment.py --output build/gop-experiment-usb-v10-isolation-trace --zip build/gop-experiment-usb-v10-isolation-trace.zip --assets B:\firmware
```

아래 v9와 같은 USB 배치 및 실행 절차를 사용하되 화면과 ENTRY에서 **v10-isolation-trace**를 확인한다. U 후 화면 변화가 없어도 60초까지 기다리고, REPORT SAVED가 없으면 물리 전원을 종료한다. 두 새 RUN의 REPORT 전체를 가져온다. 자산은 포함된 ZIP을 그대로 사용한다. 이번 빌드도 실제 언락을 실행하며 Windows로 계속 부팅하지 않는다.

## 이전 실험: diagnostic v9-unlock-rejoin

사용자가 보고한 v8 실기 결과는 `FULL_ISOLATION_RESULT status=0 interfaces=0`, `FULL_FLR_PCI_RESTORED command=0x3 BARs=matched BME=0`, `FULL_FLR_RESULT attempted=1 status=0`, `FULL_FINAL status=0 touched=1 restored=1`, `console_output_status=0`이며 약 5초 뒤 화면이 복귀했다. PCI `0x80`의 상위 상태 필드만 지워지고 하위 제어값 `2930`과 BAR/Command가 유지됐다. 이는 이 보드에서 해제·FLR·화면 복구가 성공했다는 사용자 관측이며, 언락 보존 결과는 아니다.

v9는 같은 완전 해제 경로에서 **기존 실제 언락 코어 → 코어 자체의 FLR 한 번과 PCI/DMA 정리 → NVIDIA GOP 및 콘솔 재초기화 → 셀렉터 재조회**를 수행한다. v8의 별도 FLR을 추가하지 않는다. `profiles/board.json`의 캡처 근거를 검사해 만든 헤더로 장치·Subsystem·PROM·칩·초기 레지스터를 고정하며, 초기 상태와 펌웨어 자산 5개의 크기/SHA256을 U 전에 확인한다. GOP 해제 후 독점 접근 상태에서도 decode/config/BAR/cold/PROM을 다시 검사한다. 기존 Unlock의 독립 화면 조건은 이 빌드에서 v8의 완전 해제 및 공유 콘솔 보호 검사로 대체한다. 원래 Unlock 빌드의 독립 화면 검사는 유지한다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build-gop-experiment.ps1 -FullUnlock -Profile profiles/board.json
experiments\gop\test.cmd
experiments\gop\test-patch-state.cmd
python tools/package_gop_experiment.py --output build/gop-experiment-usb-v9-unlock-rejoin --zip build/gop-experiment-usb-v9-unlock-rejoin.zip --assets B:\firmware
```

`--assets`는 자산 파일 5개가 직접 들어 있는 폴더다. 패키징에서도 크기와 SHA256이 일치해야 파일을 포함한다. 생략하면 `RTX3080-UNLOCK/ASSETS`에 기존 자산을 넣어야 하며, 누락된 채 부팅하면 U/화면 해제 전에 거부한다. 빌드 프로필 재검사에는 원본 Probe REPORT/PROM이 필요하다. 배포본에는 생성한 프로필 헤더와 보드 JSON, 언락 소스를 포함한다.

실행 절차:

1. ZIP 내용물을 FAT32 USB 루트에 복사한다. 기존 RUN은 보존하고 EFI만 옛 파일로 남지 않도록 한다. `EFI/BOOT/BOOTX64.EFI`, `RTX30-GOP-TEST`, `RTX3080-UNLOCK/ASSETS`가 있어야 한다.
2. 완전히 전원을 종료한 뒤 NVIDIA 모니터를 연결한 상태로 USB UEFI 부팅한다. **diagnostic v9-unlock-rejoin**, manifest `Mode=full-unlock`을 확인한다. 자산 검사 완료 후 U를 한 번 누른다.
3. 화면 복귀 후 **REPORT SAVED까지 기다린 뒤 물리 전원을 종료**한다. 복귀 안내가 60초 안에 없으면 물리 전원 버튼으로 종료한다. 실패 후 재시도하거나 Windows로 계속 부팅하지 않는다.
4. `RTX30-GOP-TEST/RUNxx/REPORT.TXT`와 `RTX3080-UNLOCK/RUNyy/REPORT.TXT` 전체를 가져온다. RUN 번호는 서로 다를 수 있다. 화면 소실 시간과 복귀·REPORT SAVED 표시 여부도 기록한다.

판정:

- 언락 로그의 `PATCH_TRANSACTION result=0 containment=0 ... quiesced=1 fault=0 retained=0 maps=0 FLR=1 safe=1`: 언락 및 한 번의 FLR/DMA 정리 완료.
- GOP 로그의 `FULL_PATCH_RESULT status=0 safe=1` 뒤 `PATCH_STATE phase=before-gop SS0=88888888 SS1=00000008 ... read_ok=1 matched=1`: GOP 시작 전 패치 상태 확인.
- `PATCH_STATE phase=after-gop SS0=88888888 SS1=00000008 ... read_ok=1 matched=1`과 `FULL_FINAL status=0 touched=1 restored=1`: GOP 복구 직후에도 두 셀렉터 유지.
- `before-gop matched=1`, `after-gop read_ok=1 matched=0`, `restored=1`: 화면 프로토콜은 복구됐지만 셀렉터가 유지되지 않음. `FULL_FINAL status`는 오류가 된다. 값을 다시 쓰거나 추가 FLR을 하지 않는다.
- `read_ok=0` 또는 `after-gop` 없음: 보존 여부를 판정할 수 없다. 언락/정리 실패, 사전 셀렉터 불일치 또는 독점 접근 해제 실패 시 GOP를 시작하지 않고 정지한다.
- `console_output_status=0`, PCI 사후 기록, `PATCH_LOG_CLOSE status=0`, 마지막 `END`와 실제 `REPORT SAVED` 표시까지 확인한다.

언락 코어의 로그는 화면을 호출하지 않고 USB에만 기록한다. 정리가 불확실하면 DMA 자원과 독점 접근을 유지하고 전원 종료를 기다린다. **v9는 아직 실기 미검증이며, 셀렉터 유지가 확인돼도 실제 연산 성능과 Windows 드라이버 인계를 증명하지 않는다.** 이번 실험은 Windows로 자동 진행하지 않는다.

## 이전 실험: diagnostic v8-virtual-gop

v7은 GPU 루트 소유자·자식이 0이고 물리 GOP·중간 TextOut·EDID 전용 자식의 검사 대상 인터페이스가 사라진 뒤, 경로와 프레임버퍼가 없는 중간 GOP 하나 때문에 중단됐다. v8은 **기존 중간 핸들의 GOP에 한해서** 장치 경로 없음, 유효한 Mode와 프레임버퍼 주소·크기 모두 0, 해당 핸들의 모든 설치 프로토콜에 활성 소유자·자식·독점 open 없음이 확인되면 잔존을 허용한다. 수동 GET/TEST/BY_HANDLE open은 활성 소유권으로 세지 않는다. 잔존 인터페이스는 GOP 열거에도 나타나야 한다. 알 수 없는 경로 없는 GOP, 물리 GOP 잔존, 루트 활성 관계, 조회 오류, 진단 로그 실패는 계속 리셋을 차단한다. 이 조건은 관측 가능한 UEFI 관계의 검사이며 펌웨어 내부 참조가 전혀 없음을 증명하지는 않는다.

`FULL_VIRTUAL_GOP ... known_intermediate=1 ... owners=0 children=0 ... framebuffer=0 bytes=0 allowed=1`은 이 예외 검사의 통과를 뜻한다. `FULL_ROOT_PROTOCOL`의 handle은 현재 조사 중인 핸들이며 루트 외에 중간 GOP 핸들도 기록한다. `FULL_OLD_INTERFACE`, `FULL_REMAINING_GOP`, `FULL_ISOLATION_RESULT` 진단을 유지한다. 완전 해제 조건이 모두 통과하면 FLR 한 번과 PCI/GOP 복구까지 진행한다.

이번 모드는 U 승인 후 **GPU GOP 전체 해제 → PCIe FLR 한 번 → PCI 설정 및 화면 복구**를 수행한다. 연산 언락이나 FWSEC/V67 자산은 사용하지 않는다. v5와 달리 실제 GPU 리셋과 PCI 설정 쓰기가 있다. 모델·보드 제조사·Subsystem 제한은 없지만, v4에서 확인한 물리 GOP 하나·중간 출력 하나·공용 콘솔·EDID 전용 자식 하나의 구조와 역할별 드라이버를 정확히 식별해야 한다. 다른 구조나 알 수 없는 활성 소유자가 있으면 해제 전에 거부한다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build-gop-experiment.ps1 -FullFlr
experiments\gop\test.cmd
python tools/package_gop_experiment.py --output build/gop-experiment-usb-v8-virtual-gop --zip build/gop-experiment-usb-v8-virtual-gop.zip
```

완전히 전원을 종료한 뒤 NVIDIA 카드에 모니터를 연결하고 USB로 UEFI 부팅한다. 기존 RUN 로그는 보존한다. 화면과 첫 로그에 **diagnostic v8-virtual-gop**, manifest에 `Mode=full-flr`가 있는지 확인한다. 안내를 읽고 U를 한 번 누른다. 화면이 꺼질 수 있다. 복구가 확인되면 PCI 사후 로그를 저장하므로 **REPORT SAVED가 나올 때까지 기다린 다음** 물리 전원을 종료한다. 복구 안내 자체가 60초 안에 나오지 않으면 물리 전원 버튼으로 종료한다. 이 프로그램은 성공해도 Windows로 자동 진행하지 않는다. 새 RUN의 REPORT 전체와 화면 복귀·REPORT SAVED 표시 여부를 전달한다.

실행 순서는 다음과 같다.

1. PCI 식별·초기 256바이트·PM D0·FLR 지원·PCIe capability 구조·BME/인터럽트/ROM decode 조건을 확인한다. BME가 켜져 있거나 MSI/MSI-X가 활성화되어 있으면 진행하지 않는다.
2. U 이후 소유권·콘솔·초기 PCI 설정을 다시 비교한다. 텍스트 멤버, 중간 GOP 소비자, 중간 출력을 만든 드라이버 순으로 해제한다.
3. EDID 소비 드라이버를 물리 GOP 자식 및 EDID 전용 자식 각각에 대해서만 Stop한다. 이 두 역할에 한해서 RuntimeServicesCode 드라이버를 허용한다. 공유 입력을 담당하는 같은 드라이버의 전역 상태·공용 콘솔에는 Stop을 호출하지 않는다. 각 호출 뒤 공용 콘솔/입력 객체와 활성 open 관계를 비교한다.
4. NVIDIA GOP 드라이버의 두 자식을 해제하고 필요하면 루트 BY_DRIVER 소유권도 해제한다. 활성 루트 소유자가 없고 물리 GOP·중간 TextOut·EDID 전용 자식의 검사 대상 인터페이스가 사라졌는지 다시 검사한다. 남은 중간 GOP는 위의 가상 GOP 조건을 모두 통과해야 한다.
5. PCI I/O 독점 접근을 얻은 뒤 BME=0과 PCIe Transaction Pending=0을 확인한다. FLR 비트를 한 번만 쓰고, 최소 100ms 동안 앱이 해당 PCI 함수에 접근하지 않는다. 장치 식별 복귀를 기다린 뒤 decode를 끄고 BAR·ROM decode 비활성 값·일부 PCI 제어 필드를 복원한다. 펌웨어 decode 캐시도 맞추며 BME는 활성화하지 않는다. BAR와 Command readback을 확인하고 독점 접근을 닫는다.
6. 기존 NVIDIA GOP 드라이버, EDID 소비자, 중간 출력 드라이버, GOP 소비자, 텍스트 멤버 순으로 Supported/Start를 수행한다. 모든 호출은 저장한 드라이버와 해당 장치에 한정하며 범용 재귀 Connect/DisconnectController를 사용하지 않는다.
7. 새 자식 핸들을 다시 조회한다. 바뀐 핸들 주소를 대응시켜 원래 활성 소유권 관계가 복원됐는지 확인한다. 공용 콘솔/입력 객체는 기존과 같아야 하고, 물리 GOP 및 텍스트 출력이 다시 사용 가능해야 한다. 복구 후 실제 화면 안내와 PCI 사후 로그를 기록한다.

어떤 Stop이나 복구 호출이 실패하거나 공용 입력 상태가 변하면 추가 해제·리셋을 진행하지 않고 정지한다. 일부 드라이버가 이미 해제된 상태일 수 있어 Windows로 계속 부팅하지 않는다. FLR 쓰기 오류도 최소 대기 시간은 유지하며 자동 재시도하지 않는다. 독점 접근 해제나 PCI 복원 실패 시 GOP 재시작을 하지 않고 정지한다. 이 실패 처리 경로는 화면이 돌아오지 않을 수 있으므로 물리 전원 종료를 사용한다.

주요 판정 기록:

- `FULL_READY`: 여섯 역할 및 두 자식 구조와 PCI 초기 조건을 확인함.
- `FULL_STOP_RESULT ... status=0 shared_guard=0`: 해당 장치의 Stop 및 공유 콘솔/입력 검사가 통과함.
- `FULL_FLR_INTENT`: 완전 해제·독점 접근 이후 실제 리셋 직전 의도를 Flush함.
- `FULL_FLR_RESULT attempted=1 status=0`: 한 번의 FLR과 필요한 PCI 복원이 완료됨.
- `FULL_START_RESULT ... status=0 shared_guard=0`: 저장한 드라이버의 복구 및 공유 상태 검사 통과.
- `FULL_RESULT ... detach=0 isolated=0 exclusive=0 close=0 restore=0 verify=0 restored=1`, `FULL_FINAL status=0 touched=1 restored=1`: 전체 프로토콜 복구 검사 통과.
- `console_output_status=0`, PCI 사후 64개 항목, 마지막 `END`, 화면의 `REPORT SAVED`: 출력 호출과 로그 저장 완료. 실제 표시도 사용자가 확인해야 한다.

PCI 사후 256바이트는 분석 자료다. FLR이나 GOP 초기화가 바꿀 수 있는 상태 필드까지 전부 동일하다고 자동 인증하지는 않는다. 이 실험의 성공도 **패치한 셀렉터가 GOP 재초기화 뒤 유지되는지, 실제 연산 언락과 Windows 드라이버 인계가 되는지**를 증명하지 않는다. 이 항목은 후속 실험이다.

FLR 및 PCI 복원 순서는 기존 `src/unlock/port.c`의 checked FLR/복원 절차를 참고했다. 새 실험은 해당 언락 코어를 링크하지 않고 별도의 PCI 전용 구현을 사용한다. 모의 검사는 한 번의 리셋·100ms 접근 금지·BME 조건·남은 GOP 거부·EDID Stop의 입력 영향·새 핸들·부분 실패·독점 해제 실패와 복구 실패를 확인하지만, 실제 NVIDIA/보드 펌웨어의 재초기화 성공은 아직 미검증이다.

## 이전 해제 실험: diagnostic v5-leaf

v4-tree에서 관측한 물리 GOP → 중간 텍스트 출력 → 공용 콘솔 구조 중, **중간 텍스트 출력의 공용 콘솔 연결 한 개만** 분리·복구한다. GPU GOP와 EDID 드라이버는 해제하지 않는다. 재귀 Connect/DisconnectController, GPU 독점 접근, 언락 및 FLR은 사용하지 않는다. 모델·보드 제조사·Subsystem 제한은 없으며 NVIDIA Display 장치 한 개와 식별 가능한 단일 출력 연결을 요구한다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build-gop-experiment.ps1 -LeafOnly
experiments\gop\test.cmd
python tools/package_gop_experiment.py --output build/gop-experiment-usb-v5-leaf --zip build/gop-experiment-usb-v5-leaf.zip
```

ZIP 내용물을 USB 루트에 복사하고 기존 RUN 로그는 보존한다. 부팅 시 `diagnostic v5-leaf`를 확인한 뒤, 사전 검사와 안내를 읽고 **U를 한 번** 누른다. 프로그램은 선택한 텍스트 출력 드라이버에 `Stop(parent, 1, &sharedConsole)`를 한 번 호출한다. 연결이 제거되고 보호 관계가 유지되면 같은 드라이버에 `Supported(parent, NULL)`를 호출하고 정확히 `EFI_SUCCESS`일 때만 `Start(parent, NULL)`를 한 번 호출한다. 모든 승인과 입력은 Stop 전에 끝내며, 분리 중에는 USB 로그만 기록한다.

실험 후 화면이 돌아오면 `Text-output member restored` 안내를 확인한다. 성공해도 Windows로 자동 진행하지 않고 대기하므로 물리 전원 버튼으로 종료한다. 60초 안에 안내가 돌아오지 않으면 물리 전원 버튼으로 종료한다. 화면은 기존 프레임버퍼 내용이 남아 계속 켜져 있을 수도 있으므로 화면 꺼짐 자체가 성공 조건은 아니다. 새 RUN의 REPORT 전체와 복구 안내 표시 여부를 가져온다.

`LEAF_READY`는 물리 GOP·중간 출력·공용 콘솔·선택 드라이버와 보호할 핸들·open 관계 수다. 보호 대상에는 GPU 루트, 물리 GOP, 중간 출력, 공용 콘솔/입력/오류 핸들 및 GPU의 직접 EDID 자식들이 포함된다. 선택한 TextOut의 BY_DRIVER/BY_CHILD 두 관계만 비교에서 제외하고, 다른 활성 open 관계는 정확히 일치해야 한다. 물리 GOP 객체·모드·프레임버퍼, 텍스트 출력 객체, 시스템 콘솔 핸들·프로토콜 및 입력 프로토콜 객체도 확인한다. 이는 관측한 프로토콜 상태의 검사이며 실제 키보드 입력 기능을 실험 중 시험하지는 않는다.

- `LEAF_AFTER_STOP stop=0 ... member=0 guard_unchanged=1`: 두 출력 관계가 제거됐고 보호한 상태가 유지됨.
- `LEAF_RESTORE supported=0 start=0`: 같은 드라이버가 다시 연결을 시작함.
- `LEAF_RESULT stop=0 isolated=0 connect=0 verify=0 restored=1` 및 `LEAF_FINAL status=0`: 프로토콜 수준의 분리·복구 성공. `connect`는 이 모드에서 범용 ConnectController가 아니라 선택 드라이버의 복구 호출 결과다.
- `console_output_status=0` 및 마지막 `END`: 복구 후 화면 안내와 로그 기록까지 완료됨. 실제 표시 여부도 사용자 확인이 필요하다.
- `LEAF_FINAL ... touched=0` 또는 `CANCELLED_OR_REFUSED`: 승인 취소나 사전 조건 불충족으로 Stop을 호출하지 않음.
- `STOP`: 복구·보호 상태·로그가 불확실하다. 물리 전원을 종료하고 재시도하지 않는다.

Stop 실패가 부분 변경 뒤 발생할 수 있어, 보호 상태가 유지되고 일부 연결이 사라졌으면 같은 드라이버의 복구를 한 번 시도한다. 보호한 입력·GOP·소유권 관계가 바뀌면 추가 드라이버 조작 없이 정지한다. 선택 드라이버가 RuntimeServicesCode인 경우는 실행하지 않는다. v4에서 관측한 EDID/포인터 소비 드라이버의 `code_type=5` 조건을 완화하지 않는다.

직접 DriverBinding Stop/Start는 [EDK2의 UEFI 규약 헤더](https://github.com/tianocore/edk2/blob/c1415771ac592ad8509351c46cd3cda4f38d7d52/MdePkg/Include/Protocol/DriverBinding.h)의 호출 조건을 따른다. 표준 [EDK2 ConSplitter 구현](https://github.com/tianocore/edk2/blob/master/MdeModulePkg/Universal/Console/ConSplitterDxe/ConSplitter.c)은 공용 콘솔의 출력 멤버를 분리·추가하는 참고 사례다. 설치된 펌웨어 드라이버가 같은 구현이라는 증거는 없으며, 이번 실험으로 그 동작을 확인한다. 이번 성공만으로 GPU GOP 전체 해제나 FLR 이후 복구가 검증되지는 않는다.

## 이전 수집 실험: diagnostic v4-tree (읽기 전용)

2차 로그는 타깃의 GOP 자식 아래에 추가 자식 관계가 있음을 보여줬다. v4-tree는 드라이버를 해제하지 않고 이 중첩 관계를 자동 수집한다. **U를 누를 필요가 없고, 화면 소실을 유도하지 않는다.** Start/Stop, Connect/DisconnectController, 독점 OpenProtocol, 언락·FLR 코드를 링크하지 않는다. USB 로그 파일을 쓰며 콘솔은 안내 표시용으로 사용한다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build-gop-experiment.ps1 -TreeOnly
experiments\gop\test.cmd
python tools/package_gop_experiment.py --output build/gop-experiment-usb-v4-tree --zip build/gop-experiment-usb-v4-tree.zip
```

ZIP 내용물을 USB 루트에 복사하고 기존 RUN 로그는 보존한다. 부팅하면 `diagnostic v4-tree`가 표시되고 자동 수집을 시작한다. 완료 안내 후 20초 뒤 펌웨어로 돌아오는 것은 정상이다. USB의 새 `RTX30-GOP-TEST/RUNxx/REPORT.TXT` 전체를 가져온다. 사전 검사 실패 또는 부분 수집이어도 로그가 남으면 전달한다.

`TREE_NODE`는 핸들과 깊이, 시스템 콘솔 핸들과의 일치 여부다. `TREE_PROTOCOL`은 전체 GUID와 PCI_IO/DEVICE_PATH/GOP/TEXT_OUT 역할을 기록한다. `TREE_OPEN`/`TREE_EDGE`는 소비 드라이버와 부모·자식 관계이며 `seen=1`은 이미 발견한 핸들을 가리킨다. `TREE_DRIVER`/`TREE_BINDING`/`TREE_IMAGE_INFO`는 드라이버의 식별 자료다. 이름 없는 이미지나 Device Path가 없는 가상 콘솔도 조회 결과를 기록하고 가능한 수집을 계속한다.

`TREE_END status=0 complete=1`과 `TREE_RESULT status=0 touched=0`은 제한 내 모든 발견 노드의 프로토콜 관계를 수집했다는 의미다. 동적 펌웨어 관계를 정지시킨 스냅샷이나 해제·복구 성공을 뜻하지 않는다. 최대 노드 64개, 깊이 8, 프로토콜별 open 128개, 전체 open 4096개를 넘으면 부분 수집으로 기록한다. 중복·순환 관계는 한 번만 방문한다. GPU 모델·보드 제조사·Subsystem 제한은 없으며 타깃 선택은 NVIDIA Display 장치 한 개를 요구한다.

빌드 manifest의 `Mode`가 `tree-read-only`인지 확인하면 v3 해제 실험과 구분할 수 있다. 이 문서의 아래 절차는 과거 v3 해제·복구 실험에 해당한다.

## 이전 실험: diagnostic v3

사용자 로그의 `10de:2203`, subsystem `7377:2000` 환경은 Device Path 프로토콜에 자식 컨트롤러 관계가 등록되어 있어 v2의 사전 검사에서 거부되었다. v3는 PCI I/O와 Device Path 양쪽에서 직접 자식을 수집하고 중복을 제거한다. 모든 자식 등록자가 같은 루트 소유 드라이버인지, 각 자식의 경로가 타깃 PCI 장치로 해석되는지를 확인한 뒤에만 U 승인을 받는다. 다른 소유자, 다른 GPU 경로, 중첩 자식 관계는 계속 거부한다.

실험 절차는 아래와 같다. v3 패키지의 내용물을 USB 루트에 복사하고 기존 RUN 로그는 보존한다. 화면과 새 REPORT 첫 줄에 **diagnostic v3**가 표시되는지 확인한다. `READY` 후 U를 한 번 누르고 화면 소실·복귀 여부를 기록한다. 복구 후에도 프로그램은 대기하므로 물리 전원 버튼으로 종료하고 새 RUN의 REPORT 전체를 가져온다. 중단되면 마지막 `capture`/`CANCELLED_OR_REFUSED` 기록을 함께 확인한다. 이번 실험도 언락·FLR 없이 GOP 해제·복구만 수행한다.

v3 모의 검사는 사용자 로그와 같은 Device Path 우선 열거, 양쪽 프로토콜의 동일 자식 중복 등록, 소유자 충돌·부재, 잘못된 자식 경로와 원래 오류 상태 보존을 포함한다. 모의 검사 통과는 이 보드에서 해제·복구가 성공한다는 의미가 아니다.

언락이나 FLR 없이, 현재 화면 출력 중인 GPU의 UEFI 드라이버를 해제하고 다시 연결한다. GPU 펌웨어 자산은 필요하지 않다. GPU 레지스터/PCI 설정 쓰기와 FLR 코드는 링크하지 않지만 **UEFI 드라이버의 Stop/Start 내부에서는 하드웨어 상태를 변경할 수 있다.**

NVIDIA Display 장치 한 개를 대상으로 하며 모델·보드 제조사·Subsystem 제한은 없다. 실행 시작 시 관측한 PCI ID·Subsystem·class/revision·BDF를 저장하고 이후 같은 장치인지 재검사한다. 여러 NVIDIA Display 장치, 다른 물리 GOP, 알 수 없는 소유자, 중첩 자식 관계, RGB/BGR 선형 프레임버퍼가 아닌 출력은 거부한다. 기존 Probe와 Unlock EFI의 보드 조건은 변경하지 않는다. 이 범용 GOP 실험은 다른 모델의 연산 언락 지원을 뜻하지 않는다.

## 준비

저장소 루트에서:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build-gop-experiment.ps1
experiments\gop\test.cmd
python tools/package_gop_experiment.py --output build/gop-experiment-usb
# ZIP도 만들 때는 --zip build/gop-experiment-usb.zip 추가
```

기존 출력 폴더를 덮어쓰지 않는다. 다시 패키징할 때는 `--output build/gop-experiment-usb-02`처럼 새로운 경로를 지정한다. 소스/헤더가 빌드 이후 바뀌면 다시 빌드해야 한다.

## 실제 실행

1. 작업을 저장하고 PC를 완전히 종료한다. 모니터를 **실험할 NVIDIA 카드**에 연결하고 UEFI에서 해당 카드를 기본 화면으로 선택한다. Secure Boot는 기존 실험과 같이 꺼져 있어야 한다.
2. 새 FAT32 USB에 패키지 폴더의 **내용물**을 복사한다. `EFI/BOOT/BOOTX64.EFI`와 `RTX30-GOP-TEST` 폴더가 USB 루트에 있어야 한다. 기존 Unlock USB와 혼동하지 않는다.
3. USB를 UEFI로 부팅한다. diagnostic v3는 화면 호출 전에 로그를 먼저 연다. 조건이 맞지 않으면 실패 단계·상태를 20초간 표시한 뒤 펌웨어로 돌아온다. 이 경우 아무 해제도 하지 않는다.
4. READY 안내를 읽고 **U를 한 번** 누른다. Esc 또는 30초 무입력은 취소다. 화면이 일시적으로 꺼질 수 있다. 해제 이후에는 키보드 입력을 요구하지 않는다.
5. 화면이 돌아오면 `GOP/text protocols restored` 안내와 화면 깨짐 여부를 확인한다. 프로그램은 **성공해도 자동으로 Windows로 넘어가지 않고 대기**한다. 물리 전원 버튼으로 종료한다.
6. 60초 안에 화면이 돌아오지 않으면 물리 전원 버튼으로 종료한다. 재시도나 Unlock을 이어서 실행하지 않는다. 펌웨어 내부에서 멈춘 경우 USB 로그의 마지막 줄이 단서가 된다.
7. USB의 `RTX30-GOP-TEST/RUNxx/REPORT.TXT`를 가져온다. 기존 RUN 폴더는 덮어쓰지 않는다. RUN00–RUN99가 모두 있으면 새 USB 패키지/빈 로그 폴더를 사용한다.

실행 시 U 승인을 받기 전에 전원 종료 방법과 USB 로그 위치를 화면에 표시한다. GPU를 한 번 해제한 뒤 복구나 로그 상태가 불확실하면 화면을 호출하지 않고 대기한다. 로그 오류가 발생해도 이미 시작한 드라이버 해제의 복구는 시도한다. 독점 소유권을 닫지 못하면 재연결을 시도하지 않고 대기한다.

## 로그 해석

이전 ZIP은 빈 `RTX30-GOP-TEST` 폴더가 누락되어 부팅 직후 로그를 열지 못할 수 있었다. diagnostic v2 패키지는 그 폴더에 `README.txt`를 넣고, ZIP을 다시 추출하는 검사로 폴더 보존을 확인한다. `usb-log` 실패와 로그 없음이 함께 나타나면 USB 루트에 이 폴더가 있는지 먼저 확인한다. 새 RUN 폴더 자체가 생기지 않고 진단 화면도 없다면 앱이 실행되지 않았을 가능성도 있어 USB 부팅 경로와 Secure Boot 상태를 확인해야 한다.

- `open`: 타깃/직접 자식의 프로토콜 open 관계와 소유자.
- `driver`: DriverBinding/LoadedImage 식별과 이미지 주소·크기·버전.
- `physical_gop`: 타깃의 물리 GOP와 화면 크기. 경로 없는 0 크기 가상 GOP는 물리 출력으로 세지 않는다.
- `capture child-path`: 자식과 등록자, 타깃 및 실제 해석된 PCI 핸들, 경로 조회 상태. `target`과 `resolved`가 일치해야 한다.
- `capture summary`: 소유자·자식·드라이버·물리 GOP·텍스트 출력 개수. 사전 조건 미충족 시 어떤 개수가 부족한지 확인한다.
- `config_before` / `config_after`: 읽기 전용 PCI 설정 캡처. 드라이버 해제·복구가 바꾼 상태를 비교할 입력이며, 동일성을 자동 보장하지 않는다.
- `DETACH_INTENT`: 해제 직전 USB에 Flush한 의도 기록. 이 뒤 결과가 없으면 해제/복구 중 정지했을 수 있다.
- `RESULT`: detach, isolated, claim, close, connect, verify 결과와 restored 판정. **모든 상태가 0**이고 `restored=1`, `console_output_status=0`, 마지막 `END`가 있어야 실험의 프로토콜 검사를 통과한 것이다. `EFI_NOT_STARTED`는 해당 단계를 실행하지 않았다는 의미다.
- `ENTRY`: 앱이 진입하여 USB 로그를 열었음. diagnostic v3 버전 식별을 포함한다.
- `CANCELLED_OR_REFUSED stage=... ... touched=0`: 승인 취소 또는 사전 조건 불충족. `console`, `secure-boot`, `target`, `gop-ownership`, `manual-approval` 등 단계로 원인을 구분한다.
- `STOP`: 복구 또는 로그 상태가 불확실하다. Windows로 계속 부팅하지 않는다.

`restored=1`은 원래 드라이버 집합, 물리 GOP 모드와 텍스트 출력 프로토콜을 다시 관측했다는 뜻이다. 새 자식 핸들은 허용하지만 동일한 모드와 드라이버 집합을 요구하므로 실제 화면이 복구됐어도 보수적으로 실패할 수 있다. 화면의 실제 출력은 사용자가 확인해야 한다. 이 실험은 셀렉터 보존·언락 성공·FLR 이후 복구를 검증하지 않는다.

다음 단계로 넘어가려면 REPORT 전체와 함께 **모니터 연결 위치, 메인보드/UEFI 버전, U 이후 화면 소실/복귀 여부, 복귀 안내 표시 여부**를 기록한다. 소스와 모의 검사 통과는 실기 성공 기록을 대신하지 않는다.
