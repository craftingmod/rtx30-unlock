# RTX 3080 Unlock

새 **v20-quiet-profile** 실험은 DEBUG OFF에서 EFI 내부 Probe 결과를 메모리에서 검증하고 PROFILE.BIN만 저장한다. 최초 생성과 PROM 일치 시에는 자동 진행하고, 같은 장치의 PROM 해시가 바뀌면 새 프로필로 사전 검사 후 U 승인을 받아 교체한다. DEBUG ON의 상세 수집·U 승인은 유지한다. [사용법](docs/EXTERNAL-PROFILE.md#v20-debug-off-최소-저장-실기-미검증). 실기 미검증.

현재 실험용 **v18-unified-layout**은 같은 실행 볼륨의 `RTX-UNLOCK` 아래에 `ASSETS/`, `PROFILE.BIN`, `DEBUG.ON`을 모은다. 상세 로그는 같은 `RUNnn`에 `GOP.TXT`와 `UNLOCK.TXT`로 저장한다. `DEBUG.ON`이 있으면 상세 로그와 **U 키 승인**, 없으면 RUN을 만들지 않고 자동 진행한다. v17 외부 프로필 적용 경로는 현재 환경에서 실기 성공했으며 v18의 새 경로는 실기 미검증이다. 기존 장치·PROM·메모리 배치 및 transaction 검사는 유지한다. [현재 사용법](docs/EXTERNAL-PROFILE.md)을 참고한다.

현재 `v16-ssd-selectors`는 USB/GPT SSD EFI 실행, 초기 SS0=88888888일 때 스킵, 다른 초기 셀렉터 값에서 적용 시도를 지원하는 별도 실험 빌드다. 자동 실행·기본 무로그·`DEBUG.ON` 상세 로그를 유지한다. 사용자 실기에서 예상 외 초기 SS0=24700051의 적용·영상 복구·Windows 부팅이 성공했고, 상세 로그를 켠 사전 검사~보고 완료는 약 9초였다. 이미 적용된 SS0의 스킵 경로와 무로그 실행은 아직 실기 미검증이다.

Intel GOP 없이 타깃 NVIDIA 카드로 출력하는 환경을 위한 [GOP 해제·복구 실험](experiments/gop/README.md)을 별도 EFI로 제공한다. v8의 GOP 해제·FLR·화면 복구는 사용자 실기 성공 보고를 받았다. v10은 실제 언락·FLR 1회·DMA 정리와 GOP 재초기화 전후 SS0/SS1 보존을 로그로 확인했지만 실제 화면은 돌아오지 않았다. v11은 NVIDIA Start 뒤 자식 열거 단계에 중단돼 표시 시험에 도달하지 못했고, v12는 자식 2개와 모든 API·셀렉터 검사 완료를 확인했지만 실제 모니터는 신호 없음이었다. v13은 다른 해상도 모드 왕복 후 녹색 9,216픽셀 읽기 일치와 셀렉터 보존을 로그로 확인했고, 사용자도 오른쪽 아래 녹색 사각형의 실제 표시를 확인했다. 이 보드에서 영상 복구 실기 1회는 성공했다. 현재 v14는 같은 복구 뒤 Windows Boot Manager 실행 직전까지 정상 완료했고, 제공된 Windows 결과에서 NVIDIA 장치 오류 코드 0·드라이버 정상 응답·활성 디스플레이를 확인했다. 같은 Windows 부팅에서 수정한 벤치마크 3종이 정상 완료됐다(FP32 29.619, FP16/BF16 각각 137.307 TFLOPS). 동일 실행 파일·드라이버의 패치 전 기준과 비교한 1회 측정에서 FP16/BF16 Tensor 처리량은 약 1.89배(+88.94%/+88.73%), FP32는 +0.85%였다. 반복 재현성·장시간 안정성은 미검증이다. [조사 및 설계](docs/NVIDIA-GOP-RESEARCH.md)도 참고한다.

MSI RTX 3080 10GB에서 검증한 Windows용 UEFI 연산 언락 실험입니다. USB로 UEFI 앱을 실행한 뒤, 재부팅 없이 Windows로 이어서 부팅합니다.

구현 소스와 관련 테스트는 `src/probe/`, `src/unlock/`, `src/gop/`에 모여 있습니다. 빌드·테스트 실행 스크립트와 결과물은 각각 `scripts/`, 기존 `unlock/`·`experiments/gop/`, `build/`에 있습니다.

**실기 검증 범위는 아래의 MSI RTX 3080 환경입니다.** 다른 RTX 30 GA10x GPU는 프로필 기반 실험 모드로 시도할 수 있습니다. 실험 모드에서도 장치·화면 경로·펌웨어 검사에 맞지 않으면 실행을 거부합니다.

## 범용 Probe 및 실험적 RTX 30 GA10x 포트

제조사와 Subsystem ID에 관계없이 모든 NVIDIA Display 장치의 PCI 정보, 256바이트 설정 공간, BAR, GOP와 Device Path를 수집합니다. RTX 30 후보 PCI 범위 `10de:22xx/24xx/25xx`에서는 먼저 `PMC_BOOT_0`만 읽고, 실제 칩이 **GA102·GA103·GA104·GA106·GA107**인 경우 상세 MMIO/PROM을 읽습니다. PCI 범위 자체를 칩·호환성의 증거로 취급하지 않습니다. GPU가 여러 개이거나 USB 기초 로그 저장에 실패하면 상세 읽기를 생략합니다. GPU 쓰기·Bus Master 활성화·FLR은 Probe에 링크하지 않습니다.

```powershell
make probe
make probe-usb
# build/probe-usb 내용물을 FAT32 USB에 복사하고 USB로 UEFI 부팅
# USB의 RTX30-PROBE/RUNxx에서 결과를 가져온 후:
make profile REPORT="captures/RUN00/REPORT.TXT" PROM="captures/RUN00/PROM.BIN"
make test
```

`make probe`는 현재 Windows의 GPU를 검사하지 않고 `build/probe/BOOTX64.EFI`를 빌드합니다. 새 USB 패키지는 `build/probe-usb`에 만들며 기존 출력 폴더를 덮어쓰지 않습니다(`OUTPUT=...`으로 다른 폴더 지정). `INVENTORY.TXT`는 상세 읽기 전 기록, `REPORT.TXT`는 완성된 진단입니다. `PROM.BIN`과 펌웨어가 제공하는 `ROMIMAGE.BIN`은 따로 보존합니다. 프로필 출력은 기본 `profiles/board.json`이며 `PROFILE=...`으로 바꿀 수 있습니다. 기존 프로필은 덮어쓰지 않습니다.

프로필은 입력 해시와 PROM 캡처 해시를 연결합니다. 유효한 칩 읽기가 있으면 실제 `PMC_BOOT_0`에서 GA10x 아키텍처를 식별합니다. 관측된 칩 식별과 독립적인 호환성 검증을 분리하며, 메모리 용량·화면 경로·펌웨어 호환성·Unlock 승인을 자동 인증하지 않습니다. 구형 v0.1 REPORT는 신규 생성기 입력으로 사용하지 않고 Probe를 다시 실행합니다.

기본 `make unlock`은 독립 검증과 승인된 기존 보드 조건을 요구합니다. **위험을 감수하는 다른 RTX 30 GA10x GPU의 시도**는 다음처럼 명시적으로 선택합니다.

```powershell
make unlock EXPERIMENTAL=1 PROFILE="profiles/board.json"
```

실험 모드는 승인 플래그를 조작하지 않고, 미승인 GA102·GA103·GA104·GA106·GA107 프로필로 `build/unlock/BOOTX64.EFI`를 만듭니다. 제조사·Subsystem·PROM·칩·초기 레지스터·메모리 용량은 Probe에서 가져오며 BDF와 BAR 주소는 실행 시 조회합니다. VGA/MMU/WPR 배치는 캡처와 재비교하고 코어의 동적 레이아웃 계산을 사용합니다. GPU-Z의 제품명이나 BIOS 버전을 코드에 고정하지 않습니다.

실험 모드에서도 PROM/펌웨어 해시, 완료된 GFW 상태, WPR2 미사용, PCI capability/FLR, DMA 매핑·정리 검사와 UEFI에서 **U를 누르는 단일 시도 승인**은 유지합니다. 실제 PCI 소유권을 조회해 타깃과 다른 화면 출력 장치를 요구하므로 타깃 카드에만 모니터가 연결된 환경은 거부합니다. 기존 자산 5개와 해시는 유지하며, `ga102`라는 파일명으로 다른 GA10x를 차단하지 않습니다. 공통 Booter/GSP 코드의 근거와 V67·레지스터 절차의 미검증 범위는 [GA10x 실험 호환성](docs/GA10X-EXPERIMENTAL.md)에 기록했습니다. GA100·Turing·Ada 등은 제외합니다. 실패 후 정리를 입증하지 못하면 기존 안전 정지 절차를 유지합니다.

새 Unlock EFI의 로그/자산 폴더명은 `RTX-UNLOCK`입니다. [USB 안내](docs/USB-BOOT.md)의 자산 5개를 그 폴더의 `ASSETS`에 배치하고 실험용 EFI를 `EFI/BOOT/BOOTX64.EFI`로 복사합니다. 외부 프로필 빌드는 `PROFILE.BIN`도 필요합니다. Probe USB의 `RTX30-PROBE` 폴더와 구분하세요. 실제 하드웨어·Windows/CUDA 검증은 별도로 필요합니다.

Make가 없으면 `bun tools/build-probe.ts`, `tools/probe-to-profile.py`, `bun run test`, `scripts/build-unlock.ps1 -Profile ... -Experimental`을 직접 실행할 수 있습니다. 컴파일러는 PATH 또는 `LLVM_BIN`을 사용합니다.

## 기존 검증 보드에서 USB로 사용하기

1. [USB 준비 및 부팅 안내](docs/USB-BOOT.md)에 따라 FAT32 USB와 필요한 파일을 준비합니다.
2. UEFI 부팅 메뉴에서 USB를 선택합니다.
3. 사전 검사가 끝나 `READY`가 표시되면 **U를 한 번** 누릅니다.
4. 성공 후 펌웨어 메뉴로 돌아오면 **Windows Boot Manager**를 선택합니다. 이 사이에 재부팅하지 않습니다.

USB 템플릿에는 EFI, 소스, 사용법과 라이선스가 들어 있습니다. 실행 자산 5개는 로컬에서 추가해야 합니다. `unlock/assets.json`에 파일명·크기·SHA256이 정리되어 있습니다. 템플릿만 복사한 상태에서는 실행할 수 없습니다.

## 확인한 환경

- MSI RTX 3080 10GB: PCI `10de:2206`, subsystem `1462:3890`, VBIOS `94.02.59.00.aa`.
- ASUS TUF GAMING B660M-PLUS D4, Intel i5-13600K / UHD 770 화면 출력.
- Windows 11 Pro build 26200, NVIDIA 610.60 드라이버.
- v0.10 실제 실행: UEFI transaction과 정리 완료 후 Windows CUDA 측정.

현재 코드는 BDF `0000:01:00.0`, BAR, 보드 PROM 및 Intel 화면 경로도 검사합니다. 같은 제품명이어도 다른 PC에서의 동작은 확인되지 않았습니다. 새 FAT32 USB에서의 실제 부팅은 별도 검증 대상입니다.

## 측정 결과

| 측정 | 이전 TFLOPS | 적용 후 TFLOPS | 변화 |
| --- | ---: | ---: | ---: |
| FP32 pedantic | 22.057145 | 21.922047 | −0.61% |
| FP16 Tensor / FP32 누산 | 60.874030 | 109.217226 | +79.42% |
| BF16 Tensor / FP32 누산 | 60.910719 | 113.090926 | +85.67% |

동일 실행 파일과 CUDA/cuBLAS 환경의 4096×4096 GEMM 측정입니다. 기준값은 한 프로세스, 적용 후 값은 세 프로세스 중앙값들의 중앙값입니다. 게임 성능, 모든 AI 작업의 향상률, 장시간 안정성이나 콜드부팅 후 지속성을 의미하지 않습니다. [측정 자료](results/benchmark.json)와 [실제 UEFI 실행 로그](evidence/unlock-boot-09/REPORT.TXT)를 함께 제공합니다. 측정 자료에서는 개인 경로와 장치 UUID를 제외했습니다.

## 빌드와 검사

### 코드 포맷

clang-format 19 이상과 Bun을 사용합니다. 기존 코드에 맞춰 공백 2칸, 최대 100열을 기준으로 하며 include 순서는 유지합니다.

```powershell
bun run format        # 포맷 적용
bun run format:check  # 변경 없이 검사 (불일치 시 실패)
```

clang-format은 `PATH`에서 찾으며 `LLVM_BIN`으로 설치 폴더를 지정할 수 있습니다. Git 추적 C/C++/CUDA 파일을 대상으로 하고, `.clang-format-ignore`에 지정한 외부 코드(`deps/`, `src/unlock/vendor/`), 고정 비교 스냅샷(`src/unlock/tests/reference/`), 생성물은 제외합니다. 새 소스 파일은 Git에 추가한 뒤 실행하세요. 포맷 적용 후 릴리스 패키지를 만들 때는 [소스 manifest 갱신 절차](TESTING.md#소스와-패키지-해시)를 따릅니다.

### 빌드 실행

Bun Shell에서 `src/`의 소스를 직접 컴파일·링크해 최종 **v20-quiet-profile** 구성의 EFI를 만듭니다. Unlock, GOP 복구, Windows 연결 부팅, SSD 실행, 외부 프로필 및 프로필 자동 생성 옵션을 포함합니다. PowerShell 빌드 스크립트는 호출하지 않습니다.

```powershell
bun run build
# build/efi/BOOTX64.EFI
```

LLVM의 `clang`·`lld-link`가 필요하며, LLVM 도구는 `PATH` 또는 `LLVM_BIN`에서 찾습니다. 빌드 후 Bun PE 검증을 실행하며, 같은 폴더에 링크 맵과 `pe-verification.json`을 저장합니다. ZIP·USB 패키지는 생성하지 않습니다. EFI 실행에는 기존 `RTX-UNLOCK/ASSETS`의 자산 5개가 필요하며, v20의 실제 하드웨어 부팅은 미검증입니다.

펌웨어 자산은 `bun run firmware:assets`로 내려받아 `build/firmware-assets`에 생성할 수 있습니다. EFI 패키징은 별도 단계입니다. 자세한 내용은 [`tools/firmware/README.md`](tools/firmware/README.md)를 참고하세요.

전체 모의 검사는 `bun run test`로 실행합니다. 추가 fixture를 사용하는 검사 방법은 [TESTING.md](TESTING.md)를 참고하세요.

## 출처와 라이선스

이 프로젝트의 포트와 재사용 코어는 **GPL-2.0-only**입니다. 핵심 코드는 [Zagitta/gpu-unlock](https://github.com/Zagitta/gpu-unlock/tree/0ebdb1c6308b6a2072be5bf04513472942541956)에서 가져왔습니다. 포함된 EDK2 헤더와 관련 고지는 각각의 라이선스를 따릅니다.

[LICENSE](LICENSE)와 [외부 코드 고지](THIRD_PARTY_NOTICES.md)를 확인하세요.
