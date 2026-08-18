if(NOT COMMAND add_nr_test)
    message(STATUS "mriv: add_nr_test() unavailable (ENABLE_TESTS=OFF) — skipping mriv tests")
    return()
endif()

set(MRIV_TEST_DATA_DIR ${PROJECT_SOURCE_DIR}/../test_data)
set(MRIV_STB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/stb)

# Test-only helper library: escape-sequence parser and PNG decode helpers.
add_library(mriv_test_support STATIC
    escapes.cpp
    decode.cpp
)
target_include_directories(mriv_test_support PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${MRIV_STB_DIR}
)
target_link_libraries(mriv_test_support PUBLIC mriv_lib)

# Convenience wrapper so every mriv test gets the production include dir and
# the stb headers used by decode helpers.
#
# ARGS is parsed here and forwarded to add_test() rather than passed through to
# add_nr_test(): add_nr_test() declares the ARGS keyword but never uses it (see
# new_register/tests/CMakeLists.txt), so anything given to it that way is
# silently dropped and the test runs with no arguments. That trap already cost
# us a whole CLI suite -- mriv_test_cli was registered without its fixture path
# and quietly skipped every render test while still reporting "Passed".
# cmake_parse_arguments() clears its output variables on entry, so M_ARGS does
# not leak between calls even though this is a macro.
macro(add_mriv_test NAME)
    cmake_parse_arguments(M "" "" "ARGS" ${ARGN})
    add_nr_test(${NAME}
        INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../src ${MRIV_STB_DIR}
        LINKS    mriv_lib mriv_test_support
        ${M_UNPARSED_ARGUMENTS}
    )
    add_test(NAME mriv_${NAME} COMMAND ${NAME} ${M_ARGS})
endmacro()

add_mriv_test(test_options)
add_mriv_test(test_slice_selection)
add_mriv_test(test_view_list)
add_mriv_test(test_colourmap_arg)
add_mriv_test(test_mriv_volume_info)
# This one passes its fixture via the environment rather than ARGS. Either works
# now that add_mriv_test() forwards ARGS properly; left as-is because the test
# reads MRIV_THICK_SLICES_FIXTURE directly.
add_mriv_test(test_slice_geometry)
set_property(TEST mriv_test_slice_geometry
    PROPERTY ENVIRONMENT
    "MRIV_THICK_SLICES_FIXTURE=${MRIV_TEST_DATA_DIR}/mni_icbm152_t1_tal_nlin_sym_09c_thick_slices.mnc"
)
add_mriv_test(test_view_state)
add_mriv_test(test_range_editor)
add_mriv_test(test_session)
add_mriv_test(test_interactive)
add_mriv_test(test_slice_pipeline)
add_mriv_test(test_frame_builder)
add_mriv_test(test_overlay)
add_mriv_test(test_layout)
add_mriv_test(test_compose)
add_mriv_test(test_resample)
add_mriv_test(test_terminal LINKS PkgConfig::NOTCURSES)

# Layer A: escape-sequence structure tests.
add_mriv_test(test_structure)

# Layer B: decoded-pixel correctness tests.
add_mriv_test(test_pixels)

# Layer C: CLI integration tests. Uses two small real fixtures (sq1.mnc is 21 KB
# and therefore cheap enough to load repeatedly); the second is needed so
# multi-file strip tests can tell the rows apart and assert argument order,
# which passing the same file twice cannot do.
add_mriv_test(test_cli
    ARGS ${PROJECT_SOURCE_DIR}/tests/sq1.mnc ${PROJECT_SOURCE_DIR}/tests/sq2.mnc
)
