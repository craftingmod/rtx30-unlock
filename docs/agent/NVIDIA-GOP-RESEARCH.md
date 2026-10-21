# Intel GOP 없이 NVIDIA 출력 카드에 Unlock 적용하기

조사일: 2026-10-20. 로컬 기준: `6e9cb18201562e03b923a2928b6c928afd3783c8`.
이 문서는 설계 제안이며 실행 코드나 GPU 상태를 변경하지 않는다.

## 결론

Intel GOP는 연산 언락 자체의 필수 요소라는 증거가 없다. 현재 포트에서 Intel/독립 GPU 출력은 타깃의 펌웨어 드라이버 해제와 FLR 동안 콘솔을 유지하는 수단이다.
타깃 NVIDIA 카드가 이미 GOP 출력을 담당하는 환경은 **콘솔/GOP 해제 → 독점 소유 → 기존 언락 및 FLR → GOP 복구 → Windows 인계**를 별도 실험 경로로 구현하는 것이 적절하다.
이는 NVIDIA GOP 바이너리나 VBIOS 플래시를 수정하는 제안이 아니다.

화면의 일시 소실은 허용해야 한다. 단일 GPU에서 패치 중에도 NVIDIA GOP가 계속 정상 동작하도록 보장하는 방법은 이번 조사로 확인하지 못했다.
GOP 재연결 후 언락 상태 유지와 Windows 부팅 성공이 가장 큰 미검증 항목이다.

## 확인한 근거

### 저장소

- `src/unlock/app.c:58`의 `DisplayCheck()`는 기본 빌드에서 Intel에 해당하는 고정 경로만 허용한다. 프로필 빌드는 `GuIndependentDisplay()`로 타깃과 다른 PCI 장치인지 확인하므로 이미 Intel 제조사에 한정되지 않는다. 다만 타깃 GOP가 열거되면 전체 검사를 거부한다.
- `src/unlock/checks.c:10`은 PCI handle/프로토콜/BDF를 비교한다. `:172`의 `GuDisplayPath()`는 고정 PCI 경로를 해석한다. Intel 문자열만 바꾸는 것으로 단일 GPU 지원이 되지 않는다.
- `src/unlock/ownership.c:55`의 `GuFindReleaseOwner()`는 단일 드라이버만 허용하고 모든 활성 자식 관계를 거부한다. GOP가 자식 핸들을 생성한 환경을 처리할 수 없다.
- `src/unlock/app.c:341`부터 화면·소유권 검사를 수행하고, U 승인 후 `DisconnectController()`와 재검사를 수행한다. 종료 시 독점 프로토콜을 닫지만 `ConnectController()`로 화면을 복구하는 경로는 없다.
- `src/unlock/app_io.c:30`은 USB 로그마다 먼저 `GuConsole()`을 호출한다. 콘솔 오류는 `LogFault`, armed 상태에서는 `Gu.fault`까지 설정한다. 화면 해제 후에는 정상적인 USB 기록까지 실패 취급될 수 있다. 오류가 없어도 오래된 콘솔의 GPU 접근을 허용하면 안 된다.
- `src/unlock/compat.c:85`의 `aperture_remove_conflicting_pci_devices()`는 실제 제거 대신 화면 검사를 재실행한다. 이름이 같아도 Linux의 프레임버퍼 제거 기능을 제공하지 않는다.
- `src/unlock/vendor/unlock_610.c`의 `unlock_transaction_reset()`은 BME 해제, aperture 처리, FLR, PCI 복원, GFW/WPR2/셀렉터 확인을 수행한다. `src/unlock/port.c:210`도 FLR 전 `display_ok`를 요구한다.
- 공개 성공 로그 `evidence/unlock-boot-09/REPORT.TXT`는 독립 Intel 출력과 `children=0`인 소유자 해제 사례다. NVIDIA 출력 카드의 GOP 복구 성공을 보여주지 않는다.

### 외부 1차 자료

