# Contributing to cmd

`cmd` is a faithful reimplementation of the Windows `cmd.exe` command
interpreter for Unix-like systems and Windows. It is written in C89 and has no
dependencies beyond a POSIX.1 libc or Win32.

Contributions of all kinds are welcome: bug reports, portability fixes,
builtins, documentation, and translations. Please read this guide before
opening a pull request. A Simplified Chinese translation is available at
[CONTRIBUTING.zh_CN.md](CONTRIBUTING.zh_CN.md).

By participating, you agree to follow the [Code of Conduct](CODE_OF_CONDUCT.md).
By submitting a contribution, you agree to license it under the GNU General
Public License v3.0 or later (see [LICENSE](LICENSE)).

## Quick start

```sh
git clone https://github.com/ChenPi11/cmd.git
cd cmd
make -j$(nproc)
./cmd.exe /q /c "echo hello & dir"
```

The build produces `cmd.exe` and `COMMAND.COM`. `COMMAND.COM` is the same
interpreter compiled with `-DENABLE_AUTOEXEC=1`, which makes it execute
`/AUTOEXEC.BAT` at startup.

There is no automated test suite yet. Before opening a pull request, build both
binaries and manually exercise the changed paths:

```sh
make clean
make -j$(nproc)
./cmd.exe /q /c "set X=1 & set X"
./cmd.exe /q /c "echo one & echo two"
printf '@echo off\necho batch-ok\n' > /tmp/smoke.bat
./cmd.exe /q /c /tmp/smoke.bat
```

Use `LANG=en_US.UTF-8` to force the English UI during tests. The program checks
`LANG` before `LC_MESSAGES`, `LC_ALL`, and `LANGUAGE`. AutoRun scripts are read
from `$CMD_PREFIX/etc/cmd/AutoRun` (default `/usr/local`), so set `CMD_PREFIX`
to a temporary tree instead of installing to test them.

## Supported build systems

- GNU Make (primary): `make`, `make install PREFIX=/usr/local`
- Generic Unix / System V: `sh tbuild.sh`. It defaults to `SYSV=1` and always
  defines `LIBCMD_NO_VSNPRINTF=1` to exercise the fallback formatter; use
  `SYSV=0 sh tbuild.sh` to disable the System V shims.
- CMake: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`, then
  `cmake --build build`
- Meson: `meson setup build --buildtype=release`, then `meson compile -C build`
- Gradle: `./gradlew build`
- XMake: `xmake build`
- mcpp: `mcpp build --release`
- MSBuild (Windows): `msbuild cmd.slnx /p:Configuration=Release`

Build output is gitignored. Do not commit `.o`, `.exe`, `.COM`, or archive
files.

## Portability rules

This is the most important section for code review.

1. **C89 / ISO C.** Keep the code compilable with `-std=c89 -pedantic`. Avoid
   `//` comments, declarations after statements, designated initialisers, C99
   types, variadic macros, and GNU extensions.
2. **Stay inside the portability boundary.** Only files matching `l*.c`,
   `l*.h`, `lw*.c`, and `lw*.h` may call POSIX or Win32 APIs. Every other
   translation unit may call only ISO C libc functions and the `libcmd_*`
   functions declared in `glibcmd.h`. Do not add `<unistd.h>`, `<dirent.h>`,
   `<sys/stat.h>`, `<windows.h>`, or similar headers to `b*.c`, `c*.c`, or
   `i*.c`. `glibcmd.h` may include platform headers to declare the API, but it
   must not call platform functions. Known cleanup: `bvol.c` still calls POSIX
   `stat()` directly.
3. **Implement both sides.** Every portable operation has a POSIX
   implementation in `l*.c` and a Windows implementation in `lw*.c`. Change
   `glibcmd.h` and both implementations together. Both sides are compiled on
   every host; the inapplicable one is compiled out by its platform guard.
4. **Keep translation units non-empty.** Include `glibcmd.h` first in l/lw
   files (it sets the feature-test macros before system headers) and keep the
   file non-empty even when the guard excludes its contents: System V C
   rejects empty translation units.
5. **Supported hosts.** Linux, FreeBSD, OpenBSD, NetBSD, Haiku, Darwin/macOS,
   MSYS2, System V R4, other POSIX systems, and Windows. Do not assume Linux
   headers, `/proc`, GNU Make, or a particular shell. Platform quirks belong in
   the l/lw layer or `lsysport.c`.

## Architecture

The code is intentionally flat: C sources live in the repository root,
`mcpp/` holds the mcpp entry wrapper, and `gradle/` holds the Gradle wrapper
configuration.

