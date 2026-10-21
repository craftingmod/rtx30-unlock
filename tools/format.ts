import { existsSync } from "node:fs"
import { resolve } from "node:path"
import Bun from "bun"

const root = resolve(import.meta.dir, "..")
const args = process.argv.slice(2)
if (args.some((arg) => arg !== "--check")) {
  throw new Error("Usage: bun tools/format.ts [--check]")
}
const formatter = Bun.which("clang-format", {
  PATH: process.env.LLVM_BIN ?? process.env.PATH,
})
if (!formatter)
  throw new Error("Install clang-format 19+ and set PATH or LLVM_BIN.")

const git = Bun.spawnSync(["git", "ls-files", "-z"], { cwd: root })
if (git.exitCode !== 0) throw new Error(git.stderr.toString())
const untrackedSrc = Bun.spawnSync(
  ["git", "ls-files", "--others", "--exclude-standard", "-z", "--", "src"],
  { cwd: root },
)
if (untrackedSrc.exitCode !== 0) throw new Error(untrackedSrc.stderr.toString())
// Apply exclusions before invoking clang-format, including on Windows.
const ignored = (await Bun.file(resolve(root, ".clang-format-ignore")).text())
  .split(/\r?\n/)
  .map((line) => line.trim())
  .filter((line) => line && !line.startsWith("#"))
  .map((pattern) => new Bun.Glob(pattern))
const candidates = new Set([
  ...git.stdout.toString().split("\0"),
  ...untrackedSrc.stdout.toString().split("\0"),
])
const sources = [...candidates].filter(
  (name) =>
    existsSync(resolve(root, name)) &&
    /\.(c|h|cpp|hpp|cu|cuh)$/.test(name) &&
    !ignored.some((glob) => glob.match(name)),
)
const flags = args.includes("--check") ? ["--dry-run", "--Werror"] : ["-i"]
for (let start = 0; start < sources.length; start += 50) {
  const result = Bun.spawnSync(
    [formatter, "--style=file", ...flags, ...sources.slice(start, start + 50)],
    { cwd: root, stdout: "inherit", stderr: "inherit" },
  )
  if (result.exitCode !== 0) process.exit(result.exitCode)
}
console.log(
  `${args.includes("--check") ? "Checked" : "Formatted"} ${sources.length} source files.`,
)
