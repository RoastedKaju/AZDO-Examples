add_library(stb STATIC "${CMAKE_CURRENT_SOURCE_DIR}/source/stb.cpp")
target_include_directories(stb PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")