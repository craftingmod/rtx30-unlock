#!/usr/bin/env bun
// SPDX-License-Identifier: GPL-2.0-only
import { resolve } from "node:path"
import { loadManifest } from "./common.ts"

const root = resolve(import.meta.dir, "../..")
const cache = resolve(root, "build/firmware-inputs")

async function run(script: string, args: string[]): Promise<void> {
  const child = Bun.spawn(
    [process.execPath, resolve(import.meta.dir, script), ...args],
    {
      cwd: root,
      stdout: "inherit",
      stderr: "inherit",
    },
  )
  const status = await child.exited
  if (status !== 0) throw new Error(`${script} failed with exit code ${status}`)
}

async function main(): Promise<void> {
  const [outputArg] = process.argv.slice(2)
  if (process.argv.length > 3) {
    throw new Error(
      "Usage: bun tools/firmware/build_assets.ts [OUTPUT_DIRECTORY]",
    )
  }

  const output = resolve(root, outputArg ?? "build/firmware-assets")
  const manifest = await loadManifest()
  const [source, , packageArtifact] = manifest.source_artifacts
  if (!source?.filename || !packageArtifact?.filename) {
    throw new Error(
      "manifest must include the source archive and firmware package",
    )
  }

  await run("fetch_artifacts.ts", [cache])
  await run("generate_v67.ts", [
    resolve(cache, packageArtifact.filename),
    output,
  ])
  await run("extract_boot_assets.ts", [resolve(cache, source.filename), output])
  console.log(`EFI firmware assets ready: ${output}`)
}

main().catch((error: unknown) => {
  console.error(`firmware assets failed: ${error instanceof Error ? error.message : error}`)
  process.exitCode = 1
})
