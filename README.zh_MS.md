# 指挥官

> cmd.exe，Windows 的命令解释器，也是最广泛使用的之一
> 命令行壳。
>
> 但它在类 Unix 系统中不可用。为什么不把它带到 Unix 上
> 扩大市场？
>
> —— 陈皮11

**对Unix版Windows"cmd.exe"命令解释器的忠实再实现。**

诞生于 GNU/Linux，运行在**所有类 UNIX 系统**上

遗憾的是，它不支持 Windows，也无法占据 Windows 市场。

**仅需LIBC和POSIX API。**

**[README: American English](README.md)** |
**[README: 简体中文](README.zh_CN.md)** |
**[README: 微软式中文](README.zh_MS.md)** |
**[README: 八股文](README.zh_WY.md)**

## 支持的语言

- 美式英语（en_US。UTF-8，默认）
- 简体中文（zh_CN。UTF-8）
- Microsoft中文译本（zh_MS年）。UTF-8）
- 古典汉语（zh_WY。UTF-8）

通过设置"LANG"、"LC_MESSAGES"或"LC_ALL"环境变量，你
可以改变"cmd"壳的语言。

## 截图

"cmd"已被验证可在多种操作系统上运行——
甚至经典的System V也是如此。

<table>
 <tr>
 <td align="center">
 <img src="ohaiku.png" alt="cmd running on haiku" height="150" />
 <br/><sub><b>俳句</b></sub>
 </td>
 <td align="center">
 <img src="oopenbsd.png" alt="cmd running on OpenBSD" height="150" />
 <br/><sub><b>OpenBSD</b></sub>
 </td>
 <td align="center">
 <img src="opuredarwin.png" alt="cmd running on PureDarwin" height="150" />
 <br/><sub><b>纯达尔文</b></sub>
 </td>
 </tr>
 <tr>
 <td align="center">
 <img src="onetbsd.png" alt="cmd running on NetBSD" height="150" />
 <br/><sub><b>NetBSD</b></sub>
 </td>
 <td align="center">
 <img src="ofreebsd.png" alt="cmd running on FreeBSD" height="150" />
 <br/><sub><b>FreeBSD</b></subr>
 </td>
 <td align="center">
 <img src="osystemv.png" alt="cmd running on System V" height="150" />
 <br/><sub><b>系统V</b></sub>
 </td>
 </tr>
 <tr>
 <td align="center">
 <img src="ohurd.png" alt="cmd running on GNU/Hurd" height="150" />
 <br/><sub><b>GNU/Hurd</b></sub>
 </td>
 <td align="center">
 <img src="omacos.png" alt="macOS 运行中的 cmd" height="150" />
 <br/><sub><b>macOS</b></sub>
 </td>
 </tr>
</table>

## 特色

- **批处理脚本** — '呼叫'，'GOTO'，'如果'，'为'，'shift'，
 '%0'.."%9"扩展，"SETLOCAL"/"ENDLOCAL"范围，以及标签。
- **全管及重定向支撑** — '|'， '<'， '>'， '>>'， '2>'
 "cmd.exe"优先权。
- **40+ 内置命令** — 'ASSOC'， '复制'， 'DIR'， 'ECHO'， 'FOR'， 'IF'，
 "集合"、"开始"、"标题"、"类型"等。
- **Windows风格环境语义** — 大小写不区分的"%VAR%"
 扩展，'%CD%'， '%date%'， '%time%'， '%errorlevel%'， '%cmdcmdline%'，
 延迟扩展，包含"/V：on"。
- **行编辑** — 捆绑线噪声与Emacs绑定，持久化
 历史（'~/.cmd_history'）以及TAB文件/目录补全。
- **AutoRun 支持** — 全站/用户初始化脚本来自
 "$PREFIX/etc/cmd/AutoRun/"，加上"AUTOEXEC.BAT"表示"COMMAND.COM"。
- **本地化UI** — 英文和简体中文，选自
 "LC_ALL" / "LC_MESSAGES" / "LANG"环境。
- **两个人格** — 'cmd.exe' 表示标准解释器 和
 "COMMAND.COM"，具有经典的MS-DOS自动执行功能。
