cmake_minimum_required(VERSION 3.15)
project(NewRegister VERSION 0.1.0 LANGUAGES C CXX)

# Add custom cmake modules path for finding NetCDF
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake-modules" "${CMAKE_MODULE_PATH}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Disable installation of all dependencies by default ---
# Must be set before any FetchContent_MakeAvailable or add_subdirectory
set(CMAKE_DISABLE_INSTALL_PACKAGES ON)

# --- Static linking (avoid conflicts with system libraries) ---
# Set after finding system packages to avoid interfering with their detection
set(BUILD_SHARED_LIBS_DEFAULT ${BUILD_SHARED_LIBS})

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
# Allow config-mode discovery (hdf5-config.cmake) so that CMAKE_PREFIX_PATH
# works for non-standard install prefixes (e.g. /opt/minc/1.9.18).
# Module-mode (FindHDF5) is the automatic fallback when no config file exists.
find_package(HDF5 REQUIRED COMPONENTS C)

# Safety net: if FindHDF5 found the library but not the headers (common when
# h5cc is not on PATH and no config-mode file exists), derive include dir
# from the library path (e.g. /opt/minc/1.9.18/lib -> /opt/minc/1.9.18/include).
if(HDF5_FOUND AND NOT HDF5_INCLUDE_DIRS)
    foreach(_hdf5_lib IN LISTS HDF5_C_LIBRARIES)
        if(_hdf5_lib MATCHES "^(.+)/lib[^/]*$")
            set(_hdf5_prefix "${CMAKE_MATCH_1}")
            if(EXISTS "${_hdf5_prefix}/include/hdf5.h")
                set(HDF5_INCLUDE_DIRS "${_hdf5_prefix}/include")
                message(STATUS "HDF5: derived include dir from library path: ${HDF5_INCLUDE_DIRS}")
                break()
            # Debian/Ubuntu multi-arch: lib/x86_64-linux-gnu -> include/hdf5/serial
            elseif(EXISTS "${_hdf5_prefix}/../include/hdf5/serial/hdf5.h")
                get_filename_component(_hdf5_inc "${_hdf5_prefix}/../include/hdf5/serial" ABSOLUTE)
                set(HDF5_INCLUDE_DIRS "${_hdf5_inc}")
                message(STATUS "HDF5: derived include dir (Debian multi-arch): ${HDF5_INCLUDE_DIRS}")
                break()
            endif()
        endif()
    endforeach()
    if(NOT HDF5_INCLUDE_DIRS)
        message(FATAL_ERROR "HDF5 found but HDF5_INCLUDE_DIRS is empty. "
            "Set CMAKE_PREFIX_PATH or HDF5_ROOT to the HDF5 install prefix.")
    endif()
endif()

# For MINC1 support
FIND_PACKAGE(NETCDF REQUIRED)


if(NETCDF_FOUND)
    message(STATUS "NETCDF found: ${NETCDF_INCLUDE_DIR}")
else()
    message(STATUS "NETCDF not found - MINC1 support will be disabled")
endif()

# 2.6 Threads (Required for Prefetcher background thread)
find_package(Threads REQUIRED)

# --- Force static linking for fetched dependencies ---
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(CMAKE_FIND_LIBRARY_SUFFIXES ".a" ".lib" ".so"  CACHE STRING "" FORCE)


SET(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)
SET(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)


# 3. Dear ImGui (Docking Branch)
# Disable all optional builds - we only need the core library
include(FetchContent)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui
    GIT_TAG        docking
)
set(ImGui_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ImGui_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ImGui_DIR_SOURCES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(imgui)

# 3.5. nlohmann/json (JSON serialization, C++11+)
# Header-only library - no build needed
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json)

# 3.75. GLM (Math library for vector/matrix operations)
# Header-only library - no build needed
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
    GIT_SHALLOW    TRUE
)
set(GLM_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glm)


# 3.8. Eigen3 (Linear algebra: SVD, matrix solvers — used by Transform)
# Use ExternalProject_Add to build in build directory only
include(ExternalProject)
set(EIGEN_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/eigen_install")
ExternalProject_Add(Eigen
    GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
    GIT_TAG        3.4.0
    GIT_SHALLOW    TRUE
    INSTALL_DIR    ${EIGEN_INSTALL_PREFIX}
    CMAKE_ARGS
        -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}
        -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>
        -DBUILD_DOCUMENTATION=OFF
        -DBUILD_TESTING=OFF
        -DBUILD_PACKAGE=OFF
        -DEIGEN_BUILD_PKGCONFIG=OFF
    BUILD_BYPRODUCTS ${EIGEN_INSTALL_PREFIX}/include/eigen3/Eigen
)
set(eigen_SOURCE_DIR "${CMAKE_BINARY_DIR}/eigen-src")
set(eigen_BINARY_DIR "${CMAKE_BINARY_DIR}/eigen-build")
set(eigen_INCLUDE_DIR "${EIGEN_INSTALL_PREFIX}/include/eigen3")

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

