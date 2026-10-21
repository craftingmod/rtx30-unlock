import { mkdir, mkdtemp, rm, writeFile } from "node:fs/promises"
import { isAbsolute, join, relative, resolve } from "node:path"
import {
  clang,
  python,
  requireTool,
  root,
  run,
  setOutputHandler,
} from "./test_support"

const cc = () => requireTool(clang, "clang")
const py = () => requireTool(python, "Python 3.9+")
const includes = [
  "-I",
  "deps/edk2/MdePkg/Include",
  "-I",
  "deps/edk2/MdePkg/Include/X64",
]
const appIncludes = ["-I", "src/unlock/compat", ...includes]
const outputDir = "build/tests"
const fixtureProm = "build/PROM.BIN"
const fixtureAssets = "build/firmware-assets"
const profileFamilies = ["GA102", "GA103", "GA104", "GA106", "GA107"]

async function compile(name: string, sources: string[], flags: string[] = []) {
  const binary = `${outputDir}/${name}.exe`
  await run(["mkdir", "-p", outputDir])
  await run([cc(), ...flags, ...sources, "-o", binary])
  return resolve(root, binary)
}

async function compileAndRun(
  name: string,
  sources: string[],
  flags: string[] = [],
  args: string[] = [],
) {
  const binary = await compile(name, sources, flags)
  await run([binary, ...args])
}

async function pythonScript(...args: string[]) {
  await run([py(), ...args])
}

async function buildProbe() {
  await run([process.execPath, "tools/build-probe.ts"])
}

async function buildGopExperiment() {
  await run([process.execPath, "tools/build-gop-experiment.ts"])
}

async function buildUnlock() {
  await run([process.execPath, "tools/build.ts"])
}

async function probe() {
  await compileAndRun(
    "probe-core",
    [
      "src/probe/test_core.c",
      "src/probe/probe_core.c",
      "src/probe/probe_app.c",
      "src/probe/probe_profile.c",
      "src/unlock/sha256.c",
    ],
    [
      "-std=c11",
      "-Wall",
      "-Wextra",
      "-Werror",
      "-D_CRT_SECURE_NO_WARNINGS",
      ...includes,
    ],
  )
}

async function profileRuntime() {
  for (const family of profileFamilies) {
    const fixture = `build/profile-fixture-${family}`
    await pythonScript(
      "tools/test_probe_profile.py",
      "--write-fixture",
      fixture,
      family,
    )
    await compileAndRun(
      `profile-${family}`,
      [
        "src/unlock/tests/test_profile.c",
        "src/unlock/checks.c",
        "src/unlock/port.c",
        "src/unlock/vendor/layout_610.c",
      ],
      [
        "-std=c11",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-include",
        `${fixture}/profile.h`,
        ...includes,
      ],
    )
  }
}

async function port() {
  await compileAndRun(
    "test-port",
    ["src/unlock/tests/test_port.c", "src/unlock/port.c"],
    [
      "-std=c11",
      "-Wall",
      "-Wextra",
      "-Werror",
      "-D_CRT_SECURE_NO_WARNINGS",
      ...includes,
    ],
  )
  await compileAndRun(
    "master-attributes",
    [
      "src/unlock/tests/test_master_attributes.c",
      "src/unlock/port.c",
      "src/unlock/compat.c",
      "src/unlock/vendor/unlock_610.c",
      "src/unlock/vendor/prep_610.c",
      "src/unlock/vendor/layout_610.c",
      "src/unlock/vendor/vbios_610.c",
      "src/unlock/vendor/gfw_eval.c",
    ],
    [
      "-std=c11",
      "-O1",
      "-Wall",
      "-Wextra",
      "-Werror",
      "-Wno-unused-parameter",
      "-Wno-unused-function",
      "-D_CRT_SECURE_NO_WARNINGS",
      "-D__KERNEL__",
      "-DGU_UEFI_PORT",
      "-DGU_TARGET_PCI_ID=0x220610deU",
      ...appIncludes,
    ],
  )
}

