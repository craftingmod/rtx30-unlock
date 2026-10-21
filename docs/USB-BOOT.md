# USB 준비 및 부팅

이 안내는 README에 명시된 MSI RTX 3080 실험 환경을 대상으로 합니다. 기존 실행은 확인됐지만 새 FAT32 USB로 옮긴 구성의 실제 부팅은 아직 검증하지 않았습니다.

## 준비

- x64 UEFI 환경, Secure Boot 해제, Intel 내장그래픽에 연결한 화면.
- 비어 있는 FAT32 USB. 포맷이 필요하면 USB의 기존 파일을 먼저 백업하고 대상 장치를 확인합니다.
- 빌드한 `BOOTX64.EFI` 또는 제공되는 USB 템플릿.
- `unlock/assets.json`과 이름·크기·SHA256이 일치하는 자산 5개. 자산 준비 방식은 [상위 프로젝트의 firmware 문서](https://github.com/Zagitta/gpu-unlock/tree/0ebdb1c6308b6a2072be5bf04513472942541956/firmware)를 참고합니다.

Windows 장치 암호화/BitLocker를 사용하는 PC에서는 펌웨어 설정 변경 전에 복구 키를 확인해 두세요.

## 로컬에서 패키지 만들기

저장소 루트에서 실행합니다. 먼저 EFI를 빌드한 뒤, 자산 5개를 로컬 `assets` 디렉터리에 준비합니다. 아래 명령은 새 로컬 폴더를 만들며 USB를 포맷하거나 자동으로 선택하지 않습니다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\unlock\build.ps1
python .\tools\package_usb.py --assets .\assets --output .\usb-package
```

`--assets`를 생략하면 실행 자산을 뺀 템플릿이 만들어집니다. 템플릿의 `ASSETS/REQUIRED-ASSETS.json`은 필요한 파일 목록입니다. 자산 파일 자체가 아니므로, 5개 파일을 별도로 준비해야 합니다.

완성된 `usb-package` **안의 내용**을 USB 최상위에 복사합니다. `usb-package` 폴더 자체를 한 단계 아래에 넣지 않습니다.

```text
USB:/
  EFI/BOOT/BOOTX64.EFI
  RTX-UNLOCK/
    ASSETS/
      booter_load_ga102.bin
      v67-ga102-610.43.03.bin
      gsp_ga10x.bin
      gsp_boot_ga102.bin
      gsp_boot_desc_ga102.bin
    LICENSES/
    SOURCE.zip
  USB-README.md
  PACKAGE.json
  SHA256SUMS.txt
```

`RTX-UNLOCK` 폴더는 로그를 쓸 수 있어야 합니다. `BOOTX64.EFI`만 복사한 상태에서는 실행할 수 없습니다. 이 앱은 파일 내용과 보드 조건을 다시 검사합니다.

## 실행

1. Windows 작업을 저장하고 PC를 완전히 종료한 뒤 전원을 켭니다. 이미 드라이버가 사용한 GPU의 warm 상태에서는 검사가 거부될 수 있습니다.
2. 메인보드의 부팅 메뉴에서 **UEFI USB**를 선택합니다. 검증한 ASUS 보드에서는 F8을 사용했습니다.
3. `READY`까지 사전 검사가 끝나면 **U를 한 번** 누릅니다. Esc 또는 U 없이 30초 대기는 취소입니다.
4. 실행 중에는 USB를 뽑거나 재부팅하지 않습니다.
5. `UEFI transaction completed and cleaned up`이 표시되고 펌웨어로 돌아오면 **Windows Boot Manager**를 선택합니다. 중간에 재부팅하면 적용 상태가 유지되지 않을 수 있습니다.

USB 앱이 다시 열리면 Esc로 빠져나오세요. 같은 부팅에서 U를 다시 누르지 않습니다.

`STOP / POWER OFF`가 표시되면 전원 버튼으로 PC를 끕니다. 그 화면에서 Windows로 계속 진행하지 않습니다. 다시 시도하기 전에 `RTX-UNLOCK/RUNxx/UNLOCK.TXT`를 확인합니다.

## 적용 범위

UEFI 성공은 Windows에서의 성능 향상이나 장시간 안정성을 보장하지 않습니다. 먼저 장치 상태를 확인하고 자신의 작업에서 성능을 비교하세요. 다음 콜드부팅 뒤에도 효과가 남는지는 확인되지 않았습니다.

기존 Windows 디스크의 부트로더를 바꿀 필요는 없습니다. USB 사용을 중단하려면 USB를 제거하고 Windows Boot Manager로 부팅합니다.
