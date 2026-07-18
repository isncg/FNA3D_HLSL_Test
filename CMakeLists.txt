cmake_minimum_required(VERSION 3.16)
project(FNA3D_HLSL_Test C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Find SDL3
find_package(SDL3 CONFIG REQUIRED)

# Build FNA3D_HLSL from source (Linux)
set(BUILD_SHARED_LIBS ON CACHE BOOL "Build shared library")
add_subdirectory(
	${CMAKE_CURRENT_SOURCE_DIR}/../FNA/lib/FNA3D
	${CMAKE_CURRENT_BINARY_DIR}/FNA3D_build
)

# Output directory
set(FNA3D_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/out_linux")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${FNA3D_OUTPUT_DIR}")

# --- triangle_test ---
add_executable(triangle_test
	src/triangle_test.c
)
target_include_directories(triangle_test PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/../FNA/lib/FNA3D/include"
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
	"${CMAKE_CURRENT_SOURCE_DIR}/../FNA/lib/FNA3D/include"
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
	"${CMAKE_CURRENT_SOURCE_DIR}/../FNA/lib/FNA3D/include"
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
	"${CMAKE_CURRENT_SOURCE_DIR}/../FNA/lib/FNA3D/include"
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
	"${CMAKE_CURRENT_SOURCE_DIR}/../FNA/lib/FNA3D/include"
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
	"${CMAKE_CURRENT_SOURCE_DIR}/../FNA/lib/FNA3D/include"
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
	"${CMAKE_CURRENT_SOURCE_DIR}/../FNA/lib/FNA3D/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(teapot_pbr_test
	SDL3::SDL3
	FNA3D
	m
)

# --- shadow_test ---
add_executable(shadow_test
	src/shadow_test.c
	src/common.c
	src/math3d.c
)
target_include_directories(shadow_test PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/../FNA/lib/FNA3D/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(shadow_test
	SDL3::SDL3
	FNA3D
	m
)

# --- alpha_test ---
add_executable(alpha_test
	src/alpha_test.c
	src/common.c
	src/test_imgui.c
)
target_include_directories(alpha_test PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/../FNA/lib/FNA3D/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(alpha_test
	SDL3::SDL3
	FNA3D
	m
)

# --- basic_effect_test ---
add_executable(basic_effect_test
	src/basic_effect_test.c
	src/common.c
	src/math3d.c
	src/test_imgui.c
)
target_include_directories(basic_effect_test PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/../FNA/lib/FNA3D/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(basic_effect_test
	SDL3::SDL3
	FNA3D
	m
)

# --- dual_texture_test ---
add_executable(dual_texture_test
	src/dual_texture_test.c
	src/common.c
	src/math3d.c
	src/test_imgui.c
)
target_include_directories(dual_texture_test PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/../FNA/lib/FNA3D/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(dual_texture_test
	SDL3::SDL3
	FNA3D
	m
)

# --- env_map_test ---
add_executable(env_map_test
	src/env_map_test.c
	src/common.c
	src/math3d.c
	src/test_imgui.c
)
target_include_directories(env_map_test PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/../FNA/lib/FNA3D/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(env_map_test
	SDL3::SDL3
	FNA3D
	m
)

# --- skinned_test ---
add_executable(skinned_test
	src/skinned_test.c
	src/common.c
	src/math3d.c
	src/test_imgui.c
)
target_include_directories(skinned_test PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/../FNA/lib/FNA3D/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(skinned_test
	SDL3::SDL3
	FNA3D
	m
)

# --- yuv_to_rgba_test ---
add_executable(yuv_to_rgba_test
	src/yuv_to_rgba_test.c
	src/common.c
)
target_include_directories(yuv_to_rgba_test PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/../FNA/lib/FNA3D/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(yuv_to_rgba_test
	SDL3::SDL3
	FNA3D
	m
)
