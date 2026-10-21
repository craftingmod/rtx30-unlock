#!/usr/bin/env bun
// SPDX-License-Identifier: GPL-2.0-only
import { inflateRawSync } from "node:zlib"
import { mkdir, readFile, stat, writeFile } from "node:fs/promises"
import { join } from "node:path"
import {
  loadManifest,
  loadTarArchive,
  readTarMember,
  sha256,
} from "./common.ts"

const MAGIC = 0x314c5442
const PINNED: Record<string, string> = {
  "g_bindata_kgspGetBinArchiveBooterLoadUcode_GA102.c":
    "04cd3db7f670adc4efd79d2e76217bb809fffddfbd6f1af884cde2af26dd39af",
  "g_bindata_kgspGetBinArchiveGspRmBoot_GA102.c":
    "35e5aa9075ad3898b6022ffd5820c953d809caa7dabd530cfc1a83dc2a67c5bd",
}

function lastMatches(pattern: RegExp, text: string): string[] {
  return [...text.matchAll(pattern)].map((match) => match[1]!)
}

function crc32(data: Uint8Array): number {
  let crc = 0xffffffff
  for (const byte of data) {
    crc ^= byte
    for (let bit = 0; bit < 8; bit++) {
      crc = (crc >>> 1) ^ (crc & 1 ? 0xedb88320 : 0)
    }
  }
  return (crc ^ 0xffffffff) >>> 0
}

function arrayBytes(
  text: string,
  label: string,
): { data: Buffer; compressed: boolean; expected: number } {
  const marker = `BINDATA_LABEL_${label}_data[]`
  const occurrences = text.split(marker).length - 1
  if (occurrences !== 1) throw new Error(`missing or duplicate ${label}`)
  const start = text.indexOf(marker)
  const window = text.slice(Math.max(0, start - 400), start)
  const compression = lastMatches(/COMPRESSION: (YES|NO)/g, window)
  const sizes = lastMatches(/DATA SIZE \(bytes\): (\d+)/g, window)
  if (!compression.length || !sizes.length) {
    throw new Error(`missing storage declaration for ${label}`)
  }
  const expected = Number(sizes.at(-1))
  const compressed = compression.at(-1) === "YES"
  if (
    !Number.isSafeInteger(expected) ||
    expected <= 0 ||
    expected > 16 * 1024 * 1024
  ) {
    throw new Error("asset exceeds extraction limit")
  }

  const declaration = new RegExp(
    `${marker.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")}\\s*=\\s*\\{([^}]*)\\};`,
    "s",
  ).exec(text.slice(start))
  if (!declaration) throw new Error("malformed byte-array declaration")
  const tokens = declaration[1]!.trim().replace(/,+$/, "").split(",")
  if (tokens.some((token) => !/^\s*0x[0-9a-fA-F]{2}\s*$/.test(token))) {
    throw new Error("non-byte token in generated array")
  }
  const raw = Buffer.from(
    tokens.map((token) => Number.parseInt(token.trim(), 16)),
  ) as Buffer<ArrayBuffer>
  let data = raw

  if (compressed) {
    const stored = lastMatches(/COMPRESSED SIZE \(bytes\): (\d+)/g, window)
    if (!stored.length || raw.length !== Number(stored.at(-1))) {
      throw new Error("compressed storage size mismatch")
    }
    if (raw.length < 8) throw new Error("truncated compressed stream")
    const compressedSize = raw.length - 8
    const result = inflateRawSync(raw.subarray(0, compressedSize), {
      info: true,
      maxOutputLength: expected + 1,
    }) as unknown as { buffer: Buffer; engine: { bytesWritten: number } }
    data = Buffer.from(result.buffer)
    if (result.engine.bytesWritten !== compressedSize) {
      throw new Error("trailing or malformed compressed stream")
    }

    const trailer = raw.subarray(compressedSize)

    if (
      trailer.readUInt32LE(0) !== crc32(data) ||
      trailer.readUInt32LE(4) !== data.length
    ) {
      throw new Error("compressed checksum or size mismatch")
    }
  }

  if (data.length !== expected) {
    throw new Error(
      `${label}: inflated ${data.length} bytes, header says ${expected}`,
    )
  }
  return { data, compressed, expected }
}

function words(data: Buffer, count: number): number[] {
  if (data.length !== count * 4) throw new Error("unexpected record size")
  return Array.from({ length: count }, (_, index) =>
    data.readUInt32LE(index * 4),
  )
}

function word(value: number): Buffer {
  const result = Buffer.alloc(4)
  result.writeUInt32LE(value)
  return result
}

