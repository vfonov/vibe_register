# --- Test targets ---
# All test executables and CTest registrations for new_register and new_qc.

# --- Shared Catch2 entry point ---
# nr_test_main provides main() (a Catch::Session with the extra --data-dir and
# --src-dir options, read back through nr_test.h).  It links Catch2 only — not
# nr_core — so tests of tools without nr_core (new_qc) can use it too.
add_library(nr_test_main STATIC ${CMAKE_CURRENT_SOURCE_DIR}/nr_test_main.cpp)
target_include_directories(nr_test_main PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(nr_test_main PUBLIC Catch2::Catch2)

if(NR_NEED_CORE)

# Common paths
set(SRC_DIR ${PROJECT_SOURCE_DIR}/src)
set(INC_DIR ${PROJECT_SOURCE_DIR}/include)
set(TEST_DATA_DIR ${PROJECT_SOURCE_DIR}/../test_data)

# Shared include dirs for tests that use Volume / MINC
set(COMMON_INCLUDES ${INC_DIR} ${glm_SOURCE_DIR} ${eigen_INCLUDE_DIR} ${minc2-simple-static_INCLUDE_DIR} ${MINC2_INCLUDE_DIR})

# Library paths for linking (from parent CMakeLists)
set(TEST_LIBS ${minc2-simple-static_LIB} ${MINC2_LIB} ${HDF5_C_LIBRARIES} ${NETCDF_LIBRARY} ${ZLIB_LIBRARIES})
message("TEST_LIBS=${TEST_LIBS}")

# --- Helper macro to reduce boilerplate ---
# add_nr_test(<name>
#     CATCH2       Catch2 test: link nr_test_main, which provides main()
#     SOURCES      extra .cpp from src/ (optional)
#     INCLUDES     include dirs          (defaults to COMMON_INCLUDES)
#     LINKS        link libraries
#     ARGS         command-line args for ctest
# )
macro(add_nr_test NAME)
    cmake_parse_arguments(T "CATCH2" "" "SOURCES;INCLUDES;LINKS;ARGS" ${ARGN})

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

    if(T_CATCH2)
        target_link_libraries(${NAME} PRIVATE nr_test_main)
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

add_nr_test(test_tag_wrapper CATCH2
    INCLUDES  ${INC_DIR}
    LINKS     nr_core
)
add_test(NAME TagWrapperTest COMMAND test_tag_wrapper
    --src-dir ${CMAKE_CURRENT_SOURCE_DIR}
)

add_nr_test(test_label_lut CATCH2
    SOURCES   AppState.cpp
    INCLUDES  ${INC_DIR} ${imgui_SOURCE_DIR}
    LINKS     nr_core
)
add_test(NAME LabelLutTest COMMAND test_label_lut)

add_nr_test(test_apply_config CATCH2
    SOURCES   AppState.cpp
    INCLUDES  ${INC_DIR} ${imgui_SOURCE_DIR}
    LINKS     nr_core
)
add_test(NAME ApplyConfigTest COMMAND test_apply_config)

add_nr_test(test_qc_row_switch CATCH2
    SOURCES   AppState.cpp
    INCLUDES  ${INC_DIR} ${imgui_SOURCE_DIR}
    LINKS     nr_core
)
add_test(NAME QCRowSwitchTest COMMAND test_qc_row_switch)

add_nr_test(test_view_manager CATCH2
    SOURCES   ViewManager.cpp AppState.cpp
    INCLUDES  ${INC_DIR} ${imgui_SOURCE_DIR}
    LINKS     nr_core
)
add_test(NAME ViewManagerTest COMMAND test_view_manager)

add_nr_test(test_slice_coords CATCH2
    INCLUDES  ${INC_DIR}
)
add_test(NAME SliceCoordsTest COMMAND test_slice_coords)

if(TARGET imgui)
    add_nr_test(test_hotkeys CATCH2
        INCLUDES  ${INC_DIR} ${imgui_SOURCE_DIR}
        LINKS     imgui
    )
    add_test(NAME HotkeysTest COMMAND test_hotkeys)
endif()

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

add_nr_test(test_colour_map CATCH2
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
# NIfTI file loading test
# ------------------------------------------------------------------
add_nr_test(test_nifti_load
    INCLUDES  ${INC_DIR}
    LINKS     nr_core
)
add_test(NAME NiftiLoadTest COMMAND test_nifti_load
    ${TEST_DATA_DIR}/mni_icbm152_t1_tal_nlin_sym_09c.nii.gz
)

# ------------------------------------------------------------------
# NIfTI datatypes, scaling, geometry — synthetic images written at run time
# ------------------------------------------------------------------
add_nr_test(test_nifti_datatypes CATCH2
    INCLUDES  ${INC_DIR}
    LINKS     nr_core
)
add_test(NAME NiftiDatatypesTest COMMAND test_nifti_datatypes)
# Memory errors in the loader (e.g. use-after-free on error paths) only show up
# under a checker; run the datatype cases under valgrind when it is installed.
find_program(VALGRIND_EXECUTABLE valgrind)
if(VALGRIND_EXECUTABLE)
    add_test(NAME NiftiDatatypesMemcheck
        COMMAND ${VALGRIND_EXECUTABLE} --error-exitcode=9 --leak-check=full
                $<TARGET_FILE:test_nifti_datatypes> "[datatype]")
    add_test(NAME TagWrapperMemcheck
        COMMAND ${VALGRIND_EXECUTABLE} --error-exitcode=9 --leak-check=full
                $<TARGET_FILE:test_tag_wrapper> "[memcheck]"
                --src-dir ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME ViewManagerMemcheck
        COMMAND ${VALGRIND_EXECUTABLE} --error-exitcode=9
                $<TARGET_FILE:test_view_manager> "[sync]")
endif()

# ------------------------------------------------------------------
# NIfTI vs MINC coordinate matching test
# ------------------------------------------------------------------
add_nr_test(test_nifti_mnc_match
    INCLUDES  ${INC_DIR}
    LINKS     nr_core
)
add_test(NAME NiftiMncMatchTest COMMAND test_nifti_mnc_match
    ${TEST_DATA_DIR}/clp_VF_20190508_t1.nii.gz
    ${TEST_DATA_DIR}/clp_VF_20190508_t1.mnc
)

# ------------------------------------------------------------------
# Transform computation test — nr_core provides Transform
# ------------------------------------------------------------------

add_nr_test(test_transform
    INCLUDES  ${INC_DIR} ${glm_SOURCE_DIR} ${eigen_INCLUDE_DIR} ${minc2-simple-static_INCLUDE_DIR}
    LINKS     nr_core
)
add_test(NAME TransformTest COMMAND test_transform)

add_nr_test(test_transform_lsq CATCH2
    INCLUDES  ${INC_DIR} ${glm_SOURCE_DIR}
    LINKS     nr_core
)
add_test(NAME TransformLsqTest COMMAND test_transform_lsq)

add_nr_test(test_xfm_direction CATCH2
    INCLUDES  ${INC_DIR} ${glm_SOURCE_DIR} ${minc2-simple-static_INCLUDE_DIR}
    LINKS     nr_core
)
add_test(NAME XfmDirectionTest COMMAND test_xfm_direction)

# ------------------------------------------------------------------
# SliceRenderer / mincpik rendering test
# ------------------------------------------------------------------

if(BUILD_NEW_MINCPIK)
    add_nr_test(test_mincpik
        SOURCES   mincpik/text_render.cpp mincpik/mosaic.cpp mincpik/colour_bar.cpp
        INCLUDES  ${COMMON_INCLUDES} ${SRC_DIR}/mincpik
        LINKS     nr_core
    )
    add_test(NAME MincpikTest COMMAND test_mincpik
        ${TEST_DATA_DIR}/mni_icbm152_t1_tal_nlin_sym_09c.mnc
    )
endif()

# ------------------------------------------------------------------
# Robustness against non-finite (inf/NaN) voxel values
# ------------------------------------------------------------------

add_nr_test(test_robust_nonfinite
    INCLUDES  ${INC_DIR}
    LINKS     nr_core
)
add_test(NAME RobustNonfiniteTest COMMAND test_robust_nonfinite
    ${TEST_DATA_DIR}/model_mlog_q_sex__Male.mnc
)

# ------------------------------------------------------------------
# Overlay alpha-blend regression test (no external data needed)
# ------------------------------------------------------------------
add_nr_test(test_overlay_blend
    INCLUDES  ${INC_DIR} ${glm_SOURCE_DIR}
    LINKS     nr_core
)
add_test(NAME OverlayBlendTest COMMAND test_overlay_blend)

add_nr_test(test_volume_core
    INCLUDES  ${INC_DIR} ${glm_SOURCE_DIR}
    LINKS     nr_core
)
add_test(NAME VolumeCoreTest COMMAND test_volume_core)

add_nr_test(test_volume_index CATCH2
    INCLUDES  ${INC_DIR} ${glm_SOURCE_DIR}
    LINKS     nr_core
)
add_test(NAME VolumeIndexTest COMMAND test_volume_index)

add_nr_test(test_minc_nonspatial CATCH2
    INCLUDES  ${INC_DIR} ${glm_SOURCE_DIR} ${minc2-simple-static_INCLUDE_DIR}
    LINKS     nr_core
)
add_test(NAME MincNonSpatialTest COMMAND test_minc_nonspatial)

add_nr_test(test_volume_reorient
    INCLUDES  ${INC_DIR}
    LINKS     nr_core
)
add_test(NAME VolumeReorientTest COMMAND test_volume_reorient)

add_nr_test(test_minc_reorient_match
    LINKS  nr_core
)
add_test(NAME MincReorientMatchTest COMMAND test_minc_reorient_match
    ${TEST_DATA_DIR}/clp_VF_20190508_t1.mnc
    ${TEST_DATA_DIR}/mni_icbm152_t1_tal_nlin_sym_09c.mnc
    ${TEST_DATA_DIR}/mni_icbm152_t1_tal_nlin_sym_09c_thick_slices.mnc
    ${CMAKE_CURRENT_SOURCE_DIR}/sq1.mnc
)

# ------------------------------------------------------------------
# Overlay rendering correctness test
# ------------------------------------------------------------------

add_nr_test(test_overlap
    INCLUDES  ${COMMON_INCLUDES} ${stb_SOURCE_DIR}
    LINKS     nr_core
)
add_test(NAME OverlapTest COMMAND test_overlap
    ${CMAKE_CURRENT_SOURCE_DIR}/sq1.mnc
    ${CMAKE_CURRENT_SOURCE_DIR}/sq2_tr.mnc
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# ------------------------------------------------------------------
# Overlay rendering correctness test — sq1 + sq2 (unregistered)
# ------------------------------------------------------------------

add_nr_test(test_overlap2
    INCLUDES  ${COMMON_INCLUDES} ${stb_SOURCE_DIR}
    LINKS     nr_core
)
add_test(NAME Overlap2Test COMMAND test_overlap2
    ${CMAKE_CURRENT_SOURCE_DIR}/sq1.mnc
    ${CMAKE_CURRENT_SOURCE_DIR}/sq2.mnc
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# ------------------------------------------------------------------
# Coordinate-chain diagnostic test (H1/H2/H3 for sq2_tr)
# ------------------------------------------------------------------

add_nr_test(test_coord_sq2tr
    INCLUDES  ${COMMON_INCLUDES}
    LINKS     nr_core
)
add_test(NAME CoordSq2TrTest COMMAND test_coord_sq2tr
    ${CMAKE_CURRENT_SOURCE_DIR}/sq1.mnc
    ${CMAKE_CURRENT_SOURCE_DIR}/sq2_tr.mnc
)

# ------------------------------------------------------------------
# Volume metadata dump tool (run manually for debugging)
# ------------------------------------------------------------------

add_executable(dump_vol
    ${CMAKE_CURRENT_SOURCE_DIR}/dump_vol.cpp)
target_include_directories(dump_vol PRIVATE ${COMMON_INCLUDES})
target_link_libraries(dump_vol PRIVATE nr_core)

# ------------------------------------------------------------------
# Reference PNG generator for test_overlap (run manually, not a ctest)
# ------------------------------------------------------------------

add_executable(generate_overlap_refs
    ${CMAKE_CURRENT_SOURCE_DIR}/generate_overlap_refs.cpp)
target_include_directories(generate_overlap_refs PRIVATE
    ${COMMON_INCLUDES} ${stb_SOURCE_DIR})
target_link_libraries(generate_overlap_refs PRIVATE nr_core)

# ------------------------------------------------------------------
# Visual regression test for new_register --test mode
# ------------------------------------------------------------------

add_nr_test(test_test_mode
    INCLUDES  ${INC_DIR} ${glm_SOURCE_DIR} ${stb_SOURCE_DIR}
    LINKS     nr_core
)
add_test(NAME TestModeRenderTest COMMAND test_test_mode
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(BUILD_NEW_REGISTER)  # headless UI tests need the imgui target and OSMesa

    # ------------------------------------------------------------------
    # Headless test for coordinate input fields
    # ------------------------------------------------------------------

    add_executable(test_coordinate_input
        ${CMAKE_CURRENT_SOURCE_DIR}/test_coordinate_input.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/backends/imgui_impl_osmesa.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
    target_include_directories(test_coordinate_input PRIVATE
        ${COMMON_INCLUDES} ${stb_SOURCE_DIR} ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/backends)
    target_link_libraries(test_coordinate_input PRIVATE nr_core imgui ${OSMESA_LIBRARY} OpenGL::GL Threads::Threads ${CMAKE_DL_LIBS})
    add_test(NAME CoordinateInputTest COMMAND test_coordinate_input)

    # ------------------------------------------------------------------
    # Headless test for dock column equal-width layout (overlay toggle)
    # ------------------------------------------------------------------

    add_executable(test_column_layout
        ${CMAKE_CURRENT_SOURCE_DIR}/test_column_layout.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/backends/imgui_impl_osmesa.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
    target_include_directories(test_column_layout PRIVATE
        ${COMMON_INCLUDES} ${stb_SOURCE_DIR} ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/backends)
    target_link_libraries(test_column_layout PRIVATE
        nr_core imgui ${OSMESA_LIBRARY} OpenGL::GL Threads::Threads ${CMAKE_DL_LIBS})
    add_test(NAME ColumnLayoutTest COMMAND test_column_layout)

endif() # BUILD_NEW_REGISTER

# ------------------------------------------------------------------
# Reference PNG generator for test_test_mode (run manually, not a ctest)
# ------------------------------------------------------------------

add_executable(generate_test_mode_refs
    ${CMAKE_CURRENT_SOURCE_DIR}/generate_test_mode_refs.cpp)
target_include_directories(generate_test_mode_refs PRIVATE
    ${INC_DIR} ${glm_SOURCE_DIR} ${stb_SOURCE_DIR})
target_link_libraries(generate_test_mode_refs PRIVATE nr_core)

# --- GCC < 10 needs -lstdc++fs for std::filesystem ---
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 10)
    foreach(_tgt test_qc_csv test_app_config test_matrix_debug test_world_to_voxel test_coordinate_sync)
        target_link_libraries(${_tgt} PRIVATE stdc++fs)
    endforeach()
endif()

endif() # NR_NEED_CORE

# ------------------------------------------------------------------
# new_qc CSV handler unit tests
# ------------------------------------------------------------------

if(BUILD_NEW_QC OR BUILD_TERMINAL_VIEWER)
    add_executable(csv_handler_test
        ${CMAKE_CURRENT_SOURCE_DIR}/csv_test.cpp
    )
    target_include_directories(csv_handler_test PRIVATE
        ${PROJECT_SOURCE_DIR}/src
    )
    target_link_libraries(csv_handler_test PRIVATE qc_csv)
    add_test(NAME QCCsvHandlerTest COMMAND csv_handler_test
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
    set_tests_properties(QCCsvHandlerTest PROPERTIES
        LABELS "unit"
        TIMEOUT 30
    )
endif()
