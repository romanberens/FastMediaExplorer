cmake_minimum_required(VERSION 3.24)
project(FastMediaExplorer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(FastMediaExplorer WIN32
    src/main.cpp
    src/ui/main_window.cpp
    src/ui/list_panel.cpp
    src/ui/thumb_service.cpp
    src/core/file_indexer.cpp
    src/core/duplicate_analysis.cpp
    src/core/media_query.cpp
)

target_include_directories(FastMediaExplorer PRIVATE include)

target_compile_definitions(FastMediaExplorer PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_link_libraries(FastMediaExplorer PRIVATE
    comctl32
    gdiplus
    shell32
    ole32
)
