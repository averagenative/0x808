cmake_minimum_required(VERSION 3.16)
project(0x808 VERSION 0.1.0 LANGUAGES C CXX)

# Use C11 standard (future-proofs for stdatomic.h, static_assert, etc.)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# C++ for GUI layer (Dear ImGui)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Compiler warnings — disable pedantic for vendored headers using extensions
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra)
endif()

# ─── Sanitizer options ────────────────────────────────────────────────────────
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
if(ENABLE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address)
endif()

option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
if(ENABLE_UBSAN)
    add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=undefined)
endif()

# Platform-specific link libraries
if(MSVC)
    set(PLATFORM_LIBS "")
else()
    set(PLATFORM_LIBS m pthread ${CMAKE_DL_LIBS})
endif()

# ─── Dependencies ──────────────────────────────────────────────────────────────

# Single-header libs live in deps/ — we just need the include path
include_directories(${CMAKE_SOURCE_DIR}/deps)

# ─── Shine MP3 encoder library (LGPL-2, opt-in) ──────────────────────────────
# MP3 export uses shine (LGPL-2). Disabled by default for MIT license purity.
# Enable with: cmake .. -DENABLE_MP3=ON
option(ENABLE_MP3 "Enable MP3 export via shine (LGPL-2)" OFF)

if(ENABLE_MP3)
    add_library(shine STATIC
        deps/shine/bitstream.c
        deps/shine/huffman.c
        deps/shine/l3bitstream.c
        deps/shine/l3loop.c
        deps/shine/l3mdct.c
        deps/shine/l3subband.c
        deps/shine/layer3.c
        deps/shine/reservoir.c
        deps/shine/tables.c
    )
    target_include_directories(shine PUBLIC ${CMAKE_SOURCE_DIR}/deps/shine)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(shine PRIVATE -w)
    endif()
    if(CMAKE_C_COMPILER_ID MATCHES "MSVC")
        target_compile_options(shine PRIVATE /w)
    endif()
    if(NOT MSVC)
        target_link_libraries(shine PRIVATE m)
    endif()
endif()

# ─── Engine library (Layer 1: pure DSP, no GUI or platform deps) ───────────────

