# mriv — terminal-based slice viewer.  Builds as a subdirectory of new_register.

# Set to download, build, and statically link notcurses from source via
# ExternalProject instead of requiring a system install -- for hosts with no
# system-provided notcurses, or to produce a fully self-contained mriv
# binary. See cmake/BuildNotcurses.cmake for what this builds and why.
option(MRIV_BUILD_NOTCURSES
    "Download, build, and statically link notcurses from source (ExternalProject) instead of using a system install"
    OFF)

find_package(PkgConfig REQUIRED)

if(MRIV_BUILD_NOTCURSES)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/BuildNotcurses.cmake)
else()
    # The parent forces CMAKE_FIND_LIBRARY_SUFFIXES to prefer .a globally (see
    # new_register/CMakeLists.txt, static-linking section) so libminc's own
    # dependencies resolve statically. A *system-packaged* notcurses's static
    # archive does not carry its optional transitive deps (qrcodegen, gpm,
    # libdeflate) in its .pc file, so linking it statically fails with
    # undefined references at link time. Prefer the shared library for
    # notcurses specifically, in this directory scope only -- this does not
    # affect the parent's own dependency resolution. (MRIV_BUILD_NOTCURSES=ON
    # sidesteps this entirely -- see cmake/BuildNotcurses.cmake.)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".so" ".a")
    pkg_check_modules(NOTCURSES QUIET IMPORTED_TARGET notcurses)
    if(NOT NOTCURSES_FOUND)
        message(FATAL_ERROR
            "mriv: no system notcurses found (pkg-config module 'notcurses'). "
            "Re-run cmake with -DMRIV_BUILD_NOTCURSES=ON to download and "
            "statically build a private copy instead.")
    endif()
    add_library(mriv_notcurses INTERFACE)
    target_link_libraries(mriv_notcurses INTERFACE PkgConfig::NOTCURSES)
endif()

include(FetchContent)
FetchContent_Declare(cxxopts
    GIT_REPOSITORY https://github.com/jarro2783/cxxopts.git
    GIT_TAG        v3.3.1)
set(CXXOPTS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CXXOPTS_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(cxxopts)

add_library(mriv_lib STATIC
    src/cli/Options.cpp
    src/cli/SliceSelection.cpp
    src/cli/ColourMapArg.cpp
    src/cli/VolumeInfo.cpp
    src/cli/InteractiveDecision.cpp
    src/cli/ViewList.cpp
    src/cli/Run.cpp
    src/interactive/ViewState.cpp
    src/interactive/FramePlan.cpp
    src/interactive/RangeEditor.cpp
    src/interactive/Overlay.cpp
    src/interactive/Screen.cpp
    src/interactive/Session.cpp
    src/interactive/StatusLine.cpp
    src/render/SliceGeometry.cpp
    src/render/Resample.cpp
    src/render/Crosshair.cpp
    src/render/Compose.cpp
    src/render/FrameBuilder.cpp
    src/render/Layout.cpp
    src/render/SlicePipeline.cpp
    src/render/Terminal.cpp
    src/render/Encode.cpp
    src/render/Screenshot.cpp
    src/util/Base64.cpp
)
target_include_directories(mriv_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_include_directories(mriv_lib PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb)
target_link_libraries(mriv_lib PUBLIC nr_core cxxopts::cxxopts)
target_link_libraries(mriv_lib PRIVATE mriv_notcurses)

add_executable(mriv src/main.cpp)
target_link_libraries(mriv PRIVATE mriv_lib)

if(ENABLE_TESTS)
    add_subdirectory(tests)
endif()