async function checks() {
  await compileAndRun(
    "checks",
    ["src/unlock/tests/test_checks.c", "src/unlock/checks.c"],
    [
      "-std=c11",
      "-O2",
      "-Wall",
      "-Wextra",
      "-Werror",
      "-D_CRT_SECURE_NO_WARNINGS",
      ...includes,
    ],
  )
  await run([
    cc(),
    "-target",
    "x86_64-pc-windows-msvc",
    "-std=c11",
    "-Os",
    "-ffreestanding",
    "-fno-builtin",
    "-fno-stack-protector",
    "-fshort-wchar",
    "-mno-red-zone",
    "-Wall",
    "-Wextra",
    "-Werror",
    ...includes,
    "-c",
    "src/unlock/checks.c",
    "-o",
    `${outputDir}/checks-freestanding.obj`,
  ])
}

async function runtime() {
  await run([
    cc(),
    "-std=c11",
    "-O1",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-D_CRT_SECURE_NO_WARNINGS",
    "-fno-builtin",
    "-Dmemcpy=GuTestMemcpy",
    "-Dmemset=GuTestMemset",
    "-Dmemchr=GuTestMemchr",
    "-Dmemcmp=GuTestMemcmp",
    "-Dstrcmp=GuTestStrcmp",
    "-Dstrlen=GuTestStrlen",
    "-c",
    "src/unlock/runtime.c",
    "-o",
    `${outputDir}/runtime-host.obj`,
  ])
  await compileAndRun(
    "runtime",
    [
      "src/unlock/tests/test_runtime.c",
      "src/unlock/format.c",
      `${outputDir}/runtime-host.obj`,
    ],
    ["-std=c11", "-O1", "-Wall", "-Wextra", "-Werror", "-fno-builtin"],
  )
}

async function gopCycle() {
  const gopFlags = [
    "-std=c11",
    "-O2",
    "-Wall",
    "-Wextra",
    "-Werror",
    ...includes,
  ]
  const cases: [string, string[], string[]?][] = [
    ["gop-cycle", ["src/gop/test_cycle.c", "src/gop/cycle.c"]],
    ["gop-io", ["src/gop/test_io.c", "src/gop/cycle.c", "src/unlock/format.c"]],
    ["gop-tree", ["src/gop/test_tree.c", "src/gop/tree.c"]],
    [
      "gop-tree-app",
      ["src/gop/test_io.c", "src/gop/tree.c", "src/unlock/format.c"],
      ["-DGC_TREE_ONLY"],
    ],
    ["gop-leaf", ["src/gop/test_leaf.c", "src/gop/leaf.c"]],
    [
      "gop-leaf-app",
      ["src/gop/test_io.c", "src/gop/leaf.c", "src/unlock/format.c"],
      ["-DGC_LEAF_ONLY"],
    ],
    [
      "gop-full",
      [
        "src/gop/test_full.c",
        "src/gop/full.c",
        "src/gop/reset.c",
        "src/gop/leaf.c",
      ],
    ],
    [
      "gop-full-app",
      [
        "src/gop/test_io.c",
        "src/gop/full.c",
        "src/gop/reset.c",
        "src/gop/leaf.c",
        "src/unlock/format.c",
      ],
      ["-DGC_FULL_FLR"],
    ],
    [
      "gop-patch",
      [
        "src/gop/test_patch.c",
        "src/gop/full.c",
        "src/gop/reset.c",
        "src/gop/leaf.c",
      ],
      ["-DGC_FULL_UNLOCK"],
    ],
  ]
  for (const [name, sources, flags = []] of cases) {
    await compileAndRun(name, sources, [...gopFlags, ...flags])
  }
}

async function gopPatchState() {
  await compileAndRun(
    "gop-patch-state",
    [
      "src/gop/test_patch_state.c",
      "src/unlock/app.c",
      "src/unlock/ownership.c",
      "src/unlock/checks.c",
      "src/unlock/sha256.c",
      "src/unlock/port.c",
      "src/unlock/compat.c",
      "src/unlock/vendor/unlock_610.c",
      "src/unlock/vendor/prep_610.c",
      "src/unlock/vendor/layout_610.c",
      "src/unlock/vendor/vbios_610.c",
      "src/unlock/vendor/gfw_eval.c",
    ],
    [
      "-std=c11",
      "-O1",
      "-Wall",
      "-Wextra",
      "-Werror",
      "-Wno-unused-function",
      "-Wno-unused-parameter",
      "-DGU_GOP_EXPERIMENT",
      "-D__KERNEL__",
      "-DGU_UEFI_PORT",
      "-DGU_TARGET_PCI_ID=0x220610deU",
      ...appIncludes,
    ],
  )
}

