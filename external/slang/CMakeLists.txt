add_library(slang INTERFACE)
target_include_directories(slang INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(slang INTERFACE
	"${CMAKE_CURRENT_SOURCE_DIR}/lib/slang.lib"
	"${CMAKE_CURRENT_SOURCE_DIR}/lib/slang-compiler.lib"
)