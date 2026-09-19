cmake_minimum_required(VERSION 3.10)

project(imgui CXX)

set(IMGUI_SOURCES
    imgui.cpp
    imgui_draw.cpp
    imgui_tables.cpp
    imgui_widgets.cpp
    misc/cpp/imgui_stdlib.cpp
)

list(APPEND IMGUI_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/backends/imgui_impl_glfw.cpp)
list(APPEND IMGUI_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)

add_library(imgui STATIC ${IMGUI_SOURCES})

target_include_directories(imgui SYSTEM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/backends
    ${CMAKE_CURRENT_SOURCE_DIR}/misc/cpp
)
target_link_libraries(imgui PRIVATE glfw)
target_compile_definitions(imgui PRIVATE GLFW_INCLUDE_NONE)