# mriv — terminal-based slice viewer.  Builds as a subdirectory of new_register.

# Set to download, build, and statically link notcurses from source via
# ExternalProject instead of requiring a system install -- for hosts with no
# system-provided notcurses, or to produce a fully self-contained mriv
# binary. See cmake/BuildNotcurses.cmake for what this builds and why.
option(MRIV_BUILD_NOTCURSES
    "Download, build, and statically link notcurses from source (ExternalProject) instead of using a system install"
    OFF)

if(MRIV_BUILD_NOTCURSES)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/BuildNotcurses.cmake)
else()
    # The parent forces CMAKE_FIND_LIBRARY_SUFFIXES to prefer .a globally (see
    # new_register/CMakeLists.txt, static-linking section) so libminc's own
    # dependencies resolve statically. A *system-packaged* notcurses's static
    # archive does not carry its optional transitive deps (qrcodegen, gpm,
    # libdeflate), so linking it statically fails with undefined references
    # at link time. Prefer the shared library for notcurses specifically, in
    # this directory scope only -- this does not affect the parent's own
    # dependency resolution. (MRIV_BUILD_NOTCURSES=ON sidesteps this
    # entirely -- see cmake/BuildNotcurses.cmake.)
    #
    # Located via find_path/find_library rather than pkg-config: this repo
    # targets Linux and macOS (AGENTS.md), and macOS ships no pkg-config
    # executable at all (see cmake/BuildNotcurses.cmake's file-header
    # comment for the fuller story), so a pkg-config-based lookup can't be
    # relied on here even for a system-installed notcurses.
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".so" ".dylib" ".a")
    find_path(MRIV_NOTCURSES_INCLUDE_DIR notcurses/notcurses.h)
    find_library(MRIV_NOTCURSES_CORE_LIBRARY NAMES notcurses-core)
    find_library(MRIV_NOTCURSES_LIBRARY NAMES notcurses)
    if(NOT MRIV_NOTCURSES_INCLUDE_DIR OR NOT MRIV_NOTCURSES_CORE_LIBRARY OR NOT MRIV_NOTCURSES_LIBRARY)
        message(FATAL_ERROR
            "mriv: no system notcurses found (looked for notcurses/notcurses.h "
            "plus libnotcurses/libnotcurses-core via find_path/find_library). "
            "Re-run cmake with -DMRIV_BUILD_NOTCURSES=ON to download and "
            "statically build a private copy instead.")
    endif()
    add_library(mriv_notcurses INTERFACE)
    target_include_directories(mriv_notcurses INTERFACE ${MRIV_NOTCURSES_INCLUDE_DIR})
    target_link_libraries(mriv_notcurses INTERFACE ${MRIV_NOTCURSES_LIBRARY} ${MRIV_NOTCURSES_CORE_LIBRARY})
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