FIND_PATH( MINC_TOOLKIT_DIR bin/mincstats  HINTS $ENV{MINC_TOOLKIT} DOC "minc-toolkit install prefix")
FIND_PACKAGE( LIBMINC QUIET HINTS ${MINC_TOOLKIT_DIR}/lib${LIB_SUFFIX})

if(LIBMINC_FOUND AND LIBMINC_DIR)
    if(NOT LIBMINC_DIR MATCHES "_deps")
        set(_USE_SYSTEM_MINC ON)
    endif()
endif()

if(_USE_SYSTEM_MINC)
    message(STATUS "Using system LIBMINC: ${LIBMINC_DIR}")
    INCLUDE( ${LIBMINC_USE_FILE} )
    set(MINC2_LIB ${LIBMINC_LIBRARIES})
    set(MINC2_INCLUDE_DIR "${LIBMINC_DIR}/include")
    set(MINC2_LIB_DIR ${LIBMINC_DIR})
else()
    message(STATUS "LIBMINC not found or in _deps, using ExternalProject...")

    # Use ExternalProject_Add to avoid install pollution
    include(ExternalProject)
    set(LIBMINC_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/libminc_install")
    # Build args - enable MINC1 support if NetCDF is available
    set(_LIBMINC_ARGS
        -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}
        -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>
        -DBUILD_TESTING=OFF
        -DBUILD_SHARED_LIBS=OFF
        -DLIBMINC_BUILD_EZMINC=OFF
        -DHDF5_INCLUDE_DIRS:STRING=${HDF5_INCLUDE_DIRS}
        -DHDF5_C_LIBRARIES:STRING=${HDF5_C_LIBRARIES}
        -DHDF5_LIBRARY:FILEPATH=${HDF5_LIBRARY}
        -DHDF5_hdf5_LIBRARY_RELEASE:FILEPATH=${HDF5_hdf5_LIBRARY_RELEASE}
        -DHDF5_hdf5_LIBRARY_DEBUG:FILEPATH=${HDF5_hdf5_LIBRARY_DEBUG}
    )
    if(NETCDF_FOUND)
        list(APPEND _LIBMINC_ARGS
            -DLIBMINC_MINC1_SUPPORT:BOOL=ON
            -DNETCDF_INCLUDE_DIR:STRING=${NETCDF_INCLUDE_DIR}
            -DNETCDF_LIBRARY:STRING=${NETCDF_LIBRARY}
        )
    #else()
    #    list(APPEND _LIBMINC_ARGS -DLIBMINC_MINC1_SUPPORT=OFF)
    endif()

    ExternalProject_Add(libminc
        GIT_REPOSITORY https://github.com/BIC-MNI/libminc.git
        GIT_TAG        modernize_cmake
        GIT_SHALLOW    TRUE
        INSTALL_DIR    ${LIBMINC_INSTALL_PREFIX}
        CMAKE_ARGS ${_LIBMINC_ARGS}
        BUILD_BYPRODUCTS ${LIBMINC_INSTALL_PREFIX}/lib/libminc2.a
    )
    set(MINC2_LIB "${LIBMINC_INSTALL_PREFIX}/lib/libminc2.a")
    set(MINC2_INCLUDE_DIR "${LIBMINC_INSTALL_PREFIX}/include")
    set(MINC2_LIB_DIR "${LIBMINC_INSTALL_PREFIX}/lib${LIB_SUFFIX}")
    set(_USE_EXTERNAL_MINC ON)
endif()

# 5. minc2-simple
# Use ExternalProject_Add to avoid install pollution
include(ExternalProject)
set(MINC2_SIMPLE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/minc2_simple_install")

# Determine dependency - if using fetched libminc, depend on it
set(_MINC2_DEPENDS "")
if(NOT _USE_SYSTEM_MINC)
    set(_MINC2_DEPENDS libminc)
    set(_MINC2_LIBMINC_PREFIX "${LIBMINC_INSTALL_PREFIX}")
else()
    set(_MINC2_LIBMINC_PREFIX "${LIBMINC_DIR}")
endif()
message("LIBMINC_DIR=${MINC2_LIB_DIR}")

ExternalProject_Add(minc2-simple-static
    GIT_REPOSITORY https://github.com/NIST-MNI/minc2-simple.git
    GIT_TAG        develop
    GIT_SHALLOW    TRUE
    UPDATE_DISCONNECTED TRUE
    INSTALL_DIR    ${MINC2_SIMPLE_INSTALL_PREFIX}
    DEPENDS        ${_MINC2_DEPENDS}
    CMAKE_ARGS
        -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}
        -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>
        -DCMAKE_PREFIX_PATH=${_MINC2_LIBMINC_PREFIX}
        -DLIBMINC_DIR=${MINC2_LIB_DIR}/cmake
        -DBUILD_TESTING:BOOL=OFF
        -DBUILD_MINC2_SIMPLE_SHARED:BOOL=OFF
        -DCMAKE_C_FLAGS:STRING=-I${HDF5_INCLUDE_DIRS} # HACK to make it find hdf5.h 
    BUILD_BYPRODUCTS ${MINC2_SIMPLE_INSTALL_PREFIX}/lib/libminc2-simple-static.a
)
if(NOT _USE_SYSTEM_MINC)
add_dependencies(minc2-simple-static libminc)
endif()

