cmake_minimum_required(VERSION 3.15)
project(NewRegister VERSION 0.1.0 LANGUAGES C CXX)

# Add custom cmake modules path for finding NetCDF
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake-modules" "${CMAKE_MODULE_PATH}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Export compile commands for clangd LSP support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Disable installation of all dependencies by default ---
# Must be set before any FetchContent_MakeAvailable or add_subdirectory
set(CMAKE_DISABLE_INSTALL_PACKAGES ON)

# --- Static linking (avoid conflicts with system libraries) ---
# Set after finding system packages to avoid interfering with their detection
set(BUILD_SHARED_LIBS_DEFAULT ${BUILD_SHARED_LIBS})

# --- Build mode options ---
# BUILD_QC_ONLY: skip all MINC/HDF5/NetCDF/Eigen/GLM dependencies and build
#                only the new_qc viewer (useful on machines without MINC toolkit)
option(BUILD_QC_ONLY "Build only new_qc viewer (skip MINC/HDF5/Eigen dependencies)" OFF)

# BUILD_NEW_QC: also build new_qc alongside new_register (default ON)
option(BUILD_NEW_QC  "Build new_qc viewer alongside new_register" ON)

# --- Backend options ---
option(ENABLE_VULKAN  "Build Vulkan backend"  ON)
option(ENABLE_OPENGL2 "Build OpenGL2 backend" ON)
option(ENABLE_METAL   "Build Metal backend"   OFF)

# --- Testing options ---
# ENABLE_TESTS: build headless UI tests (requires OSMesa library)
option(ENABLE_TESTS "Enable headless UI tests (requires OSMesa library)" ON)

# --- Dependencies always required ---
include(FetchContent)

# 1. GLFW — system package preferred; set FETCH_GLFW=ON to force-fetch 3.4.
# Note: GLFW 3.4 fixes wl_touch on Wayland; system 3.3.x drops touch events.
option(FETCH_GLFW "Always fetch GLFW 3.4 from source, ignoring any system installation" OFF)

if(NOT FETCH_GLFW)
    find_package(glfw3 QUIET)
endif()

if(glfw3_FOUND)
    message(STATUS "GLFW: using system package (version ${glfw3_VERSION})")
else()
    if(FETCH_GLFW)
        message(STATUS "GLFW: FETCH_GLFW=ON — fetching 3.4 from source")
    else()
        message(STATUS "GLFW: system package not found — fetching 3.4 from source")
    endif()
    set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    # Disable Wayland in fetched GLFW if wayland-scanner is not available
    # (GLFW 3.4 requires it to generate protocol bindings at build time)
    find_program(WAYLAND_SCANNER_EXECUTABLE NAMES wayland-scanner)
    if(NOT WAYLAND_SCANNER_EXECUTABLE)
        message(STATUS "GLFW: wayland-scanner not found — disabling Wayland backend in fetched GLFW")
        set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
    endif()
    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.4
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(glfw)
endif()

# 1.5. Wayland client (optional — enables direct wl_touch → ImGui injection
#      for reliable touch screen support; gracefully absent on X11-only systems)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(WAYLAND_CLIENT QUIET wayland-client)
endif()
if(WAYLAND_CLIENT_FOUND)
    message(STATUS "Wayland client found — building with direct touch input support")
else()
    message(STATUS "Wayland client not found — touch screen input will rely on GLFW synthesis")
endif()

# 2. Vulkan (soft find — required for new_register if ENABLE_VULKAN, optional for new_qc)
if(ENABLE_VULKAN)
    find_package(Vulkan)
    if(Vulkan_FOUND)
        message(STATUS "Vulkan found — building with Vulkan backend")
    else()
        if(NOT BUILD_QC_ONLY)
            message(FATAL_ERROR "Vulkan not found. Set ENABLE_VULKAN=OFF to disable Vulkan support.")
        else()
            message(STATUS "Vulkan not found — skipping Vulkan backend")
        endif()
    endif()
endif()

# 2.1 OpenGL (conditional)
if(ENABLE_OPENGL2)
    find_package(OpenGL REQUIRED)
endif()

# --- MINC/HDF5/NetCDF dependencies (not needed for new_qc only) ---
if(NOT BUILD_QC_ONLY)

    # 2.5 HDF5 (Required by minc2 headers)
    find_package(HDF5 REQUIRED COMPONENTS C)

    # Safety net: if FindHDF5 found the library but not the headers
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

