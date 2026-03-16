cmake_minimum_required(VERSION 3.14)
project(new_qc_tests VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Test executable
add_executable(csv_test csv_test.cpp ../src/CSVHandler.cpp)

# Include directories
target_include_directories(csv_test PRIVATE
    ${CMAKE_SOURCE_DIR}/../src
)

# Add test
add_test(NAME csv_handler_test COMMAND csv_test WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

# Set test properties
set_tests_properties(csv_handler_test PROPERTIES
    LABELS "unit"
    TIMEOUT 30
)
