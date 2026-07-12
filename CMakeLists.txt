cmake_minimum_required(VERSION 3.16)
project(FNA3D_HLSL_Test C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Find SDL3
find_package(SDL3 CONFIG REQUIRED)

# Build FNA3D_HLSL from source (Linux)
set(BUILD_SHARED_LIBS ON CACHE BOOL "Build shared library")
add_subdirectory(
	${CMAKE_CURRENT_SOURCE_DIR}/../FNA3D_HLSL
	${CMAKE_CURRENT_BINARY_DIR}/FNA3D_HLSL
)

# Output directory
set(FNA3D_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/out_linux")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${FNA3D_OUTPUT_DIR}")

# --- triangle_test ---
add_executable(triangle_test
	src/triangle_test.c
)
target_include_directories(triangle_test PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/../FNA3D_HLSL/include"
)
target_link_libraries(triangle_test
	SDL3::SDL3
	FNA3D
	m
)
