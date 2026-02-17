cmake_minimum_required(VERSION 3.15)
project(NewRegister VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Dependencies ---

# 1. GLFW
find_package(glfw3 REQUIRED)

# 2. Vulkan
find_package(Vulkan REQUIRED)

# 2.5 HDF5 (Required by minc2 headers)
find_package(HDF5 REQUIRED COMPONENTS C)

# 3. Dear ImGui (Docking Branch)
include(FetchContent)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui
    GIT_TAG        docking
)
FetchContent_MakeAvailable(imgui)

# 3.5. Glaze (JSON serialization)
FetchContent_Declare(
    glaze
    GIT_REPOSITORY https://github.com/stephenberry/glaze.git
    GIT_TAG        v4.2.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(glaze)

# 3.75. GLM (Math library for vector/matrix operations)
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(glm)

# Create ImGui library target
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR})

# 4. LIBMINC
# Try system LIBMINC first, then fall back to FetchContent
find_package(LIBMINC QUIET HINTS ENV MINC_TOOLKIT)
if(LIBMINC_FOUND)
    message(STATUS "Found system LIBMINC: ${LIBMINC_DIR}")
else()
    message(STATUS "LIBMINC not found, fetching from GitHub...")
    FetchContent_Declare(
        libminc
        GIT_REPOSITORY https://github.com/BIC-MNI/libminc.git
        GIT_TAG        develop
    )
    FetchContent_MakeAvailable(libminc)
endif()

# 5. minc2-simple
# Fetch the source but build manually to integrate with LIBMINC
FetchContent_Declare(
    minc2-simple-static
    GIT_REPOSITORY https://github.com/NIST-MNI/minc2-simple.git
    GIT_TAG        develop
)
FetchContent_GetProperties(minc2-simple-static)
if(NOT minc2-simple-static_POPULATED)
    FetchContent_Populate(minc2-simple-static)
endif()

# Build minc2-simple manually to integrate with fetched LIBMINC
# Set prefix path to find LIBMINC config
set(LIBMINC_PREFIX "${libminc_SOURCE_DIR}")
if(EXISTS "${libminc_BINARY_DIR}")
    set(LIBMINC_PREFIX "${libminc_BINARY_DIR}")
endif()
set(CMAKE_PREFIX_PATH "${LIBMINC_PREFIX};${CMAKE_PREFIX_PATH}")

add_subdirectory(${minc2-simple-static_SOURCE_DIR} ${minc2-simple-static_BINARY_DIR})

# --- Sources ---
file(GLOB_RECURSE SOURCES 
    "src/*.cpp" 
    "include/*.h"
)


# ImGui Backends (Vulkan + GLFW)
set(IMGUI_DIR ${imgui_SOURCE_DIR})
list(APPEND SOURCES
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
)

# --- Executable ---
add_executable(new_register ${SOURCES})

# --- Include Directories ---
target_include_directories(new_register PRIVATE
    include
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${glm_SOURCE_DIR}  # GLM is header-only
    ${minc2-simple_SOURCE_DIR}/src  # minc2-simple headers
)

# --- Linking ---
target_link_libraries(new_register PRIVATE
    glfw
    Vulkan::Vulkan
    imgui
    minc2-simple-static
    minc2
    glaze::glaze
    glm
)

# Definitions
target_compile_definitions(new_register PRIVATE 
    # IMGUI_IMPL_VULKAN_NO_PROTOTYPES # We link against standard loader
)

# --- Testing ---
enable_testing()

add_executable(test_real_data tests/test_real_data.cpp src/Volume.cpp src/TagWrapper.cpp)
target_include_directories(test_real_data PRIVATE include ${glm_SOURCE_DIR} ${minc2-simple_SOURCE_DIR}/src)
target_link_libraries(test_real_data PRIVATE minc2-simple-static glm)

# The test needs access to test_data, we assume it's run from build directory or we pass full path
# We'll pass the full path to the test executable
add_test(NAME ReadRealData COMMAND test_real_data 
    ${CMAKE_SOURCE_DIR}/../test_data/mni_icbm152_t1_tal_nlin_sym_09c.mnc
    ${CMAKE_SOURCE_DIR}/../test_data/mni_icbm152_t1_tal_nlin_sym_09c_mask.mnc
)

