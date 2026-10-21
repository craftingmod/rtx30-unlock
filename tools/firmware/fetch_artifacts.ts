#!/usr/bin/env bun
// SPDX-License-Identifier: GPL-2.0-only
import { createHash, randomUUID } from "node:crypto"
import { lstat, mkdir, open, rename, chmod, unlink } from "node:fs/promises"
import { join } from "node:path"
import { loadManifest, sha256File, type Artifact } from "./common.ts"

const MAX_DOWNLOAD = 1024 * 1024 * 1024

function inputs(
  manifest: Awaited<ReturnType<typeof loadManifest>>,
): Artifact[] {
  const [source, , packageArtifact] = manifest.source_artifacts
  if (!source || !packageArtifact) {
    throw new Error(
      "manifest must include the source archive and firmware package",
    )
  }
  return [source, packageArtifact]
}

async function fetchArtifact(
  entry: Artifact,
  output: string,
  offline = false,
): Promise<string> {
  await mkdir(output, { recursive: true })
  const destination = join(output, entry.filename)
  try {
    const cached = await lstat(destination)
    if (cached.isSymbolicLink() || !cached.isFile()) {
      throw new Error(
        `invalid cached artifact: ${destination}; remove it to retry`,
      )
    }
    if ((await sha256File(destination)) !== entry.sha256) {
      throw new Error(
        `invalid cached artifact: ${destination}; remove it to retry`,
      )
    }
    return destination
  } catch (error) {
    if ((error as { code?: string }).code !== "ENOENT") throw error
  }

  if (offline) throw new Error(`missing cached artifact: ${destination}`)
  if (!entry.url.startsWith("https://")) {
    throw new Error("artifact downloads require HTTPS")
  }

  let temporary: string | undefined
  let file: Awaited<ReturnType<typeof open>> | undefined
  const controller = new AbortController()
  let timeout: ReturnType<typeof setTimeout> | undefined
  const armTimeout = () => {
    if (timeout) clearTimeout(timeout)
    timeout = setTimeout(() => controller.abort(), 60_000)
  }
  try {
    armTimeout()
    const response = await fetch(entry.url, {
      headers: { "User-Agent": "gpu-unlock-artifacts/0.1.0" },
      signal: controller.signal,
    })
    if (!response.ok)
      throw new Error(`download failed with HTTP ${response.status}`)
    if (!response.url.startsWith("https://")) {
      throw new Error("refusing non-HTTPS redirect")
    }
    if (!response.body) throw new Error("download returned no body")

    temporary = join(output, `.download-${randomUUID()}`)
    file = await open(temporary, "wx", 0o600)
    const hash = createHash("sha256")
    let size = 0
    for await (const chunk of response.body) {
      const block = Buffer.from(chunk)
      size += block.byteLength
      if (size > MAX_DOWNLOAD)
        throw new Error("artifact exceeds 1 GiB download limit")
      hash.update(block)
      let offset = 0
      while (offset < block.byteLength) {
        const { bytesWritten } = await file.write(block, offset)
        offset += bytesWritten
      }
      armTimeout()
    }
    if (timeout) clearTimeout(timeout)
    timeout = undefined
    await file.sync()
    await file.close()
    file = undefined

    if (hash.digest("hex") !== entry.sha256) {
      throw new Error(`SHA-256 mismatch: ${entry.filename}`)
    }
    await chmod(temporary, 0o644)
    await rename(temporary, destination)
    temporary = undefined
    return destination
  } finally {
    if (timeout) clearTimeout(timeout)
    await file?.close()
    if (temporary) await unlink(temporary).catch(() => {})
  }
}

export async function fetchArtifacts(args: string[]): Promise<void> {
  const offline = args.includes("--offline")
  const positional = args.filter((arg) => arg !== "--offline")
  if (positional.length !== 1) {
    throw new Error(
      "Usage: bun tools/firmware/fetch_artifacts.ts OUTPUT [--offline]",
    )
  }
  for (const entry of inputs(await loadManifest())) {
    console.log(await fetchArtifact(entry, positional[0]!, offline))
  }
}

if (import.meta.main)
  fetchArtifacts(process.argv.slice(2)).catch((error: unknown) => {
    console.error(
      `fetch failed: ${error instanceof Error ? error.message : error}`,
    )
    process.exitCode = 1
  })
