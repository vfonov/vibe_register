cmake_minimum_required(VERSION 3.14)
project(new_qc VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Fetch ImGui dependencies early
include(FetchContent)

# Find required packages
find_package(glfw3 3.3 REQUIRED)
find_package(Vulkan REQUIRED)
find_package(OpenGL REQUIRED)

# Fetch nlohmann_json
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(json)

# Fetch ImGui
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.0
)
FetchContent_MakeAvailable(imgui)

# Build ImGui sources
set(IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

add_library(imgui_lib STATIC ${IMGUI_SOURCES})
target_include_directories(imgui_lib PUBLIC 
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui_lib PUBLIC glfw)

# Fetch stb_image (single-header image loading)
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master
)
FetchContent_MakeAvailable(stb)

# Sources
set(QC_SOURCES
    src/main.cpp
    src/QCApp.cpp
    src/QCApp.h
    src/CSVHandler.cpp
    src/CSVHandler.h
)

# Create executable
add_executable(new_qc ${QC_SOURCES})

# Include directories
target_include_directories(new_qc PRIVATE
    ${GLFW3_INCLUDE_DIRS}
    ${Vulkan_INCLUDE_DIRS}
    ${stb_SOURCE_DIR}
    include
)

# Link libraries
target_link_libraries(new_qc PRIVATE
    glfw
    Vulkan::Vulkan
    nlohmann_json::nlohmann_json
    imgui_lib
    OpenGL::GL
)

# Platform-specific settings
if(WIN32)
    target_link_libraries(new_qc PRIVATE user32 shell32)
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(new_qc PRIVATE dl)
endif()

# Installation
install(TARGETS new_qc DESTINATION bin)

# Enable testing
option(BUILD_TESTING "Build test suite" ON)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
