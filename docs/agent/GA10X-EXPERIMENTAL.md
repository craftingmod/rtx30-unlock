# RTX 30 GA10x 실험 경로

`make unlock EXPERIMENTAL=1 PROFILE=...`은 GA102·GA103·GA104·GA106·GA107 캡처로 기존 Unlock 절차를 시도할 수 있게 한다. 제조사나 특정 제품명에 고정하지 않는다. 기본 모드는 기존 승인 보드 조건을 유지한다. 실험 플래그는 `firmwareCompatible`, `unlockApproved`, `hardwareTested`를 변경하지 않는다.

## 코드상 공통화 근거

NVIDIA [610.43.03의 `g_kernel_gsp_nvoc.c`](https://github.com/NVIDIA/open-gpu-kernel-modules/blob/610.43.03/src/nvidia/generated/g_kernel_gsp_nvoc.c#L833-L835)에서 다섯 GA10x 칩이 같은 GSP 부트 아카이브를 선택한다. 같은 파일 [1646–1648행](https://github.com/NVIDIA/open-gpu-kernel-modules/blob/610.43.03/src/nvidia/generated/g_kernel_gsp_nvoc.c#L1646-L1648)에서 Booter Load 아카이브도 공통이다.

[Falcon 사전 리셋 대기](https://github.com/NVIDIA/open-gpu-kernel-modules/blob/610.43.03/src/nvidia/generated/g_kernel_falcon_nvoc.c#L554-L556)와 [SEC2 Falcon 설정](https://github.com/NVIDIA/open-gpu-kernel-modules/blob/610.43.03/src/nvidia/generated/g_kernel_sec2_nvoc.c#L566-L569)도 GA102 이름의 구현을 공유한다. 따라서 구현·자산 이름의 `GA102`를 그 칩만 지원한다는 뜻으로 해석하지 않는다.

확인한 원본 SHA-256:

| 610.43.03 원본 | SHA-256 |
| --- | --- |
| `src/nvidia/generated/g_kernel_gsp_nvoc.c` | `c0258d653221b04fe4fd26b434db2596a6aba7c30edb460deca7fdf1b44e5c05` |
| `src/nvidia/generated/g_kernel_falcon_nvoc.c` | `e4214a098ab4cef28feaf0a20b45fe8880b38bbe82f03b10f9aa922c0cf101c3` |
| `src/nvidia/generated/g_kernel_sec2_nvoc.c` | `8c54ad7f9cd8ab8c90c4e5db34606611127b720c2596660e94a078f56534775e` |

기존 `unlock/assets.json`의 5개 파일명·크기·해시를 그대로 사용한다. 위 HAL 선택은 정상 Booter/GSP 부트 코드 공통화의 근거다. **패치된 V67, FWSEC 실행 결과, 셀렉터 변경, FLR 이후 상태가 모든 칩에서 성공한다는 증거는 아니다.** 이 부분은 기존 절차를 사용하는 명시적 실험이며, 칩별 실기 검증은 별도로 필요하다.

## 식별과 실행 조건

[Linux nova 칩 식별값](https://github.com/torvalds/linux/blob/master/drivers/gpu/nova-core/gpu.rs)을 사용한다.

| 실제 칩 | PMC_BOOT_0 칩 필드 | 합성 호스트 검사 메모리 | 실험 빌드 |
| --- | --- | --- | --- |
| GA102 | `0x172` | 24GiB | 허용 |
| GA103 | `0x173` | 8GiB | 허용 |
| GA104 | `0x174` | 8GiB | 허용 |
| GA106 | `0x176` | 12GiB | 허용 |
| GA107 | `0x177` | 4GiB | 허용 |

PCI `10de:22xx/24xx/25xx`는 최초 칩 읽기를 허용하는 후보 범위다. 실제 칩 필드를 확인하기 전에는 다른 레지스터나 PROM을 읽지 않는다. GA100, Turing, Ada 및 알 수 없는 칩은 제외한다. 여러 NVIDIA Display 장치가 있거나 USB 기초 로그를 저장하지 못하면 상세 읽기를 생략한다.

프로필은 읽은 칩에서 아키텍처를 식별한다. PCI 제품명만으로 공통 펌웨어 호환성을 승인하지 않는다. 관측값이 유효해도 독립 검증 플래그는 자동 승인되지 않는다.

실행 시 정확한 PCI/Subsystem/칩/PROM, 초기 레지스터, 메모리 크기 및 VGA/MMU/WPR 입력을 재확인한다. BDF와 BAR 물리 주소는 실행 시 조회한다. WPR/GSP 예약은 실제 메모리 입력에서 계산한다. 실험 모드도 GFW 완료, WPR2 비활성, 독립된 화면 출력, PCI capability/FLR, 자산 해시, DMA 범위 및 정리 검사를 유지한다. UEFI에서 U를 누른 뒤 한 번 시도하며, 정리를 입증하지 못하면 Windows로 반환하지 않는 기존 정지 절차를 유지한다.

호스트 검사와 EFI 빌드 통과는 실기 성공을 뜻하지 않는다. 특히 노트북의 MUX/전원/화면 경로는 같은 GA10x 칩이어도 이 조건을 만족하지 않을 수 있다. 기존 MSI RTX 3080 실기 기록 외에 다섯 칩 전체의 UEFI/Windows/CUDA 성공을 확인한 것으로 표시하지 않는다.
