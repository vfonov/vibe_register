# --- Test targets ---
# All test executables and CTest registrations for new_register.

# Common paths
set(SRC_DIR ${PROJECT_SOURCE_DIR}/src)
set(INC_DIR ${PROJECT_SOURCE_DIR}/include)
set(TEST_DATA_DIR ${PROJECT_SOURCE_DIR}/../test_data)

# Shared include dirs for tests that use Volume / MINC
set(COMMON_INCLUDES ${INC_DIR} ${glm_SOURCE_DIR} ${eigen_INCLUDE_DIR} ${minc2-simple-static_SOURCE_DIR}/src)

# --- Helper macro to reduce boilerplate ---
# add_nr_test(<name>
#     SOURCES      extra .cpp from src/ (optional)
#     INCLUDES     include dirs          (defaults to COMMON_INCLUDES)
#     LINKS        link libraries
#     ARGS         command-line args for ctest
# )
macro(add_nr_test NAME)
    cmake_parse_arguments(T "" "" "SOURCES;INCLUDES;LINKS;ARGS" ${ARGN})

    # Build source list: the test file itself + any extra source files
    set(_srcs ${CMAKE_CURRENT_SOURCE_DIR}/${NAME}.cpp)
    foreach(_s ${T_SOURCES})
        list(APPEND _srcs ${SRC_DIR}/${_s})
    endforeach()

    add_executable(${NAME} ${_srcs})

    if(T_INCLUDES)
        target_include_directories(${NAME} PRIVATE ${T_INCLUDES})
    else()
        target_include_directories(${NAME} PRIVATE ${COMMON_INCLUDES})
    endif()

    if(T_LINKS)
        target_link_libraries(${NAME} PRIVATE ${T_LINKS})
    endif()
endmacro()

# ------------------------------------------------------------------
# Volume / MINC tests (link minc2-simple-static + glm)
# ------------------------------------------------------------------

add_nr_test(test_real_data
    SOURCES  Volume.cpp TagWrapper.cpp
    LINKS    minc2-simple-static glm
)
add_test(NAME ReadRealData COMMAND test_real_data
    ${TEST_DATA_DIR}/mni_icbm152_t1_tal_nlin_sym_09c.mnc
    ${TEST_DATA_DIR}/mni_icbm152_t1_tal_nlin_sym_09c_mask.mnc
)

add_nr_test(test_coordinate_sync
    SOURCES  Volume.cpp TagWrapper.cpp
    LINKS    minc2-simple-static glm
)
add_test(NAME CoordinateSyncTest COMMAND test_coordinate_sync ${TEST_DATA_DIR})

add_nr_test(test_center_of_mass
    SOURCES  Volume.cpp TagWrapper.cpp
    LINKS    minc2-simple-static glm
)
add_test(NAME CenterOfMassTest COMMAND test_center_of_mass
    ${TEST_DATA_DIR}/mni_icbm152_t1_tal_nlin_sym_09c.mnc
)

add_nr_test(test_thick_slices_com
    SOURCES  Volume.cpp TagWrapper.cpp
    LINKS    minc2-simple-static glm
)
add_test(NAME ThickSlicesCOMTest COMMAND test_thick_slices_com
    ${TEST_DATA_DIR}/mni_icbm152_t1_tal_nlin_sym_09c_thick_slices.mnc
)

add_nr_test(test_world_to_voxel
    SOURCES  Volume.cpp TagWrapper.cpp
    LINKS    minc2-simple-static minc2 glm
)
add_test(NAME WorldToVoxelTest COMMAND test_world_to_voxel ${TEST_DATA_DIR})

add_nr_test(test_matrix_debug
    SOURCES  Volume.cpp TagWrapper.cpp
    LINKS    minc2-simple-static minc2 glm
)
add_test(NAME MatrixDebugTest COMMAND test_matrix_debug ${TEST_DATA_DIR})

# ------------------------------------------------------------------
# Tag test (no Volume.cpp, links minc2 directly)
# ------------------------------------------------------------------

add_nr_test(test_tag_load
    SOURCES  TagWrapper.cpp
    LINKS    minc2-simple-static minc2 glm
)
add_test(NAME TagLoadTest COMMAND test_tag_load
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# ------------------------------------------------------------------
# Standalone MINC tests (no project source files)
# ------------------------------------------------------------------

add_nr_test(test_volume_info
    LINKS  minc2-simple-static minc2 glm
)
add_test(NAME VolumeInfoTest COMMAND test_volume_info ${TEST_DATA_DIR})

add_nr_test(test_minc_dims
    LINKS  minc2-simple-static minc2 glm
)
add_test(NAME MincDimsTest COMMAND test_minc_dims ${TEST_DATA_DIR})

# ------------------------------------------------------------------
# Colour map test (no MINC, no GLM)
# ------------------------------------------------------------------

add_nr_test(test_colour_map
    SOURCES   ColourMap.cpp
    INCLUDES  ${INC_DIR}
    LINKS     ""
)
add_test(NAME ColourMapTests COMMAND test_colour_map)

# ------------------------------------------------------------------
# QC CSV test (no Vulkan, no MINC, no ImGui)
# ------------------------------------------------------------------

add_nr_test(test_qc_csv
    SOURCES   QCState.cpp AppConfig.cpp
    INCLUDES  ${INC_DIR}
    LINKS     nlohmann_json::nlohmann_json
)
add_test(NAME QCCsvTest COMMAND test_qc_csv)

# ------------------------------------------------------------------
# AppConfig JSON round-trip test
# ------------------------------------------------------------------

add_nr_test(test_app_config
    SOURCES   AppConfig.cpp
    INCLUDES  ${INC_DIR}
    LINKS     nlohmann_json::nlohmann_json
)
add_test(NAME AppConfigTest COMMAND test_app_config)

# ------------------------------------------------------------------
# Transform computation test (Eigen, GLM, no MINC)
# ------------------------------------------------------------------

add_nr_test(test_transform
    SOURCES   Transform.cpp
    INCLUDES  ${INC_DIR} ${glm_SOURCE_DIR} ${eigen_INCLUDE_DIR} ${minc2-simple-static_SOURCE_DIR}/src
    LINKS     glm minc2-simple-static minc2
)
add_test(NAME TransformTest COMMAND test_transform)

# --- GCC < 10 needs -lstdc++fs for std::filesystem ---
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 10)
    foreach(_tgt test_qc_csv test_app_config test_matrix_debug test_world_to_voxel test_coordinate_sync)
        target_link_libraries(${_tgt} PRIVATE stdc++fs)
    endforeach()
endif()