1. [원본 gpu-unlock 고정 커밋 README](https://github.com/Zagitta/gpu-unlock/blob/0ebdb1c6308b6a2072be5bf04513472942541956/README.md): 화면 카드도 지원하며 펌웨어 프레임버퍼를 FLR 직전에 제거하고 이후 nvidia-drm이 화면을 인계한다고 설명한다. 보고된 환경은 RTX 3090/Linux이고 다른 보드·환경은 검증되지 않았다. 본 포트와 같은 고정 커밋에서도 이 설계 근거를 찾을 수 있다.
2. [원본 reset 구현](https://github.com/Zagitta/gpu-unlock/blob/0ebdb1c6308b6a2072be5bf04513472942541956/kernel/gpu_unlock/unlock_610.c): FLR이 display engine을 재설정하므로 firmware framebuffer를 먼저 제거한다고 명시한다.
3. [Linux aperture 소유권 문서](https://docs.kernel.org/driver-api/aperture.html): generic framebuffer 소유자를 해제하고 이후 해당 드라이버가 장치 레지스터·프레임버퍼에 접근하지 않도록 한다. UEFI Boot Services와는 다른 계층이다.
4. [UEFI Boot Services 7.3.12–13](https://uefi.org/specs/UEFI/2.10_A/07_Services_Boot_Services.html): Connect/DisconnectController의 표준 절차. ChildHandle이 NULL이면 자식을 파괴한 뒤 드라이버를 해제한다. 독점 OpenProtocol도 기존 드라이버 해제를 유발할 수 있다.
5. [UEFI Driver Model](https://uefi.org/specs/UEFI/2.10/11_Protocols_UEFI_Driver_Model.html): ConnectController는 드라이버 Start, DisconnectController는 Stop을 통해 장치를 연결·해제한다. Stop/Start 호출만으로 보드별 GPU 상태나 언락 보존이 보장되지는 않는다.
6. [UEFI Console Support](https://uefi.org/specs/UEFI/2.10/12_Protocols_Console_Support.html): GOP가 PCI 장치 아래 자식 핸들을 생성할 수 있다. System Table의 가상 콘솔 핸들과 물리 GOP 핸들이 같다고 가정할 수 없다.

## 환경별 선택

| 환경 | 제안 | 현재 상태 |
| --- | --- | --- |
| AMD/BMC 등 다른 장치가 화면 담당 | 프로필 빌드의 독립 PCI 출력 검사를 활용 | 제조사 독립 판정은 있으나 보드별 실기 검증 필요 |
| 다른 NVIDIA가 화면 담당 | 타깃 선택과 복수 GPU 처리부터 확장 | 현재 타깃 선택/Probe의 복수 NVIDIA 정책을 함께 확인해야 함 |
| 패치할 NVIDIA 한 장이 화면 담당 | 명시적 실험 모드에서 GOP 해제·복구 | 새 구현과 실기 검증 필요 |
| Linux 사용 가능 | 원본의 드라이버 로드 전 방식을 참고 | RTX 3080 허용/검증을 별도로 해야 하며 원본 그대로 지원한다고 볼 수 없음 |

## 권장 구현 순서

### 1. 읽기 전용 Probe 확장

기존 `RecordGop()`에 더해 타깃의 모든 프로토콜과 OpenProtocolInformation, BY_DRIVER/BY_CHILD_CONTROLLER 관계, 각 자식의 GOP/Device Path, DriverBinding 및 LoadedImage 식별 정보를 수집한다.
ConsoleOut/StandardError가 가리키는 가상 콘솔과 물리 GOP의 연결도 기록한다. 관계를 특정하지 못하면 해제 실험은 거부한다.
해상도가 같다는 사실만으로 콘솔 소유권을 증명하지 않는다.

### 2. GPU 펌웨어 쓰기 없는 화면 해제·복구 실험

먼저 보드 한 종류와 단일 NVIDIA 출력에만 한정한다. USB 파일과 자산을 열고, 복구 대상 드라이버와 경로를 보존하고, 화면이 사라질 수 있음을 표시한 후 U 승인을 받는다.
화면 출력 호출을 중단하고 USB Write/Flush는 유지한다. 입력도 해제 후 필요 없도록 모든 승인을 먼저 받는다.

확인된 타깃 하위 콘솔 소비자를 표준 드라이버 해제 절차로 분리한 후 해당 GOP 소유자만 DisconnectController로 해제한다. 루트 PCI 버스나 무관한 USB/저장장치까지 재귀적으로 해제하지 않는다.
소유권을 재열거하고 PCI 프로토콜을 재조회하여 독점 접근을 획득한다. 해제 함수의 성공 반환만으로 격리되었다고 판단하지 않는다.

이 단계에서는 언락이나 FLR 없이 독점 접근을 닫고 저장한 드라이버를 ConnectController로 다시 연결하여 화면 복구를 검사한다. 재연결은 기존 프로토콜 포인터를 무효화할 수 있으므로 GOP/콘솔을 새로 조회한다.
GOP 복구와 시스템 콘솔 복구는 별도로 확인한다. `ConnectController()` 한 번의 성공 반환만으로 완료 처리하지 않는다.

### 3. 실험용 단일 GPU transaction

앞 단계가 성공한 보드에서만 펌웨어 작업을 추가한다.

```text
기존 자산·PROM·cold-state 검사와 사용자 승인
  → USB 전용 로그 전환, 타깃 콘솔/GOP 소비자 해제
  → PCI 재조회, 독점 소유, 실제 config/BAR/cold-state 재검사
  → 기존 firmware transaction 및 selector 쓰기
  → BME=0, DMA 자원 유지, 타깃 소비자 부재 재검사
  → 기존 FLR / PCI 복원 / GFW·WPR2·selector·정리 검사
  → 자원 정리와 독점 소유 해제
  → GOP 및 콘솔 재연결, 재열거, selector/GFW/WPR2 재확인
  → 화면 복구 성공 시 Windows Boot Manager로 인계
```

기존 `display_ok`를 무조건 TRUE로 만드는 대신, 독립 화면이 유지되는 경로와 타깃 화면 소비자가 해제된 경로의 조건을 구분한다. 초기 검사는 사용 중인 타깃 GOP를 식별하고, 해제 후 및 FLR 직전 검사는 그 타깃 소비자가 사라졌음을 검증해야 한다.
UEFI 드라이버는 Linux framebuffer와 달리 초기 펌웨어 작업 전부터 소유권 충돌을 일으킬 수 있으므로, 원본의 FLR 직전 제거 시점을 그대로 복사하지 않는다.

현재 config 검사는 256바이트 전체의 동일성까지 요구한다. GOP Stop에 따른 정상적인 IO/MEM decode 변경도 있을 수 있다. 기존 RestoreDecode의 제한을 확인하고 허용할 변경만 명시해야 하며, 비교 전체를 삭제해서는 안 된다.
GOP 환경에서 BME·ROM decode·cold-state가 다르더라도 프로필에 관측값을 넣었다는 이유로 안전성이 입증되지는 않는다. 드라이버 해제 전에는 하드웨어 상태를 임의로 교정하지 않는다.

### 4. Windows 인계 검증

최우선 판정은 **GOP 재초기화가 언락 selector를 보존하는가**이다. ConnectController/SetMode가 GPU 초기화를 실행할 가능성이 있어 실측해야 한다.
복구 직후 상태가 유지되고 Windows 화면·CUDA까지 정상인 경우에만 이 경로를 지원 대상으로 삼는다. 펌웨어 메뉴로 돌아오면서 추가 재연결이 발생하는지도 확인한다.

GOP 복구가 언락을 없애는 경우, 화면을 복구하지 않고 Windows Boot Manager를 직접 LoadImage/StartImage하는 방법은 후속 연구 후보일 뿐이다. bootmgfw/winload의 GOP 사용과 초기 화면 접근 때문에 성공을 보장할 근거가 없다. 기존 GOP 구조체를 남겨두거나 가짜 GOP를 제공하는 것으로 해결되었다고 판단하지 않는다.
Linux를 한번 부팅해 패치하고 재부팅해서 Windows로 가는 방법도 재부팅 후 상태 보존이 입증되지 않아 대안으로 제시하지 않는다.

## 필요한 검증과 실패 처리

- 모의 검사: 타깃 GOP/자식 식별, 알 수 없는 소유자 거부, 부분 해제 실패, 해제 후 재등장, PCI 프로토콜 교체, 콘솔 미호출 상태의 USB 기록·실패 전파, 독점 접근 종료 전 재연결 금지.
- 실기 순서: Probe → 해제/복구만 → 언락/FLR/복구 → Windows 화면·CUDA → 완전 전원 종료 후 반복. warm reboot는 별도 조건으로 취급한다.
- 성공 기준: 화면 복구, selector 유지, WPR2/GFW 및 BME/DMA 정리 확인, Windows 드라이버 정상 시작과 연산 검증을 모두 충족한다.
- GOP 해제 후 상태가 불명확하거나 DMA 정리를 증명하지 못하면 자동 재시도·Windows 인계를 하지 않는다. USB에 마지막 단계와 결과를 남기고 정지한다. 콘솔이 없으므로 정지 시 화면 안내에 의존할 수 없다.

첫 구현 범위는 Probe 확장과 해제/복구 실험까지가 적절하다. 이 관문을 통과하면 기존 코어를 유지하면서 UEFI 소유권·로그·화면 복구 부분을 확장할 수 있다.
이번에는 코드 변경, 빌드, GPU 실행 및 Windows 실기 검증을 수행하지 않았다.
