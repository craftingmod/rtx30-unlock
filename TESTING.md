# 테스트

## 기본 검사

저장소 루트에서 전체 기본 검사를 실행합니다.

```powershell
bun run test
```

Bun, Python 3.9+, Clang, `lld-link`가 필요합니다. Clang이 `PATH`에 없으면 `LLVM_BIN`으로 LLVM 설치 경로를 지정할 수 있습니다. 기본 검사는 Probe, GOP 실험 EFI, Unlock EFI를 빌드한 뒤 호스트 모의 검사, Python 검사와 패키지 검사를 실행합니다. 실제 GPU와 UEFI 서비스를 사용하지 않습니다.

## 필요한 검사만 실행

```powershell
bun tools/test.ts gop       # GOP 모의 검사
bun tools/test.ts python    # Python 검사
bun tools/test.ts gop-cycle # 단일 검사 묶음 예시
```

그 외 단일 검사 묶음 이름은 `tools/test.ts`의 suite 목록을 참고하세요. 예: `probe`, `runtime`, `pe`, `gop-ssd`.

## 로컬 fixture 검사

전체 검사에 fixture 의존 검사를 추가하려면 다음 파일을 준비합니다.

- `build/firmware-assets/`: `bun run firmware:assets`로 생성합니다.
- `build/PROM.BIN`: 로컬 PROM 캡처(1 MiB)를 이 경로에 둡니다. 루트에 `PROM.BIN`이 있으면 `Copy-Item .\PROM.BIN .\build\PROM.BIN`으로 복사할 수 있습니다.
- `evidence/boot-03/REPORT.TXT`: 저장소에 포함된 테스트 입력입니다.

준비한 뒤 다음 명령으로 전체 검사를 실행합니다.

```powershell
bun tools/test.ts --with-local-fixtures
```

fixture 의존 검사만 실행하려면 다음을 사용합니다.

```powershell
bun tools/test.ts local --with-local-fixtures
```

결과 요약과 로그는 `unlock/build/test-results.json`과 `unlock/build/*.log`에 저장됩니다.