add_library(sq_engine STATIC
    src/core/log.c
    src/engine/engine.c
    src/engine/command_queue.c
    src/engine/sampler.c
    src/engine/mixer.c
    src/engine/transport.c
    src/engine/sequencer.c
    src/engine/envelope.c
    src/engine/synth.c
    src/engine/export.c
    src/engine/effects.c
    src/engine/kits.c
    src/formats/sample_io.c
    src/formats/project.c
    src/formats/sf2.c
    ${CMAKE_SOURCE_DIR}/deps/cJSON.c
)
target_include_directories(sq_engine PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/deps
)
# PIC required so engine can be linked into plugin shared libraries
set_target_properties(sq_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)
# Engine links only math (+ shine if MP3 enabled)
if(ENABLE_MP3)
    set_target_properties(shine PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_compile_definitions(sq_engine PRIVATE SQ_HAVE_MP3=1)
endif()
if(MSVC)
    if(ENABLE_MP3)
        target_link_libraries(sq_engine PRIVATE shine)
    endif()
else()
    if(ENABLE_MP3)
        target_link_libraries(sq_engine PRIVATE m shine)
    else()
        target_link_libraries(sq_engine PRIVATE m)
    endif()
endif()

# ─── Standalone terminal executable (Phase 1-2) ──────────────────────────────

add_executable(sequencer_standalone
    src/standalone/main.c
)
target_include_directories(sequencer_standalone PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(sequencer_standalone PRIVATE
    sq_engine
    ${PLATFORM_LIBS}
)
set_target_properties(sequencer_standalone PROPERTIES
    OUTPUT_NAME "0x808_cli"
)

# ─── App controller library (C99, shared by all frontends) ────────────────────

add_library(sq_app STATIC
    src/app/sq_app.c
    src/gui/undo.c
)
target_include_directories(sq_app PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(sq_app PUBLIC sq_engine)
set_target_properties(sq_app PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ─── GUI executable (Phase 3+: requires SDL2 and OpenGL) ─────────────────────

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND AND NOT SDL2_FOUND)
    pkg_check_modules(SDL2 QUIET sdl2)
endif()
if(NOT OpenGL_FOUND)
    find_package(OpenGL QUIET)
endif()

# Normalize OpenGL link target
if(OPENGL_gl_LIBRARY)
    set(SQ_OPENGL_LIB ${OPENGL_gl_LIBRARY})
elseif(TARGET OpenGL::GL)
    set(SQ_OPENGL_LIB OpenGL::GL)
endif()

if(SDL2_FOUND AND OpenGL_FOUND)
    message(STATUS "SDL2 and OpenGL found — building GUI target")

    # ── Dear ImGui library (C++) ──────────────────────────────────
    add_library(imgui STATIC
        deps/imgui/imgui.cpp
        deps/imgui/imgui_draw.cpp
        deps/imgui/imgui_tables.cpp
        deps/imgui/imgui_widgets.cpp
        deps/imgui/imgui_demo.cpp
        deps/imgui/backends/imgui_impl_sdl2.cpp
        deps/imgui/backends/imgui_impl_opengl3.cpp
    )
    target_include_directories(imgui PUBLIC
        ${CMAKE_SOURCE_DIR}/deps/imgui
        ${CMAKE_SOURCE_DIR}/deps/imgui/backends
        ${SDL2_INCLUDE_DIRS}
    )
    set_target_properties(imgui PROPERTIES POSITION_INDEPENDENT_CODE ON)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(imgui PRIVATE -w)  # Suppress warnings in vendored code
    endif()

    # ── GUI component library (shared between standalone and plugin) ──
    add_library(sq_gui STATIC
        src/gui/gui_globals.cpp
        src/gui/toolbar.cpp
        src/gui/drum_grid.cpp
        src/gui/knobs.cpp
        src/gui/synth_editor.cpp
        src/gui/sample_browser.cpp
        src/gui/export_dialog.cpp
        src/gui/piano_roll.cpp
        src/gui/arrangement.cpp
        src/gui/mixer_view.cpp
        src/gui/pattern_presets.cpp
        src/gui/virtual_keyboard.cpp
        src/gui/theme.cpp
    )
    target_include_directories(sq_gui PUBLIC
        ${CMAKE_SOURCE_DIR}/src
        ${SDL2_INCLUDE_DIRS}
        ${CMAKE_SOURCE_DIR}/deps/imgui
        ${CMAKE_SOURCE_DIR}/deps/imgui/backends
        ${CMAKE_SOURCE_DIR}/resources
    )
    target_link_libraries(sq_gui PUBLIC sq_app imgui)
    set_target_properties(sq_gui PROPERTIES POSITION_INDEPENDENT_CODE ON)

    # Windows icon resource
    if(WIN32)
        set(SQ_WIN_RC ${CMAKE_SOURCE_DIR}/resources/0x808.rc)
        set_source_files_properties(${SQ_WIN_RC} PROPERTIES
            COMPILE_FLAGS "-I${CMAKE_SOURCE_DIR}/resources")
    else()
        set(SQ_WIN_RC "")
    endif()

    add_executable(sequencer_gui
        src/standalone/main_gui.c
        src/gui/gui.cpp
        ${SQ_WIN_RC}
    )
    target_include_directories(sequencer_gui PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${SDL2_INCLUDE_DIRS}
        ${CMAKE_SOURCE_DIR}/deps/imgui
        ${CMAKE_SOURCE_DIR}/deps/imgui/backends
    )
    if(MSVC)
        target_link_libraries(sequencer_gui PRIVATE
            sq_gui
            ${SDL2_LIBRARIES}
            ${SQ_OPENGL_LIB}
        )
    elseif(MINGW)
        target_link_libraries(sequencer_gui PRIVATE
            sq_gui
            ${SDL2_LIBRARIES}
            ${SQ_OPENGL_LIB}
            comdlg32
        )
        target_link_options(sequencer_gui PRIVATE -static -static-libgcc -static-libstdc++ -mwindows)
    else()
        target_link_libraries(sequencer_gui PRIVATE
            sq_gui
            ${SDL2_LIBRARIES}
            ${SQ_OPENGL_LIB}
            m
            pthread
            ${CMAKE_DL_LIBS}
        )
    endif()
    set_target_properties(sequencer_gui PROPERTIES
        OUTPUT_NAME "0x808"
    )
else()
    message(STATUS "SDL2 or OpenGL not found — skipping GUI target")
    message(STATUS "  Install with: sudo apt-get install pkg-config libsdl2-dev libgl-dev")
endif()

# ─── GTK 4.0 frontend (optional, Linux only) ──────────────────────────────────

option(BUILD_GTK "Build GTK 4.0 frontend" OFF)

if(BUILD_GTK)
    if(PkgConfig_FOUND)
        pkg_check_modules(GTK4 QUIET gtk4)
    endif()
    if(GTK4_FOUND AND SDL2_FOUND)
        message(STATUS "GTK 4.0 found (${GTK4_VERSION}) — building GTK frontend")
        add_executable(sequencer_gtk
            src/gui_gtk/main_gtk.c
            src/gui_gtk/gtk_window.c
            src/gui_gtk/gtk_theme.c
            src/gui_gtk/gtk_drum_grid.c
            src/gui_gtk/gtk_piano_roll.c
            src/gui_gtk/gtk_knob.c
            src/gui_gtk/gtk_synth_editor.c
            src/gui_gtk/gtk_mixer.c
            src/gui_gtk/gtk_browser.c
            src/gui_gtk/gtk_keyboard.c
            src/gui_gtk/gtk_arrangement.c
            src/gui_gtk/gtk_presets.c
            src/gui_gtk/gtk_export.c
        )
        target_include_directories(sequencer_gtk PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${GTK4_INCLUDE_DIRS}
            ${SDL2_INCLUDE_DIRS}
        )
        target_compile_options(sequencer_gtk PRIVATE ${GTK4_CFLAGS_OTHER})
        target_link_libraries(sequencer_gtk PRIVATE
            sq_app
            ${GTK4_LIBRARIES}
            ${SDL2_LIBRARIES}
            m pthread ${CMAKE_DL_LIBS}
        )
        set_target_properties(sequencer_gtk PROPERTIES
            OUTPUT_NAME "0x808_gtk"
        )
    else()
        if(NOT GTK4_FOUND)
            message(STATUS "GTK 4.0 not found — skipping GTK frontend")
            message(STATUS "  Install with: sudo apt-get install libgtk-4-dev")
        endif()
        if(NOT SDL2_FOUND)
            message(STATUS "SDL2 not found — skipping GTK frontend (needed for audio)")
        endif()
    endif()
endif()

# ─── Plugin targets (Phase 9: VST3 and CLAP via CPLUG) ────────────────────────

option(BUILD_PLUGINS "Build VST3 and CLAP plugin targets" ON)

if(BUILD_PLUGINS)
    # Suppress warnings only on vendored CPLUG sources (not our plugin code)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        set_source_files_properties(deps/cplug/cplug_vst3.c deps/cplug/cplug_clap.c
            PROPERTIES COMPILE_OPTIONS "-w")
    elseif(MSVC)
        set_source_files_properties(deps/cplug/cplug_vst3.c deps/cplug/cplug_clap.c
            PROPERTIES COMPILE_OPTIONS "/w")
    endif()

    # Determine if we can build with embedded GUI (requires SDL2 + OpenGL)
    if(SDL2_FOUND AND OpenGL_FOUND)
        set(PLUGIN_HAS_GUI TRUE)
        message(STATUS "Building plugin targets (VST3 + CLAP) with embedded GUI")
    else()
        set(PLUGIN_HAS_GUI FALSE)
        message(STATUS "Building plugin targets (VST3 + CLAP) without GUI (SDL2/OpenGL not available)")
    endif()

    # Plugin sources: always include plugin.c + embedded samples, conditionally include GUI
    set(PLUGIN_SOURCES src/plugin/plugin.c src/plugin/embedded_samples.c)
    if(PLUGIN_HAS_GUI)
        list(APPEND PLUGIN_SOURCES src/plugin/plugin_gui.cpp)
    endif()
    set(PLUGIN_CONFIG ${CMAKE_SOURCE_DIR}/src/plugin/config.h)

    # Common plugin compile settings
    function(configure_plugin_target target_name)
        target_include_directories(${target_name} PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/deps
            ${CMAKE_SOURCE_DIR}/deps/cplug
        )
        target_link_libraries(${target_name} PRIVATE sq_engine)

        if(PLUGIN_HAS_GUI)
            target_include_directories(${target_name} PRIVATE
                ${SDL2_INCLUDE_DIRS}
                ${CMAKE_SOURCE_DIR}/deps/imgui
                ${CMAKE_SOURCE_DIR}/deps/imgui/backends
            )
            target_link_libraries(${target_name} PRIVATE sq_gui ${SDL2_LIBRARIES} ${SQ_OPENGL_LIB})
        else()
            target_compile_definitions(${target_name} PRIVATE SQ_PLUGIN_NO_GUI=1)
        endif()

        if(NOT MSVC)
            target_link_libraries(${target_name} PRIVATE m pthread ${CMAKE_DL_LIBS})
        endif()
        # Force-include the plugin config header
        if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(${target_name} PRIVATE
                -include${PLUGIN_CONFIG}
                -Wno-multichar
            )
        elseif(MSVC)
            target_compile_options(${target_name} PRIVATE /FI${PLUGIN_CONFIG})
        endif()
        # Position-independent code for shared libraries
        set_target_properties(${target_name} PROPERTIES POSITION_INDEPENDENT_CODE ON)
        # Static-link all mingw runtimes into the DLL
        if(MINGW)
            target_link_options(${target_name} PRIVATE -static -static-libgcc -static-libstdc++)
        endif()
    endfunction()

    # ── VST3 plugin ──────────────────────────────────────────────
    add_library(sequencer_vst3 MODULE
        ${PLUGIN_SOURCES}
        deps/cplug/cplug_vst3.c
    )
    configure_plugin_target(sequencer_vst3)
    set_target_properties(sequencer_vst3 PROPERTIES
        OUTPUT_NAME "0x808"
        PREFIX ""
    )

    # Post-build: create VST3 bundle structure
    if(WIN32)
        # Windows: .vst3/Contents/x86_64-win/0x808.vst3
        set(VST3_BUNDLE "${CMAKE_BINARY_DIR}/0x808.vst3")
        add_custom_command(TARGET sequencer_vst3 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${VST3_BUNDLE}/Contents/x86_64-win"
            COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:sequencer_vst3>" "${VST3_BUNDLE}/Contents/x86_64-win/0x808.vst3"
            COMMENT "Creating VST3 bundle: ${VST3_BUNDLE}"
        )
    elseif(NOT APPLE)
        # Linux: .vst3/Contents/x86_64-linux/0x808.so
        set(VST3_BUNDLE "${CMAKE_BINARY_DIR}/0x808.vst3")
        add_custom_command(TARGET sequencer_vst3 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${VST3_BUNDLE}/Contents/x86_64-linux"
            COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:sequencer_vst3>" "${VST3_BUNDLE}/Contents/x86_64-linux/0x808.so"
            COMMENT "Creating VST3 bundle: ${VST3_BUNDLE}"
        )
    endif()

    # ── CLAP plugin ──────────────────────────────────────────────
    add_library(sequencer_clap MODULE
        ${PLUGIN_SOURCES}
        deps/cplug/cplug_clap.c
    )
    configure_plugin_target(sequencer_clap)
    set_target_properties(sequencer_clap PROPERTIES
        OUTPUT_NAME "0x808"
        PREFIX ""
        SUFFIX ".clap"
    )

    message(STATUS "Plugin targets: 0x808 VST3, 0x808 CLAP")
endif()

# ─── Engine render test (offline WAV export, no audio device needed) ─────────

add_executable(engine_render_test
    tests/engine_render_test.c
)
target_include_directories(engine_render_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(engine_render_test PRIVATE
    sq_engine
    ${PLATFORM_LIBS}
)

add_executable(export_test
    tests/export_test.c
)
target_include_directories(export_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(export_test PRIVATE
    sq_engine
    ${PLATFORM_LIBS}
)

add_executable(project_test
    tests/project_test.c
)
target_include_directories(project_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(project_test PRIVATE
    sq_engine
    ${PLATFORM_LIBS}
)

add_executable(fm_synth_test
    tests/fm_synth_test.c
)
target_include_directories(fm_synth_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(fm_synth_test PRIVATE
    sq_engine
    ${PLATFORM_LIBS}
)

add_executable(swing_humanize_test
    tests/swing_humanize_test.c
)
target_include_directories(swing_humanize_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(swing_humanize_test PRIVATE
    sq_engine
    ${PLATFORM_LIBS}
)

add_executable(effects_test tests/effects_test.c)
target_include_directories(effects_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(effects_test PRIVATE sq_engine ${PLATFORM_LIBS})

add_executable(synth_test tests/synth_test.c)
target_include_directories(synth_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(synth_test PRIVATE sq_engine ${PLATFORM_LIBS})

add_executable(wavetable_test tests/wavetable_test.c)
target_include_directories(wavetable_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(wavetable_test PRIVATE sq_engine ${PLATFORM_LIBS})

add_executable(undo_test tests/undo_test.c src/gui/undo.c)
target_include_directories(undo_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(undo_test PRIVATE sq_engine ${PLATFORM_LIBS})

add_executable(malformed_input_test tests/malformed_input_test.c)
target_include_directories(malformed_input_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(malformed_input_test PRIVATE sq_engine ${PLATFORM_LIBS})

add_executable(edge_case_test tests/edge_case_test.c)
target_include_directories(edge_case_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(edge_case_test PRIVATE sq_engine ${PLATFORM_LIBS})

add_executable(gui_feature_test tests/gui_feature_test.c)
target_include_directories(gui_feature_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(gui_feature_test PRIVATE sq_engine ${PLATFORM_LIBS})

add_executable(fuzz_project tests/fuzz_project.c)
target_include_directories(fuzz_project PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(fuzz_project PRIVATE sq_engine ${PLATFORM_LIBS})

add_executable(fuzz_sample_io tests/fuzz_sample_io.c)
target_include_directories(fuzz_sample_io PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(fuzz_sample_io PRIVATE sq_engine ${PLATFORM_LIBS})

add_executable(plugin_load_test tests/plugin_load_test.c)
target_include_directories(plugin_load_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(plugin_load_test PRIVATE sq_engine ${PLATFORM_LIBS})

add_executable(snapshot_test tests/snapshot_test.c)
target_include_directories(snapshot_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(snapshot_test PRIVATE sq_engine ${PLATFORM_LIBS})
