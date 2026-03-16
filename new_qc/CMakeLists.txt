cmake_minimum_required(VERSION 3.14)
project(new_qc VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# --- Backend options ---
option(ENABLE_VULKAN  "Build Vulkan backend"  ON)
option(ENABLE_OPENGL2 "Build OpenGL2 backend" ON)

# --- Find required packages ---
find_package(glfw3 3.3 REQUIRED)

# Vulkan (optional)
if(ENABLE_VULKAN)
    find_package(Vulkan)
    if(Vulkan_FOUND)
        message(STATUS "Vulkan found — building with Vulkan backend")
        add_compile_definitions(HAS_VULKAN)
    else()
        message(STATUS "Vulkan not found — skipping Vulkan backend")
    endif()
endif()

# OpenGL (for OpenGL2 backend)
if(ENABLE_OPENGL2)
    find_package(OpenGL REQUIRED)
    add_compile_definitions(HAS_OPENGL2)
endif()

# --- Fetch nlohmann_json ---
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(json)

# --- Fetch ImGui (docking branch for AddFontDefaultVector) ---
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG docking
)
FetchContent_MakeAvailable(imgui)

# --- Build ImGui static library ---
set(IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl2.cpp
)

if(ENABLE_VULKAN AND Vulkan_FOUND)
    list(APPEND IMGUI_SOURCES ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)
endif()

add_library(imgui_lib STATIC ${IMGUI_SOURCES})
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui_lib PUBLIC glfw)

if(ENABLE_VULKAN AND Vulkan_FOUND)
    target_link_libraries(imgui_lib PUBLIC Vulkan::Vulkan)
endif()

# --- Fetch stb_image ---
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master
)
FetchContent_MakeAvailable(stb)

# --- Sources ---
set(QC_SOURCES
    src/main.cpp
    src/QCApp.cpp
    src/QCApp.h
    src/CSVHandler.cpp
    src/CSVHandler.h
    src/Backend.h
    src/BackendFactory.cpp
    src/OpenGL2Backend.h
    src/OpenGL2Backend.cpp
)

if(ENABLE_VULKAN AND Vulkan_FOUND)
    list(APPEND QC_SOURCES
        src/VulkanHelpers.h
        src/VulkanHelpers.cpp
        src/VulkanBackend.h
        src/VulkanBackend.cpp
    )
endif()

# --- Create executable ---
add_executable(new_qc ${QC_SOURCES})

# --- Include directories ---
target_include_directories(new_qc PRIVATE
    ${stb_SOURCE_DIR}
    src
)

if(ENABLE_VULKAN AND Vulkan_FOUND)
    target_include_directories(new_qc PRIVATE ${Vulkan_INCLUDE_DIRS})
endif()

# --- Link libraries ---
target_link_libraries(new_qc PRIVATE
    glfw
    nlohmann_json::nlohmann_json
    imgui_lib
)

if(ENABLE_OPENGL2)
    target_link_libraries(new_qc PRIVATE OpenGL::GL)
endif()

if(ENABLE_VULKAN AND Vulkan_FOUND)
    target_link_libraries(new_qc PRIVATE Vulkan::Vulkan)
endif()

# --- Platform-specific ---
if(WIN32)
    target_link_libraries(new_qc PRIVATE user32 shell32)
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(new_qc PRIVATE dl)
endif()

# --- Installation ---
install(TARGETS new_qc DESTINATION bin)

# --- Tests ---
option(BUILD_TESTING "Build test suite" ON)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
