-- xmake project for cmd and COMMAND.COM.
set_project("cmd")
set_version("0.1.0")

add_rules("mode.debug", "mode.release")

-- Both personalities are built from the same C sources.  COMMAND.COM is
-- the classic MS-DOS variant, which additionally executes AUTOEXEC.BAT.
target("cmd")
    set_kind("binary")
    add_files("*.c")
    set_filename("cmd.exe")
    if is_plat("windows") then
        add_files("cmd.rc")
    end

target("COMMAND")
    set_kind("binary")
    add_files("*.c")
    add_defines("ENABLE_AUTOEXEC=1")
    set_filename("COMMAND.COM")
    if is_plat("windows") then
        add_files("cmd.rc")
    end
