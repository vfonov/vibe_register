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
