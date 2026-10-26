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


