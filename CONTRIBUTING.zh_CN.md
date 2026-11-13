# 为 cmd 贡献

`cmd` 是 Windows `cmd.exe` 命令解释器在类 Unix 系统和 Windows 上的忠实重实现。
它使用 C89 编写，除 POSIX.1 libc 或 Win32 外没有其他依赖。

欢迎各种形式的贡献：缺陷报告、可移植性修复、内建命令、文档和翻译。
在提交 Pull Request 之前，请先阅读本指南。英文版见
[CONTRIBUTING.md](CONTRIBUTING.md)。

参与本项目即表示你同意遵守[行为准则](CODE_OF_CONDUCT.zh_CN.md)。
提交贡献即表示你同意以 GNU 通用公共许可证第 3 版或更高版本
（见 [LICENSE](LICENSE)）授权你的贡献。

## 快速开始

```sh
git clone https://github.com/ChenPi11/cmd.git
cd cmd
make -j$(nproc)
./cmd.exe /q /c "echo hello & dir"
```

构建会生成 `cmd.exe` 和 `COMMAND.COM`。`COMMAND.COM` 是使用
`-DENABLE_AUTOEXEC=1` 编译的同一解释器，启动时会执行 `/AUTOEXEC.BAT`。

目前还没有自动化测试套件。提交 Pull Request 前，请构建两个可执行文件，
并手动验证你修改的代码路径：

```sh
make clean
make -j$(nproc)
./cmd.exe /q /c "set X=1 & set X"
./cmd.exe /q /c "echo one & echo two"
printf '@echo off\necho batch-ok\n' > /tmp/smoke.bat
./cmd.exe /q /c /tmp/smoke.bat
```

测试时可用 `LANG=en_US.UTF-8` 强制使用英文界面。程序会依次检查
`LANG`、`LC_MESSAGES`、`LC_ALL` 和 `LANGUAGE`。AutoRun 脚本从
`$CMD_PREFIX/etc/cmd/AutoRun` 读取（默认 `/usr/local`），因此测试时可设置
`CMD_PREFIX` 指向临时目录，而不必安装。

## 支持的构建系统

- GNU Make（主要方式）：`make`、`make install PREFIX=/usr/local`
- 通用 Unix / System V：`sh tbuild.sh`。它默认设置 `SYSV=1`，并始终定义
  `LIBCMD_NO_VSNPRINTF=1` 以验证回退格式化实现；使用
  `SYSV=0 sh tbuild.sh` 可关闭 System V 兼容层。
- CMake：`cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`，然后
  `cmake --build build`
- Meson：`meson setup build --buildtype=release`，然后
  `meson compile -C build`
- Gradle：`./gradlew build`
- XMake：`xmake build`
- mcpp：`mcpp build --release`
- MSBuild（Windows）：`msbuild cmd.slnx /p:Configuration=Release`

构建产物已被 git 忽略。不要提交 `.o`、`.exe`、`.COM` 或归档文件。

## 可移植性规则

这是代码审查中最重要的一节。

1. **C89 / ISO C。** 代码必须能通过 `-std=c89 -pedantic` 编译。不要使用
   `//` 注释、语句后的声明、指定初始化器、C99 类型、可变参数宏或 GNU 扩展。
2. **遵守可移植性边界。** 只有匹配 `l*.c`、`l*.h`、`lw*.c` 和 `lw*.h`
   的文件可以调用 POSIX 或 Win32 API。其他翻译单元只能调用 ISO C libc
   函数以及 `glibcmd.h` 中声明的 `libcmd_*` 函数。不要在 `b*.c`、`c*.c`
   或 `i*.c` 中加入 `<unistd.h>`、`<dirent.h>`、`<sys/stat.h>`、
   `<windows.h>` 等头文件。`glibcmd.h` 可以包含平台头文件来声明 API，但不得
   调用平台函数。已知待清理项：`bvol.c` 仍直接调用 POSIX `stat()`。
3. **同时实现两个平台。** 每个可移植操作都要有 `l*.c` 中的 POSIX 实现和
   `lw*.c` 中的 Windows 实现。修改 `glibcmd.h` 时必须同时修改两边。两个文件
   在所有平台上都会参与编译，不适用的一侧由平台条件编译排除。
4. **保持翻译单元非空。** l/lw 文件必须首先包含 `glibcmd.h`（它会在系统
   头文件之前设置特性测试宏），并且即使条件编译排除了全部内容，文件也不能
   为空：System V C 不接受空翻译单元。
5. **支持的主机。** Linux、FreeBSD、OpenBSD、NetBSD、Haiku、Darwin/macOS、
   MSYS2、System V R4、其他 POSIX 系统以及 Windows。不要假定使用 Linux
   专有头文件、`/proc`、GNU Make 或某种特定 shell。平台差异应放在 l/lw
   层或 `lsysport.c` 中。

## 架构

代码有意保持扁平：C 源文件位于仓库根目录，`mcpp/` 存放 mcpp 入口包装，
`gradle/` 存放 Gradle wrapper 配置。

- `cmain.c` —— 入口点。解析命令行开关，初始化 `cmd_context_t`
  （`ccontext.h`），并选择 `/c`、批处理文件或交互式执行。