add_executable(test_colour_map tests/test_colour_map.cpp src/ColourMap.cpp)
target_include_directories(test_colour_map PRIVATE include)
add_test(NAME ColourMapTests COMMAND test_colour_map)

add_executable(test_coordinate_sync tests/test_coordinate_sync.cpp src/Volume.cpp src/TagWrapper.cpp)
target_include_directories(test_coordinate_sync PRIVATE include ${glm_SOURCE_DIR} ${minc2-simple_SOURCE_DIR}/src)
target_link_libraries(test_coordinate_sync PRIVATE minc2-simple-static glm)
add_test(NAME CoordinateSyncTest COMMAND test_coordinate_sync)

add_executable(test_center_of_mass tests/test_center_of_mass.cpp src/Volume.cpp src/TagWrapper.cpp)
target_include_directories(test_center_of_mass PRIVATE include ${glm_SOURCE_DIR} ${minc2-simple_SOURCE_DIR}/src)
target_link_libraries(test_center_of_mass PRIVATE minc2-simple-static glm)
add_test(NAME CenterOfMassTest COMMAND test_center_of_mass
    ${CMAKE_SOURCE_DIR}/../test_data/mni_icbm152_t1_tal_nlin_sym_09c.mnc)

add_executable(test_thick_slices_com tests/test_thick_slices_com.cpp src/Volume.cpp src/TagWrapper.cpp)
target_include_directories(test_thick_slices_com PRIVATE include ${glm_SOURCE_DIR} ${minc2-simple_SOURCE_DIR}/src)
target_link_libraries(test_thick_slices_com PRIVATE minc2-simple-static glm)
add_test(NAME ThickSlicesCOMTest COMMAND test_thick_slices_com
    ${CMAKE_SOURCE_DIR}/../test_data/mni_icbm152_t1_tal_nlin_sym_09c_thick_slices.mnc)

# Tag loading test using minc2-simple API
add_executable(test_tag_load tests/test_tag_load.cpp src/TagWrapper.cpp)
target_include_directories(test_tag_load PRIVATE include ${glm_SOURCE_DIR} ${minc2-simple_SOURCE_DIR}/src)
target_link_libraries(test_tag_load PRIVATE minc2-simple-static minc2 glm)
add_test(NAME TagLoadTest COMMAND test_tag_load ${CMAKE_SOURCE_DIR}/../test_data/mni_icbm152_t1_tal_nlin_sym_09c.tag)

# Volume info test - check dimensions and transformations
add_executable(test_volume_info tests/test_volume_info.cpp)
target_include_directories(test_volume_info PRIVATE include ${glm_SOURCE_DIR} ${minc2-simple_SOURCE_DIR}/src)
target_link_libraries(test_volume_info PRIVATE minc2-simple-static minc2 glm)
add_test(NAME VolumeInfoTest COMMAND test_volume_info)

# World to voxel test
add_executable(test_world_to_voxel tests/test_world_to_voxel.cpp src/Volume.cpp src/TagWrapper.cpp)
target_include_directories(test_world_to_voxel PRIVATE include ${glm_SOURCE_DIR} ${minc2-simple_SOURCE_DIR}/src)
target_link_libraries(test_world_to_voxel PRIVATE minc2-simple-static minc2 glm)
add_test(NAME WorldToVoxelTest COMMAND test_world_to_voxel)

# MINC dimensions test
add_executable(test_minc_dims tests/test_minc_dims.cpp)
target_include_directories(test_minc_dims PRIVATE include ${glm_SOURCE_DIR} ${minc2-simple_SOURCE_DIR}/src)
target_link_libraries(test_minc_dims PRIVATE minc2-simple-static minc2 glm)
add_test(NAME MincDimsTest COMMAND test_minc_dims)

# Matrix debug test
add_executable(test_matrix_debug tests/test_matrix_debug.cpp src/Volume.cpp src/TagWrapper.cpp)
target_include_directories(test_matrix_debug PRIVATE include ${glm_SOURCE_DIR} ${minc2-simple_SOURCE_DIR}/src)
target_link_libraries(test_matrix_debug PRIVATE minc2-simple-static minc2 glm)
add_test(NAME MatrixDebugTest COMMAND test_matrix_debug)
