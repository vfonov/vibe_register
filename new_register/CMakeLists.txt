cmake_minimum_required(VERSION 3.15)
project(NewRegister VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Backend options ---
option(ENABLE_VULKAN  "Build Vulkan backend"  ON)
option(ENABLE_OPENGL2 "Build OpenGL2 backend" ON)
option(ENABLE_METAL   "Build Metal backend"   OFF)

# --- Dependencies ---

# 1. GLFW
find_package(glfw3 REQUIRED)

# 2. Vulkan (conditional)
if(ENABLE_VULKAN)
    find_package(Vulkan REQUIRED)
endif()

# 2.1 OpenGL (conditional)
if(ENABLE_OPENGL2)
    find_package(OpenGL REQUIRED)
endif()

# 2.5 HDF5 (Required by minc2 headers)
find_package(HDF5 REQUIRED COMPONENTS C)

# 2.6 Threads (Required for Prefetcher background thread)
find_package(Threads REQUIRED)

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

# 3.8. cxxopts (Command-line argument parsing, single-header)
FetchContent_Declare(
    cxxopts
    GIT_REPOSITORY https://github.com/jarro2783/cxxopts.git
    GIT_TAG        v3.2.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(cxxopts)

# 3.8. Eigen3 (Linear algebra: SVD, matrix solvers — used by Transform)
FetchContent_Declare(
    eigen
    GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
    GIT_TAG        3.4.0
    GIT_SHALLOW    TRUE
)
set(EIGEN_BUILD_DOC OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(EIGEN_BUILD_PKGCONFIG OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(eigen)

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
# Only use system libminc if found outside _deps directory
set(_USE_SYSTEM_MINC OFF)
find_package(LIBMINC QUIET HINTS ENV MINC_TOOLKIT)
if(LIBMINC_FOUND AND LIBMINC_DIR)
    if(NOT LIBMINC_DIR MATCHES "_deps")
        set(_USE_SYSTEM_MINC ON)
    endif()
endif()

if(_USE_SYSTEM_MINC)
    message(STATUS "Using system LIBMINC: ${LIBMINC_DIR}")
    set(MINC2_LIB minc2)
    set(MINC2_INCLUDE_DIR "${LIBMINC_DIR}/include")
else()
    message(STATUS "LIBMINC not found or in _deps, fetching from GitHub...")
    FetchContent_Declare(
        libminc
        GIT_REPOSITORY https://github.com/BIC-MNI/libminc.git
        GIT_TAG        develop
    )
    # Suppress upstream libminc tests (known failures: test-io-full missing
    # history attribute, test-nan-short Inf/NaN in integer storage)
    set(_SAVE_BUILD_TESTING ${BUILD_TESTING})
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(libminc)
    set(BUILD_TESTING ${_SAVE_BUILD_TESTING} CACHE BOOL "" FORCE)
    
    # Use absolute path to fetched library
    set(MINC2_LIB "${CMAKE_BINARY_DIR}/_deps/libminc-build/libminc2.a")
    set(MINC2_INCLUDE_DIR "${libminc_SOURCE_DIR}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -L${CMAKE_BINARY_DIR}/_deps/libminc-build")
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

# Suppress upstream minc2-simple tests (same known failures as libminc)
set(_SAVE_BUILD_TESTING ${BUILD_TESTING})
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory(${minc2-simple-static_SOURCE_DIR} ${minc2-simple-static_BINARY_DIR})
set(BUILD_TESTING ${_SAVE_BUILD_TESTING} CACHE BOOL "" FORCE)

# --- Sources (explicit list instead of GLOB_RECURSE) ---
set(SOURCES
    src/main.cpp
    src/AppState.cpp
    src/AppConfig.cpp
    src/ColourMap.cpp
    src/Interface.cpp
    src/Prefetcher.cpp
    src/QCState.cpp
    src/TagWrapper.cpp
    src/Transform.cpp
    src/ViewManager.cpp
    src/Volume.cpp
    src/BackendFactory.cpp
)

# Backend-specific sources
if(ENABLE_VULKAN)
    list(APPEND SOURCES src/VulkanBackend.cpp src/VulkanHelpers.cpp)
endif()
if(ENABLE_OPENGL2)
    list(APPEND SOURCES src/OpenGL2Backend.cpp)
endif()

# ImGui platform backend (always needed)
set(IMGUI_DIR ${imgui_SOURCE_DIR})
list(APPEND SOURCES
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
)

# Backend-specific ImGui renderer sources
if(ENABLE_VULKAN)
    list(APPEND SOURCES ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp)
endif()
if(ENABLE_OPENGL2)
    list(APPEND SOURCES ${IMGUI_DIR}/backends/imgui_impl_opengl2.cpp)
endif()

# --- Executable ---
add_executable(new_register ${SOURCES})

# --- Include Directories ---
target_include_directories(new_register PRIVATE
    include
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${glm_SOURCE_DIR}  # GLM is header-only
    ${eigen_SOURCE_DIR}  # Eigen is header-only
    ${minc2-simple-static_SOURCE_DIR}/src  # minc2-simple headers
    ${cxxopts_SOURCE_DIR}/include  # cxxopts headers
)

# --- Compile Definitions ---
target_compile_definitions(new_register PRIVATE
    # IMGUI_IMPL_VULKAN_NO_PROTOTYPES # We link against standard loader
)
if(ENABLE_VULKAN)
    target_compile_definitions(new_register PRIVATE HAS_VULKAN=1)
endif()
if(ENABLE_OPENGL2)
    target_compile_definitions(new_register PRIVATE HAS_OPENGL2=1)
endif()
if(ENABLE_METAL)
    target_compile_definitions(new_register PRIVATE HAS_METAL=1)
endif()

# --- Linking ---
target_link_libraries(new_register PRIVATE
    glfw
    imgui
    minc2-simple-static
    ${MINC2_LIB}
    nlohmann_json::nlohmann_json
    glm
    Threads::Threads
)
target_include_directories(new_register PRIVATE ${MINC2_INCLUDE_DIR})
if(ENABLE_VULKAN)
    target_link_libraries(new_register PRIVATE Vulkan::Vulkan)
endif()
if(ENABLE_OPENGL2)
    target_link_libraries(new_register PRIVATE OpenGL::GL)
endif()

# std::filesystem requires separate library on GCC 9
if(CMAKE_COMPILER_IS_GNUCXX AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "10.0")
    target_link_libraries(new_register PRIVATE stdc++fs)
endif()

# --- Testing ---
enable_testing()
add_subdirectory(tests)
