cmake_minimum_required(VERSION 3.20)
project(NESEmu VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG   ${CMAKE_SOURCE_DIR}/bin/debug)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/bin/release)

find_package(SDL2   CONFIG REQUIRED)
find_package(libzip CONFIG REQUIRED)
find_package(OpenGL        REQUIRED)

# ── ImGui (vendored) ──────────────────────────────────────────────────────────
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)

add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    ${IMGUI_DIR}/file/ImGuiFileDialog.cpp
)

target_include_directories(imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${IMGUI_DIR}/file
)

target_link_libraries(imgui PUBLIC SDL2::SDL2 OpenGL::GL)

set(SHARED_DIR ${CMAKE_SOURCE_DIR}/src/core/shared)
set(NES_DIR ${CMAKE_SOURCE_DIR}/src/core/nes)
set(MAPPERS_DIR ${CMAKE_SOURCE_DIR}/src/core/nes/mappers)
set(UI_DIR ${CMAKE_SOURCE_DIR}/src/ui)

# ── NES Core Library ──────────────────────────────────────────────────────────
add_library(nes_core STATIC
    ${SHARED_DIR}/AudioBackend.cpp
    ${SHARED_DIR}/EmulatorCore.cpp
    ${SHARED_DIR}/Serializer.cpp
    ${SHARED_DIR}/SharedContext.cpp
    ${MAPPERS_DIR}/AxROMMapper.cpp
    ${MAPPERS_DIR}/CNROM.cpp
    ${MAPPERS_DIR}/DxROM.cpp
    ${MAPPERS_DIR}/MapperBase.cpp
    ${MAPPERS_DIR}/MMC1.cpp
    ${MAPPERS_DIR}/MMC2Mapper.cpp
    ${MAPPERS_DIR}/MMC3.cpp
    ${MAPPERS_DIR}/NROM.cpp
    ${MAPPERS_DIR}/UxROMMapper.cpp
    ${NES_DIR}/DebuggerContext.cpp
    ${NES_DIR}/INESLoader.cpp
    ${NES_DIR}/InputMappers.cpp
    ${NES_DIR}/Nes.cpp
    ${NES_DIR}/NesAudioMapper.cpp
    ${NES_DIR}/NesBus.cpp
    ${NES_DIR}/NesCartridge.cpp
    ${NES_DIR}/NesCpu.cpp
    ${NES_DIR}/NesInput.cpp
    ${NES_DIR}/NesPpu.cpp
    ${NES_DIR}/RendererLoopy.cpp
    ${UI_DIR}/Core.cpp
    ${UI_DIR}/DebuggerUI.cpp
    ${UI_DIR}/HexViewer.cpp
    ${UI_DIR}/PPUViewer.cpp
    ${UI_DIR}/SDL_UI.cpp
)

# Add include directory for this target
target_include_directories(nes_core
    PUBLIC
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/core/shared
        ${CMAKE_SOURCE_DIR}/src/core/nes
        ${CMAKE_SOURCE_DIR}/src/core/nes/mappers
        ${CMAKE_SOURCE_DIR}/src/ui
)

target_link_libraries(nes_core PUBLIC
    $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
    SDL2::SDL2
    imgui
    libzip::zip
    OpenGL::GL
)

# ── Executable ────────────────────────────────────────────────────────────────
add_executable(nes_emu src/main.cpp)

if(MSVC)
    target_compile_options(nes_emu PRIVATE
        $<$<CONFIG:Release>:/O2 /GL>
    )
    target_link_options(nes_emu PRIVATE
        $<$<CONFIG:Release>:/LTCG>
    )
else()
    target_compile_options(nes_emu PRIVATE
        $<$<CONFIG:Release>:-O2 -s>
    )
endif()

target_link_libraries(nes_emu PRIVATE nes_core)

if(APPLE)
    target_compile_definitions(nes_emu PRIVATE GL_SILENCE_DEPRECATION)
endif()

if(WIN32)
    add_custom_command(TARGET nes_emu POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL2::SDL2>
            $<TARGET_FILE_DIR:nes_emu>
        COMMENT "Copying SDL2.dll"
    )
endif()

# ── Tests ─────────────────────────────────────────────────────────────────────
enable_testing()

find_package(GTest CONFIG REQUIRED)

add_executable(nes_emu_tests tests/test_cpu.cpp tests/test_ppu.cpp)

target_link_libraries(nes_emu_tests PRIVATE
    nes_core
    GTest::gtest
    GTest::gtest_main
)

target_include_directories(nes_emu_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/core/shared
    ${CMAKE_SOURCE_DIR}/src/core/nes
    ${CMAKE_SOURCE_DIR}/src/core/nes/mappers
    ${CMAKE_SOURCE_DIR}/src/ui
)

include(GoogleTest)
gtest_discover_tests(nes_emu_tests)