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
macro(add_mriv_test NAME)
    add_nr_test(${NAME}
        INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../src ${MRIV_STB_DIR}
        LINKS    mriv_lib mriv_test_support
        ${ARGN}
    )
    add_test(NAME mriv_${NAME} COMMAND ${NAME})
endmacro()

add_mriv_test(test_options)
add_mriv_test(test_slice_selection)
add_mriv_test(test_colourmap_arg)
add_mriv_test(test_mriv_volume_info)
# DO NOT pass test data as positional ARGS to add_mriv_test here: add_nr_test()
# ignores ARGS (it does not declare the keyword). Instead set an environment
# variable via CTest's PROPERTIES.
add_mriv_test(test_slice_geometry)
set_property(TEST mriv_test_slice_geometry
    PROPERTY ENVIRONMENT
    "MRIV_THICK_SLICES_FIXTURE=${MRIV_TEST_DATA_DIR}/mni_icbm152_t1_tal_nlin_sym_09c_thick_slices.mnc"
)
add_mriv_test(test_resample)
add_mriv_test(test_terminal LINKS PkgConfig::NOTCURSES)

# Layer A: escape-sequence structure tests.
add_mriv_test(test_structure)

# Layer B: decoded-pixel correctness tests.
add_mriv_test(test_pixels)

# Layer C: CLI integration tests. Use a small real fixture (sq1.mnc is 21 KB
# and therefore cheap enough to load repeatedly).
add_mriv_test(test_cli
    ARGS ${PROJECT_SOURCE_DIR}/tests/sq1.mnc
)