async function gopVisual() {
  await compileAndRun(
    "gop-visual",
    ["src/gop/test_visual.c", "src/unlock/format.c"],
    ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", ...includes],
  )
  await compileAndRun(
    "gop-timed-flr",
    ["src/gop/test_timed_flr.c", "src/unlock/port.c"],
    [
      "-std=c11",
      "-O2",
      "-Wall",
      "-Wextra",
      "-Werror",
      "-DGU_GOP_EXPERIMENT",
      ...includes,
    ],
  )
}

async function gopWindows() {
  await compileAndRun(
    "gop-windows",
    ["src/gop/test_windows.c"],
    ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", ...includes],
  )
}

async function gopProduction() {
  await compileAndRun(
    "gop-production",
    ["src/gop/test_io.c", "src/gop/cycle.c", "src/unlock/format.c"],
    [
      "-std=c11",
      "-O2",
      "-Wall",
      "-Wextra",
      "-Werror",
      "-DGC_PRODUCTION",
      ...includes,
    ],
  )
  await compileAndRun(
    "gop-quiet-io",
    ["src/gop/test_quiet_io.c", "src/unlock/format.c", "src/unlock/sha256.c"],
    [
      "-std=c11",
      "-O2",
      "-Wall",
      "-Wextra",
      "-Werror",
      "-DGU_GOP_EXPERIMENT",
      ...appIncludes,
    ],
  )
}

async function gopSsd() {
  await compileAndRun(
    "gop-ssd-selector",
    [
      "src/gop/test_patch_state.c",
      "src/unlock/app.c",
      "src/unlock/ownership.c",
      "src/unlock/checks.c",
      "src/unlock/sha256.c",
      "src/unlock/port.c",
      "src/unlock/compat.c",
      "src/unlock/vendor/unlock_610.c",
      "src/unlock/vendor/prep_610.c",
      "src/unlock/vendor/layout_610.c",
      "src/unlock/vendor/vbios_610.c",
      "src/unlock/vendor/gfw_eval.c",
    ],
    [
      "-std=c11",
      "-O1",
      "-Wall",
      "-Wextra",
      "-Werror",
      "-Wno-unused-function",
      "-Wno-unused-parameter",
      "-DGU_SSD_EXPERIMENT",
      "-DGU_GOP_EXPERIMENT",
      "-D__KERNEL__",
      "-DGU_UEFI_PORT",
      "-DGU_TARGET_PCI_ID=0x220610deU",
      ...appIncludes,
    ],
  )
  await compileAndRun(
    "gop-ssd-source",
    ["src/gop/test_quiet_io.c", "src/unlock/format.c", "src/unlock/sha256.c"],
    [
      "-std=c11",
      "-O2",
      "-Wall",
      "-Wextra",
      "-Werror",
      "-DGU_GOP_EXPERIMENT",
      "-DGU_SSD_EXPERIMENT",
      ...appIncludes,
    ],
  )
  await compileAndRun(
    "gop-ssd-flow",
    ["src/gop/test_ssd_flow.c", "src/unlock/format.c"],
    [
      "-std=c11",
      "-O2",
      "-Wall",
      "-Wextra",
      "-Werror",
      "-DGC_FULL_FLR",
      "-DGC_FULL_UNLOCK",
      "-DGC_WINDOWS_HANDOFF",
      "-DGC_PRODUCTION",
      "-DGU_SSD_EXPERIMENT",
      ...includes,
    ],
  )
}

async function writeExternalProfileFixtures() {
  for (const family of profileFamilies) {
    const fixture = `build/runtime-profile-${family}`
    await pythonScript(
      "tools/test_probe_profile.py",
      "--write-fixture",
      fixture,
      family,
    )
    await pythonScript(
      "-c",
      `import sys;sys.path.insert(0,'tools');from runtime_profile import encode;from check_profile import check;from pathlib import Path;p=Path('${fixture}');(p/'PROFILE.BIN').write_bytes(encode(check(p/'board.json',experimental=True)))`,
    )
  }
}

