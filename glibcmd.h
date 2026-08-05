/*
 * libcmd.h - Public API for libcmd
 *
 * libcmd wraps POSIX operations for use by the cmd interpreter.
 * All functions are prefixed with libcmd_.
 *
 * This file may be included from both C89 and C11 code.  It is kept
 * C89-clean so it can be compiled against old System V headers, and
 * uses off_t (from <sys/types.h>) for file sizes instead of the C99
 * long long type.
 *
 * License: GNU GPLv3
 */

/* #include directives live outside the include guard: the AT&T C
 * Compiler's #ifndef handling is unreliable after a very long macro
 * expansion, which could defeat the guard. */

#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifndef LIBCMD_H
#define LIBCMD_H

/* Some POSIX variants (notably old System V) lack the convenience
 * mode-test macros even though they provide S_IFMT/S_IFLNK etc. in
 * <sys/stat.h>.  Supply them when missing. */
#ifndef S_ISREG
# define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#endif
#ifndef S_ISDIR
# define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif
#ifndef S_ISLNK
# define S_ISLNK(m) (((m) & S_IFMT) == S_IFLNK)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Version
 * ---------------------------------------------------------------------- */

#define LIBCMD_VERSION_MAJOR 0
#define LIBCMD_VERSION_MINOR 1
#define LIBCMD_VERSION_PATCH 0

/* -------------------------------------------------------------------------
 * Boolean
 * ---------------------------------------------------------------------- */

#ifndef LIBCMD_BOOL_DEFINED
#define LIBCMD_BOOL_DEFINED
typedef int libcmd_bool_t;
#define LIBCMD_TRUE  1
#define LIBCMD_FALSE 0
#endif

/* -------------------------------------------------------------------------
 * File open flags (platform-neutral)
 * ---------------------------------------------------------------------- */

#define LIBCMD_O_RDONLY  0x0001
#define LIBCMD_O_WRONLY  0x0002
#define LIBCMD_O_RDWR    0x0003
#define LIBCMD_O_CREAT   0x0010
#define LIBCMD_O_TRUNC   0x0020
#define LIBCMD_O_APPEND  0x0040
#define LIBCMD_O_EXCL    0x0080

/* -------------------------------------------------------------------------
 * Standard file descriptors
 * ---------------------------------------------------------------------- */

#define LIBCMD_STDIN_FILENO  0
#define LIBCMD_STDOUT_FILENO 1
#define LIBCMD_STDERR_FILENO 2

/* -------------------------------------------------------------------------
 * Time / date
 * ---------------------------------------------------------------------- */

typedef struct libcmd_time {
    int year;    /* full year, e.g. 2024            */
    int month;   /* 1-12                             */
    int day;     /* 1-31                             */
    int hour;    /* 0-23                             */
    int minute;  /* 0-59                             */
    int second;  /* 0-59                             */
    int ms;      /* 0-999                            */
    int wday;    /* 0 = Sunday                       */
} libcmd_time_t;

/* -------------------------------------------------------------------------
 * File / directory stat
 * ---------------------------------------------------------------------- */

typedef struct libcmd_stat {
    libcmd_bool_t is_dir;
    libcmd_bool_t is_link;
    libcmd_bool_t is_regular;
    off_t         size;          /* bytes                               */
    libcmd_time_t mtime;
    libcmd_time_t atime;
    libcmd_time_t ctime;
    unsigned int  mode;          /* POSIX permission bits, 0 on Windows */
} libcmd_stat_t;

/* -------------------------------------------------------------------------
 * Directory entry (returned by readdir)
 * ---------------------------------------------------------------------- */

#define LIBCMD_NAME_MAX 512

typedef struct libcmd_dirent {
    char          name[LIBCMD_NAME_MAX];
    libcmd_bool_t is_dir;
    libcmd_bool_t is_link;
    off_t         size;
    libcmd_time_t mtime;
    libcmd_time_t atime;
    libcmd_time_t ctime;
    unsigned int  uid;
    unsigned int  mode;
} libcmd_dirent_t;

/* Opaque directory handle */
typedef void *libcmd_dir_t;

/* -------------------------------------------------------------------------
 * Glob result
 * ---------------------------------------------------------------------- */

