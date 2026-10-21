import { createHash } from "node:crypto"
import { readdir } from "node:fs/promises"
import { relative, resolve } from "node:path"
import { clang, lldLink, requireTool, root, run } from "./test_support"

const compiler = requireTool(clang, "clang")
const linker = requireTool(lldLink, "lld-link")
const output = "build/gop-experiment"
const sources = [
  "src/gop/app",
  "src/gop/cycle",
  "src/unlock/runtime",
  "src/unlock/format",
]
const flags = [
  "-target",
  "x86_64-pc-windows-msvc",
  "-std=c11",
  "-ffreestanding",
  "-fno-builtin",
  "-fno-stack-protector",
  "-mno-red-zone",
  "-fshort-wchar",
  "-Os",
  "-Wall",
  "-Wextra",
  "-Werror",
  "-I",
  "deps/edk2/MdePkg/Include",
  "-I",
  "deps/edk2/MdePkg/Include/X64",
]

async function filesUnder(directory: string): Promise<string[]> {
  const files: string[] = []
  for (const entry of await readdir(resolve(root, directory), {
    withFileTypes: true,
  })) {
    const path = `${directory}/${entry.name}`
    files.push(...(entry.isDirectory() ? await filesUnder(path) : [path]))
  }
  return files
}

async function sha256(path: string) {
  const data = new Uint8Array(await Bun.file(resolve(root, path)).arrayBuffer())
  return createHash("sha256").update(data).digest("hex").toUpperCase()
}

await run(["mkdir", "-p", output])
const objects: string[] = []
for (const source of sources) {
  const object = `${output}/${source.replaceAll("/", "_")}.obj`
  await run([compiler, ...flags, "-c", `${source}.c`, "-o", object])
  objects.push(object)
}
await run([
  linker,
  "/nologo",
  "/nodefaultlib",
  "/subsystem:efi_application",
  "/entry:efi_main",
  "/machine:x64",
  "/base:0",
  "/dynamicbase",
  "/nxcompat",
  "/timestamp:0",
  "/opt:ref",
  "/opt:icf",
  `/map:${output}/BOOTX64.map`,
  `/out:${output}/BOOTX64.EFI`,
  ...objects,
])
await run([
  process.execPath,
  "tools/verify_pe.ts",
  `${output}/BOOTX64.EFI`,
  `${output}/BOOTX64.map`,
  "--output",
  `${output}/pe-verification.json`,
])

const records = [
  ...sources.map((source) => `${source}.c`),
  "src/gop/cycle.h",
  "src/gop/tree.h",
  "src/gop/test_tree.c",
  "src/gop/leaf.h",
  "src/gop/test_leaf.c",
  "src/gop/full.h",
  "src/gop/reset.h",
  "src/gop/test_full.c",
  "src/gop/patch.h",
  "src/gop/visual.h",
  "src/gop/test_patch.c",
  "src/gop/test_patch_state.c",
  "src/gop/test_cycle.c",
  "src/gop/test_io.c",
  "tools/build-gop-experiment.ts",
  "tools/build-probe.ts",
  "tools/build.ts",
  "tools/test.ts",
  "tools/test_support.ts",
  "src/unlock/format.h",
  "src/unlock/runtime.h",
  "tools/verify_pe.ts",
  "src/unlock/boot_source.h",
  "src/gop/cycle.c",
  "src/gop/tree.c",
  "src/gop/leaf.c",
  "src/gop/full.c",
  "src/gop/reset.c",
  ...(await filesUnder("deps/edk2/MdePkg/Include")),
]
const uniqueRecords = [...new Set(records)].sort()
const sourceRecords = await Promise.all(
  uniqueRecords.map(async (path) => ({
    Path: path,
    Sha256: await sha256(path),
  })),
)
const efiSha256 = await sha256(`${output}/BOOTX64.EFI`)
await Bun.write(
  resolve(root, output, "build-manifest.json"),
  `${JSON.stringify(
    {
      BuiltUtc: new Date().toISOString(),
      HardwareTested: false,
      WindowsHandoff: false,
      Production: false,
      SsdExperiment: false,
      ExternalProfile: false,
      AutoProfile: false,
      Mode: "cycle",
      EfiSha256: efiSha256,
      Sources: sourceRecords,
    },
    null,
    2,
  )}\n`,
)
console.log(
  `Built GOP cycle experiment: ${relative(root, resolve(root, output, "BOOTX64.EFI"))}`,
)