- `cinterp.c` —— REPL、批处理驱动、行处理和信号处理。
- `cparser.c` —— 将一行解析为命令树（`&`、`&&`、`||`、`|`、`(...)`）
  并执行。
- `cdispatch.c` —— 内建命令查找表与外部命令分发。
- `credir.c` —— 重定向的保存与恢复。
- `cvars.c` —— `%VAR%`、`%0`..`%9` 和 `%~` 展开。
- `b*.c` —— 每个文件实现一个内建命令。
- `l*.c` / `lw*.c` —— `libcmd_*` 的 POSIX / Windows 实现。
- `ii18n.c`、`i*.txt`、`i*.h` —— 运行时消息目录与翻译。

执行流程：`cmd_run_line` -> `cmd_parse` -> `cmd_exec_node` ->
`cmd_dispatch` -> 内建命令或外部命令。

## 常见修改

### 添加内建命令

1. 按 8.3 命名规则创建 `b<name>.c`，并包含 `ccontext.h` 和 `glibcmd.h`。
2. 实现 `int builtin_<name>(cmd_context_t *ctx, int argc, char **argv)`。
3. 在 `cdispatch.c` 中声明它，并加入 `builtin_table`（以及别名）。
4. 在 `imessages.h` 和全部四个 `i*.txt` 文件中加入用户可见文本，然后运行
   `./ti18ngen.sh`。
5. 将新的 `.c` 文件加入显式构建清单（见下文）。
6. 构建两个可执行文件，并测试该命令、`/?` 帮助和失败路径。

### 添加 libcmd 操作

1. 在 `glibcmd.h` 中声明 `libcmd_*` 原型。
2. 在对应的 `l*.c`（POSIX）和 `lw*.c`（Windows）中实现。
3. 保持两个文件非空，并首先包含 `glibcmd.h`。
4. 至少在一个 POSIX 主机上测试；如果修改 Windows 代码，请使用
   MSYS2/MinGW 或 MSVC 构建。

### 添加或修改消息

1. 在 `imessages.h` 中添加或重命名 `MSG_*` 标识符。
2. 在全部四个目录文件中加入文本：`ien_us.txt`、`izh_cn.txt`、
   `izh_ms.txt` 和 `izh_wy.txt`。
3. 在仓库根目录运行 `./ti18ngen.sh`。
4. 将 `.txt` 文件与重新生成的头文件一起提交。切勿手动编辑生成的 `i*.h`
   或 `ilanguages.h`。

### 添加源文件

更新所有显式清单：

- `meson.build`
- `cmd.vcxproj`、`COMMAND.vcxproj` 以及 VS 的 `.filters` 文件
- `cmd.cbp`、`COMMAND.cbp`
- `cmd.dev`、`COMMAND.dev`

GNU Make、CMake、Gradle、XMake 和 mcpp 会自动发现源文件。mcpp 只编译一次
共享源码，因此 `COMMAND.COM` 使用 `mcpp/cmain_command.c`
（`#include "../cmain.c"`）；不要为 mcpp 再添加一个 `main`。

## 代码风格

- 宏使用 `UPPER_CASE`，函数和变量使用 `snake_case`。
- 使用 `clang-format` 生成的 Microsoft C 风格。
- 不要使用 `//` 注释；ANSI C 不支持这种注释。
- 不要在 `#define` 行后添加注释或尾随空白；AT&T C 无法处理。
- 使用包含守卫，切勿使用 `#pragma once`。
- 在系统头文件之前包含 `glibcmd.h` 或 `ccontext.h`，以便首先设置 POSIX
  特性测试宏。
- 一个函数只做一件事。将解析、展开、状态修改、I/O 和执行放在不同的辅助
  函数中；不要让单个函数承担多种职责。
- 保持 `-Wall -Wextra -Wpedantic` 下无警告。
- 源文件必须是 ASCII。非 ASCII 的用户界面文本应放入 `i*.txt` 文件。
- 使用 8.3 文件名（最多 12 个字符）并遵循前缀规则：
  `b` 内建命令、`c` cmd 核心、`g` 通用头文件、`i` 国际化、`l` libcmd、
  `t` 工具、`o` 概览。`./tlint.sh <file>` 会检查文件名长度、结尾换行和
  ASCII 编码。
- 保持扁平目录结构。未经维护者同意，不要新增顶层目录。

## 提交更改

- 大型修改或新内建命令请先开 issue 讨论设计。
- 每个 Pull Request 只做一个逻辑修改。不要重新格式化无关代码。
- 说明修改内容、构建和测试过的平台，以及运行过的具体命令。
- 在同一个 Pull Request 中更新文档和翻译。
- 推送前确认两个可执行文件都能构建，且修改的路径可正常工作。
- 安全问题请私下报告给维护者，不要开公开 issue。

## 发布打包

发布归档由 `./tpack.sh` 生成。该脚本会重新生成国际化表，对打包文件清单运行
`tlint.sh`，并将归档写入仓库根目录。这是维护者的工作；普通 Pull Request
不应包含生成的归档。