typedef struct libcmd_glob_result {
    char  **paths;   /* NULL-terminated array of matched paths */
    size_t  count;
} libcmd_glob_result_t;

/* -------------------------------------------------------------------------
 * Process exit information
 * ---------------------------------------------------------------------- */

typedef struct libcmd_exit_info {
    int exited;     /* 1 if process exited normally */
    int exit_code;  /* exit code if exited          */
    int signaled;   /* 1 if killed by signal        */
    int signal;     /* signal number if signaled    */
} libcmd_exit_info_t;

/* -------------------------------------------------------------------------
 * sprintf_s
 * ---------------------------------------------------------------------- */

/*
 * libcmd_sprintf_s - safe sprintf
 *
 * Always null-terminates buf when size > 0.
 * Returns the number of characters written (not counting '\0'), or -1 on
 * error.  Unlike C11 sprintf_s, truncation is NOT treated as an error;
 * the function writes as many characters as fit and returns the number
 * written.
 */
int libcmd_sprintf_s(char *buf, size_t size, const char *fmt, ...);

/*
 * libcmd_vsprintf_s - vargs variant of libcmd_sprintf_s
 */
int libcmd_vsprintf_s(char *buf, size_t size, const char *fmt, va_list ap);

/* -------------------------------------------------------------------------
 * String utilities
 * ---------------------------------------------------------------------- */

/* Case-insensitive string compare */
int libcmd_strcasecmp(const char *a, const char *b);
int libcmd_strncasecmp(const char *a, const char *b, size_t n);

/* Duplicate a string (caller must free) */
char *libcmd_strdup(const char *s);

/* Duplicate at most n characters (caller must free) */
char *libcmd_strndup(const char *s, size_t n);

/* Strip leading and trailing whitespace in-place; returns s */
char *libcmd_strtrim(char *s);

/* -------------------------------------------------------------------------
 * Environment variable management
 * ---------------------------------------------------------------------- */

/*
 * libcmd_getenv - get value of environment variable
 * Returns pointer into the environment or NULL if not found.
 * Do not modify or free the returned pointer.
 */
const char *libcmd_getenv(const char *name);

/*
 * libcmd_setenv - set or overwrite an environment variable
 * Returns 0 on success, -1 on error.
 */
int libcmd_setenv(const char *name, const char *value, int overwrite);

/*
 * libcmd_unsetenv - remove an environment variable
 * Returns 0 on success, -1 on error.
 */
int libcmd_unsetenv(const char *name);

/*
 * libcmd_putenv - set env from "NAME=VALUE" string (takes ownership)
 * Returns 0 on success, -1 on error.
 */
int libcmd_putenv(char *string);

/*
 * libcmd_get_environ - return the current environ array
 * The returned pointer may be invalidated by setenv/unsetenv calls.
 */
char **libcmd_get_environ(void);

/* -------------------------------------------------------------------------
 * Filesystem operations
 * ---------------------------------------------------------------------- */

/*
 * libcmd_chdir - change current working directory
 * Returns 0 on success, -1 on error.
 */
int libcmd_chdir(const char *path);

/*
 * libcmd_getcwd - get current working directory
 * If buf is NULL, allocates a new buffer (caller must free).
 * Returns buf (or allocated buffer) on success, NULL on error.
 */
char *libcmd_getcwd(char *buf, size_t size);

/*
 * libcmd_mkdir - create a directory
 * If make_parents is non-zero, creates intermediate directories (like mkdir -p).
 * Returns 0 on success, -1 on error.
 */
int libcmd_mkdir(const char *path, int make_parents);

/*
 * libcmd_rmdir - remove an empty directory
 * Returns 0 on success, -1 on error.
 */
int libcmd_rmdir(const char *path);

/*
 * libcmd_unlink - remove a file
 * Returns 0 on success, -1 on error.
 */
int libcmd_unlink(const char *path);

/*
 * libcmd_chmod - change permission bits of a file
 * Returns 0 on success, -1 on error.
 */
int libcmd_chmod(const char *path, unsigned int mode);

/*
 * libcmd_rename - rename or move a file/directory
 * Returns 0 on success, -1 on error.
 */
int libcmd_rename(const char *old_path, const char *new_path);

