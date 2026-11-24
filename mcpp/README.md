# Building cmd with mcpp

[简体中文](README.zh_CN.md) · [Try it in the browser](https://chenpi11.github.io/cmd/)

[mcpp](https://github.com/mcpp-community/mcpp) builds `cmd.exe` and `COMMAND.COM` for the
machine you are on, or for any of 33 targets from that one machine: no toolchain to install
per system. It needs mcpp 2026.11.22.1 or later.

## Quick start

```sh
mcpp build --release                                  # this machine
mcpp run cmd.exe --release -- /c ver

mcpp build --release --target x86_64-windows-msvc     # another system
mcpp build --release --target x86_64-openkal          # one image, run by okrun anywhere
mcpp pack cmd.exe --profile web --target wasm32-emscripten --pkg-format web   # the web page
```

## Targets

| System | Arch | Variant | Target | Level |
|---|---|---|---|---|
| Linux | x86_64, aarch64 | glibc | `x86_64-linux-gnu`, `aarch64-linux-gnu` | L2 |
| | x86_64, aarch64, i686 | musl, static | `x86_64-linux-musl`, `aarch64-linux-musl`, `i686-linux-musl` | L2 |
| Windows | x86_64, aarch64 | MinGW | `x86_64-windows-gnu`, `aarch64-windows-gnu` | L2 |
| | x86_64, i686 | MSVC | `x86_64-windows-msvc`, `i686-windows-msvc` | L2 |
| | x86_64 | musl, POSIX | `x86_64-windows-musl` | L1 |
| macOS | aarch64 | Apple SDK | `aarch64-macos` | L2 |
| | aarch64, x86_64 | musl, static | `aarch64-macos-musl`, `x86_64-macos-musl` (also PureDarwin) | L2 |
| FreeBSD | x86_64, aarch64 | base; musl, static | `x86_64-freebsd`, `aarch64-freebsd`, `x86_64-freebsd-musl`, `aarch64-freebsd-musl` | L2 |
| NetBSD, OpenBSD | x86_64, aarch64 | base | `x86_64-netbsd`, `aarch64-netbsd`, `x86_64-openbsd`, `aarch64-openbsd` | L2 |
| Android | x86_64 / aarch64 | NDK | `x86_64-linux-android` / `aarch64-linux-android` | L2 / build |
| GNU/Hurd, Haiku | x86_64 | | `x86_64-hurd`, `x86_64-haiku` | L2 |
| System V R4 | i686 | | `i686-svr4-musl` | L2 |
| Web | wasm32 | Emscripten | `wasm32-emscripten` | L1 |
| UEFI | x86_64 | openkal | `x86_64-uefi` | L1 |
| MS-DOS | i686 / i586 | openkal / DJGPP | `i686-dos-musl` / `i586-dos-djgpp` | L1 |
| openkal image | x86_64, aarch64 / i686 | | `x86_64-openkal`, `aarch64-openkal` / `i686-openkal` | L2 / L1 |

## Levels

CI runs [`.github/mcpp/test.bat`](../.github/mcpp/test.bat) on every target it can, in a
machine, a virtual machine or an emulator, and grades the output:

- **L0** the interpreter alone: variables, `IF`, `FOR`, `CALL :label`, `SET /A`
- **L1** files: batch files, `MD`, `COPY`, `REN`, `TYPE`, `DIR`, `DEL`, redirection
- **L2** starting a program: external commands, `%COMSPEC%`, their exit status

A run below a target's level fails; [`floor.txt`](../.github/mcpp/floor.txt) says why a target
is below L2. A target reaching more than its level is reported, so a capability its system
gains shows by itself.

## Running in a virtual machine or an emulator

The BSD, Android, GNU/Hurd, Haiku, System V, UEFI and DOS targets run in a machine mcpp
installs on first use. Enable the runner once in `mcpp.toml`
(`[target.x86_64-netbsd] runner = { enabled = true }`), or for one run:

```sh
MCPP_TARGET_X86_64_NETBSD_RUNNER_ENABLED=true \
  mcpp run cmd.exe --release --target x86_64-netbsd --file test.bat -- /c test.bat
```

`--file` gives the program a file in its working directory.