async function gopExternalProfile() {
  await run(["mkdir", "-p", "build/gop-experiment"])
  await writeExternalProfileFixtures()
  await compileAndRun(
    "external-profile",
    [
      "src/unlock/tests/test_runtime_profile.c",
      "src/unlock/runtime_profile.c",
      "src/unlock/sha256.c",
      "src/unlock/checks.c",
      "src/unlock/port.c",
      "src/unlock/vendor/layout_610.c",
    ],
    [
      "-std=c11",
      "-O2",
      "-Wall",
      "-Wextra",
      "-Werror",
      "-DGU_RUNTIME_PROFILE_BUILD",
      ...includes,
    ],
    profileFamilies.map(
      (family) => `build/runtime-profile-${family}/PROFILE.BIN`,
    ),
  )
  await compileAndRun(
    "external-profile-flow",
    [
      "src/gop/test_external_flow.c",
      "src/unlock/format.c",
      "src/unlock/runtime_profile.c",
      "src/unlock/sha256.c",
    ],
    [
      "-std=c11",
      "-O2",
      "-Wall",
      "-Wextra",
      "-Werror",
      "-DGC_FULL_FLR",
      "-DGC_FULL_UNLOCK",
      "-DGC_WINDOWS_HANDOFF",
      "-DGC_PRODUCTION",
      "-DGU_SSD_EXPERIMENT",
      "-DGU_RUNTIME_PROFILE_BUILD",
      ...includes,
    ],
    ["build/runtime-profile-GA102/PROFILE.BIN"],
  )
}

async function gopAutoProfile() {
  await gopExternalProfile()
  await compileAndRun(
    "auto-profile-encode",
    [
      "src/gop/test_auto_profile.c",
      "src/probe/probe_profile.c",
      "src/probe/probe_core.c",
      "src/unlock/sha256.c",
    ],
    ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", ...includes],
    profileFamilies.map(
      (family) => `build/runtime-profile-${family}/PROFILE.BIN`,
    ),
  )
  await compileAndRun(
    "auto-profile-capture",
    [
      "src/gop/test_auto_capture.c",
      "src/probe/probe_app.c",
      "src/probe/probe_core.c",
      "src/probe/probe_profile.c",
      "src/unlock/profile_probe.c",
      "src/unlock/runtime_profile.c",
      "src/unlock/sha256.c",
    ],
    [
      "-std=c11",
      "-O2",
      "-Wall",
      "-Wextra",
      "-Werror",
      "-D_CRT_SECURE_NO_WARNINGS",
      ...includes,
    ],
  )
  await pythonScript(
    "-c",
    "import sys;sys.path.insert(0,'tools');from pathlib import Path;from probe_profile import generate;from runtime_profile import encode;assert encode(generate('build/auto-profile-report.txt','build/auto-profile-prom.bin'))==Path('build/auto-profile.bin').read_bytes();print('PASS real C capture/profile equals Python output')",
  )
  await compileAndRun(
    "auto-profile-flow",
    [
      "src/gop/test_auto_flow.c",
      "src/probe/probe_core.c",
      "src/unlock/runtime_profile.c",
      "src/unlock/format.c",
      "src/unlock/sha256.c",
    ],
    [
      "-std=c11",
      "-O2",
      "-Wall",
      "-Wextra",
      "-Werror",
      "-DGC_FULL_FLR",
      "-DGC_FULL_UNLOCK",
      "-DGC_WINDOWS_HANDOFF",
      "-DGC_PRODUCTION",
      "-DGU_SSD_EXPERIMENT",
      "-DGU_RUNTIME_PROFILE_BUILD",
      "-DGU_AUTO_PROFILE",
      ...includes,
    ],
    ["build/runtime-profile-GA102/PROFILE.BIN"],
  )
}

async function pythonTests() {
  await pythonScript(
    "-m",
    "unittest",
    "discover",
    "-s",
    "tools",
    "-p",
    "test_*.py",
    "-v",
  )
}

async function transaction() {
  await compileAndRun(
    "transaction-3080",
    [
      "src/unlock/tests/test_transaction_3080.c",
      "src/unlock/vendor/prep_610.c",
      "src/unlock/vendor/layout_610.c",
      "src/unlock/vendor/vbios_610.c",
      "src/unlock/vendor/gfw_eval.c",
    ],
    [
      "-std=c11",
      "-O1",
      "-Wall",
      "-Wextra",
      "-Werror",
      "-Wno-unused-parameter",
      "-Wno-sign-compare",
      "-D_CRT_SECURE_NO_WARNINGS",
      "-D__KERNEL__",
      "-DGU_TARGET_PCI_ID=0x220610deu",
      "-I",
      "src/unlock/tests/transaction_shim",
    ],
    [fixtureProm, fixtureAssets],
  )
}