/*
 * libcmd_symlink - create a symbolic link
 * Returns 0 on success, -1 on error.
 */
int libcmd_symlink(const char *target, const char *link_path);

/*
 * libcmd_link - create a hard link
 * Returns 0 on success, -1 on error.
 */
int libcmd_link(const char *old_path, const char *new_path);

/*
 * libcmd_stat - stat a file
 * If follow_links is non-zero, follows symbolic links (stat vs lstat).
 * Returns 0 on success, -1 on error.
 */
int libcmd_stat(const char *path, libcmd_stat_t *st, int follow_links);

/*
 * libcmd_access - check if a file is accessible
 * mode: 0=exists, 1=read, 2=write, 4=exec
 * Returns 0 if accessible, -1 otherwise.
 */
int libcmd_access(const char *path, int mode);

/*
 * libcmd_copy_file - copy src to dst
 * If overwrite is 0 and dst exists, returns -1.
 * Returns 0 on success, -1 on error.
 */
int libcmd_copy_file(const char *src, const char *dst, int overwrite);

/* -------------------------------------------------------------------------
 * Directory iteration
 * ---------------------------------------------------------------------- */

/*
 * libcmd_opendir - open a directory for reading
 * Returns an opaque handle, or NULL on error.
 */
libcmd_dir_t libcmd_opendir(const char *path);

/*
 * libcmd_readdir - read the next entry from an open directory
 * Fills *entry and returns 0 on success; returns -1 at end or on error.
 */
int libcmd_readdir(libcmd_dir_t dir, libcmd_dirent_t *entry);

/*
 * libcmd_closedir - close a directory handle
 */
void libcmd_closedir(libcmd_dir_t dir);

/* -------------------------------------------------------------------------
 * Glob (wildcard expansion)
 * ---------------------------------------------------------------------- */

/*
 * libcmd_glob - expand a pattern (may contain * and ?) into matching paths
 * The result must be freed with libcmd_glob_free().
 * Returns 0 on success (even if no matches), -1 on error.
 */
int libcmd_glob(const char *pattern, libcmd_glob_result_t *result);

/*
 * libcmd_glob_free - free a glob result
 */
void libcmd_glob_free(libcmd_glob_result_t *result);

/*
 * libcmd_fnmatch - match a filesystem name against a wildcard pattern.
 *
 * This is a portable replacement for fnmatch(3) with FNM_NOESCAPE
 * semantics (backslash is an ordinary character).  Returns 0 if the
 * string matches, FNM_NOMATCH (1) otherwise, like fnmatch(3).
 * On modern systems it simply forwards to the libc fnmatch();
 * on old System V targets a compatible implementation is used.
 */
int libcmd_fnmatch(const char *pattern, const char *string);

/* -------------------------------------------------------------------------
 * File I/O (low-level fd-based)
 * ---------------------------------------------------------------------- */

/*
 * libcmd_open - open a file
 * flags: combination of LIBCMD_O_* constants
 * mode: permission bits (e.g. 0644)
 * Returns fd >= 0 on success, -1 on error.
 */
int libcmd_open(const char *path, int flags, unsigned int mode);

/*
 * libcmd_close - close a file descriptor
 * Returns 0 on success, -1 on error.
 */
int libcmd_close(int fd);

/*
 * libcmd_dup - duplicate a file descriptor
 * Returns the new fd on success, -1 on error.
 */
int libcmd_dup(int fd);

/*
 * libcmd_dup2 - duplicate fd to new_fd, closing new_fd first if open
 * Returns new_fd on success, -1 on error.
 */
int libcmd_dup2(int fd, int new_fd);

/*
 * libcmd_pipe - create a pipe
 * fds[0] is the read end, fds[1] is the write end.
 * Returns 0 on success, -1 on error.
 */
int libcmd_pipe(int fds[2]);

/*
 * libcmd_read - read from a file descriptor
 * Returns number of bytes read, 0 at EOF, -1 on error.
 */
long libcmd_read(int fd, void *buf, size_t count);

/*
 * libcmd_write - write to a file descriptor
 * Returns number of bytes written, or -1 on error.
 */
long libcmd_write(int fd, const void *buf, size_t count);

/*
 * libcmd_fdopen - wrap a file descriptor as a FILE*
 * mode: "r", "w", "a", "r+", etc.
 * Returns FILE* on success, NULL on error.
 */
