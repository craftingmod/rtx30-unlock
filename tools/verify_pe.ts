import { createHash } from "node:crypto"
import { readFile, writeFile } from "node:fs/promises"
import { basename } from "node:path"

export interface PeVerification {
  path: string
  bytes: number
  sha256: string
  machine: "AMD64"
  subsystem: 10
  entry: "efi_main"
  entry_rva: number
  imports: 0
  iat: 0
  delay_imports: 0
  nx: true
  dynamic_base: true
}

function requirePe(condition: boolean, message: string) {
  if (!condition) throw new Error(message)
}

function requireRange(data: Buffer, offset: number, size: number) {
  requirePe(
    Number.isSafeInteger(offset) &&
      Number.isSafeInteger(size) &&
      offset >= 0 &&
      size >= 0 &&
      offset + size <= data.length,
    "truncated PE file",
  )
}

function u16(data: Buffer, offset: number) {
  requireRange(data, offset, 2)
  return data.readUInt16LE(offset)
}

function u32(data: Buffer, offset: number) {
  requireRange(data, offset, 4)
  return data.readUInt32LE(offset)
}

export async function inspectPe(
  efiPath: string,
  mapPath: string,
): Promise<PeVerification> {
  const data = await readFile(efiPath)
  requirePe(data.toString("ascii", 0, 2) === "MZ", "DOS signature")
  const pe = u32(data, 0x3c)
  requirePe(data.toString("ascii", pe, pe + 4) === "PE\0\0", "PE signature")
  requirePe(u16(data, pe + 4) === 0x8664, "must be AMD64")

  const optional = pe + 24
  requirePe(u16(data, optional) === 0x20b, "must be PE32+")
  requirePe(u16(data, optional + 68) === 10, "must be EFI application")
  const entry = u32(data, optional + 16)
  requirePe(entry !== 0, "entry point missing")
  requireRange(data, optional + 24, 8)
  requirePe(data.readBigUInt64LE(optional + 24) === 0n, "expected image base 0")
  requirePe(
    (u16(data, optional + 70) & 0x140) === 0x140,
    "ASLR and NX flags required",
  )
  requirePe(u32(data, optional + 108) >= 16, "data directory missing")

  for (const [index, name] of [
    [1, "imports"],
    [12, "IAT"],
    [13, "delay imports"],
  ] as const) {
    const directory = optional + 112 + 8 * index
    requirePe(
      u32(data, directory) === 0 && u32(data, directory + 4) === 0,
      name,
    )
  }
  requirePe(u32(data, optional + 112 + 8 * 5 + 4) !== 0, "relocations missing")

  const table = optional + u16(data, pe + 20)
  let executableEntry = false
  for (let index = 0; index < u16(data, pe + 6); index++) {
    const section = table + 40 * index
    const virtualSize = u32(data, section + 8)
    const virtualAddress = u32(data, section + 12)
    if (virtualAddress <= entry && entry < virtualAddress + virtualSize) {
      executableEntry = (u32(data, section + 36) & 0x20000000) !== 0
    }
  }
  requirePe(executableEntry, "entry must be in executable section")

  const map = await readFile(mapPath, "utf8")
  const found =
    /^\s*[0-9A-Fa-f]+:[0-9A-Fa-f]+\s+efi_main\s+([0-9A-Fa-f]+)/m.exec(map)
  requirePe(
    found !== null && Number.parseInt(found[1], 16) === entry,
    "entry does not match efi_main map symbol",
  )

  return {
    path: basename(efiPath),
    bytes: data.length,
    sha256: createHash("sha256").update(data).digest("hex"),
    machine: "AMD64",
    subsystem: 10,
    entry: "efi_main",
    entry_rva: entry,
    imports: 0,
    iat: 0,
    delay_imports: 0,
    nx: true,
    dynamic_base: true,
  }
}

async function main() {
  const [efiPath, mapPath, ...options] = process.argv.slice(2)
  if (
    !efiPath ||
    !mapPath ||
    options.length > 2 ||
    (options.length > 0 && (options.length !== 2 || options[0] !== "--output"))
  ) {
    throw new Error(
      "Usage: bun tools/verify_pe.ts <efi> <map> [--output <json>]",
    )
  }

  const result = await inspectPe(efiPath, mapPath)
  const json = `${JSON.stringify(result, null, 2)}\n`
  if (options.length === 2) await writeFile(options[1], json, "utf8")
  process.stdout.write(`${JSON.stringify(result)}\n`)
}

if (import.meta.main) {
  main().catch((error: unknown) => {
    const message = error instanceof Error ? error.message : String(error)
    console.error(`PE verification failed: ${message}`)
    process.exitCode = 1
  })
}
