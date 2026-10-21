import { existsSync, mkdirSync } from "node:fs"
import { resolve } from "node:path"
import AdmZip from "adm-zip"

const root = resolve(import.meta.dir, "..")
const zip = new AdmZip()
zip.addLocalFile(resolve(root, "build/efi/BOOTX64.EFI"), "EFI/BOOT")
zip.addLocalFile(resolve(root, "static/UNLOCK.md"), "RTX-UNLOCK")

const assets = resolve(root, "build/firmware-assets")
if (existsSync(assets)) {
  zip.addLocalFolder(assets, "RTX-UNLOCK/ASSETS")
}

mkdirSync(resolve(root, "build"), { recursive: true })
const output = resolve(root, "build/rtx30-unlock.zip")
zip.writeZip(output)
console.log(`Created ZIP: ${output}`)
