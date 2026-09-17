cmake_minimum_required(VERSION 3.10)

project(gltf LANGUAGES C)

add_library(gltf STATIC "src/gltf.cpp")

target_include_directories(gltf PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

if(MSVC)
    target_compile_definitions(gltf PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()