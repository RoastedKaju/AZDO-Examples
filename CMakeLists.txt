cmake_minimum_required(VERSION 3.10)
project("AZDO")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

add_subdirectory("external/glfw")
add_subdirectory("external/glm")
add_subdirectory("external/glad")
add_subdirectory("external/stb")
add_subdirectory("external/gltf")

file(GLOB_RECURSE SRC_FILES "source/*.cpp" "source/*.hpp")

function(example NAME SOURCE)
	add_executable (${NAME})
	target_sources(${NAME} PRIVATE ${SOURCE})
	target_include_directories(${NAME} PRIVATE "include")
	target_compile_definitions(${NAME} PRIVATE GLFW_INCLUDE_NONE)
	target_link_libraries(${NAME} PRIVATE glfw glm::glm glad stb gltf)
endfunction()

example(Triangle "source/triangle.cpp")
example(Cube "source/cube.cpp")
example(Texture "source/texture.cpp")
example(Model "source/model.cpp")
example(Text "source/text.cpp")