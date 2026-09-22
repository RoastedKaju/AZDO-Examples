add_library(tinyglTF STATIC "src/gltf.cpp")
target_include_directories(tinyglTF PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")