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

# --- textured_quad_test ---
add_executable(textured_quad_test
	src/textured_quad.c
	src/common.c
)
target_include_directories(textured_quad_test PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/../FNA3D_HLSL/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(textured_quad_test
	SDL3::SDL3
	FNA3D
	m
)

# --- teapot_test ---
add_executable(teapot_test
	src/teapot.c
	src/common.c
	src/math3d.c
)
target_include_directories(teapot_test PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/../FNA3D_HLSL/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(teapot_test
	SDL3::SDL3
	FNA3D
	m
)

# --- teapot_light_test ---
add_executable(teapot_light_test
	src/teapot_light.c
	src/common.c
	src/math3d.c
)
target_include_directories(teapot_light_test PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/../FNA3D_HLSL/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(teapot_light_test
	SDL3::SDL3
	FNA3D
	m
)

# --- matviz_test ---
add_executable(matviz_test
	src/matviz_test.c
	src/common.c
)
target_include_directories(matviz_test PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/../FNA3D_HLSL/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(matviz_test
	SDL3::SDL3
	FNA3D
	m
)

# --- ssao_test ---
add_executable(ssao_test
	src/ssao_test.c
	src/common.c
	src/math3d.c
)
target_include_directories(ssao_test PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/../FNA3D_HLSL/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(ssao_test
	SDL3::SDL3
	FNA3D
	m
)

# --- teapot_pbr_test ---
add_executable(teapot_pbr_test
	src/teapot_pbr.c
	src/common.c
	src/math3d.c
)
target_include_directories(teapot_pbr_test PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/../FNA3D_HLSL/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(teapot_pbr_test
	SDL3::SDL3
	FNA3D
	m
)
