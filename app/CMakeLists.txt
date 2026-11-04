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
        src/dw5e_frame_timing.cpp
        src/dw5e_pacing.cpp
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
        setup/install_location.cpp
        setup/run_process.cpp
        setup/setup_layout.cpp
        setup/codegen.cpp
        setup/build_game.cpp
        setup/setup_pipeline.cpp
        setup/installation.cpp
        setup/uninstall_process.cpp
        setup/wizard_storage.cpp
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
        enable_language(C)
        add_executable(dw5e_bootstrap WIN32 setup/bootstrap_win.c)
        set_target_properties(dw5e_bootstrap PROPERTIES OUTPUT_NAME "DW5E-bootstrap")
        if(MINGW)
            target_link_options(dw5e_bootstrap PRIVATE -municode)
        endif()
        add_dependencies(dw5e_setup dw5e_bootstrap)
    else()
        add_executable(dw5e_setup ${DW5E_SETUP_SOURCES})
    endif()

    add_executable(dw5e_uninstall setup/uninstall_main.cpp setup/installation.cpp
        setup/install_location.cpp setup/install_state.cpp)
    set_target_properties(dw5e_uninstall PROPERTIES OUTPUT_NAME "dw5e-uninstall")
    if(WIN32)
        target_link_libraries(dw5e_uninstall PRIVATE shell32 user32)
        set_target_properties(dw5e_uninstall PROPERTIES WIN32_EXECUTABLE TRUE)
        if(NOT MSVC)
            target_link_options(dw5e_uninstall PRIVATE -static)
        endif()
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # This standalone helper has no C++ ABI boundary with the wizard.
        # Keep glibc dynamic to avoid conflicting libc.a and libpthread.a in Sniper.
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            target_compile_options(dw5e_uninstall PRIVATE -stdlib=libstdc++)
            target_link_options(dw5e_uninstall PRIVATE -stdlib=libstdc++)
        endif()
        target_link_options(dw5e_uninstall PRIVATE -static-libstdc++ -static-libgcc)
    endif()
    add_dependencies(dw5e_setup dw5e_uninstall)

    set_target_properties(dw5e_setup PROPERTIES OUTPUT_NAME "DW5E")
    if(WIN32 AND DW5E_BUILD_GAME)
        # Windows file names ignore case: DW5E.exe and the game's dw5e.exe would be one file.
        set_target_properties(dw5e_setup PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/setup")
    endif()

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
    setup/install_location.cpp
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
    setup/xdvdfs.cpp setup/install_state.cpp
    setup/install_location.cpp setup/setup_layout.cpp
    setup/build_game.cpp setup/run_process.cpp setup/codegen.cpp setup/setup_pipeline.cpp setup/installation.cpp
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

add_executable(dw5e_frame_timing_test tests/frame_timing_test.cpp)
target_include_directories(dw5e_frame_timing_test PRIVATE
    $<TARGET_PROPERTY:rex::runtime,INTERFACE_INCLUDE_DIRECTORIES>)
add_test(NAME frame_timing_tests COMMAND dw5e_frame_timing_test)

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

# Portable resolver and wizard selection fixtures require no game/disc inputs.
add_executable(dw5e_install_location_test tests/install_location_test.cpp
    setup/install_state.cpp setup/install_location.cpp setup/setup_layout.cpp)
target_include_directories(dw5e_install_location_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME install_location_tests COMMAND dw5e_install_location_test)
if(WIN32)
    foreach(target dw5e_setup dw5e_setup_test dw5e_setup_reliability_test dw5e_install_location_test)
        if(TARGET ${target})
            target_link_libraries(${target} PRIVATE shell32)
        endif()
    endforeach()
endif()

add_executable(dw5e_install_folder_test tests/install_folder_test.cpp
    setup/install_state.cpp setup/install_location.cpp)
target_include_directories(dw5e_install_folder_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME install_folder_default_tests COMMAND dw5e_install_folder_test $<TARGET_FILE_DIR:dw5e_install_folder_test>)
set_tests_properties(install_folder_default_tests PROPERTIES ENVIRONMENT "DW5E_DATA_DIR=;APPIMAGE=")
add_executable(dw5e_install_fake_game tests/install_fake_game.cpp setup/install_location.cpp)
if(WIN32)
    # Match the real GUI game: a console fixture creates a separate console lifetime/working-folder owner.
    set_target_properties(dw5e_install_fake_game PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
target_include_directories(dw5e_install_fake_game PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_executable(dw5e_launcher_selection_driver
    tests/wizard_destination_fixture.cpp setup/wizard_main.cpp setup/wizard_storage.cpp setup/installation.cpp
    setup/xdvdfs.cpp setup/install_state.cpp setup/install_location.cpp setup/setup_layout.cpp
    setup/build_game.cpp setup/run_process.cpp setup/codegen.cpp setup/setup_pipeline.cpp)
target_include_directories(dw5e_launcher_selection_driver PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/setup)
target_link_libraries(dw5e_launcher_selection_driver PRIVATE Threads::Threads)
if(WIN32)
    set_target_properties(dw5e_launcher_selection_driver PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
if(WIN32)
    foreach(target dw5e_install_folder_test dw5e_install_fake_game dw5e_launcher_selection_driver)
        target_link_libraries(${target} PRIVATE shell32)
    endforeach()
endif()
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND AND TARGET dw5e_setup AND CMAKE_HOST_SYSTEM_NAME STREQUAL CMAKE_SYSTEM_NAME)
    if(WIN32)
        set(bootstrap_test_args --bootstrap $<TARGET_FILE:dw5e_bootstrap>)
    endif()
    add_test(NAME install_launcher_integration_tests COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/launcher_install_test.py
        --launcher $<TARGET_FILE:dw5e_setup> --game $<TARGET_FILE:dw5e_install_fake_game>
        --selection-driver $<TARGET_FILE:dw5e_launcher_selection_driver>
        --uninstaller $<TARGET_FILE:dw5e_uninstall>
        ${bootstrap_test_args}
        --out ${CMAKE_CURRENT_BINARY_DIR}/install-launcher-test)
endif()

# Host filesystem/config/logging only; no window, devices or guest code.
add_executable(dw5e_host_unicode_test tests/host_unicode_test.cpp)
target_link_libraries(dw5e_host_unicode_test PRIVATE rex::runtime)
rexglue_apply_target_settings(dw5e_host_unicode_test)
add_test(NAME host_unicode_paths_tests COMMAND dw5e_host_unicode_test)

add_executable(dw5e_storage_policy_test tests/storage_policy_test.cpp setup/installation.cpp setup/install_location.cpp setup/install_state.cpp)
target_include_directories(dw5e_storage_policy_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dw5e_storage_policy_test PRIVATE Threads::Threads)
if(WIN32)
    target_link_libraries(dw5e_storage_policy_test PRIVATE shell32)
endif()
add_test(NAME storage_policy_tests COMMAND dw5e_storage_policy_test)
add_executable(dw5e_storage_default_test tests/storage_default_test.cpp setup/install_location.cpp)
target_include_directories(dw5e_storage_default_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(WIN32)
    target_link_libraries(dw5e_storage_default_test PRIVATE shell32)
endif()
add_test(NAME storage_default_tests COMMAND dw5e_storage_default_test)
set_tests_properties(storage_default_tests PROPERTIES ENVIRONMENT "DW5E_DATA_DIR=")

if(WIN32 AND Python3_Interpreter_FOUND AND CMAKE_HOST_SYSTEM_NAME STREQUAL CMAKE_SYSTEM_NAME)
    add_test(NAME bootstrap_tests COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/bootstrap_test.py
        --bootstrap $<TARGET_FILE:dw5e_bootstrap> --child $<TARGET_FILE:dw5e_install_fake_game>
        --out ${CMAKE_CURRENT_BINARY_DIR}/bootstrap-test)
endif()
