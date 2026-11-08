cmake_minimum_required(VERSION 3.13)

project(cmd VERSION 0.1.0 LANGUAGES C)

file(GLOB CMDLIB_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.c")

if(WIN32)
    enable_language(RC)
    list(APPEND CMDLIB_SOURCES cmd.rc)
endif()

add_executable(cmd ${CMDLIB_SOURCES})
add_executable(COMMAND ${CMDLIB_SOURCES})

target_compile_definitions(COMMAND PRIVATE ENABLE_AUTOEXEC=1)

set_target_properties(cmd PROPERTIES
    PREFIX ""
    OUTPUT_NAME "cmd"
    SUFFIX ".exe"
)

set_target_properties(COMMAND PROPERTIES
    PREFIX ""
    OUTPUT_NAME "COMMAND"
    SUFFIX ".COM"
)
