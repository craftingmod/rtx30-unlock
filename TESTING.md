# 빌드 및 검증

## NVIDIA GOP 해제·복구 실험

다음 실기는 `scripts/build-gop-experiment.ps1 -FullFlr`로 만드는 v8-virtual-gop다. GPU GOP 전체 해제 및 한 번의 FLR·PCI/GOP 복구 모의 시나리오 31개와 full-FLR 앱 진입/로그/승인 검사 21개를 추가한다. 기존 실험 회귀 검사 122개와 함께 실행한다. 리셋 미지원, BME 활성, capability 순환, 입력 변화, 잔존 GOP, pending timeout, 리셋 쓰기 오류·100ms 접근 금지, PCI 복원 오류, 새 자식 핸들, Supported 거부, 독점 해제 실패와 로그 오류를 확인한다. 루트 잔존 소유자와 GOP가 동시에 남아도 전체 진단을 기록하는지 확인한다. v8은 기존 중간 핸들의 소유자 없는 가상 GOP 잔존 및 새 핸들로의 복구, 프레임버퍼 주소/크기 잔존, BY_DRIVER/BY_CHILD/EXCLUSIVE 잔존, 알 수 없는 가상 GOP, 열거 불일치와 진단 로그 실패 시 FLR 차단도 검사한다. 실제 GPU 접근은 모의 서비스로 대체한다.

다음 해제 실기는 `scripts/build-gop-experiment.ps1 -LeafOnly`로 만드는 v5-leaf다. `experiments/gop/test.cmd`는 출력 멤버 분리·복구 검사 20개와 leaf 앱의 로그·승인·진입 검사 21개를 추가로 실행한다. 모의 환경에는 범용 Connect/DisconnectController, GPU 독점 접근과 PCI 쓰기가 없고, 선택 드라이버의 Stop 한 번 및 Supported 성공 후 Start 한 번만 허용한다. 부분 실패·미복구·입력/소유권 변화·드라이버 변경·로그 오류를 확인한다. `Mode=leaf-member` 패키지를 실기 검증 전 상태로 생성한다.

다음 실기는 `scripts/build-gop-experiment.ps1 -TreeOnly`로 만드는 diagnostic v4-tree 읽기 전용 수집이다. `experiments/gop/test.cmd`는 기존 검사에 더해 중첩·중복·순환 관계, 경로 없는 핸들, 깊이 제한, 손상된 응답과 파일 로그 오류를 확인하는 트리 검사 12개, 실제 읽기 전용 앱 진입 검사 17개를 실행한다. 해당 모의 서비스에는 드라이버 해제·재연결·독점 접근 함수가 없다. v4-tree 빌드는 cycle 코어를 링크하지 않고 출력 map에 변경 코어가 없는지도 검사한다.

독립된 실험용 EFI의 준비와 실기 절차는 [실험 안내](experiments/gop/README.md)에 있다. 기본 검사에 이 EFI의 빌드/PE 검사, 소유권·복구·장치 식별 31개 시나리오, 실제 앱 로그·승인·진입 21개 시나리오와 USB 패키지 검사를 포함한다. GPU는 실행하지 않는다. 소유권 모의 검사는 부분 해제 실패 시 복구, 독점 접근을 닫기 전 재연결 금지, GOP 재등장, 소유자 변경, PCI/GOP 핸들 재생성, 복구·로그 오류, 임의 NVIDIA 모델/Subsystem 허용과 실행 중 장치 교체 거부를 확인한다. diagnostic v3는 Device Path 자식 관계가 PCI I/O보다 먼저 열거되는 사용자 로그의 구성을 재현하고, 중복 자식 제거·자식 소유자 및 경로 검증·원래 오류 상태 보존을 확인한다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build-gop-experiment.ps1
experiments\gop\test.cmd
python tools/test_gop_experiment.py
```

이 실험은 자체 빌드 manifest로 사용한 소스·EDK2 헤더를 기록하고 패키징 시 다시 검증한다. 기존 Unlock 릴리스용 SOURCE-MANIFEST와 독립된 경로다. 실기 로그 없이 GOP 복구나 Intel GOP 없는 Unlock 성공으로 해석하지 않는다.

## 기본 검사

Windows에서 README의 컴파일러와 Python을 설치한 뒤 저장소 루트에서 실행합니다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\unlock\build.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\unlock\test-all.ps1
```

기본 검사는 probe, port/Bus Master, preflight, runtime, PE 거부 검사와 USB 패키지 도구를 실행합니다. `evidence/boot-03/REPORT.TXT`를 preflight fixture로 사용합니다. GPU와 UEFI 서비스는 모의 환경입니다.

신규 범용 경로는 `make probe` 후 `make test`로 확인합니다. 프로필 파서의 손상·누락·중복·해시 불일치, 미지원 카드, 미승인 기본 빌드 거부 및 명시적 실험 모드를 검사합니다. C Probe가 실제 출력한 REPORT도 프로필 생성기에 통과시킵니다. Probe와 프로필/호스트 런타임 검사는 GA102·GA103·GA104·GA106·GA107 전체를 포함합니다. 합성 프로필은 각각 24/8/8/12/4GiB 메모리로 다른 BDF, 정확한 Subsystem 매칭, 동적 레이아웃, BME/FLR 거부, 타깃과 독립된 GOP 소유권을 확인합니다. 이 합성 데이터는 실제 보드 캡처가 아닙니다.

