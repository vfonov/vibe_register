# mriv — terminal-based slice viewer.  Builds as a subdirectory of new_register.
find_package(PkgConfig REQUIRED)

# The parent forces CMAKE_FIND_LIBRARY_SUFFIXES to prefer .a globally (see
# new_register/CMakeLists.txt, static-linking section) so libminc's own
# dependencies resolve statically. notcurses's static archive does not carry
# its optional transitive deps (qrcodegen, gpm, libdeflate) in its .pc file,
# so linking it statically fails with undefined references at link time.
# Prefer the shared library for notcurses specifically, in this directory
# scope only -- this does not affect the parent's own dependency resolution.
set(CMAKE_FIND_LIBRARY_SUFFIXES ".so" ".a")
pkg_check_modules(NOTCURSES REQUIRED IMPORTED_TARGET notcurses)

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
    src/cli/Run.cpp
    src/interactive/ViewState.cpp
    src/render/SliceGeometry.cpp
    src/render/Resample.cpp
    src/render/Terminal.cpp
    src/render/Encode.cpp
    src/util/Base64.cpp
)
target_include_directories(mriv_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_include_directories(mriv_lib PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb)
target_link_libraries(mriv_lib PUBLIC nr_core cxxopts::cxxopts)
target_link_libraries(mriv_lib PRIVATE PkgConfig::NOTCURSES)

add_executable(mriv src/main.cpp)
target_link_libraries(mriv PRIVATE mriv_lib)

if(ENABLE_TESTS)
    add_subdirectory(tests)
endif()
