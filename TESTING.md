# 빌드 및 검증

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
