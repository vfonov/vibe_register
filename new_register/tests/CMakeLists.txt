# --- Test targets ---
# All test executables and CTest registrations for new_register and new_qc.

if(NOT BUILD_QC_ONLY)

# Common paths
set(SRC_DIR ${PROJECT_SOURCE_DIR}/src)
set(INC_DIR ${PROJECT_SOURCE_DIR}/include)
set(TEST_DATA_DIR ${PROJECT_SOURCE_DIR}/../test_data)

# Shared include dirs for tests that use Volume / MINC
set(COMMON_INCLUDES ${INC_DIR} ${glm_SOURCE_DIR} ${eigen_INCLUDE_DIR} ${minc2-simple-static_INCLUDE_DIR} ${MINC2_INCLUDE_DIR})

# Library paths for linking (from parent CMakeLists)
set(TEST_LIBS ${minc2-simple-static_LIB} ${MINC2_LIB} ${HDF5_C_LIBRARIES} ${NETCDF_LIBRARY} z)
message("TEST_LIBS=${TEST_LIBS}")

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
# Volume / MINC tests — link nr_core (provides Volume, TagWrapper, etc.)
# ------------------------------------------------------------------

add_nr_test(test_real_data
    LINKS  nr_core
)
add_test(NAME ReadRealData COMMAND test_real_data
    ${TEST_DATA_DIR}/mni_icbm152_t1_tal_nlin_sym_09c.mnc
    ${TEST_DATA_DIR}/mni_icbm152_t1_tal_nlin_sym_09c_mask.mnc
)

add_nr_test(test_coordinate_sync
    LINKS  nr_core
)
add_test(NAME CoordinateSyncTest COMMAND test_coordinate_sync ${TEST_DATA_DIR})

add_nr_test(test_center_of_mass
    LINKS  nr_core
)
add_test(NAME CenterOfMassTest COMMAND test_center_of_mass
    ${TEST_DATA_DIR}/mni_icbm152_t1_tal_nlin_sym_09c.mnc
)

add_nr_test(test_thick_slices_com
    LINKS  nr_core
)
add_test(NAME ThickSlicesCOMTest COMMAND test_thick_slices_com
    ${TEST_DATA_DIR}/mni_icbm152_t1_tal_nlin_sym_09c_thick_slices.mnc
)

add_nr_test(test_world_to_voxel
    LINKS  nr_core
)
add_test(NAME WorldToVoxelTest COMMAND test_world_to_voxel ${TEST_DATA_DIR})

add_nr_test(test_matrix_debug
    LINKS  nr_core
)
add_test(NAME MatrixDebugTest COMMAND test_matrix_debug ${TEST_DATA_DIR})

# ------------------------------------------------------------------
# Tag test (no Volume.cpp needed — nr_core provides TagWrapper)
# ------------------------------------------------------------------

add_nr_test(test_tag_load
    LINKS  nr_core
)
add_test(NAME TagLoadTest COMMAND test_tag_load
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# ------------------------------------------------------------------
# Standalone MINC tests (no project source files)
# ------------------------------------------------------------------

add_nr_test(test_volume_info
    LINKS  ${TEST_LIBS} glm
)
add_dependencies(test_volume_info minc2-simple-static)
if(NOT _USE_SYSTEM_MINC)
    add_dependencies(test_volume_info libminc)
endif()
add_test(NAME VolumeInfoTest COMMAND test_volume_info ${TEST_DATA_DIR})

add_nr_test(test_minc_dims
    LINKS  ${TEST_LIBS} glm
)
add_dependencies(test_minc_dims minc2-simple-static)
if(NOT _USE_SYSTEM_MINC)
    add_dependencies(test_minc_dims libminc)
endif()
add_test(NAME MincDimsTest COMMAND test_minc_dims ${TEST_DATA_DIR})

# ------------------------------------------------------------------
# Colour map test — nr_core provides ColourMap
# ------------------------------------------------------------------

add_nr_test(test_colour_map
    INCLUDES  ${INC_DIR}
    LINKS     nr_core
)
add_nr_test(test_label_rounding)
add_test(NAME LabelRoundingTest COMMAND test_label_rounding)
add_test(NAME ColourMapTests COMMAND test_colour_map)

# ------------------------------------------------------------------
# QC CSV test — needs QCState.cpp (not in nr_core) + nr_core for AppConfig
# ------------------------------------------------------------------

add_nr_test(test_qc_csv
    SOURCES   QCState.cpp
    INCLUDES  ${INC_DIR}
    LINKS     nr_core
)
add_test(NAME QCCsvTest COMMAND test_qc_csv)

# ------------------------------------------------------------------
# AppConfig JSON round-trip test — nr_core provides AppConfig
# ------------------------------------------------------------------

add_nr_test(test_app_config
    INCLUDES  ${INC_DIR}
    LINKS     nr_core
)
add_test(NAME AppConfigTest COMMAND test_app_config)

# ------------------------------------------------------------------
# Transform computation test — nr_core provides Transform
# ------------------------------------------------------------------

add_nr_test(test_transform
    INCLUDES  ${INC_DIR} ${glm_SOURCE_DIR} ${eigen_INCLUDE_DIR} ${minc2-simple-static_INCLUDE_DIR}
    LINKS     nr_core
)
add_test(NAME TransformTest COMMAND test_transform)

# ------------------------------------------------------------------
# SliceRenderer / mincpik rendering test
# ------------------------------------------------------------------

add_nr_test(test_mincpik
    SOURCES   mincpik/text_render.cpp mincpik/mosaic.cpp mincpik/colour_bar.cpp
    INCLUDES  ${COMMON_INCLUDES} ${SRC_DIR}/mincpik
    LINKS     nr_core
)
add_test(NAME MincpikTest COMMAND test_mincpik
    ${TEST_DATA_DIR}/mni_icbm152_t1_tal_nlin_sym_09c.mnc
)

# --- GCC < 10 needs -lstdc++fs for std::filesystem ---
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 10)
    foreach(_tgt test_qc_csv test_app_config test_matrix_debug test_world_to_voxel test_coordinate_sync)
        target_link_libraries(${_tgt} PRIVATE stdc++fs)
    endforeach()
endif()

endif() # NOT BUILD_QC_ONLY

# ------------------------------------------------------------------
# new_qc CSV handler unit tests
# ------------------------------------------------------------------

if(BUILD_NEW_QC OR BUILD_QC_ONLY)
    add_executable(csv_handler_test
        ${CMAKE_CURRENT_SOURCE_DIR}/csv_test.cpp
        ${PROJECT_SOURCE_DIR}/src/qc/CSVHandler.cpp
    )
    target_include_directories(csv_handler_test PRIVATE
        ${PROJECT_SOURCE_DIR}/src
    )
    add_test(NAME QCCsvHandlerTest COMMAND csv_handler_test
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
    set_tests_properties(QCCsvHandlerTest PROPERTIES
        LABELS "unit"
        TIMEOUT 30
    )
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 10)
        target_link_libraries(csv_handler_test PRIVATE stdc++fs)
    endif()
endif()
