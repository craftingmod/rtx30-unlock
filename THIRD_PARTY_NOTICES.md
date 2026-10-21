# 외부 코드 고지

## GPU unlock 코어 및 UEFI 포트 — GPL-2.0-only

- 원본: [Zagitta/gpu-unlock](https://github.com/Zagitta/gpu-unlock/tree/0ebdb1c6308b6a2072be5bf04513472942541956)
- 고정 커밋: `0ebdb1c6308b6a2072be5bf04513472942541956`
- 대상: `src/unlock/vendor/`의 `unlock_610`, `prep_610`, `layout_610`, `vbios_610`, `gfw_eval`, `diagnostic.h`.
- 라이선스 원문: [LICENSE](LICENSE).

2026-10-18의 로컬 변경: MSI RTX 3080 대상 선택, UEFI의 checked I/O 및 실행·정리 상태 연결, FWSEC 진단. `tests/reference/unlock_610_v06.c`는 비교 검사에 사용하는 로컬 v0.6 스냅샷입니다. UEFI 포트와 host/mock 테스트도 GPL-2.0-only로 제공합니다. v0.10에서는 PCI I/O Attributes를 통해 Bus Master를 관리하고 프로토콜 상태와 실제 PCI Command를 함께 검사합니다.

2026-10-19의 공개용 도구·문서 정리: 로컬 USB 패키지 생성, 공개용 테스트 실행기, 상대 경로 기반 빌드 메타데이터. GPU 실행 조건과 transaction 순서는 유지했습니다.

## EDK2 헤더 — BSD-2-Clause-Patent

- 원본: [TianoCore EDK2](https://github.com/tianocore/edk2/tree/c1415771ac592ad8509351c46cd3cda4f38d7d52)
- 고정 커밋: `c1415771ac592ad8509351c46cd3cda4f38d7d52`
- 대상: `deps/edk2/MdePkg/Include/`.
- 원문과 저작권 고지: [EDK2 라이선스](LICENSES/EDK2-BSD-2-Clause-Patent.txt) 및 각 파일의 헤더.

앱은 헤더 선언을 사용하며 EDK2 라이브러리 바이너리를 링크하지 않습니다.

## 상위 프로젝트의 관련 고지

상위 프로젝트에서 보존한 NVIDIA 소스 및 EFI 참조 고지를 함께 제공합니다.

- [NVIDIA MIT 고지](LICENSES/NVIDIA-MIT.txt)
- [CMP40HX MIT 고지](LICENSES/CMP40HX-MIT.txt)
- [NVIDIA Driver License Agreement](LICENSES/NVIDIA-Driver.txt)

소스와 USB 패키지를 복사할 때 해당 라이선스와 저작권 고지를 함께 유지하세요.
