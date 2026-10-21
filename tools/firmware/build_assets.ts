#!/usr/bin/env bun
// SPDX-License-Identifier: GPL-2.0-only
import { resolve } from "node:path"
import { loadManifest } from "./common.ts"
import { fetchArtifacts } from "./fetch_artifacts.ts"
import { generateV67 } from "./generate_v67.ts"
import { extractBootAssets } from "./extract_boot_assets.ts"

const root = resolve(import.meta.dir, "../..")
export async function buildAssets(
  args: string[],
  workingRoot = root,
): Promise<void> {
  const [outputArg] = args
  if (args.length > 1) {
    throw new Error(
      "Usage: bun tools/firmware/build_assets.ts [OUTPUT_DIRECTORY]",
    )
  }

  const output = resolve(workingRoot, outputArg ?? "build/firmware-assets")
  const cache = resolve(workingRoot, "build/firmware-inputs")
  const manifest = await loadManifest()
  const [source, , packageArtifact] = manifest.source_artifacts
  if (!source?.filename || !packageArtifact?.filename) {
    throw new Error(
      "manifest must include the source archive and firmware package",
    )
  }

  await fetchArtifacts([cache])
  await generateV67([resolve(cache, packageArtifact.filename), output])
  await extractBootAssets([resolve(cache, source.filename), output])
  console.log(`EFI firmware assets ready: ${output}`)
}

if (import.meta.main)
  buildAssets(process.argv.slice(2)).catch((error: unknown) => {
    console.error(
      `firmware assets failed: ${error instanceof Error ? error.message : error}`,
    )
    process.exitCode = 1
  })
