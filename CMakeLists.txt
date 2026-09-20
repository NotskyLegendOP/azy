cmake_minimum_required(VERSION 3.20)

project(AzySkin
    VERSION 1.3.0
    DESCRIPTION "Azy Skin - a lightweight visual skin for Adobe Premiere Pro"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(AZY_BUILD_TESTS "Build the portable core unit tests" ON)
option(AZY_EMBED_MANIFEST "Embed the DPI-aware/compatibility application manifest" ON)
option(AZY_WARNINGS "Enable a strict warning set" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# Keep the binary small: no unnecessary runtime, no exceptions across the Win32
# boundary. Azy uses error codes internally, not exceptions.
if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /EHsc /Gy /Gw)
    add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    add_link_options(/OPT:REF /OPT:ICF)
else()
    add_compile_options(-Wall -Wextra)
    add_compile_definitions(UNICODE _UNICODE)
endif()
if(AZY_WARNINGS AND NOT MSVC)
    add_compile_options(-Wno-unused-parameter)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # Only affects libc++'s own headers, which are not our code.
    add_compile_options(-Wno-nullability-completeness)
endif()

# ---------------------------------------------------------------------------
# Portable core (no Windows dependencies - unit tested on any platform)
# ---------------------------------------------------------------------------
add_library(azy_core STATIC
    src/core/capture_math.cpp
    src/core/compat.cpp
    src/core/failure_tracker.cpp
    src/core/log.cpp
    src/core/overlay_style.cpp
    src/core/panel_map.cpp
    src/core/product.cpp
    src/core/settings.cpp
    src/core/strings.cpp
    src/core/theme.cpp
    src/core/version.cpp)
target_include_directories(azy_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# ---------------------------------------------------------------------------
# Windows application
# ---------------------------------------------------------------------------
if(WIN32)
    add_executable(AzySkin WIN32
        src/app/app_controller.cpp
        src/app/app_settings_store.cpp
        src/app/main.cpp
        src/win32/capture/d3d_shared.cpp
        src/win32/capture/wgc_abi.cpp
        src/win32/capture/window_capture.cpp
        src/win32/detect/premiere_detector.cpp
        src/win32/detect/premiere_probe.cpp
        src/win32/detect/process_scanner.cpp
        src/win32/gloss/gloss_overlay.cpp
        src/win32/os/autostart.cpp
        src/win32/os/file_watcher.cpp
        src/win32/os/win_api.cpp
        src/win32/os/win_util.cpp
        src/win32/os/win_version.cpp
        src/win32/performance/performance_manager.cpp
        src/win32/skin/composition_surface.cpp
        src/win32/skin/debug_overlay.cpp
        src/win32/skin/dwm_composer.cpp
        src/win32/skin/gdiplus_renderer.cpp
        src/win32/skin/input_guard.cpp
        src/win32/skin/overlay_veil.cpp
        src/win32/skin/skin_engine.cpp
        src/win32/skin/skin_types.cpp
        src/win32/skin/window_tracker.cpp
        src/win32/ui/app_icon.cpp
        src/win32/ui/settings_window.cpp
        src/win32/ui/tray.cpp
        src/win32/watch/event_watch.cpp)

    target_include_directories(AzySkin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/src
                                               ${CMAKE_CURRENT_BINARY_DIR}/generated)

    # The composition shader is a real .hlsl file that is embedded at configure
    # time, so the executable stays self-contained (no shader file to find next to
    # the .exe) while the shader is still editable as a shader. The dependency
    # makes cmake re-run when it changes.
    set(AZY_GLOSS_HLSL "${CMAKE_CURRENT_SOURCE_DIR}/resources/shaders/gloss.hlsl")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${AZY_GLOSS_HLSL}")
    if(EXISTS "${AZY_GLOSS_HLSL}")
        file(READ "${AZY_GLOSS_HLSL}" AZY_GLOSS_SOURCE)
        file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/azy_gloss_shader.hpp"
"// Generated from resources/shaders/gloss.hlsl by CMake - do not edit.
#pragma once
namespace azy { namespace win { inline constexpr const char* kGlossShaderSource = R\"AZYHLSL(${AZY_GLOSS_SOURCE})AZYHLSL\"; } }
")
    else()
        message(FATAL_ERROR "resources/shaders/gloss.hlsl is missing")
    endif()
    # One source of truth for the version: the installer, the executable's
    # version resource and the runtime banner all follow project(... VERSION).
    target_compile_definitions(AzySkin PRIVATE AZY_VERSION_STRING="${PROJECT_VERSION}")
    target_link_libraries(AzySkin PRIVATE
        azy_core
        # Documented, long-stable Windows libraries only.
        advapi32     # registry (Start with Windows, system theme)
        comctl32     # settings window controls
        dwmapi       # window composition attributes + extended frame bounds
        gdi32 gdiplus  # the single ring bitmap Azy draws
        ole32 oleaut32 # COM/WMI (optional process notifications)
        shlwapi      # path helpers
        user32       # window management, hooks, DPI
        uxtheme       # dark theming of Azy's own settings window
        # The duplicate window. Documented, long-stable Windows libraries only:
        d3d11 dcomp dxgi  # GPU capture stage and the compositor hand-off
        version      # VERSIONINFO of the Premiere executable (read-only)
        wbemuuid     # WMI CLSIDs for the optional process observer
        shell32)     # tray icon, shell integration

    # Rich edit / comctl32 v6 live in the manifest; when a resource compiler is
    # available we embed it (the code also works without it, using its runtime
    # DPI-awareness fallback).
    set(_azy_have_rc FALSE)
    if(MSVC)
        set(_azy_have_rc TRUE)  # rc.exe ships with the toolchain
    elseif(CMAKE_RC_COMPILER AND EXISTS "${CMAKE_RC_COMPILER}")
        # CMake happily records a resource compiler that is not actually
        # installed (a `windres` that is merely on the search path, for example);
        # embedding the resources then fails at build time, so verify it exists.
        set(_azy_have_rc TRUE)
    endif()

    if(AZY_EMBED_MANIFEST AND _azy_have_rc)
        target_sources(AzySkin PRIVATE resources/azy_skin.rc)
        target_include_directories(AzySkin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/resources)
        if(MSVC)
            # The manifest comes from the resource script. Without this the linker
            # generates a second one and cvtres fails with CVT1100 ("duplicate
            # resource: type:MANIFEST, name:1"). One manifest: the one in
            # resources/azy_skin.manifest.
            target_link_options(AzySkin PRIVATE /MANIFEST:NO)
        endif()
    elseif(AZY_EMBED_MANIFEST)
        message(STATUS "No usable resource compiler: the manifest, icon and version "
                       "resources are skipped (the application falls back to its "
                       "runtime DPI-awareness call)")
    endif()

    if(MINGW)
        # MinGW needs this to pick wWinMain instead of the ANSI entry point, and
        # strip is worth ~40% of the binary for a release build.
        target_link_options(AzySkin PRIVATE -municode)
        if(CMAKE_BUILD_TYPE STREQUAL "Release")
            target_link_options(AzySkin PRIVATE -s)
        endif()
    endif()

    set_target_properties(AzySkin PROPERTIES
        OUTPUT_NAME "AzySkin"
        VS_DPI_AWARE "PerMonitor")
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(AZY_BUILD_TESTS)
    enable_testing()
    add_executable(azy_core_tests tests/core_tests.cpp)
    target_link_libraries(azy_core_tests PRIVATE azy_core)
    if(WIN32)
        # The tests are a console application even on Windows.
        set_target_properties(azy_core_tests PROPERTIES WIN32_EXECUTABLE OFF)
    endif()
    add_test(NAME azy_core_tests COMMAND azy_core_tests)
endif()