FILE *libcmd_fdopen(int fd, const char *mode);

/* -------------------------------------------------------------------------
 * Process execution
 * ---------------------------------------------------------------------- */

/*
 * libcmd_exec_sync - fork, exec path with argv/envp, and wait for it
 *
 * stdin_fd / stdout_fd / stderr_fd: if >= 0, replace the corresponding
 * standard stream in the child.  Pass -1 to inherit.
 *
 * On return, *exit_info is filled in.
 * Returns 0 on success (child ran), -1 if fork/exec failed.
 */
int libcmd_exec_sync(const char *path,
                     char *const argv[],
                     char *const envp[],
                     int stdin_fd,
                     int stdout_fd,
                     int stderr_fd,
                     int nice_level,
                     libcmd_exit_info_t *exit_info);

/*
 * libcmd_exec_async - fork and exec without waiting
 * nice_level: if non-zero, setpriority() is applied in the child.
 * Returns child PID on success, -1 on error.
 */
int libcmd_exec_async(const char *path,
                      char *const argv[],
                      char *const envp[],
                      int stdin_fd,
                      int stdout_fd,
                      int stderr_fd,
                      int nice_level);

/*
 * libcmd_wait_pid - wait for a specific child process
 * Returns 0 on success, -1 on error.
 */
int libcmd_wait_pid(int pid, libcmd_exit_info_t *exit_info);

/*
 * libcmd_set_process_priority - set the calling process's scheduling
 * priority to the given nice level.
 *
 * Returns 0 on success, -1 on failure.  On old System V targets this
 * falls back to nice(2), which adjusts the priority *relative* to the
 * current value instead of setting it absolutely; modern POSIX uses
 * setpriority() (absolute).
 */
int libcmd_set_process_priority(int nice_level);

/*
 * libcmd_find_exec - search PATH for an executable named name
 * Writes the full path into out (up to out_size bytes).
 * Returns 0 on success, -1 if not found.
 */
int libcmd_find_exec(const char *name,
                     const char *path_env,
                     char *out,
                     size_t out_size);

/*
 * libcmd_exec_pipeline - execute an array of commands connected by pipes
 *
 * cmds[i]  : argv array for command i (NULL-terminated)
 * paths[i] : full path for command i
 * n        : number of commands
 * envp     : environment for all commands
 * stdin_fd / stdout_fd: first/last I/O overrides (-1 = inherit)
 *
 * Waits for all processes and fills exit_info with the exit status of the
 * last command.
 * Returns 0 on success, -1 on error.
 */
int libcmd_exec_pipeline(char *const *const *cmds,
                         const char *const *paths,
                         int n,
                         char *const envp[],
                         int stdin_fd,
                         int stdout_fd,
                         libcmd_exit_info_t *exit_info);

/*
 * libcmd_fork - raw fork(2) wrapper
 * Returns the child PID in the parent, 0 in the child, -1 on error.
 * Used by cparser.c to run pipeline stages concurrently.
 */
int libcmd_fork(void);

/*
 * libcmd_exit - raw _exit(2) wrapper (no stdio flushing, no atexit)
 */
void libcmd_exit(int status);

/*
 * libcmd_popen - run a shell command and return a pipe FILE*
 * Wraps popen(3) so cmd code stays libc-only.
 */
FILE *libcmd_popen(const char *cmd, const char *mode);

/*
 * libcmd_pclose - close a pipe opened with libcmd_popen
 * Returns the exit status of the command.
 */
int libcmd_pclose(FILE *stream);

/* -------------------------------------------------------------------------
 * Process identity
 * ---------------------------------------------------------------------- */

/*
 * libcmd_init_self - remember argv[0] and the initial working directory
 * so that libcmd_get_self_path() can fall back to locating the running
 * executable by name.  Call once early in main(); argv0 is argv[0].
 */
void libcmd_init_self(const char *argv0);

/*
 * libcmd_get_self_path - return the path of the current executable.
 *
 * Resolution order:
 *   1. /proc/self/exe (procfs; Linux, Android, *BSD with linprocfs)
 *   2. FreeBSD: sysctl(KERN_PROC_PATHNAME)
 *   3. Darwin:  _NSGetExecutablePath()
 *   4. argv[0] relative to the initial working directory
 *   5. argv[0] searched in PATH
 *
 * Returns a pointer to an internal static buffer (do not free or modify),
 * or NULL if the path could not be determined.
 */
