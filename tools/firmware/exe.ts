import { buildAssets } from "./build_assets.ts"

buildAssets(["RTX-UNLOCK/ASSETS"], process.cwd()).catch((error: unknown) => {
  console.error(
    `firmware assets failed: ${error instanceof Error ? error.message : error}`,
  )
  process.exitCode = 1
})