endif() # NOT BUILD_QC_ONLY

# --- Force static linking for fetched dependencies ---
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(CMAKE_FIND_LIBRARY_SUFFIXES ".a" ".lib" ".so"  CACHE STRING "" FORCE)

SET(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)
SET(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)


# 3. Dear ImGui (Docking Branch)
# Disable all optional builds - we only need the core library
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

# 3.6. stb (stb_image for new_qc image loading)
if(BUILD_NEW_QC OR BUILD_QC_ONLY)
    FetchContent_Declare(
        stb
        GIT_REPOSITORY https://github.com/nothings/stb.git
        GIT_TAG        master
    )
    FetchContent_MakeAvailable(stb)
endif()

if(NOT BUILD_QC_ONLY)

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

    # 3.9. NIfTI-1 library (for .nii and .nii.gz file support)
    # Note: We compile nifti1_io and znzlib directly (bundled with libminc)
    # This avoids network fetch and uses the same version as legacy nii2mnc tool
    set(nifti_INCLUDE_DIR "${CMAKE_BINARY_DIR}/libminc-prefix/src/libminc/nifti")
    set(nifti_SOURCE_DIR "${CMAKE_BINARY_DIR}/libminc-prefix/src/libminc/nifti")
    
    # Compile nifti1_io.c and znzlib.c as part of nr_core
    set(NIFTI_SOURCES
        ${nifti_SOURCE_DIR}/nifti1_io.c
        ${nifti_SOURCE_DIR}/znzlib.c
    )

endif() # NOT BUILD_QC_ONLY

# Create ImGui library target (core only — backends compiled per-executable)
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR})


