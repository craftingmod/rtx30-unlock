import { spawnSync } from "node:child_process"
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs"
import { join, resolve } from "node:path"

const root = resolve(import.meta.dir, "..")
const build = resolve(root, "build/efi")
const efi = resolve(build, "BOOTX64.EFI")
const map = resolve(build, "BOOTX64.map")
const verifier = resolve(import.meta.dir, "verify_pe.ts")
const original = readFileSync(efi)
const pe = original.readUInt32LE(0x3c)

function runVerifier(path: string) {
  const result = spawnSync(process.execPath, [verifier, path, map], {
    encoding: "utf8",
  })
  if (result.error) throw result.error
  if (result.status === null)
    throw new Error("PE verifier did not exit normally")
  return result
}

if (runVerifier(efi).status !== 0) {
  throw new Error("PE verifier rejected the built EFI")
}

const scratch = mkdtempSync(join(build, "pe-negative-"))
try {
  for (const [name, offset, value, message] of [
    ["machine", pe + 4, Buffer.from([0, 0]), "must be AMD64"],
    ["subsystem", pe + 24 + 68, Buffer.from([3, 0]), "must be EFI application"],
    ["import", pe + 24 + 112 + 8, Buffer.from([1, 0, 0, 0]), "imports"],
  ] as const) {
    const changed = Buffer.from(original)
    value.copy(changed, offset)
    const path = join(scratch, `${name}.EFI`)
    writeFileSync(path, changed)
    const result = runVerifier(path)
    if (result.status === 0 || !result.stderr.includes(message)) {
      throw new Error(
        `PE verifier did not reject malformed ${name}: ${result.stderr}`,
      )
    }
    console.log(`PE verifier rejects malformed ${name}: PASS`)
  }
} finally {
  rmSync(scratch, { recursive: true, force: true })
}
