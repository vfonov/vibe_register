cmake_minimum_required(VERSION 3.15)
project(NewRegister VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
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

# 3.5. nlohmann/json (JSON serialization, C++11+)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

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
set(MINC2_LIB "${CMAKE_BINARY_DIR}/_deps/libminc-build/libminc2.a")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -L${CMAKE_BINARY_DIR}/_deps/libminc-build")
target_link_libraries(new_register PRIVATE
    glfw
    Vulkan::Vulkan
    imgui
    minc2-simple-static
    ${MINC2_LIB}
    nlohmann_json::nlohmann_json
    glm
)

# std::filesystem requires separate library on GCC 9
if(CMAKE_COMPILER_IS_GNUCXX AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "10.0")
    target_link_libraries(new_register PRIVATE stdc++fs)
endif()

# Definitions
target_compile_definitions(new_register PRIVATE 
    # IMGUI_IMPL_VULKAN_NO_PROTOTYPES # We link against standard loader
)

# --- Testing ---
enable_testing()
add_subdirectory(tests)