const char *libcmd_get_self_path(void);

/* -------------------------------------------------------------------------
 * Terminal operations
 * ---------------------------------------------------------------------- */

/*
 * libcmd_isatty - return 1 if fd is connected to a terminal
 */
int libcmd_isatty(int fd);

/*
 * libcmd_cls - clear the terminal screen
 * Returns 0 on success, -1 on error.
 */
int libcmd_cls(void);

/*
 * libcmd_set_color - set terminal foreground/background color
 * fg and bg are the CMD color nibbles (0-15).
 * Pass -1 to leave the corresponding color unchanged.
 * Returns 0 on success, -1 on error.
 */
int libcmd_set_color(int fg, int bg);

/*
 * libcmd_reset_color - reset terminal colors to defaults
 */
void libcmd_reset_color(void);

/*
 * libcmd_set_title - set terminal window title
 * Returns 0 on success, -1 on error.
 */
int libcmd_set_title(const char *title);

/*
 * libcmd_get_terminal_size - get terminal dimensions
 * Returns 0 on success, -1 on error.
 */
int libcmd_get_terminal_size(int *cols, int *rows);

/* -------------------------------------------------------------------------
 * Path utilities
 * ---------------------------------------------------------------------- */

/*
 * libcmd_path_join - join two path components
 * Writes result into buf (up to size bytes).
 * Returns 0 on success, -1 if truncated.
 */
int libcmd_path_join(const char *base,
                     const char *rel,
                     char *buf,
                     size_t size);

/*
 * libcmd_path_dirname - get the directory part of a path
 * Writes into buf.  Returns 0 on success, -1 on error.
 */
int libcmd_path_dirname(const char *path, char *buf, size_t size);

/*
 * libcmd_path_basename - get the filename part of a path
 * Returns a pointer into path (not a copy).
 */
const char *libcmd_path_basename(const char *path);

/*
 * libcmd_path_ext - get the file extension (including the dot)
 * Returns pointer to the dot in path, or pointer to '\0' if no extension.
 */
const char *libcmd_path_ext(const char *path);

/*
 * libcmd_path_abs - convert a (possibly relative) path to absolute
 * Writes into buf.  Returns 0 on success, -1 on error.
 */
int libcmd_path_abs(const char *path, char *buf, size_t size);

/*
 * libcmd_path_is_abs - return 1 if path is absolute
 */
int libcmd_path_is_abs(const char *path);

/*
 * libcmd_path_normalize - normalize a path (remove . and ..)
 * Modifies path in-place.  Returns path.
 */
char *libcmd_path_normalize(char *path);

/*
 * libcmd_path_norm_sep - convert '\' separators to '/' in place
 * \usr\bin -> /usr/bin
 */
void libcmd_path_norm_sep(char *s);

/*
 * libcmd_is_switch - return 1 if arg is a switch ('/X') with the option
 * letter in known (case-insensitive).  '/-' and '/?' always count.
 * A bare '/' or an unknown letter is not a switch, so Unix absolute
 * paths like /usr/bin can be passed as arguments.
 */
int libcmd_is_switch(const char *arg, const char *known);

/* -------------------------------------------------------------------------
 * Time / date
 * ---------------------------------------------------------------------- */

/*
 * libcmd_get_local_time - get the current local time
 */
void libcmd_get_local_time(libcmd_time_t *t);

/*
 * libcmd_get_utc_time - get the current UTC time
 */
void libcmd_get_utc_time(libcmd_time_t *t);

/*
 * libcmd_set_system_time - set the system clock to the given time
 *
 * Returns 0 on success, -1 on failure (errno set).  On old System V
 * targets the sub-second part is always cleared (the stime(2) call
 * only accepts whole seconds); modern POSIX keeps the behaviour of
 * settimeofday() with a zero microsecond part.
 */
int libcmd_set_system_time(time_t t);

/*
 * libcmd_random - return a pseudo-random integer in [0, 32767]
 */
int libcmd_random(void);

