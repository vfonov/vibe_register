# CMake generated Testfile for 
# Source directory: /app/new_qc/tests
# Build directory: /app/new_qc/tests/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(csv_handler_test "/app/new_qc/tests/build/csv_test")
set_tests_properties(csv_handler_test PROPERTIES  LABELS "unit" TIMEOUT "30" WORKING_DIRECTORY "/app/new_qc/tests" _BACKTRACE_TRIPLES "/app/new_qc/tests/CMakeLists.txt;19;add_test;/app/new_qc/tests/CMakeLists.txt;0;")
