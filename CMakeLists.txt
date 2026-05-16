cmake_minimum_required(VERSION 3.20)
project(NESEmu VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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

add_executable(nes_emu src/main.cpp)

target_link_libraries(nes_emu PRIVATE
    $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
    SDL2::SDL2
    imgui
    libzip::zip
    OpenGL::GL
)

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