if(NOT BUILD_QC_ONLY)


    IF(FALSE) # NEUROSLOP
    # 4. LIBMINC
    # Generate fake Find*.cmake modules injected via CMAKE_MODULE_PATH into subprojects.
    # Background: cmake's FindHDF5 does `set(HDF5_INCLUDE_DIRS)` as a *local* variable
    # before rebuilding it from per-language find_path results.  That local set()
    # shadows any CACHE entry we pre-set (via -D or -C), so neither approach works on
    # systems where the per-language find_path fails (e.g. Debian multi-arch HDF5 with
    # headers in /usr/include/hdf5/serial/).  A FindHDF5.cmake placed first on
    # CMAKE_MODULE_PATH completely replaces the system module and avoids the issue.
    list(GET HDF5_INCLUDE_DIRS 0 _HDF5_INCLUDE_DIR_0)
    set(_FAKE_MODULES_DIR "${CMAKE_BINARY_DIR}/fake-find-modules")
    file(MAKE_DIRECTORY "${_FAKE_MODULES_DIR}")

    file(WRITE "${_FAKE_MODULES_DIR}/FindHDF5.cmake"
        "# Auto-generated by new_register/CMakeLists.txt — do not edit\n"
        "if(HDF5_FOUND)\n  return()\nendif()\n"
        "set(HDF5_FOUND          TRUE)\n"
        "set(HDF5_IS_PARALLEL    FALSE)\n"
        "set(HDF5_VERSION        \"${HDF5_VERSION}\")\n"
        "set(HDF5_INCLUDE_DIR    \"${_HDF5_INCLUDE_DIR_0}\")\n"
        "set(HDF5_C_INCLUDE_DIR  \"${_HDF5_INCLUDE_DIR_0}\")\n"
        "set(HDF5_INCLUDE_DIRS   \"${HDF5_INCLUDE_DIRS}\")\n"
        "set(HDF5_C_LIBRARIES    \"${HDF5_C_LIBRARIES}\")\n"
        "set(HDF5_LIBRARIES      \"${HDF5_C_LIBRARIES}\")\n"
        "set(HDF5_LIBRARY        \"${HDF5_LIBRARY}\")\n"
        "set(HDF5_hdf5_LIBRARY_RELEASE \"${HDF5_hdf5_LIBRARY_RELEASE}\")\n"
        "set(HDF5_hdf5_LIBRARY_DEBUG   \"${HDF5_hdf5_LIBRARY_DEBUG}\")\n"
        "set(HDF5_C_FOUND        TRUE)\n"
        "set(HDF5_HL_FOUND       FALSE)\n"
    )
    if(NETCDF_FOUND)
        file(WRITE "${_FAKE_MODULES_DIR}/FindNETCDF.cmake"
            "# Auto-generated by new_register/CMakeLists.txt — do not edit\n"
            "if(NETCDF_FOUND OR NetCDF_FOUND)\n  return()\nendif()\n"
            "set(NETCDF_FOUND       TRUE)\n"
            "set(NetCDF_FOUND       TRUE)\n"
            "set(NETCDF_INCLUDE_DIR \"${NETCDF_INCLUDE_DIR}\")\n"
            "set(NETCDF_LIBRARY     \"${NETCDF_LIBRARY}\")\n"
            "set(NETCDF_LIBRARIES   \"${NETCDF_LIBRARY}\")\n"
        )
    endif()
    message(STATUS "Fake find modules written to: ${_FAKE_MODULES_DIR}")
    ENDIF() # NEUROSLOP

    # Try system LIBMINC first, then fall back to FetchContent
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

        include(ExternalProject)

        set(LIBMINC_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/libminc_install")
        set(_LIBMINC_ARGS
            #"-DCMAKE_MODULE_PATH=${_FAKE_MODULES_DIR}"
            -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}
            -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>
            -DBUILD_TESTING=OFF
            -DBUILD_SHARED_LIBS=OFF
            -DLIBMINC_BUILD_EZMINC=OFF
        )

        if(NETCDF_FOUND)
            list(APPEND _LIBMINC_ARGS
                -DLIBMINC_MINC1_SUPPORT:BOOL=ON
                "-DNETCDF_INCLUDE_DIR:PATH=${NETCDF_INCLUDE_DIR}"
                "-DNETCDF_LIBRARY:FILEPATH=${NETCDF_LIBRARY}"
                -DNETCDF_FOUND:BOOL=ON
            )
        endif()

        ExternalProject_Add(libminc
            GIT_REPOSITORY https://github.com/BIC-MNI/libminc.git
            GIT_TAG        develop-1.9.18
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
    include(ExternalProject)
    set(MINC2_SIMPLE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/minc2_simple_install")

    set(_MINC2_DEPENDS "")
    if(NOT _USE_SYSTEM_MINC)
        set(_MINC2_DEPENDS libminc)
        set(_MINC2_LIBMINC_PREFIX "${LIBMINC_INSTALL_PREFIX}")
    else()
        set(_MINC2_LIBMINC_PREFIX "${LIBMINC_DIR}")
    endif()
    message("LIBMINC_DIR=${MINC2_LIB_DIR}")

    # Build CMAKE_PREFIX_PATH for minc2-simple (libminc prefix + optional HDF5/NetCDF roots).
    set(_MINC2_PREFIX_LIST "${_MINC2_LIBMINC_PREFIX}")
    if(DEFINED HDF5_ROOT AND HDF5_ROOT)
        list(APPEND _MINC2_PREFIX_LIST "${HDF5_ROOT}")
    endif()
    if(DEFINED NETCDF_ROOT AND NETCDF_ROOT)
        list(APPEND _MINC2_PREFIX_LIST "${NETCDF_ROOT}")
    endif()
    string(REPLACE ";" "\\;" _MINC2_CMAKE_PREFIX_PATH "${_MINC2_PREFIX_LIST}")

    ExternalProject_Add(minc2-simple-static
        GIT_REPOSITORY https://github.com/NIST-MNI/minc2-simple.git
        GIT_TAG        develop
        GIT_SHALLOW    TRUE
        UPDATE_DISCONNECTED TRUE
        INSTALL_DIR    ${MINC2_SIMPLE_INSTALL_PREFIX}
        DEPENDS        ${_MINC2_DEPENDS}
        CMAKE_ARGS
           # "-DCMAKE_MODULE_PATH=${_FAKE_MODULES_DIR}"
            -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}
            -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>
            "-DCMAKE_PREFIX_PATH=${_MINC2_CMAKE_PREFIX_PATH}"
            -DLIBMINC_DIR=${MINC2_LIB_DIR}/cmake
            -DBUILD_TESTING:BOOL=OFF
            -DBUILD_MINC2_SIMPLE_SHARED:BOOL=OFF
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
        src/NiftiVolume.cpp  # NIfTI file support
    )
    
    # Add NIfTI sources (nifti1_io.c and znzlib.c)
    list(APPEND NR_CORE_SOURCES ${NIFTI_SOURCES})

    add_library(nr_core STATIC ${NR_CORE_SOURCES})
    target_include_directories(nr_core PUBLIC
        include
        ${glm_SOURCE_DIR}
        ${eigen_INCLUDE_DIR}
        ${minc2-simple-static_INCLUDE_DIR}
        ${MINC2_INCLUDE_DIR}
        ${nifti_INCLUDE_DIR}  # NIfTI headers (nifti1_io.h, znzlib.h)
        ${CMAKE_BINARY_DIR}/libminc-prefix/src/libminc-build  # config.h for nifti
    )
    message("HDF5_LIBRARIES=${HDF5_LIBRARIES}")
    message("HDF5_C_LIBRARIES=${HDF5_C_LIBRARIES}")

    target_link_libraries(nr_core PUBLIC
        ${minc2-simple-static_LIB}
        ${MINC2_LIB}
        ${NETCDF_LIBRARY}
        ${HDF5_LIBRARIES}
        nlohmann_json::nlohmann_json
        glm
        z # Have to figure out how to do this more elegantly
    )
    add_dependencies(nr_core Eigen)
    if(NOT _USE_SYSTEM_MINC)
        add_dependencies(nr_core libminc minc2-simple-static)
    else()
        add_dependencies(nr_core minc2-simple-static)
    endif()

    # --- Sources for new_register (explicit list instead of GLOB_RECURSE) ---
    set(SOURCES
        src/main.cpp
        src/AppState.cpp
        src/Interface.cpp
        src/Prefetcher.cpp
        src/QCState.cpp
        src/ViewManager.cpp
        src/BackendFactory.cpp
        src/WindowManager.cpp
    )

    # Wayland direct touch input (optional)
    if(WAYLAND_CLIENT_FOUND)
        list(APPEND SOURCES src/WaylandTouchInput.cpp)
    endif()

    # Backend-specific sources
    if(ENABLE_VULKAN AND Vulkan_FOUND)
        list(APPEND SOURCES src/VulkanBackend.cpp src/VulkanHelpers.cpp)
    endif()
    if(ENABLE_OPENGL2)
        list(APPEND SOURCES src/OpenGL2Backend.cpp)
    endif()

    # ImGui platform backend
    set(IMGUI_DIR ${imgui_SOURCE_DIR})
    list(APPEND SOURCES
        ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    )

    # Backend-specific ImGui renderer sources
    if(ENABLE_VULKAN AND Vulkan_FOUND)
        list(APPEND SOURCES ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp)
    endif()
    if(ENABLE_OPENGL2)
        list(APPEND SOURCES ${IMGUI_DIR}/backends/imgui_impl_opengl2.cpp)
    endif()

    # --- new_register executable ---
    add_executable(new_register ${SOURCES})

    target_include_directories(new_register PRIVATE
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
        ${minc2-simple-static_SOURCE_DIR}/src  # minc2-simple headers (for extern "C" include)
        include   # for WaylandTouchInput.h
    )

    target_compile_definitions(new_register PRIVATE)
    if(ENABLE_VULKAN AND Vulkan_FOUND)
        target_compile_definitions(new_register PRIVATE HAS_VULKAN=1)
    endif()
    if(ENABLE_OPENGL2)
        target_compile_definitions(new_register PRIVATE HAS_OPENGL2=1)
    endif()
    if(ENABLE_METAL)
        target_compile_definitions(new_register PRIVATE HAS_METAL=1)
    endif()
    if(WAYLAND_CLIENT_FOUND)
        target_compile_definitions(new_register PRIVATE HAS_WAYLAND_TOUCH=1)
        target_include_directories(new_register PRIVATE ${WAYLAND_CLIENT_INCLUDE_DIRS})
    endif()

    target_link_libraries(new_register PRIVATE
        nr_core
        imgui
        glfw
        Threads::Threads
        ${CMAKE_DL_LIBS}
    )
    if(ENABLE_VULKAN AND Vulkan_FOUND)
        target_link_libraries(new_register PRIVATE Vulkan::Vulkan)
    endif()
    if(ENABLE_OPENGL2)
        target_link_libraries(new_register PRIVATE OpenGL::GL)
    endif()
    if(WAYLAND_CLIENT_FOUND)
        target_link_libraries(new_register PRIVATE ${WAYLAND_CLIENT_LIBRARIES})
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

endif() # NOT BUILD_QC_ONLY

# --- new_qc: lightweight QC viewer for JPEG/PNG image datasets ---
if(BUILD_NEW_QC OR BUILD_QC_ONLY)

    set(IMGUI_DIR ${imgui_SOURCE_DIR})

    set(QC_SOURCES
        src/qc/main.cpp
        src/qc/QCApp.cpp
        src/qc/CSVHandler.cpp
        src/qc/BackendFactory.cpp
        src/qc/OpenGL2Backend.cpp
    )

    # Wayland direct touch input (optional)
    if(WAYLAND_CLIENT_FOUND)
        list(APPEND QC_SOURCES src/WaylandTouchInput.cpp)
    endif()

    # Backend-specific sources
    if(ENABLE_VULKAN AND Vulkan_FOUND)
        list(APPEND QC_SOURCES
            src/qc/VulkanHelpers.cpp
            src/qc/VulkanBackend.cpp
        )
    endif()

    # ImGui platform backend
    list(APPEND QC_SOURCES
        ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    )
    if(ENABLE_VULKAN AND Vulkan_FOUND)
        list(APPEND QC_SOURCES ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp)
    endif()
    if(ENABLE_OPENGL2)
        list(APPEND QC_SOURCES ${IMGUI_DIR}/backends/imgui_impl_opengl2.cpp)
    endif()

    add_executable(new_qc ${QC_SOURCES})

    target_include_directories(new_qc PRIVATE
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
        ${stb_SOURCE_DIR}
        src/qc
        include   # for WaylandTouchInput.h
    )

    if(ENABLE_VULKAN AND Vulkan_FOUND)
        target_compile_definitions(new_qc PRIVATE HAS_VULKAN=1)
        target_include_directories(new_qc PRIVATE ${Vulkan_INCLUDE_DIRS})
    endif()
    if(ENABLE_OPENGL2)
        target_compile_definitions(new_qc PRIVATE HAS_OPENGL2=1)
    endif()
    if(WAYLAND_CLIENT_FOUND)
        target_compile_definitions(new_qc PRIVATE HAS_WAYLAND_TOUCH=1)
        target_include_directories(new_qc PRIVATE ${WAYLAND_CLIENT_INCLUDE_DIRS})
    endif()

    target_link_libraries(new_qc PRIVATE
        imgui
        glfw
        nlohmann_json::nlohmann_json
        ${CMAKE_DL_LIBS}
    )
    if(ENABLE_VULKAN AND Vulkan_FOUND)
        target_link_libraries(new_qc PRIVATE Vulkan::Vulkan)
    endif()
    if(ENABLE_OPENGL2)
        target_link_libraries(new_qc PRIVATE OpenGL::GL)
    endif()
    if(WAYLAND_CLIENT_FOUND)
        target_link_libraries(new_qc PRIVATE ${WAYLAND_CLIENT_LIBRARIES})
    endif()
    if(WIN32)
        target_link_libraries(new_qc PRIVATE user32 shell32)
    endif()

    # std::filesystem requires separate library on GCC 9
    if(CMAKE_COMPILER_IS_GNUCXX AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "10.0")
        target_link_libraries(new_qc PRIVATE stdc++fs)
    endif()

    INSTALL(TARGETS new_qc DESTINATION bin)

endif() # BUILD_NEW_QC OR BUILD_QC_ONLY

# --- Testing ---
enable_testing()
if(ENABLE_TESTS)
    add_subdirectory(tests)

    # ============================================================
    # headless_tests — OSMesa + ImGui offscreen UI tests
    # ============================================================

# Find OSMesa (libosmesa6-dev provides libOSMesa)
find_library(OSMESA_LIBRARY NAMES OSMesa osmesa)
if(NOT OSMESA_LIBRARY)
    set(OSMESA_LIBRARY -lOSMesa)
endif()
message(STATUS "OSMesa library: ${OSMESA_LIBRARY}")

# Ensure OpenGL is available (may already be found via ENABLE_OPENGL2)
if(NOT TARGET OpenGL::GL)
    find_package(OpenGL REQUIRED)
endif()

# Ensure Threads is available (may already be found in NOT BUILD_QC_ONLY block)
if(NOT TARGET Threads::Threads)
    find_package(Threads REQUIRED)
endif()

# stb — used for stb_image_write.h (PNG saving in imgui_impl_osmesa.cpp).
# stb may already be fetched for new_qc; guard to avoid duplicate declarations.
if(NOT stb_POPULATED)
    FetchContent_Declare(
        stb
        GIT_REPOSITORY https://github.com/nothings/stb.git
        GIT_TAG        master
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(stb)
endif()

# imgui_test_engine — advanced UI test framework
# The repo has no CMakeLists.txt in the imgui_test_engine/ sub-directory,
# so we compile the sources directly into a static library.
FetchContent_Declare(
    imgui_test_engine_repo
    GIT_REPOSITORY https://github.com/ocornut/imgui_test_engine.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(imgui_test_engine_repo)
if(NOT imgui_test_engine_repo_POPULATED)
    FetchContent_Populate(imgui_test_engine_repo)
endif()

set(IMGUI_TE_DIR ${imgui_test_engine_repo_SOURCE_DIR}/imgui_test_engine)

# imgui_test_engine: compiled together with the imgui core sources (recompiled
# with IMGUI_ENABLE_TEST_ENGINE) in a single static library.  This avoids the
# circular link dependency that arises when they are separate static libraries
# (imgui_te.a calls ImGuiTestEngineHook_* from imgui_test_engine.a and vice-versa).
add_library(imgui_test_engine STATIC
    # imgui core re-compiled with test-engine hooks enabled
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    # imgui_test_engine sources
    ${IMGUI_TE_DIR}/imgui_capture_tool.cpp
    ${IMGUI_TE_DIR}/imgui_te_context.cpp
    ${IMGUI_TE_DIR}/imgui_te_coroutine.cpp
    ${IMGUI_TE_DIR}/imgui_te_engine.cpp
    ${IMGUI_TE_DIR}/imgui_te_exporters.cpp
    ${IMGUI_TE_DIR}/imgui_te_perftool.cpp
    ${IMGUI_TE_DIR}/imgui_te_ui.cpp
    ${IMGUI_TE_DIR}/imgui_te_utils.cpp
)
target_compile_definitions(imgui_test_engine PUBLIC IMGUI_ENABLE_TEST_ENGINE)
target_include_directories(imgui_test_engine PUBLIC
    ${imgui_SOURCE_DIR}
    ${IMGUI_TE_DIR}
)

# headless_tests executable
# Compiles ImGui + OSMesa backend; no GLFW, no Vulkan, no OpenGL2 backend.
add_executable(headless_tests
    src/headless_test_main.cpp
    src/backends/imgui_impl_osmesa.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

target_compile_definitions(headless_tests PRIVATE
    HEADLESS_TEST_BUILD=1
)

target_include_directories(headless_tests PRIVATE
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${stb_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/backends
)

target_link_libraries(headless_tests PRIVATE
    imgui_test_engine    # includes imgui recompiled with IMGUI_ENABLE_TEST_ENGINE
    ${OSMESA_LIBRARY}
    OpenGL::GL
    Threads::Threads
    ${CMAKE_DL_LIBS}
)

add_test(NAME headless_ui_tests COMMAND headless_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests)
set_tests_properties(headless_ui_tests PROPERTIES
    TIMEOUT 60
    ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")

# generate_headless_baselines — generates baseline PNGs for headless_ui_tests
add_executable(generate_headless_baselines
    tests/generate_headless_baselines.cpp
    src/backends/imgui_impl_osmesa.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

target_compile_definitions(generate_headless_baselines PRIVATE
    HEADLESS_TEST_BUILD=1
)

target_include_directories(generate_headless_baselines PRIVATE
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${stb_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/backends
)

target_link_libraries(generate_headless_baselines PRIVATE
    imgui_test_engine
    ${OSMESA_LIBRARY}
    OpenGL::GL
    Threads::Threads
    ${CMAKE_DL_LIBS}
)

endif() # ENABLE_TESTS