async function sha() {
  await compileAndRun(
    "sha256",
    ["src/unlock/tests/test_sha256.c", "src/unlock/sha256.c"],
    [
      "-std=c11",
      "-O2",
      "-Wall",
      "-Wextra",
      "-Werror",
      "-D_CRT_SECURE_NO_WARNINGS",
      ...includes,
    ],
    [`${fixtureAssets}/gsp_ga10x.bin`, fixtureProm],
  )
  await run([
    cc(),
    "-target",
    "x86_64-pc-windows-msvc",
    "-std=c11",
    "-Os",
    "-ffreestanding",
    "-fno-builtin",
    "-fno-stack-protector",
    "-fshort-wchar",
    "-mno-red-zone",
    "-Wall",
    "-Wextra",
    "-Werror",
    ...includes,
    "-c",
    "src/unlock/sha256.c",
    "-o",
    `${outputDir}/sha-freestanding.obj`,
  ])
}

async function fwsecSequence() {
  await pythonScript("src/unlock/tests/check_fwsec_sequence.py", "--prepare")
  const shared = [
    "src/unlock/tests/test_fwsec_trace.c",
    "src/unlock/port.c",
    "src/unlock/compat.c",
    "src/unlock/vendor/prep_610.c",
    "src/unlock/vendor/layout_610.c",
    "src/unlock/vendor/vbios_610.c",
    "src/unlock/vendor/gfw_eval.c",
  ]
  const flags = [
    "-std=c11",
    "-O1",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-Wno-unused-parameter",
    "-D_CRT_SECURE_NO_WARNINGS",
    "-D__KERNEL__",
    "-DGU_UEFI_PORT",
    "-DGU_TARGET_PCI_ID=0x220610deU",
    "-I",
    "src/unlock/vendor",
    ...appIncludes,
  ]
  await compileAndRun(
    "fwsec-current-trace",
    [...shared, "src/unlock/vendor/unlock_610.c"],
    flags,
    [fixtureProm, fixtureAssets, "build/fwsec-current.trace"],
  )
  await compileAndRun(
    "fwsec-v06-trace",
    [...shared, "build/reference-v06/vendor/unlock_610.c"],
    flags,
    [fixtureProm, fixtureAssets, "build/fwsec-v06.trace"],
  )
  await pythonScript("src/unlock/tests/check_fwsec_sequence.py")
}

async function efiTransaction() {
  await compileAndRun(
    "efi-transaction-3080",
    [
      "src/unlock/tests/test_efi_transaction.c",
      "src/unlock/port.c",
      "src/unlock/compat.c",
      "src/unlock/vendor/unlock_610.c",
      "src/unlock/vendor/prep_610.c",
      "src/unlock/vendor/layout_610.c",
      "src/unlock/vendor/vbios_610.c",
      "src/unlock/vendor/gfw_eval.c",
    ],
    [
      "-std=c11",
      "-O1",
      "-Wall",
      "-Wextra",
      "-Werror",
      "-Wno-unused-parameter",
      "-D_CRT_SECURE_NO_WARNINGS",
      "-D__KERNEL__",
      "-DGU_UEFI_PORT",
      "-DGU_TARGET_PCI_ID=0x220610deU",
      ...appIncludes,
    ],
    [fixtureProm, fixtureAssets],
  )
  await fwsecSequence()
}

async function app() {
  await compileAndRun(
    "app",
    [
      "src/unlock/tests/test_app.c",
      "src/unlock/app.c",
      "src/unlock/app_io.c",
      "src/unlock/ownership.c",
      "src/unlock/format.c",
      "src/unlock/sha256.c",
      "src/unlock/checks.c",
      "src/unlock/port.c",
      "src/unlock/compat.c",
      "src/unlock/vendor/unlock_610.c",
      "src/unlock/vendor/prep_610.c",
      "src/unlock/vendor/layout_610.c",
      "src/unlock/vendor/vbios_610.c",
      "src/unlock/vendor/gfw_eval.c",
    ],
    [
      "-std=c11",
      "-O1",
      "-Wall",
      "-Wextra",
      "-Werror",
      "-Wno-unused-parameter",
      "-Wno-unused-function",
      "-D_CRT_SECURE_NO_WARNINGS",
      "-D__KERNEL__",
      "-DGU_UEFI_PORT",
      "-DGU_TARGET_PCI_ID=0x220610deU",
      ...appIncludes,
    ],
    [fixtureProm, fixtureAssets],
  )
}

