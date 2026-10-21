// SPDX-License-Identifier: GPL-2.0-only
import { createHash } from "node:crypto"
import { createReadStream } from "node:fs"
import manifest from "./artifacts-610.43.03.json"

export type Artifact = {
  filename: string
  sha256: string
  url: string
  notice?: { path: string; sha256: string }
}

export type FirmwareManifest = {
  source_artifacts: Artifact[]
  derived_assets: Record<string, string>
}

export async function loadManifest(): Promise<FirmwareManifest> {
  return manifest as FirmwareManifest
}

export function sha256(data: Uint8Array): string {
  return createHash("sha256").update(data).digest("hex")
}

export async function sha256File(path: string): Promise<string> {
  const hash = createHash("sha256")
  for await (const block of createReadStream(path)) hash.update(block)
  return hash.digest("hex")
}

export async function loadTarArchive(
  archivePath: string,
): Promise<Bun.Archive> {
  const compressed = await Bun.file(archivePath).bytes()
  let tarball = compressed
  if (archivePath.endsWith(".zst")) {
    tarball = (await Bun.zstdDecompress(compressed)) as Uint8Array<ArrayBuffer>
  } else if (archivePath.endsWith(".xz")) {
    // xz-compat can install optional native bindings; use its JS decoder only.
    process.env.LZMA_NATIVE_DISABLE = "1"
    const { decodeXZ } = await import("xz-compat")
    const decoded = await decodeXZ(Buffer.from(compressed))
    tarball = (
      Buffer.isBuffer(decoded) ? decoded : decoded.toBuffer()
    ) as Uint8Array<ArrayBuffer>
  }
  return new Bun.Archive(tarball)
}

export async function readTarMember(
  archive: Bun.Archive,
  memberPattern: string,
  maxBytes = 128 * 1024 * 1024,
): Promise<Buffer> {
  const matches = await archive.files(memberPattern)
  if (matches.size !== 1) {
    throw new Error(
      `archive member pattern must match exactly once: ${memberPattern}`,
    )
  }
  const member = matches.values().next().value
  if (!member) throw new Error(`missing archive member: ${memberPattern}`)
  if (member.size > maxBytes) {
    throw new Error(`archive member exceeds extraction limit: ${memberPattern}`)
  }
  return Buffer.from(await member.arrayBuffer())
}