- `cmain.c` — entry point. Parses switches, initialises `cmd_context_t`
  (`ccontext.h`), and chooses `/c`, batch-file, or interactive execution.
- `cinterp.c` — REPL, batch driver, line processing, and signal handling.
- `cparser.c` — parses a line into a command tree (`&`, `&&`, `||`, `|`,
  `(...)`) and executes it.
- `cdispatch.c` — builtin lookup/table and external-command dispatch.
- `credir.c` — redirection save/restore.
- `cvars.c` — `%VAR%`, `%0`..`%9`, and `%~` expansion.
- `b*.c` — one builtin per file.
- `l*.c` / `lw*.c` — POSIX / Windows implementations of `libcmd_*`.
- `ii18n.c`, `i*.txt`, `i*.h` — runtime message catalog and translations.

Execution flow: `cmd_run_line` -> `cmd_parse` -> `cmd_exec_node` ->
`cmd_dispatch` -> builtin or external command.

## Common changes

### Add a builtin

1. Create `b<name>.c` following the 8.3 rule; include `ccontext.h` and
   `glibcmd.h`.
2. Implement `int builtin_<name>(cmd_context_t *ctx, int argc, char **argv)`.
3. Declare it in `cdispatch.c` and add it to `builtin_table` (and aliases).
4. Add user-visible text to `imessages.h` and all four `i*.txt` files, then run
   `./ti18ngen.sh`.
5. Add the new `.c` file to the explicit build manifests (see below).
6. Build both binaries and test the command, its `/?` help, and failure paths.

### Add a libcmd operation

1. Declare the `libcmd_*` prototype in `glibcmd.h`.
2. Implement it in the matching `l*.c` (POSIX) and `lw*.c` (Windows) files.
3. Keep both files non-empty and include `glibcmd.h` first.
4. Test on at least one POSIX host; if touching Windows code, build with
   MSYS2/MinGW or MSVC.

### Add or change a message

1. Add or rename the `MSG_*` ID in `imessages.h`.
2. Add the text to all four catalogs: `ien_us.txt`, `izh_cn.txt`,
   `izh_ms.txt`, and `izh_wy.txt`.
3. Run `./ti18ngen.sh` from the repository root.
4. Commit the `.txt` files and regenerated headers together. Never edit a
   generated `i*.h` or `ilanguages.h` by hand.

### Add a source file

Update every explicit list:

- `meson.build`
- `cmd.vcxproj`, `COMMAND.vcxproj`, and the VS `.filters` files
- `cmd.cbp`, `COMMAND.cbp`
- `cmd.dev`, `COMMAND.dev`

GNU Make, CMake, Gradle, XMake, and mcpp discover sources by glob. mcpp
compiles shared sources once, so `COMMAND.COM` uses `mcpp/cmain_command.c`
(`#include "../cmain.c"`); do not add another `main` for mcpp.

## Coding style

- Use `UPPER_CASE` for macros and `snake_case` for functions and variables.
- Use Microsoft C style as produced by `clang-format`.
- Do not use `//` comments; ANSI C does not support them.
- Do not put comments or trailing whitespace on `#define` lines; AT&T C cannot
  handle them.
- Use include guards, never `#pragma once`.
- Include `glibcmd.h` or `ccontext.h` before system headers so the POSIX
  feature-test macros are set first.
- One function, one job. Keep parsing, expansion, state mutation, I/O, and
  execution in separate helpers; do not grow multi-purpose functions.
- Keep the build clean under `-Wall -Wextra -Wpedantic`.
- Source files are ASCII. Non-ASCII UI strings belong in `i*.txt` files.
- Use 8.3 file names (at most 12 characters) with the prefix scheme:
  `b` builtin, `c` cmd core, `g` general header, `i` i18n, `l` libcmd,
  `t` tools, `o` overview. `./tlint.sh <file>` verifies the name length,
  trailing newline, and ASCII encoding.
- Keep the flat layout. Do not add new top-level directories without
  maintainer approval.

## Submitting changes

- Open an issue first for large changes or new builtins so the design can be
  discussed.
- Keep each pull request focused on one logical change. Do not reformat
  unrelated code.
- Describe what changed, which platforms you built and tested on, and the exact
  commands you ran.
- Add or update documentation and translations in the same pull request.
- Make sure both binaries build and the changed paths work before pushing.
- Report security issues privately to the maintainer instead of opening a
  public issue.

## Release packaging

Release archives are produced with `./tpack.sh`, which regenerates the i18n
tables, runs `tlint.sh` over the packaged file list, and writes the archives to
the repository root. This is maintainer work; normal pull requests should not
include generated archives.