function psQuote(value: string) {
  return `'${value.replaceAll("'", "''")}'`
}

async function windowsCheck() {
  if (process.platform !== "win32") {
    throw new Error("The Windows result collector scenarios require Windows.")
  }
  const powershell = Bun.which("powershell.exe") ?? Bun.which("pwsh")
  if (!powershell)
    throw new Error("Install Windows PowerShell or PowerShell 7.")

  const base = resolve(root, "experiments/gop/windows-check.ps1")
  const temporaryParent = resolve(root, "build/gop-experiment")
  await mkdir(temporaryParent, { recursive: true })
  const temporary = await mkdtemp(join(temporaryParent, "windows-check-"))
  try {
    const cases = [
      "valid",
      "3090ti",
      "wrong-pci",
      "bad-result",
      "failed",
      "invalid",
    ]
    const valid =
      '{"gpu":"NVIDIA GeForce RTX 3080","pci":"0000:01:00.0","benchmarks":[{"reference_checked":true,"gflops":1},{"reference_checked":true,"gflops":2},{"reference_checked":true,"gflops":3}]}'
    const noBenchmark = join(temporary, "no-benchmark")
    const script = join(temporary, "scenario.ps1")
    const preamble = `$ErrorActionPreference='Stop'\nfunction Get-CimInstance($ClassName) { if ($ClassName -eq 'Win32_OperatingSystem') { return [pscustomobject]@{LastBootUpTime=[datetime]'2026-10-21T01:00:00'} }; return [pscustomobject]@{Name='fixture';DriverVersion='test';PNPDeviceID='test';Status='OK';ConfigManagerErrorCode=0} }\nfunction Get-Command($Name) { if ($Name -ne 'nvidia-smi.exe') { throw 'Unexpected command lookup' } }\n`
    await writeFile(
      script,
      `${preamble}& ${psQuote(base)} -Output ${psQuote(noBenchmark)}\n$summary=Get-Content -Raw ${psQuote(join(noBenchmark, "summary.json"))} | ConvertFrom-Json\nif ($summary.BenchmarkState -ne 'not-run') { throw 'Missing benchmark was reported as run' }\n`,
    )
    await run([
      powershell,
      "-NoProfile",
      "-ExecutionPolicy",
      "Bypass",
      "-File",
      script,
    ])

    for (const name of cases) {
      const fake = join(temporary, `${name}.cmd`)
      const payload =
        name === "3090ti"
          ? valid.replace("RTX 3080", "RTX 3090 Ti")
          : name === "wrong-pci"
            ? valid.replace("0000:01:00.0", "0000:02:00.0")
            : name === "bad-result"
              ? valid.replace(
                  '"reference_checked":true',
                  '"reference_checked":false',
                )
              : valid
      const body =
        name === "failed"
          ? "@exit /b 3\r\n"
          : name === "invalid"
            ? "@echo invalid\r\n@exit /b 0\r\n"
            : `@echo ${payload}\r\n@exit /b 0\r\n`
      const output = join(temporary, name)
      await writeFile(fake, body, "ascii")
      await writeFile(
        script,
        `${preamble}& ${psQuote(base)} -Benchmark ${psQuote(fake)} -Output ${psQuote(output)}\n$summary=Get-Content -Raw ${psQuote(join(output, "summary.json"))} | ConvertFrom-Json\n$expected=if ('${name}' -in @('valid','3090ti')) {'passed'} else {'failed'}\nif ($summary.BenchmarkState -ne $expected -or -not $summary.BenchmarkSha256) { throw 'Wrong result for ${name}' }\n`,
      )
      await run([
        powershell,
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        script,
      ])
    }
    console.log(
      "PASS 7 Windows result collection scenarios; device queries and benchmark processes simulated",
    )
  } finally {
    const temporaryRelative = relative(temporaryParent, resolve(temporary))
    if (temporaryRelative.startsWith("..") || isAbsolute(temporaryRelative)) {
      throw new Error(
        "Refusing to clean a Windows-check fixture outside its temp folder.",
      )
    }
    await rm(temporary, { recursive: true, force: true })
  }
}

