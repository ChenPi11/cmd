/*
 * cmain_command.c - COMMAND.COM entry point for the mcpp build.
 *
 * mcpp compiles a package's shared sources once, so a target-level define
 * cannot produce two different cmain.o files.  This wrapper gives
 * COMMAND.COM its own entry translation unit; mcpp.toml applies
 * ENABLE_AUTOEXEC=1 to this entry only.  The other build systems compile
 * the original cmain.c directly for both targets.
 */

#include "../cmain.c"
