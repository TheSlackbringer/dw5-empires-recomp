# dw5e - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(dw5e LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(DW5E_BUILD_GAME "Build the game executable" ON)
option(DW5E_BUILD_SETUP "Build the setup wizard launcher" ON)

# Configurable generated code directory (commit 258fc59)
# In developer builds, default is generated/default.
# When built by the player's release launcher, DW5E_GENERATED_DIR points to <player-data>/generated.
set(DW5E_GENERATED_DIR "" CACHE PATH "Directory containing generated recompiled code")

include(generated/rexglue.cmake)


# =============================================================================
# Game Executable
# =============================================================================
if(DW5E_BUILD_GAME)
    set(DW5E_SOURCES
        src/main.cpp
        src/dw5e_input.cpp
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

    if(TARGET imgui)
        target_include_directories(dw5e PRIVATE
            $<TARGET_PROPERTY:imgui,INTERFACE_INCLUDE_DIRECTORIES>)
    endif()
endif()

# =============================================================================
# Setup Wizard / Release Launcher (DW5E)
# =============================================================================
if(DW5E_BUILD_SETUP)
    set(DW5E_SETUP_SOURCES
        setup/xdvdfs.cpp
        setup/install_state.cpp
        setup/run_process.cpp
        setup/setup_layout.cpp
        setup/codegen.cpp
        setup/build_game.cpp
        setup/setup_pipeline.cpp
        setup/wizard_ui.cpp
        setup/wizard_main.cpp
    )

    # ImGui SDL3 & SDL_Renderer backends from SDK thirdparty
    if(REXSDK_DIR)
        set(IMGUI_BACKENDS_DIR "${REXSDK_DIR}/thirdparty/imgui/backends")
    else()
        set(IMGUI_BACKENDS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../rexglue-sdk/thirdparty/imgui/backends")
    endif()

    if(EXISTS "${IMGUI_BACKENDS_DIR}/imgui_impl_sdl3.cpp")
        list(APPEND DW5E_SETUP_SOURCES
            "${IMGUI_BACKENDS_DIR}/imgui_impl_sdl3.cpp"
            "${IMGUI_BACKENDS_DIR}/imgui_impl_sdlrenderer3.cpp"
        )
    endif()

    if(WIN32)
        add_executable(dw5e_setup WIN32 ${DW5E_SETUP_SOURCES})
    else()
        add_executable(dw5e_setup ${DW5E_SETUP_SOURCES})
    endif()

    set_target_properties(dw5e_setup PROPERTIES OUTPUT_NAME "DW5E")

    target_include_directories(dw5e_setup PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/setup
        ${CMAKE_CURRENT_SOURCE_DIR}/../rexglue-sdk/thirdparty/imgui
        ${CMAKE_CURRENT_SOURCE_DIR}/../rexglue-sdk/thirdparty/imgui/backends
        ${CMAKE_CURRENT_SOURCE_DIR}/../rexglue-sdk/thirdparty/sdl3/include
    )

    find_package(Threads REQUIRED)
    target_link_libraries(dw5e_setup PRIVATE Threads::Threads)

    if(TARGET imgui)
        target_link_libraries(dw5e_setup PRIVATE imgui)
        target_include_directories(dw5e_setup PRIVATE
            $<TARGET_PROPERTY:imgui,INTERFACE_INCLUDE_DIRECTORIES>)
    endif()

    if(TARGET SDL3-static)
        target_link_libraries(dw5e_setup PRIVATE SDL3-static)
    elseif(TARGET SDL3::SDL3-static)
        target_link_libraries(dw5e_setup PRIVATE SDL3::SDL3-static)
    elseif(TARGET SDL3::SDL3)
        target_link_libraries(dw5e_setup PRIVATE SDL3::SDL3)
    endif()
endif()

# =============================================================================
# Setup Tests
# =============================================================================
enable_testing()
add_executable(dw5e_setup_test
    tests/setup_test.cpp
    setup/xdvdfs.cpp
    setup/install_state.cpp
    setup/setup_layout.cpp
    setup/build_game.cpp
    setup/run_process.cpp
)
find_package(Threads REQUIRED)
target_link_libraries(dw5e_setup_test PRIVATE Threads::Threads)
target_include_directories(dw5e_setup_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/setup
)
add_test(NAME setup_unit_tests COMMAND dw5e_setup_test)



# Standalone lifecycle/recovery fixtures; no game data or visible windows.
add_executable(dw5e_setup_reliability_test
    tests/setup_reliability_test.cpp
    setup/xdvdfs.cpp setup/install_state.cpp setup/setup_layout.cpp
    setup/build_game.cpp setup/run_process.cpp setup/codegen.cpp setup/setup_pipeline.cpp
)
target_include_directories(dw5e_setup_reliability_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dw5e_setup_reliability_test PRIVATE Threads::Threads)
add_test(NAME setup_reliability_tests COMMAND dw5e_setup_reliability_test)

add_executable(dw5e_safe_read_test tests/safe_read_test.cpp)
target_include_directories(dw5e_safe_read_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME snapshot_safe_read_tests COMMAND dw5e_safe_read_test)

# Header-only guest frame rate used by the F3 overlay (issue 007); SDK headers only, no runtime link.
add_executable(dw5e_guest_frame_stats_test tests/guest_frame_stats_test.cpp)
target_include_directories(dw5e_guest_frame_stats_test PRIVATE
    $<TARGET_PROPERTY:rex::runtime,INTERFACE_INCLUDE_DIRECTORIES>)
add_test(NAME guest_frame_stats_tests COMMAND dw5e_guest_frame_stats_test)

# Compile host diagnostics in CI even when retail game code is unavailable.
add_library(dw5e_host_portability OBJECT EXCLUDE_FROM_ALL src/dw5e_snapshot.cpp)
target_link_libraries(dw5e_host_portability PRIVATE rex::runtime)
rexglue_apply_target_settings(dw5e_host_portability)
if(WIN32)
    add_executable(dw5e_dxc_uuid_test tests/dxc_uuid_test.cpp)
    target_include_directories(dw5e_dxc_uuid_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../rexglue-sdk/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../rexglue-sdk/thirdparty/dxc/include)
    add_test(NAME dxc_uuid_tests COMMAND dw5e_dxc_uuid_test)
endif()

add_executable(dw5e_kernel_export_test tests/kernel_export_test.cpp)
target_link_libraries(dw5e_kernel_export_test PRIVATE rex::runtime)
add_test(NAME kernel_export_link_tests COMMAND dw5e_kernel_export_test)

# Run the real application path-config hook and native input with no SDL
# initialization, device access, window, or generated/retail game code.
add_executable(dw5e_keyboard_test tests/keyboard_test.cpp src/dw5e_input.cpp
    ${REXGLUE_SHARE_DIR}/rex_app.cpp)
target_link_libraries(dw5e_keyboard_test PRIVATE rex::runtime)
target_compile_definitions(dw5e_keyboard_test PRIVATE REXGLUE_BUILD_CONFIG="$<CONFIG>")
target_include_directories(dw5e_keyboard_test PRIVATE
    $<TARGET_PROPERTY:imgui,INTERFACE_INCLUDE_DIRECTORIES>)
rexglue_apply_target_settings(dw5e_keyboard_test)
add_test(NAME keyboard_unit_tests COMMAND dw5e_keyboard_test)

# Load the real runtime plugin, rather than only proving it compiles/links.
add_executable(dw5e_gpu_plugin_test tests/gpu_plugin_test.cpp)
target_link_libraries(dw5e_gpu_plugin_test PRIVATE rex::runtime)
rexglue_apply_target_settings(dw5e_gpu_plugin_test)
add_dependencies(dw5e_gpu_plugin_test rexgpu-xenos)
add_custom_command(TARGET dw5e_gpu_plugin_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:rexgpu-xenos>
        $<TARGET_FILE_DIR:dw5e_gpu_plugin_test>
    VERBATIM)
if(WIN32)
    if(MINGW)
        # Like the game host, initialize the Unicode CRT executable path.
        target_link_options(dw5e_gpu_plugin_test PRIVATE -municode)
    endif()
    add_custom_command(TARGET dw5e_gpu_plugin_test POST_BUILD
        COMMAND "$<$<BOOL:$<TARGET_RUNTIME_DLLS:dw5e_gpu_plugin_test>>:${CMAKE_COMMAND};-E;copy_if_different;$<TARGET_RUNTIME_DLLS:dw5e_gpu_plugin_test>;$<TARGET_FILE_DIR:dw5e_gpu_plugin_test>>"
        COMMAND_EXPAND_LISTS
        VERBATIM)
endif()
add_test(NAME gpu_plugin_load_tests COMMAND dw5e_gpu_plugin_test)

# Hardware check, invoked explicitly on a Vulkan-capable Linux host. CTests
# stay independent of GPU availability (including clean CI).
if(UNIX AND NOT APPLE AND REXGLUE_USE_VULKAN)
    add_executable(dw5e_vulkan_startup_test EXCLUDE_FROM_ALL tests/vulkan_startup_test.cpp)
    target_link_libraries(dw5e_vulkan_startup_test PRIVATE rex::runtime)
    target_include_directories(dw5e_vulkan_startup_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../rexglue-sdk/thirdparty/renderdoc)
    rexglue_apply_target_settings(dw5e_vulkan_startup_test)
endif()