const availableSuites: Record<string, () => Promise<void>> = {
  "probe-build": buildProbe,
  "gop-experiment-build": buildGopExperiment,
  "unlock-build": buildUnlock,
  probe,
  "profile-runtime": profileRuntime,
  port,
  checks,
  runtime,
  "gop-cycle": gopCycle,
  "gop-patch-state": gopPatchState,
  "gop-visual": gopVisual,
  "gop-windows": gopWindows,
  "gop-production": gopProduction,
  "gop-ssd": gopSsd,
  "gop-external-profile": gopExternalProfile,
  "auto-profile-runtime": gopAutoProfile,
  "probe-profile": () => pythonScript("tools/test_probe_profile.py"),
  "external-profile-format": () =>
    pythonScript("tools/test_runtime_profile.py"),
  pe: () => run([process.execPath, "tools/test_pe.ts"]),
  python: pythonTests,
  "windows-check": windowsCheck,
}

const localSuites: Record<string, () => Promise<void>> = {
  transaction,
  sha,
  "efi-transaction": efiTransaction,
  app,
  "fwsec-sequence": fwsecSequence,
}

const defaultSuites = [
  "probe-build",
  "gop-experiment-build",
  "unlock-build",
  "gop-cycle",
  "probe",
  "probe-profile",
  "profile-runtime",
  "external-profile-format",
  "auto-profile-runtime",
  "gop-production",
  "gop-ssd",
  "port",
  "checks",
  "runtime",
  "pe",
]

const args = process.argv.slice(2)
const withLocalFixtures = args.includes("--with-local-fixtures")
const requested = args.filter(
  (argument) => argument !== "--with-local-fixtures",
)
if (requested.length > 1)
  throw new Error("Usage: bun tools/test.ts [suite] [--with-local-fixtures]")
const selected = requested[0] ?? "all"
let suites: string[]
if (selected === "all") {
  suites = [...defaultSuites]
  if (withLocalFixtures) suites.push(...Object.keys(localSuites))
} else if (selected === "python") {
  suites = ["python"]
} else if (selected === "gop") {
  suites = [
    "gop-cycle",
    "gop-patch-state",
    "gop-visual",
    "gop-windows",
    "gop-production",
    "gop-ssd",
    "gop-external-profile",
    "auto-profile-runtime",
  ]
} else if (selected === "local") {
  if (!withLocalFixtures) {
    throw new Error("Local fixture suites require --with-local-fixtures.")
  }
  suites = Object.keys(localSuites)
} else if (selected in availableSuites) {
  suites = [selected]
} else {
  throw new Error(`Unknown test suite: ${selected}`)
}

const allSuites = { ...availableSuites, ...localSuites }
const logDirectory = resolve(root, "unlock/build")
await mkdir(logDirectory, { recursive: true })
await run(["mkdir", "-p", outputDir])
const results: {
  Name: string
  Command: string
  ExitCode: number
  Log: string
}[] = []

for (const name of suites) {
  const logParts: string[] = []
  setOutputHandler((text) => logParts.push(text))
  let exitCode = 0
  try {
    const suite = allSuites[name]
    if (!suite) throw new Error(`Unknown test suite: ${name}`)
    await suite()
  } catch (error) {
    exitCode = 1
    const message = error instanceof Error ? error.message : String(error)
    logParts.push(`${message}\n`)
    console.error(message)
  } finally {
    setOutputHandler(undefined)
  }
  const log = `${name}.log`
  await writeFile(join(logDirectory, log), logParts.join(""), "utf8")
  const command = localSuites[name]
    ? "bun tools/test.ts local --with-local-fixtures"
    : `bun tools/test.ts ${name}`
  results.push({
    Name: name,
    Command: command,
    ExitCode: exitCode,
    Log: log,
  })
  console.log(`${name}: exit ${exitCode}`)
}

const summary = {
  AllPassed: results.every((result) => result.ExitCode === 0),
  HardwareAccess: false,
  WithLocalFixtures: withLocalFixtures,
  Results: results,
}
await writeFile(
  join(logDirectory, "test-results.json"),
  `${JSON.stringify(summary, null, 2)}\n`,
  "utf8",
)
if (!summary.AllPassed) process.exitCode = 1
else {
  console.log(
    `All ${suites.length} selected verification groups passed; hardware was simulated.`,
  )
}
