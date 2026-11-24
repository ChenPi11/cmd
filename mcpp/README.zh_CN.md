# 用 mcpp 构建 cmd

[English](README.md) · [在浏览器中试用](https://chenpi11.github.io/cmd/)

[mcpp](https://github.com/mcpp-community/mcpp) 可为当前机器构建 `cmd.exe` 与 `COMMAND.COM`，也可在这一台机器上
为 33 个目标中的任一个构建，无需为每个系统安装工具链。需要 mcpp 2026.11.22.1 或更新。

## 快速开始

```sh
mcpp build --release                                  # 当前机器
mcpp run cmd.exe --release -- /c ver

mcpp build --release --target x86_64-windows-msvc     # 其他系统
mcpp build --release --target x86_64-openkal          # 一个映像，由 okrun 在各处运行
mcpp pack cmd.exe --profile web --target wasm32-emscripten --pkg-format web   # 网页
```

## 目标

| 系统 | 架构 | 变体 | 目标 | 级别 |
|---|---|---|---|---|
| Linux | x86_64、aarch64 | glibc | `x86_64-linux-gnu`、`aarch64-linux-gnu` | L2 |
| | x86_64、aarch64、i686 | musl，静态 | `x86_64-linux-musl`、`aarch64-linux-musl`、`i686-linux-musl` | L2 |
| Windows | x86_64、aarch64 | MinGW | `x86_64-windows-gnu`、`aarch64-windows-gnu` | L2 |
| | x86_64、i686 | MSVC | `x86_64-windows-msvc`、`i686-windows-msvc` | L2 |
| | x86_64 | musl，POSIX | `x86_64-windows-musl` | L1 |
| macOS | aarch64 | Apple SDK | `aarch64-macos` | L2 |
| | aarch64、x86_64 | musl，静态 | `aarch64-macos-musl`、`x86_64-macos-musl`（也在 PureDarwin 中运行） | L2 |
| FreeBSD | x86_64、aarch64 | base；musl，静态 | `x86_64-freebsd`、`aarch64-freebsd`、`x86_64-freebsd-musl`、`aarch64-freebsd-musl` | L2 |
| NetBSD、OpenBSD | x86_64、aarch64 | base | `x86_64-netbsd`、`aarch64-netbsd`、`x86_64-openbsd`、`aarch64-openbsd` | L2 |
| Android | x86_64 / aarch64 | NDK | `x86_64-linux-android` / `aarch64-linux-android` | L2 / 仅构建 |
| GNU/Hurd、Haiku | x86_64 | | `x86_64-hurd`、`x86_64-haiku` | L2 |
| System V R4 | i686 | | `i686-svr4-musl` | L2 |
| Web | wasm32 | Emscripten | `wasm32-emscripten` | L1 |
| UEFI | x86_64 | openkal | `x86_64-uefi` | L1 |
| MS-DOS | i686 / i586 | openkal / DJGPP | `i686-dos-musl` / `i586-dos-djgpp` | L1 |
| openkal 映像 | x86_64、aarch64 / i686 | | `x86_64-openkal`、`aarch64-openkal` / `i686-openkal` | L2 / L1 |

## 级别

CI 在每个能运行的目标上（真机、虚拟机或模拟器中）运行 [`.github/mcpp/test.bat`](../.github/mcpp/test.bat)，并评判输出：

- **L0** 解释器本身：变量、`IF`、`FOR`、`CALL :label`、`SET /A`
- **L1** 文件：批处理文件、`MD`、`COPY`、`REN`、`TYPE`、`DIR`、`DEL`、重定向
- **L2** 启动程序：外部命令、`%COMSPEC%`、它们的退出码

低于目标级别的运行即失败；[`floor.txt`](../.github/mcpp/floor.txt) 注明低于 L2 的原因。实际达到更高级别时会在结果中报告，
系统补上的能力因此自动显现。

## 在虚拟机或模拟器中运行

BSD、Android、GNU/Hurd、Haiku、System V、UEFI 与 DOS 目标在 mcpp 首次使用时安装的机器中运行。在 `mcpp.toml`
中启用一次运行器（`[target.x86_64-netbsd] runner = { enabled = true }`），或对单次运行：

```sh
MCPP_TARGET_X86_64_NETBSD_RUNNER_ENABLED=true \
  mcpp run cmd.exe --release --target x86_64-netbsd --file test.bat -- /c test.bat
```

`--file` 把文件放进程序的工作目录。
