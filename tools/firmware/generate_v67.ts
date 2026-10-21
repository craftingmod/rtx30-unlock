#!/usr/bin/env bun
// SPDX-License-Identifier: GPL-2.0-only
import { mkdir, writeFile } from "node:fs/promises"
import { join } from "node:path"
import {
  loadManifest,
  loadTarArchive,
  readTarMember,
  sha256,
  sha256File,
} from "./common.ts"

const PACKAGE_SHA256 =
  "e17e7c92eeffa178b0fe1f40eb17a47095f8fc3ce4a77bf115cf5bd5a4df86e9"
const GSP_SHA256 =
  "572373620a37418f24dc16b5031c39338778c3257e48e8408de9a57291b24f3a"
const PAYLOAD_SHA256 =
  "2a0aa1f86d0b6afff5214d641d87d46dc98ef06ed908ff05d0ddbad5ffb7e197"
const PACKAGE_VERSION = "610.43.03-1"
const FIRMWARE_PREFIX = "usr/lib/firmware/nvidia/610.43.03/"
const PAYLOAD_SIZE = 0xfa00
const FILL_DWORD = 0x0000019c
const PATCHED_WORDS = new Map([
  [0x1100, 0x00000007],
  [0xf948, 0xffffffff],
  [0xf950, 0x00000d44],
  [0xf960, 0x00823804],
  [0xf968, 0x00001fce],
  [0xf974, 0x00000000],
  [0xf97c, 0x00001101],
  [0xf980, 0x000084c8],
  [0xf984, 0x00008e18],
  [0xf98c, 0x000084c8],
  [0xf990, 0x00000000],
  [0xf998, 0x00001fce],
  [0xf9a4, 0x0000ffbc],
  [0xf9ac, 0x00005789],
  [0xf9bc, 0x00000d44],
  [0xf9cc, 0x00000003],
  [0xf9d4, 0x00001fce],
  [0xf9e8, 0x00000d52],
  [0xf9ec, 0x000081ee],
])

function buildPayload(): Buffer {
  const payload = Buffer.alloc(PAYLOAD_SIZE)
  for (let offset = 0; offset < PAYLOAD_SIZE; offset += 4) {
    payload.writeUInt32LE(FILL_DWORD, offset)
  }
  for (const [offset, value] of PATCHED_WORDS) {
    payload.writeUInt32LE(value, offset)
  }
  return payload
}

export async function generateV67(args: string[]): Promise<void> {
  const [packagePath, output] = args
  if (!packagePath || !output || args.length !== 2) {
    throw new Error("Usage: bun tools/firmware/generate_v67.ts PACKAGE OUTPUT")
  }

  if ((await Bun.file(packagePath).exists()) === false) {
    throw new Error(`missing package: ${packagePath}`)
  }
  const packageHash = await sha256File(packagePath)
  if (packageHash !== PACKAGE_SHA256) {
    throw new Error(
      "package SHA-256 does not match the pinned nvidia-utils artifact",
    )
  }

  const archive = await loadTarArchive(packagePath)
  const pkginfo = (await readTarMember(archive, ".PKGINFO")).toString("utf8")
  if (!pkginfo.split(/\r?\n/).includes(`pkgver = ${PACKAGE_VERSION}`)) {
    throw new Error("package metadata version does not match 610.43.03-1")
  }

  const gsp = await readTarMember(archive, `${FIRMWARE_PREFIX}gsp_ga10x.bin`)
  if (sha256(gsp) !== GSP_SHA256) {
    throw new Error(
      "GA10x GSP firmware SHA-256 does not match the pinned artifact",
    )
  }

  const payload = buildPayload()
  if (sha256(payload) !== PAYLOAD_SHA256) {
    throw new Error("generated V67 image differs from its pinned SHA-256")
  }

  const manifest = await loadManifest()
  const notice = manifest.source_artifacts[2]?.notice
  if (!notice)
    throw new Error("manifest is missing the NVIDIA package license notice")
  const licenseText = await readTarMember(archive, notice.path)
  if (sha256(licenseText) !== notice.sha256) {
    throw new Error("NVIDIA license notice differs from pinned package")
  }

  await mkdir(output, { recursive: true })
  await Promise.all([
    writeFile(join(output, "gsp_ga10x.bin"), gsp),
    writeFile(join(output, "v67-ga102-610.43.03.bin"), payload),
    writeFile(join(output, "NVIDIA-LICENSE"), licenseText),
  ])
}

if (import.meta.main)
  generateV67(process.argv.slice(2)).catch((error: unknown) => {
    console.error(
      `generate failed: ${error instanceof Error ? error.message : error}`,
    )
    process.exitCode = 1
  })