`make unlock EXPERIMENTAL=1 PROFILE=...`은 미승인 RTX 30 GA10x 캡처를 허용하지만 증거 해시, 칩·초기 상태·레이아웃·화면 소유권·DMA 안전 검사를 유지합니다. GA100·Turing·Ada·알 수 없는 칩은 후보 PCI ID를 가진 경우에도 엔진/PROM 접근 및 실험 빌드를 거부합니다. 현재 GPU-Z 정보만으로 실제 보드 프로필이나 실험용 EFI를 생성하지 않습니다. 공통 Booter/GSP 자산은 V67 실기 성공의 증거가 아닙니다.

Python 패키지 검사만 실행하려면 다음 명령을 사용합니다.

```text
python -m unittest discover -s tools -p "test_*.py" -v
```

## 로컬 fixture를 사용하는 추가 검사

GOP v12의 해제·언락 연결 및 자식 진단 모의 검사 22개는 `experiments/gop/test.cmd`에 포함된다. 자식 0/1/3개, 열거 API 오류, 다른 소유자, GOP 부재, open 수 범위 초과와 진단 로그 실패를 구분하고 기존 실패 처리를 유지하는지 검사한다. 실제 셀렉터 조회 검사 10개는 `experiments/gop/test-patch-state.cmd`, 실제 SetMode/Blt 및 RTC 코드 검사 22개와 시간 기록을 포함한 FLR 검사 4개는 `experiments/gop/test-visual.cmd`로 실행한다. 다른 해상도 모드 선택·왕복 설정, 프레임버퍼 전체 색상 읽기·불일치, 표시 실패·셀렉터 소실 시 후속 작업 차단, 시계 역행·오류·윤일 계산, 로그 콜백 이후 BME 재검사 및 100ms 무접근을 포함하며 실제 펌웨어는 실행하지 않는다. EFI 빌드와 자산을 포함한 패키지 생성은 [GOP 실험 안내](experiments/gop/README.md)를 따른다.

다음 파일을 준비한 뒤 `-WithLocalFixtures`를 지정합니다.

- `analysis/pinned-assets/`: `unlock/assets.json`의 5개 자산.
- `evidence/boot-03/PROM.BIN`: 1MiB, SHA256 `F3DC8F07B0B8C9856D192DF172DFA7F7F84AC17EF6EDFDB448AD7FA94EFD2789`.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\unlock\test-all.ps1 -WithLocalFixtures
```

transaction, SHA256, EFI transaction, 전체 앱과 FWSEC 순서 비교 검사가 추가됩니다. PROM fixture는 기록된 보드의 회귀 검사 입력입니다. 다른 카드의 캡처로 바꿔 같은 검증이라고 해석하지 않습니다.

## 소스와 패키지 해시

`SOURCE-MANIFEST.json`에는 공개 소스 파일 목록과 SHA256이 있습니다. USB 패키지 도구는 이 목록에 있는 파일만 `SOURCE.zip`에 넣고 빌드 입력·EFI·실행 자산을 검사합니다. 개인 작업 파일과 빌드 캐시는 패키지에 넣지 않습니다.

소스를 수정한 개발자는 새 파일을 Git에 추가하고 소스 manifest를 갱신한 뒤 다시 빌드·검사합니다.

```powershell
git add .
python .\tools\source_manifest.py
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\unlock\build.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\unlock\test-all.ps1
```

`tools/source_manifest.py`는 Git의 추적 파일을 기준으로 동작합니다. ZIP으로 받은 소스는 제공된 manifest를 사용해 그대로 빌드하고 패키징할 수 있습니다.

검사 결과는 `unlock/build/test-results.json`에 기록합니다. host/mock 통과와 실제 USB 부팅 결과는 별개입니다.

GOP v14의 Windows 파일 탐색·이미지 로드·실행 인계 모의 검사 22개는 `experiments/gop/test-windows.cmd`로 실행한다. USB 제외, 후보 부재/중복, 비정상 경로/파일/이미지, 로드 거부, 미검증 상태 및 보고 Close 실패 시 실행 차단과 StartImage 반환 처리를 검사한다. 디스크 쓰기·NVRAM·재부팅 서비스를 설치하지 않은 모의 환경이다. Windows 결과 수집 스크립트의 미측정/RTX 3080·3090 Ti 성공/다른 PCI 위치·결과 검증 실패/실행 실패/출력 오류 7개는 `experiments/gop/test-windows-check.ps1`에서 장치 조회와 벤치마크를 대체해 검사한다. 실제 Windows 부팅·NVIDIA 초기화·CUDA 성능은 별도 실기 검증 대상이다.

GOP v15 production 경로는 `experiments/gop/test-production.cmd`로 검사한다. 실제 앱의 U 없는 시작 게이트, DEBUG.ON 유무에 따른 로그 선택과 쓰기 오류 검사, 실제 자산 IO의 기본 무생성/무쓰기/무Flush, 디버그 보고서 유지와 자산·정리 실패 처리를 모의 서비스로 확인한다. Windows 인계 검사에는 무로그 상태에서 검증 성공 시 실행 및 검증 실패 시 실행 차단을 포함한다. 실제 USB 자동 부팅 시간·SSD EFI 설치는 검사하지 않는다.

GOP v16은 `experiments/gop/test-ssd.cmd`로 실제 셀렉터 읽기 정책 6개와 기존 적용 후 관측 10개, 실제 자산 IO의 GPT ESP 허용/일반 파티션·프로토콜 부재·잘못된 revision 거부, 실제 앱의 스킵/적용/사전 실패/transaction 실패 흐름 4개를 검사한다. SS0가 적용값일 때 GOP 계획·실제 적용 호출 없이 Windows로 이어지고, 예상 외 값은 스킵하지 않는지 확인한다. GPU·펌웨어 실행은 모의 처리하며 SSD에 쓰지 않는다.