set(minc2-simple-static_SOURCE_DIR "${MINC2_SIMPLE_INSTALL_PREFIX}/src")
set(minc2-simple-static_INCLUDE_DIR "${MINC2_SIMPLE_INSTALL_PREFIX}/include")
set(minc2-simple-static_LIB "${MINC2_SIMPLE_INSTALL_PREFIX}/lib/libminc2-simple-static.a")
set(minc2-simple-static_LIB_DIR "${MINC2_SIMPLE_INSTALL_PREFIX}/lib")

# Add library search paths for ExternalProject builds
if(NOT _USE_SYSTEM_MINC AND DEFINED MINC2_LIB_DIR)
    link_directories(${MINC2_LIB_DIR})
endif()
if(DEFINED minc2-simple-static_LIB_DIR)
    link_directories(${minc2-simple-static_LIB_DIR})
endif()

# --- nr_core: GPU-free static library shared by new_register and new_mincpik ---
set(NR_CORE_SOURCES
    src/Volume.cpp
    src/ColourMap.cpp
    src/Transform.cpp
    src/TagWrapper.cpp
    src/AppConfig.cpp
    src/SliceRenderer.cpp
)

add_library(nr_core STATIC ${NR_CORE_SOURCES})
target_include_directories(nr_core PUBLIC
    include
    ${glm_SOURCE_DIR}
    ${eigen_INCLUDE_DIR}
    ${minc2-simple-static_INCLUDE_DIR}
    ${MINC2_INCLUDE_DIR}
)
target_link_libraries(nr_core PUBLIC
    ${minc2-simple-static_LIB}
    ${MINC2_LIB}
    ${NETCDF_LIBRARY}
    ${HDF5_C_LIBRARIES}
    nlohmann_json::nlohmann_json
    glm
)
add_dependencies(nr_core Eigen)
if(NOT _USE_SYSTEM_MINC)
    add_dependencies(nr_core libminc minc2-simple-static)
else()
    add_dependencies(nr_core minc2-simple-static)
endif()

# --- Sources (explicit list instead of GLOB_RECURSE) ---
set(SOURCES
    src/main.cpp
    src/AppState.cpp
    src/Interface.cpp
    src/Prefetcher.cpp
    src/QCState.cpp
    src/ViewManager.cpp
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
# nr_core PUBLIC includes already provide: include/, glm, eigen, minc2
target_include_directories(new_register PRIVATE
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${minc2-simple-static_SOURCE_DIR}/src  # minc2-simple headers (for extern "C" include)
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
# nr_core provides Volume, ColourMap, Transform, TagWrapper, AppConfig, SliceRenderer
# and their transitive MINC/HDF5/GLM/Eigen/JSON dependencies.

message("NETCDF_LIBRARY=${NETCDF_LIBRARY}")
target_link_libraries(new_register PRIVATE
    nr_core
    imgui
    glfw
    Threads::Threads
    ${CMAKE_DL_LIBS}
)
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


INSTALL(TARGETS new_register DESTINATION bin)

# --- new_mincpik: headless mosaic image generator ---
add_executable(new_mincpik
    src/mincpik/mincpik_main.cpp
    src/mincpik/mosaic.cpp
    src/mincpik/text_render.cpp
    src/mincpik/mincpik_cli.cpp
    src/mincpik/colour_bar.cpp
)
target_link_libraries(new_mincpik PRIVATE
    nr_core
)
target_include_directories(new_mincpik PRIVATE
    ${minc2-simple-static_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/mincpik
)
if(CMAKE_COMPILER_IS_GNUCXX AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "10.0")
    target_link_libraries(new_mincpik PRIVATE stdc++fs)
endif()
INSTALL(TARGETS new_mincpik DESTINATION bin)

# --- Testing ---
enable_testing()
add_subdirectory(tests)
