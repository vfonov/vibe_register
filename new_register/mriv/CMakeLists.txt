# mriv — terminal-based slice viewer.  Builds as a subdirectory of new_register.
find_package(PkgConfig REQUIRED)
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
    src/render/SliceGeometry.cpp
)
target_include_directories(mriv_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(mriv_lib PUBLIC nr_core cxxopts::cxxopts)
target_link_libraries(mriv_lib PRIVATE PkgConfig::NOTCURSES)

add_executable(mriv src/main.cpp)
target_link_libraries(mriv PRIVATE mriv_lib)

if(ENABLE_TESTS)
    add_subdirectory(tests)
endif()
