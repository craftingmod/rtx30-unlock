import { $ } from "bun"
import { delimiter, resolve } from "node:path"

export const root = resolve(import.meta.dir, "..")

const searchPath = [process.env.LLVM_BIN, process.env.PATH]
  .filter(Boolean)
  .join(delimiter)
const whichOptions = searchPath ? { PATH: searchPath } : undefined

function findTool(names: (string | undefined)[]) {
  for (const name of names) {
    if (!name) continue
    const path = Bun.which(name, whichOptions)
    if (path) return path
  }
  return undefined
}

export const clang = findTool([process.env.CC, "clang"])
export const lldLink = findTool(["lld-link"])
export const python = findTool(["python", "python3"])

let outputHandler: ((text: string) => void) | undefined

export function setOutputHandler(handler?: (text: string) => void) {
  outputHandler = handler
}

export async function run(args: string[], cwd = root) {
  const [executable, ...parameters] = args
  if (!executable) throw new Error("A command is required.")

  const result = await $`${executable} ${parameters}`.cwd(cwd).nothrow().quiet()
  const stdout = result.stdout.toString()
  const stderr = result.stderr.toString()
  const output = stdout + stderr
  if (output) {
    outputHandler?.(output)
    process.stdout.write(stdout)
    process.stderr.write(stderr)
  }
  if (result.exitCode !== 0) {
    throw new Error(`${executable} exited with status ${result.exitCode}`)
  }
  return result
}

export function requireTool(path: string | undefined, name: string) {
  if (!path) throw new Error(`Install ${name} and add it to PATH or LLVM_BIN.`)
  return path
}
