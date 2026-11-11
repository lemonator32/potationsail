cmake_minimum_required(VERSION 3.16)
project(potationsail VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# libsai (C99, no dependencies). Its own tools/tests are not needed here.
set(SAI_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(SAI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(libsai)

# ---------------------------------------------------------------------------
# potcore: everything that does not touch the Win32 UI.  Unit tested.
# ---------------------------------------------------------------------------
add_library(potcore STATIC
    src/core/Blend.cpp
    src/core/Compositor.cpp
    src/core/Brush.cpp
    src/core/Stabilizer.cpp
    src/core/Document.cpp
    src/core/Undo.cpp)
target_include_directories(potcore PUBLIC src/core)
target_link_libraries(potcore PUBLIC sai)

if(MSVC)
    target_compile_options(potcore PRIVATE /W4 /permissive- /utf-8)
    target_compile_definitions(potcore PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(potcore PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# The application (Windows only: Win32 + GDI, no third-party UI toolkit).
# ---------------------------------------------------------------------------
option(POT_BUILD_APP "Build the Win32 application" ON)
if(WIN32 AND POT_BUILD_APP)
    add_executable(potationsail WIN32
        src/app/main.cpp
        src/app/MainWindow.cpp
        src/app/CanvasView.cpp
        src/app/LayerPanel.cpp
        src/app/ToolPanel.cpp
        src/app/Text.cpp)
    target_include_directories(potationsail PRIVATE src/app)
    target_link_libraries(potationsail PRIVATE potcore comctl32 comdlg32 user32 gdi32)
    target_compile_definitions(potationsail PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
                               _CRT_SECURE_NO_WARNINGS)
    if(MSVC)
        target_compile_options(potationsail PRIVATE /W4 /permissive- /utf-8)
    endif()
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()
add_executable(potcore_tests tests/test_core.cpp)
target_link_libraries(potcore_tests PRIVATE potcore)
if(MSVC)
    target_compile_options(potcore_tests PRIVATE /W4 /utf-8)
    target_compile_definitions(potcore_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
add_test(NAME potcore COMMAND potcore_tests)