- **无外部依赖** — C89，一个POSIX.1 libc，仅此而已。

## 建筑

### GNU/Linux（GNU Make）

```text
"嘘
制造
make install PREFIX=/usr/local
```

### 通用Unix（包括System V）

```text
"嘘
SH tbuild.sh # 使用$CC，默认为 CC。
sh tbuild.sh CC=cc V=0 # 用特定编译器进行安静构建。
env SYSV=0 ./tbuild.sh # 禁用System V可移植性标志。
```

源代码是STD C89，除了POSIX.1-1990 libc外没有其他依赖。关于
'tbuild.sh' 添加可移植性标志（'-DLIBCMD_SYSV=1' 和
'-DLIBCMD_NO_VSNPRINTF=1'） — 参见 'lsysport.c'。
使用 SYSV=0 可以禁用 '-DLIBCMD_SYSV=1'。

## 用途

```text
"''文本
cmd [/c|/k] [/s] [/q] [/d] [/a|/u] [/t:{bf|f}]
    [/e:{on|off}] [/f:{on|off}] [/v:{on|off}] [string]
```

| 选项 | 描述 |
| :--- | :--- |
| `/c string` | 执行"字符串"并退出 |
| `/k string` | 执行"字符串"并保持交互 |
| `/s` | "/c"/'/k'的特殊解析模式（去除外引号） |
| `/q` | 安静模式;无横幅或提示词 |
| `/d` | 禁用自动运行脚本 |
| `/a` | ANSI 输出（默认） |
| `/u` | Unicode 输出 |
| `/t:{bf\|f}` | 设置前景/背景色的点缀，例如 '/t：0f' |
| `/e:{开\|关}` | 启用/禁用命令扩展 |
| `/f：{开\|关}` | 启用/禁用文件名补全 |
| `/v：{开\|关}` | 启用/禁用延迟扩展 |

## 内置命令

```text
"''文本
协会休息呼叫CD / CHDIR CLS
彩色复制日期 DEL / ERASE DIR
DOSKEY ECHO ENDLOCAL EXIT FOR
如果 MD / MKDIR MKlink 则 FTYPE GOTO
移动路径暂停弹出提示
PUSHD RD / RMDIR REM REN / RENAME SET
SETLOCAL SHIFT START TIME TITLE
类型 ver 验证 vol
```

## 批处理文件

批处理文件（".bat"和".cmd"）均被完全支持，包括"CALL"，
'GOTO'，'如果'，'为'，'SHIFT'，'0%'......"%9"论元展开，以及
"SETLOCAL"/"ENDLOCAL"的范围。标签使用冒号前缀：

```batch
"蝙蝠
@ECHO 走开
：再来一次
回声 你好，%1
换班
如果不是"%~1"==""，请再次返回
```

## 管道与转向

```text
"''文本
|pipe stdout 从 left command to stdin of right command
<文件 从 文件中重定向 stdin
> 文件 重定向 stdout 到文件（覆盖）
>>文件 重定向 stdout 到 file（添加）
2>文件 重定向 stderr 到 文件
```

多个重定向可以在单一命令行中组合。

## 文件

| 路径 | 目的 |
| :--- | :--- |
| `~/.cmd_history` | 命令历史（纯文本，每行一个命令） |
| `~/.cmd_assoc` | 文件扩展名关联（"ASSOC" / "FTYPE"） |
| `$PREFIX/etc/cmd/AutoRun/` | 启动时执行的脚本 |
| `AUTOEXEC.BAT` | "COMMAND.COM"启动脚本 |

## 便携性

'cmd' 避免使用 GNU 扩展、指定初始化器和 C99 类型，其中
有可能。没有"fnmatch（3）"、"settimeofday（2）"的系统，
"setpriority（2）"或"open_memstream（3）"由可移植性覆盖
在"lsysport.c"中层。

## 许可证

GNU 通用公共许可证 **版本3或更高版本** — 参见 [LICENSE]（LICENSE）。

## 贡献

参见[贡献](CONTRIBUTING)。