/* -------------------------------------------------------------------------
 * AutoRun
 * ---------------------------------------------------------------------- */

/*
 * Forward declaration; cmd_context is defined in ccontext.h
 * and passed here as void* to avoid a circular dependency.
 */

/*
 * libcmd_exec_autorun - execute autorun scripts
 * On Unix: executes files in $PREFIX/etc/cmd/AutoRun/ that are executable.
 * run_fn is called for each autorun line (signature matches interp_run_line).
 * user_data is passed through to run_fn.
 * Returns 0 on success, -1 on error.
 */
typedef int (*libcmd_run_fn)(const char *line, void *user_data);
int libcmd_exec_autorun(const char *prefix,
                        libcmd_run_fn run_fn,
                        void *user_data);

/* -------------------------------------------------------------------------
 * Volume information
 * ---------------------------------------------------------------------- */

/*
 * libcmd_get_volume_info - get filesystem label and serial number
 * label_buf: buffer for volume label (may be empty)
 * serial: pointer to receive serial number (may be 0 on Unix)
 * Returns 0 on success, -1 on error.
 */
int libcmd_get_volume_info(const char *path,
                           char *label_buf,
                           size_t label_size,
                           unsigned long *serial);

/*
 * libcmd_get_disk_free - get free and total disk space in bytes
 * Returns 0 on success, -1 on error.
 */
int libcmd_get_disk_free(const char *path,
                         off_t *free_bytes,
                         off_t *total_bytes);

/* -------------------------------------------------------------------------
 * Error string
 * ---------------------------------------------------------------------- */

/*
 * libcmd_strerror - return a string describing the last OS error
 * The returned pointer is valid until the next call to libcmd_strerror.
 */
const char *libcmd_strerror(void);

/* -------------------------------------------------------------------------
 * Line editing / history support
 * ---------------------------------------------------------------------- */

/*
 * libcmd_readline_init - initialize line editing with history file support
 * Sets up history file at ~/.cmd_history
 */
void libcmd_readline_init(void);

/*
 * libcmd_readline_shutdown - save history and shutdown line editing
 * Persists current session history to ~/.cmd_history
 */
void libcmd_readline_shutdown(void);

/*
 * libcmd_readline - read a line with history and tab completion
 * prompt: prompt string to display
 * buf: buffer to store the read line
 * size: size of buffer
 * 
 * Returns buf on success, NULL on EOF
 */
char *libcmd_readline(const char *prompt, char *buf, size_t size);

/*
 * libcmd_readline_set_signal_hook - set the callback invoked when a
 * signal interrupts the terminal read (SIGINT/SIGTSTP while
 * editing). The callback must return the signal number that was caught
 * (0 if none); libcmd then echoes the matching '^C' / '^Z', clears the
 * line and repaints the prompt on a new line.
 */
void libcmd_readline_set_signal_hook(int (*hook)(void));

/*
 * libcmd_readline_set_file_completion - enable/disable the cmd /f:on
 * completion keys: TAB (all), CTRL+D (directories only), CTRL+F (files
 * only).  Default is disabled (emacs key behaviour kept).
 */
void libcmd_readline_set_file_completion(int on);

/* -------------------------------------------------------------------------
 * Memory streams (replacement for the GNU open_memstream)
 * ---------------------------------------------------------------------- */

/*
 * libcmd_open_memstream - open a stream that writes to a dynamic
 * in-memory buffer.
 *
 * ptr points to the buffer variable that will receive the malloc'd
 * contents, sizeloc to the variable that receives the byte length
 * (excluding the trailing NUL) when the stream is closed with
 * libcmd_memstream_close().  The caller must free(*ptr) afterwards.
 *
 * On old System V targets the stream is backed by an anonymous
 * temporary file so the same interface keeps working without the
 * GNU extension.  Returns NULL on failure (errno set).
 */
FILE *libcmd_open_memstream(char **ptr, size_t *sizeloc);

/*
 * libcmd_memstream_close - close a stream opened with
 * libcmd_open_memstream() and publish the buffered data into the
 * *ptr / *sizeloc passed at open time.  Returns 0 on success, -1 on
 * failure.
 */
int libcmd_memstream_close(FILE *stream);

#ifdef __cplusplus
}
#endif

#endif
