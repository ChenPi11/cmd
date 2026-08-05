# cmd

> cmd.exe 是 Windows 的命令解释器，也是世界上使用最广泛的命令行外壳之一。
>
> 但它在类 Unix 系统上不可用。为什么不把它带到 Unix 上，拓展一下市场呢？
>
> -- ChenPi11

**Windows `cmd.exe` 命令解释器在 Unix 上的忠实重实现。**

诞生于 GNU/Linux，可在**所有的类 Unix 系统**上运行。

很遗憾，它不支持 Windows 平台，无法占领 Windows 市场。

**仅依赖 libc 与 POSIX API。**

**[README: American English](README.md)** |
**[README: 简体中文](README.zh_CN.md)** |
**[README: 微软式中文](README.zh_MS.md)** |
**[README: 八股文](README.zh_WY.md)**

## 支持的语言

- 美式英语（en_US.UTF-8，默认）
- 简体中文（zh_CN.UTF-8）
- 微软式中文（zh_MS.UTF-8）
- 文言文（zh_WY.UTF-8）

通过设置 `LANG`、`LC_MESSAGES` 或 `LC_ALL` 环境变量，即可切换 `cmd`
外壳的显示语言。

## 截图概览

`cmd` 已在一系列操作系统上验证运行——甚至包括经典的 System V。

<table>
  <tr>
    <td align="center">
      <img src="ohaiku.png" alt="cmd 运行于 Haiku" height="150" />
      <br/><sub><b>Haiku</b></sub>
    </td>
    <td align="center">
      <img src="oopenbsd.png" alt="cmd 运行于 OpenBSD" height="150" />
      <br/><sub><b>OpenBSD</b></sub>
    </td>
    <td align="center">
      <img src="opuredarwin.png" alt="cmd 运行于 PureDarwin" height="150" />
      <br/><sub><b>PureDarwin</b></sub>
    </td>
  </tr>
  <tr>
    <td align="center">
      <img src="onetbsd.png" alt="cmd 运行于 NetBSD" height="150" />
      <br/><sub><b>NetBSD</b></sub>
    </td>
    <td align="center">
      <img src="ofreebsd.png" alt="cmd 运行于 FreeBSD" height="150" />
      <br/><sub><b>FreeBSD</b></sub>
    </td>
    <td align="center">
      <img src="osystemv.png" alt="cmd 运行于 System V" height="150" />
      <br/><sub><b>System V</b></sub>
    </td>
  </tr>
  <tr>
    <td align="center">
      <img src="ohurd.png" alt="cmd running on GNU/Hurd" height="150" />
      <br/><sub><b>GNU/Hurd</b></sub>
    </td>
    <td align="center">
      <img src="omacos.png" alt="cmd running on macOS" height="150" />
      <br/><sub><b>macOS</b></sub>
    </td>
    <td align="center">
      <img src="omsys2.png" alt="cmd running on MSYS2" height="150" />
      <br/><sub><b>MSYS2</b></sub>
    </td>
  </tr>
</table>

## 特性

- **批处理脚本** —— `CALL`、`GOTO`、`IF`、`FOR`、`SHIFT`、
  `%0`..`%9` 参数展开、`SETLOCAL`/`ENDLOCAL` 作用域，以及标签跳转。
- **完整的管道与重定向支持** —— `|`、`<`、`>`、`>>`、`2>`，遵循
  `cmd.exe` 的优先级规则。
- **40+ 内置命令** —— `ASSOC`、`COPY`、`DIR`、`ECHO`、`FOR`、`IF`、
  `SET`、`START`、`TITLE`、`TYPE` 等。
- **Windows 风格的环境变量语义** —— 不区分大小写的 `%VAR%` 展开，
  `%CD%`、`%DATE%`、`%TIME%`、`%ERRORLEVEL%`、`%CMDCMDLINE%`，
  以及 `/v:on` 延迟展开。
- **行编辑** —— 内置 linenoise，支持 Emacs 键位绑定、持久化历史
  （`~/.cmd_history`），以及 TAB 文件/目录补全。
- **AutoRun 支持** —— 来自 `$PREFIX/etc/cmd/AutoRun/` 的站点级/用户级
  初始化脚本，以及供 `COMMAND.COM` 使用的 `AUTOEXEC.BAT`。
- **本地化界面** —— 支持英文与简体中文，依据 `LC_ALL` / `LC_MESSAGES` /
  `LANG` 环境变量自动选择。
