# RTX 30 시리즈 범용 UEFI Probe 및 GPU Unlock 포팅 계획

> **상태:** Probe/Profile 및 명시적 RTX 30 GA10x 실험 빌드 경로 구현; 신규 경로는 실기 검증 전.  
> **기준 프로젝트:** [`rough2515-hub/rtx3080-unlock-public`](https://github.com/rough2515-hub/rtx3080-unlock-public)  
> **참고 프로젝트:** [`Zagitta/gpu-unlock`](https://github.com/Zagitta/gpu-unlock), [`applepines/gpu-unlock` RTX 3090 Ti 실험 브랜치](https://github.com/applepines/gpu-unlock/tree/experimental-rtx3090ti)

## 1. 목표

**구현 정책 변경:** 사용자 요청에 따라 `make unlock EXPERIMENTAL=1`은 보드 승인 이력이 없는 GA102·GA103·GA104·GA106·GA107도 캡처된 프로필로 시도할 수 있게 한다. NVIDIA 610.43.03의 공통 Booter/GSP HAL 선택에 근거하며, 파일명 `ga102`로 다른 GA10x를 차단하지 않는다. 기존 자산 5개와 해시를 재사용하되 V67의 모든 칩 실기 성공을 주장하지 않는다. 승인 플래그를 자동 변경하지 않으며, 런타임 PCI/Subsystem/PROM/칩/메모리/레이아웃 재검증과 화면 소유권·펌웨어 무결성·DMA·FLR·정리 검사를 유지한다. 기본 모드의 승인 요구는 유지한다. 근거와 범위는 [GA10x 실험 호환성](GA10X-EXPERIMENTAL.md)에 기록한다.

PMC_BOOT_0 칩 식별 필드는 GA102 `0x172`, GA103 `0x173`, GA104 `0x174`, GA106 `0x176`, GA107 `0x177`이다. 칩 마스크와 식별값은 Linux nouveau/nova를 참고한다. PCI 후보 범위와 별개로 상세 레지스터 접근 전에 실제 칩을 확인한다.

기존 MSI RTX 3080 10GB 전용 UEFI 잠금 해제 구현을 **범용 진단 도구 + 기본 승인 Unlock + 명시적 GA10x 실험 Unlock**으로 분리한다.

- `make probe`로 **읽기 전용 범용 UEFI 진단 앱**을 빌드한다.
- 사용자는 해당 EFI를 FAT32 USB에서 실행해 **자신의 GPU와 UEFI 환경**을 수집한다.
- `make profile`은 수집 결과(`REPORT.TXT`, `PROM.BIN`)로 **보드 프로필(`board.json`)**을 생성한다.
- `make test`와 별도 호환성 검사는 손상·불완전한 프로필을 거부하며, 기본 모드에서 미승인 프로필을 차단한다.
- 기본 `make unlock`은 승인된 기존 보드만 허용하고, `EXPERIMENTAL=1`은 수집·검증된 GA10x 캡처로 실험용 EFI를 빌드한다.

**주의:** `make probe` 자체가 실행 중인 Windows에서 GPU를 검사하지는 않는다. 명령은 `.efi` 진단 프로그램을 **빌드**하며, 진단은 **UEFI 부팅 시점**에 실행된다. 또한 프로필 자동 생성은 성공적인 Unlock을 의미하지 않는다.

## 2. 전체 워크플로

```text
Windows 개발 환경
    |
    +-- make probe
    |       -> build/probe/BOOTX64.EFI
    |
    +-- FAT32 USB 구성
            -> EFI/BOOT/BOOTX64.EFI
            -> RTX30-PROBE/
    |
    v
UEFI 부팅 / Read-only Probe 실행
    |
    +-- NVIDIA PCI 장치 검색
    +-- BDF, PCI Configuration Space, BAR, GOP 정보 기록
    +-- 지원·검증된 아키텍처에서만 MMIO 읽기
    +-- 접근 가능한 경우 보드 PROM / UEFI ROM 이미지 저장
    |
    v
RTX30-PROBE/RUNxx/
    +-- REPORT.TXT
    +-- PROM.BIN         (추출에 성공한 경우)
    +-- ROMIMAGE.BIN     (사용 가능한 경우)
    |
    v
make profile REPORT=... PROM=...
    |
    v
profiles/board.json
    |
    +-- PCI / VBIOS / GSP / 메모리 레이아웃 검증
    +-- 해당 UEFI 화면 출력 경로 검증
    +-- 지원 GPU 및 펌웨어 호환성 검증
    |
    v
make test
    |
    v
명시적 검증 및 승인 완료 시에만 make unlock
    |
    v
기기별 Unlock EFI 빌드 및 독립적인 실기 검증
```

## 3. 단계별 구현

### Phase 1 — `make probe`: 범용 읽기 전용 UEFI 진단 앱

기존 파일을 재사용한다.

- [`src/probe/probe_app.c`](https://github.com/rough2515-hub/rtx3080-unlock-public/blob/main/src/probe/probe_app.c): EFI 진입, PCI 탐색, GOP 열거, USB 로그 저장.
- [`src/probe/probe_core.c`](https://github.com/rough2515-hub/rtx3080-unlock-public/blob/main/src/probe/probe_core.c): BAR 및 레지스터 수집, PROM 읽기.
- `src/probe/efi_runtime.c`: 별도 C 런타임 없이 EFI 빌드.

**수정사항**

1. `ProbeMatches()`의 `10de:2206`, `1462:3890` 고정 조건을 제거한다.
2. PCI Vendor ID `0x10de`이고 Display Class인 모든 NVIDIA 장치를 **열거·기록**한다.
3. GPU별 PCI Device ID, Subsystem ID, BDF, PCI Config 256바이트, BAR 리소스, GOP와 Device Path를 기록한다.
4. MMIO 읽기와 PROM 추출은 칩별 안전성이 확인된 allowlist를 만족할 때만 수행한다.
5. 대상 장치가 여러 개면 각 장치의 식별 정보를 남기고, 선택 없이 위험한 상세 Probe를 실행하지 않는다.
6. MMIO 읽기가 실패하거나 `0xffffffff`, 접근 불가 sentinel 등이 나오면 값을 유효한 측정으로 취급하지 않는다.
7. Probe에서는 **MMIO 쓰기, Bus Master 활성화, FLR, 펌웨어 로드·실행, BootOrder/NVRAM 변경을 금지**한다.
8. 로그 출력·저장에 실패해도 GPU를 건드리지 않는 상태로 종료한다.

현재 구현의 후보 판별은 `src/probe/gpu_family.h`를 공유한다. 후보 PCI 판별 후 최초 MMIO에서 실제 칩을 검사한다.

```c
static BOOLEAN IsNvidiaDisplay(const UINT32 *config) {
    return config &&
           (config[0] & 0xffffU) == 0x10deU &&
           (config[2] >> 24) == 0x03U;
}

static BOOLEAN SupportsGa10xProbe(UINT32 id) {
    return GpuGa10xPciCandidate(id);
}
```

> GA102 이름의 Booter/GSP 구현은 여러 GA10x가 공유한다. 공통 자산 사용 가능성과 V67·FWSEC·FLR의 실기 검증을 구분한다. GA10x 실험 모드는 허용하되 미승인 칩별 상태를 검증 완료로 표시하지 않는다. GA100·Turing·Ada 및 알 수 없는 칩은 제외한다.

### Phase 2 — USB 패키지 및 로그 저장

예상 USB 구성:

```text
USB:/
├── EFI/
│   └── BOOT/
│       └── BOOTX64.EFI
└── RTX30-PROBE/
    └── RUN00/
        ├── REPORT.TXT
        ├── PROM.BIN        # 성공한 경우에만
        └── ROMIMAGE.BIN    # 성공한 경우에만
```

- USB는 FAT32 형식으로 준비한다.
- 기존 ESP 또는 Windows Boot Manager를 변경하지 않는다.
- EFI 앱의 로그 쓰기는 **실제로 해당 USB에서 로드된 경우에만** 허용한다.
- Probe 이후 **재부팅 없이** 펌웨어 부팅 메뉴 또는 Windows Boot Manager로 진행할 수 있지만, 구체적 흐름은 메인보드 구현별로 확인한다.
- ROM 이미지와 PROM 덤프는 같지 않을 수 있으므로 **별도 파일로 보존**한다.

### Phase 3 — `make profile`: 보드 프로필 생성

예정 명령:

```sh
make profile REPORT=probe/REPORT.TXT PROM=probe/PROM.BIN
```

생성 예시(`<>` 값은 플레이스홀더이며 실제 값이 아니다):

```json
{
  "schema": 1,
  "target": {
    "vendorId": "10de",
    "deviceId": "2203",
    "subsystemVendorId": "<probe result>",
    "subsystemDeviceId": "<probe result>",
    "pciAddress": "<probe result>",
    "architecture": "GA102"
  },
  "hardware": {
    "chipId": "<probe result>",
    "bar0Size": "<probe result>",
    "vramSize": "<independently verified result>",
    "promSha256": "<calculated SHA256>"
  },
  "preboot": {
    "displayRoute": "<GOP discovery>",
    "initialPlm": "<probe result>",
    "initialSs0": "<probe result>",
    "initialSs1": "<probe result>",
    "wpr2State": "<probe result>"
  },
  "validation": {
    "probeComplete": true,
    "firmwareCompatible": false,
    "unlockApproved": false
  }
}
```

**프로필 생성기 요구사항**

- 로그의 파일 형식·필수 필드·값 범위·중복 여부를 엄격하게 검사한다.
- `PROM.BIN`의 크기, ROM 시그니처 및 SHA-256을 기록한다.
- PCI ID로 아키텍처를 추정하는 경우에는 추정/검증 상태를 분리한다.
- BDF 및 BAR 물리 주소는 부팅마다 달라질 수 있으므로 **고정 불변값으로 가정하지 않는다**.
- 수집 정보만으로 `firmwareCompatible` 또는 `unlockApproved`를 자동으로 `true`로 설정하지 않는다.
- 프로필 내 입력 파일 해시를 보관해 이후 재검증에 사용한다.

### Phase 4 — `make test`: 호환성 및 회귀 검사

1. **Host/mock 테스트:** 기존 `src/probe/test_core.c`, `src/unlock/tests/`, `bun tools/test.ts` 활용.
2. **Probe 테스트:** 서로 다른 PCI ID, 복수 GPU, BAR 크기 오류, 접근 불가 MMIO, USB 로그 실패.
3. **Profile 테스트:** 누락·손상된 REPORT/PROM, 중복 키, 다른 VBIOS 해시, 미지원 아키텍처.
4. **Unlock 사전검사:** 대상 GPU의 PCI/SubVendor/SubDevice, 칩 식별, 메모리 레이아웃, PROM 해시, 펌웨어 자산 해시, 초기 레지스터, 디스플레이 소유권, DMA 매핑 가능 여부 검증.
5. **실패 동작:** 미검증 환경에서는 어떠한 GPU 쓰기도 실행되지 않아야 한다.

> Host/mock 테스트 통과는 **실제 UEFI 부팅·GPU Unlock 성공의 증거가 아니다**. 실기 검증 상태를 별도로 기록한다.

### Phase 5 — `make unlock`: 검증된 프로필로 빌드

기존 `unlock/build.ps1`과 `src/unlock/port.h`, `src/unlock/app.c`, `src/unlock/checks.c`, `src/unlock/port.c`를 프로필 기반 구조로 리팩터링한다.

| 기존 고정 조건 | 변경 설계 |
| --- | --- |
| GPU Device ID `10de:2206` | 프로필 기반, 런타임 재검증 |
| Subsystem ID `1462:3890` | 보드별 프로필의 정확한 값 |
| BDF `0000:01:00.0` | PCI 동적 탐색 후 대상 식별 |
| BAR0 물리 주소/크기 | 실제 BAR 리소스를 읽고 안전성 확인 |
| 특정 칩 레지스터 값 | 칩별 검증된 값과 비교 |
| 10GB VRAM 초기 상태 | 보드별 VRAM 및 레이아웃 검증 |
| MSI 3080의 PROM SHA | 해당 보드 PROM SHA |
| Intel GOP 고정 | 출력 경로별 소유권 및 FLR 안전성 검사 |
| SS0/SS1 초기값 고정 | 검증된 칩별 초기 상태만 허용 |

**절대 원칙:** 빌드에 쓰인 프로필이 승인되어 있어도 실제 실행 순간의 PCI/ROM/레지스터 상태를 다시 확인한다. 불일치가 발생하면 실행을 거부한다.

### Phase 6 — GPU Unlock의 실기 검증

- 가능한 경우 타깃 GPU와 별개인 화면 출력 장치를 사용한다.
- 상태를 변경하기 전에 읽기 전용 Probe 로그와 VBIOS 백업을 확보한다.
- EFI의 사전 검사 → 사용자 명시적 승인 → 단일 Unlock 시도 → FLR/정리 확인 순서를 유지한다.
- DMA 해제, Bus Master 비활성화, WPR2/GSP/SEC2 종료 상태가 입증되지 않으면 Windows로 진행하지 않는다.
- 실제 Windows NVIDIA 드라이버 인식, 장치 오류, CUDA 결과 정합성 및 성능 측정을 확인한다.
- 이후 반복 콜드부팅과 장시간 안정성은 별도의 검증 항목으로 둔다.

## 4. 개발자 CLI 설계

다음 Makefile은 **제안 인터페이스**다. 여기서 호출하는 `scripts/build-probe.ps1`, `scripts/build-unlock.ps1`, `tools/probe-to-profile.py`는 **추가 구현이 필요한 파일**이다.

```makefile
.PHONY: probe profile test unlock

probe:
	powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build-probe.ps1

profile:
	python tools/probe-to-profile.py --report "$(REPORT)" --prom "$(PROM)" --output profiles/board.json

test:
	bun tools/test.ts

unlock:
	powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build-unlock.ps1 -Profile profiles/board.json
```

예정 사용 예시:

```sh
make probe
# 생성된 EFI를 FAT32 USB에 복사해 UEFI에서 실행

make profile REPORT=probe/REPORT.TXT PROM=probe/PROM.BIN
make test

# 독립 검증과 명시적 승인 후에만 허용
make unlock
```

Windows 개발 환경에 GNU Make가 없다면 PowerShell 래퍼 명령으로 동일한 작업을 제공해도 된다. 기존 프로젝트의 LLVM/clang 및 `lld-link` 빌드 방식을 재사용한다.

## 5. 소스 디렉터리 구조

```text
rtx30-uefi-unlock/
├── src/
│   ├── probe/          # Probe EFI, profile capture and tests
│   ├── unlock/         # Unlock EFI core, vendor code and tests
│   └── gop/            # GOP, FLR, display recovery and tests
├── scripts/            # Build entry points and toolchain setup
├── tools/              # Profile processing and package tools
├── unlock/             # Build/test launchers, assets and generated output
├── experiments/gop/    # GOP instructions and test launchers
├── docs/
├── profiles/
├── build/              # Generated EFI and package output
└── deps/edk2/          # UEFI headers
```

## 6. 펌웨어 자산

기존 승인 경로와 GA10x 실험 경로는 다음 5개 파일을 사용한다(원본 `Zagitta/gpu-unlock`의 `firmware` 문서 참조). NVIDIA의 공통 HAL 선택은 Booter/GSP 재사용의 근거이며 패치된 V67의 모든 칩 호환성을 보증하지 않는다.

```text
assets/
├── booter_load_ga102.bin
├── v67-ga102-610.43.03.bin
├── gsp_ga10x.bin
├── gsp_boot_ga102.bin
└── gsp_boot_desc_ga102.bin
```

- 실행 자산은 `unlock/assets.json`에 명시된 크기와 SHA-256으로 검증한다.
- RTX 3090 Ti에 대해서는 기존 GA102 절차를 재사용한 **Linux 실기 성공 보고**가 있으나, 이것이 UEFI/Windows 성공을 보장하지는 않는다.
- GPU별로 무조건 다른 펌웨어를 생성해야 한다고 가정하지 않는다. 반대로 GA102라고 해서 모든 보드가 같은 초기 상태나 FWSEC 조건이라고도 가정하지 않는다.
- 펌웨어 및 추출물 배포 시 원본 라이선스·고지와 재현 가능한 추출 과정을 유지한다.

## 7. 위험과 보호 조건

1. **Probe와 Unlock 명확히 분리:** Probe에는 GPU 상태 변경 함수를 링크하지 않는 것이 바람직하다.
2. **지원 범위 분리:** NVIDIA 전체 카드의 PCI 정보 식별 ≠ RTX 30 전체 Unlock 지원.
3. **화면 출력 안전성:** 타깃 GPU의 GOP가 현재 콘솔 출력에 사용 중이면 FLR 과정에서 화면과 펌웨어 서비스가 손상될 수 있다. 별도 화면 경로 검증 전까지 Unlock 거부.
4. **사전 상태 엄격 검증:** WPR2가 이미 활성화되어 있거나 드라이버 초기화가 진행된 상태에서는 재실행을 차단.
5. **중단 시 보존:** 펌웨어가 DMA를 계속 수행할 가능성이 있다면 메모리를 해제하지 않고 안전 정지를 우선한다.
6. **USB 사용:** 초기에는 기존 ESP/Windows 부트로더를 수정하지 않는다.
7. **부팅 보안:** Secure Boot가 켜진 시스템에서는 미서명 EFI가 차단될 수 있으며, BitLocker 구성 변경 시 복구 문제가 생길 수 있다.
8. **데이터 보안:** REPORT·VBIOS에 GPU 식별 정보나 펌웨어 내용이 포함될 수 있으므로 공개 전 검토한다.

## 8. 완료 기준

### Probe MVP

- [ ] `make probe`로 `BOOTX64.EFI`가 빌드된다.
- [ ] 여러 NVIDIA GPU를 식별하고 PCI/SubSystem/BDF 정보를 기록한다.
- [ ] 지원하지 않는 칩은 MMIO·PROM 상세 진단을 건너뛴다.
- [ ] 지원되는 보드에서 `REPORT.TXT`와 가능한 ROM/PROM 덤프를 저장한다.
- [ ] GPU 쓰기·FLR·Bus Master 활성화가 발생하지 않는다.
- [ ] USB 부팅 및 정상 종료를 실제 PC에서 확인한다.

### Profile MVP

- [ ] `make profile`로 유효성 검사된 `board.json`을 생성한다.
- [ ] PROM 해시와 원본 진단 로그를 연결한다.
- [ ] 불완전/미지원 프로필은 `unlockApproved=false`로 남는다.

### GA102 Unlock MVP

- [ ] 승인된 특정 3090/3090 Ti/3080 보드의 초기 상태를 재검증한다.
- [ ] 자산, DMA, FWSEC/Booter, FLR, 정리 단계의 독립 검증을 마친다.
- [ ] 실패 시 Windows로 계속 부팅하지 않는 안전 정지 동작을 확인한다.
- [ ] Windows 장치 인식과 CUDA FP16/BF16 정합성·성능 측정을 기록한다.
- [ ] 반복 부팅 및 장시간 안정성은 별도 지표로 보고한다.

## 9. 권장 개발 우선순위

**우선 구현:** `make probe` → USB Probe → `REPORT.TXT`/`PROM.BIN` → `make profile`.

GA102·GA103·GA104·GA106·GA107의 실험 시도를 허용하고 각 칩의 호환성·실기 결과를 별도로 축적한다. **범용 Probe / 기본 승인 Unlock / 미검증 GA10x 실험 Unlock**을 구분한다. 실험 EFI가 빌드된다는 사실을 RTX 30 전체 Unlock 성공으로 표시하지 않는다.

---

### 참고 문서

- [RTX 3080 UEFI 포트 저장소](https://github.com/rough2515-hub/rtx3080-unlock-public)
- [RTX 3080 UEFI 포트 USB 부팅 문서](https://github.com/rough2515-hub/rtx3080-unlock-public/blob/main/docs/USB-BOOT.md)
- [Zagitta/gpu-unlock 펌웨어 자산](https://github.com/Zagitta/gpu-unlock/blob/main/firmware/README.md)
- [RTX 3090 Ti Linux 실기 검증](https://github.com/applepines/gpu-unlock/blob/experimental-rtx3090ti/docs/rtx3090ti-validation.md)
