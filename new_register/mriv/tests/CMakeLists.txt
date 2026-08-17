if(NOT COMMAND add_nr_test)
    message(STATUS "mriv: add_nr_test() unavailable (ENABLE_TESTS=OFF) — skipping mriv tests")
    return()
endif()

set(MRIV_TEST_DATA_DIR ${PROJECT_SOURCE_DIR}/../test_data)

add_nr_test(test_options
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../src
    LINKS    mriv_lib
)
add_test(NAME mriv_options COMMAND test_options)

add_nr_test(test_slice_geometry
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../src
    LINKS    mriv_lib
)
add_test(NAME mriv_slice_geometry
    COMMAND test_slice_geometry ${MRIV_TEST_DATA_DIR}/mni_icbm152_t1_tal_nlin_sym_09c_thick_slices.mnc
)

add_nr_test(test_resample
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../src
    LINKS    mriv_lib
)
add_test(NAME mriv_resample COMMAND test_resample)

add_nr_test(test_terminal
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../src
    LINKS    mriv_lib PkgConfig::NOTCURSES
)
add_test(NAME mriv_terminal COMMAND test_terminal)
