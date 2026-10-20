# NVIDIA GOP 해제·복구 실험 1

## 다음 실험: diagnostic v8-virtual-gop

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

FLR 및 PCI 복원 순서는 기존 `unlock/port.c`의 checked FLR/복원 절차를 참고했다. 새 실험은 해당 언락 코어를 링크하지 않고 별도의 PCI 전용 구현을 사용한다. 모의 검사는 한 번의 리셋·100ms 접근 금지·BME 조건·남은 GOP 거부·EDID Stop의 입력 영향·새 핸들·부분 실패·독점 해제 실패와 복구 실패를 확인하지만, 실제 NVIDIA/보드 펌웨어의 재초기화 성공은 아직 미검증이다.

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
