import { resolve } from "node:path"
import { clang, lldLink, requireTool, root, run } from "./test_support"

const compiler = requireTool(clang, "clang")
const linker = requireTool(lldLink, "lld-link")
const output = "build/probe"
const sources = [
  "src/probe/probe_app",
  "src/probe/probe_core",
  "src/probe/probe_profile",
  "src/probe/efi_runtime",
  "src/unlock/sha256",
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
console.log(
  `Built read-only probe EFI: ${resolve(root, output, "BOOTX64.EFI")}`,
)