- **两种形态** —— 标准解释器 `cmd.exe`，以及保留经典 MS-DOS AutoExec
  行为的 `COMMAND.COM`。
- **零外部依赖** —— C89、一个 POSIX.1 libc，再无其他。

## 构建

### GNU/Linux（GNU Make）

```sh
make
make install PREFIX=/usr/local
```

### 通用 Unix（含 System V）

```sh
sh tbuild.sh                 # 使用 $CC，默认为 cc。
sh tbuild.sh CC=cc V=0       # 使用指定编译器并安静构建。
env SYSV=0 ./tbuild.sh       # 禁用 System V 移植性标志。
```

源码为标准 C89，除 POSIX.1-1990 libc 外无其他依赖。
`tbuild.sh` 会自动添加移植性标志（`-DLIBCMD_SYSV=1` 与
`-DLIBCMD_NO_VSNPRINTF=1`）——详见 `lsysport.c`。
设置 `SYSV=0` 可禁用 `-DLIBCMD_SYSV=1`。

## 用法

```text
cmd [/c|/k] [/s] [/q] [/d] [/a|/u] [/t:{bf|f}]
    [/e:{on|off}] [/f:{on|off}] [/v:{on|off}] [string]
```

| 选项 | 说明 |
| :--- | :--- |
| `/c string` | 执行 `string` 后退出 |
| `/k string` | 执行 `string` 后保持交互模式 |
| `/s` | `/c`/`/k` 的特殊解析模式（去除外层引号） |
| `/q` | 安静模式；不显示横幅与提示符 |
| `/d` | 禁用 AutoRun 脚本 |
| `/a` | ANSI 输出（默认） |
| `/u` | Unicode 输出 |
| `/t:{bf\|f}` | 设置前景/背景颜色半字节，例如 `/t:0f` |
| `/e:{on\|off}` | 启用/禁用命令扩展 |
| `/f:{on\|off}` | 启用/禁用文件名补全 |
| `/v:{on\|off}` | 启用/禁用延迟展开 |

## 内置命令

```text
ASSOC        BREAK        CALL         CD / CHDIR   CLS
COLOR        COPY         DATE         DEL / ERASE  DIR
DOSKEY       ECHO         ENDLOCAL     EXIT         FOR
FTYPE        GOTO         IF           MD / MKDIR   MKLINK
MOVE         PATH         PAUSE        POPD         PROMPT
PUSHD        RD / RMDIR   REM          REN / RENAME SET
SETLOCAL     SHIFT        START        TIME         TITLE
TYPE         VER          VERIFY       VOL
```

## 批处理文件

完全支持批处理文件（`.bat` 与 `.cmd`），包括 `CALL`、`GOTO`、`IF`、
`FOR`、`SHIFT`、`%0`..`%9` 参数展开，以及 `SETLOCAL`/`ENDLOCAL` 作用域。
标签使用冒号前缀：

```bat
@ECHO OFF
:again
ECHO Hello, %1
SHIFT
IF NOT "%~1"=="" GOTO again
```

## 管道与重定向

```text
|          将左侧命令的 stdout 通过管道传给右侧命令的 stdin
< file     从文件重定向 stdin
> file     将 stdout 重定向到文件（覆盖）
>> file    将 stdout 重定向到文件（追加）
2> file    将 stderr 重定向到文件
```

单条命令行中可组合多个重定向。

## 文件

| 路径 | 用途 |
| :--- | :--- |
| `~/.cmd_history` | 命令历史（纯文本，每行一条命令） |
| `~/.cmd_assoc` | 文件扩展名关联（`ASSOC` / `FTYPE`） |
| `$PREFIX/etc/cmd/AutoRun/` | 启动时执行的脚本 |
| `AUTOEXEC.BAT` | `COMMAND.COM` 的启动脚本 |

## 可移植性

`cmd` 尽可能避免使用 GNU 扩展、指定初始化器与 C99 类型。
缺少 `fnmatch(3)`、`settimeofday(2)`、`setpriority(2)` 或
`open_memstream(3)` 的系统由 `lsysport.c` 中的移植层提供支持。

## 许可证

GNU 通用公共许可证 **第 3 版或更高版本** —— 参见 [LICENSE](LICENSE)。

## 参与贡献

参见 [CONTRIBUTING](CONTRIBUTING)。