function buildBundle(source: string): Buffer {
  const image = arrayBytes(source, "IMAGE_PROD").data
  const header = arrayBytes(source, "HEADER_PROD").data
  const signatures = arrayBytes(source, "SIG_PROD").data
  const patchLoc = arrayBytes(source, "PATCH_LOC").data
  const patchMeta = arrayBytes(source, "PATCH_META").data
  const numSigs = arrayBytes(source, "NUM_SIGS").data
  if (
    header.length !== 36 ||
    patchLoc.length !== 4 ||
    patchMeta.length !== 12 ||
    numSigs.length !== 4
  ) {
    throw new Error("unexpected BooterLoad record size")
  }
  const fields = words(header, 9)
  const loc = patchLoc.readUInt32LE(0)
  const [, engine, ucode] = words(patchMeta, 3)
  const count = numSigs.readUInt32LE(0)
  if (
    !count ||
    signatures.length !== count * 384 ||
    fields[4] !== 1 ||
    !ucode
  ) {
    throw new Error("unsupported production Booter signature/application table")
  }
  if (
    loc < fields[2]! ||
    loc + 384 > fields[2]! + fields[3]! ||
    fields[2]! + fields[3]! > image.length
  ) {
    throw new Error("signature patch is outside DMEM image")
  }
  for (const [offset, size] of [
    [fields[0]!, fields[1]!],
    [fields[2]!, fields[3]!],
    [fields[5]!, fields[6]!],
  ]) {
    if (offset + size > image.length || offset % 256 || size % 256) {
      throw new Error("invalid Falcon transfer range")
    }
  }

  const headerWords = [
    ...fields,
    loc,
    ucode!,
    engine!,
    count,
    signatures.length,
  ]
  const bundleHeader = Buffer.concat(headerWords.map(word))
  return Buffer.concat([
    word(MAGIC),
    word(1),
    bundleHeader,
    word(image.length),
    word(signatures.length),
    image,
    signatures,
  ])
}

async function extractAssets(
  sourcePath: string,
  output: string,
): Promise<void> {
  const sourceInfo = await stat(sourcePath)
  let archive: Bun.Archive | undefined
  if (!sourceInfo.isDirectory()) {
    if (!sourcePath.toLowerCase().endsWith(".tar.xz")) {
      throw new Error("source must be a directory or a .tar.xz archive")
    }
    archive = await loadTarArchive(sourcePath)
  }

  const inputs: Record<string, string> = {}
  for (const [name, digest] of Object.entries(PINNED)) {
    const data = archive
      ? await readTarMember(archive, `**/src/nvidia/generated/${name}`)
      : await readFile(join(sourcePath, "src/nvidia/generated", name))
    if (sha256(data) !== digest)
      throw new Error(`pinned generated source mismatch: ${name}`)
    inputs[name] = data.toString("ascii")
  }

  const booter = buildBundle(
    inputs["g_bindata_kgspGetBinArchiveBooterLoadUcode_GA102.c"]!,
  )
  const boot = inputs["g_bindata_kgspGetBinArchiveGspRmBoot_GA102.c"]!
  const image = arrayBytes(boot, "UCODE_IMAGE_PROD").data
  const desc = arrayBytes(boot, "UCODE_DESC_PROD").data
  if (desc.length !== 84)
    throw new Error("unexpected bootloader descriptor length")
  const fields = words(desc, 21)
  if (
    fields[0] !== 5 ||
    fields[14] !== 1 ||
    fields[19] !== image.length ||
    fields[20] !== 0
  ) {
    throw new Error("unsupported bootloader descriptor")
  }
  for (const index of [1, 3, 8, 10, 12]) {
    if (
      !fields[index + 1] ||
      fields[index]! + fields[index + 1]! > image.length
    ) {
      throw new Error("bootloader descriptor range outside image")
    }
  }

  const blobs: Record<string, Buffer> = {
    "booter_load_ga102.bin": booter,
    "gsp_boot_ga102.bin": image,
    "gsp_boot_desc_ga102.bin": desc,
  }
  const manifest = await loadManifest()
  for (const [name, data] of Object.entries(blobs)) {
    if (sha256(data) !== manifest.derived_assets[name]) {
      throw new Error(`derived asset differs from its pinned SHA-256: ${name}`)
    }
  }

  await mkdir(output, { recursive: true })
  await Promise.all(
    Object.entries(blobs).map(([name, data]) =>
      writeFile(join(output, name), data),
    ),
  )
}

export async function extractBootAssets(args: string[]): Promise<void> {
  const [sourcePath, output] = args
  if (!sourcePath || !output || args.length !== 2) {
    throw new Error(
      "Usage: bun tools/firmware/extract_boot_assets.ts SOURCE_DIR|SOURCE.tar.xz OUTPUT",
    )
  }
  await extractAssets(sourcePath, output)
}

if (import.meta.main)
  extractBootAssets(process.argv.slice(2)).catch((error: unknown) => {
    console.error(
      `extract failed: ${error instanceof Error ? error.message : error}`,
    )
    process.exitCode = 1
  })
