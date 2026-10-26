# dw5e - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(dw5e LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(DW5E_SOURCES
    src/main.cpp
    src/dw5e_trace_hooks.cpp
    src/dw5e_snapshot.cpp
    src/dw5e_camera.cpp
)

if(WIN32)
    add_executable(dw5e WIN32 ${DW5E_SOURCES})
else()
    add_executable(dw5e ${DW5E_SOURCES})
endif()

rexglue_setup_target(dw5e GPU_PLUGINS xenos)

# SDK source-tree builds compile rex_app.cpp into the host but don't export
# imgui's include path; borrow it without linking the OBJECT library twice.
if(TARGET imgui)
    target_include_directories(dw5e PRIVATE
        $<TARGET_PROPERTY:imgui,INTERFACE_INCLUDE_DIRECTORIES>)
endif()
