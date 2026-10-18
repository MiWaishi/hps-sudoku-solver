cmake_minimum_required(VERSION 3.16)
project(hps_final LANGUAGES C CXX)

# This folder is a standalone project: configure it directly with cmake -S,
# without the parent research framework, baseline experiments, or Python code.
# Runtime solving needs no JSON parser, dataset file, or reference solver.
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(Threads REQUIRED)

# One fixed implementation: no algorithm-selection or diagnostic macros.
# HPS_FINAL_BUILD below controls symbol export only. All fifteen direct detectors,
# candidate/position masks, exact subset caching and delayed validation are fixed.
set(HPS_FINAL_SOURCES
    src/state.cpp
    src/subsets.cpp
    src/patterns.cpp
    src/chains.cpp
    src/detectors.cpp
    src/solver.cpp)
add_library(hps_final SHARED ${HPS_FINAL_SOURCES})
# Consumers see only include/hps.h. Internal C++ State/detector types remain
# private; hidden visibility prevents accidentally exporting their ABI.
target_include_directories(hps_final PUBLIC include PRIVATE src)
target_compile_definitions(hps_final PRIVATE HPS_FINAL_BUILD)
target_compile_options(hps_final PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(hps_final PROPERTIES
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)

add_executable(hps_final_cli src/main.cpp)
# Parallelism lives in the CLI task wrapper; a library call solves just one board.
target_link_libraries(hps_final_cli PRIVATE hps_final Threads::Threads)
target_compile_options(hps_final_cli PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()
# All tests are native C/C++ programs, run with ordinary ctest.
# Compile this caller as C, so C++-only declarations cannot accidentally leak
# into the public header. A successful call and a solved puzzle are tested apart.
add_executable(hps_final_contract tests/api_test.c)
target_link_libraries(hps_final_contract PRIVATE hps_final)
add_test(NAME final_c_api COMMAND hps_final_contract)

# Compile internals into this test, not into the public C ABI.
# This lets tests inspect and rebuild candidate-position indexes independently
# without exposing mutable internal State objects to library consumers.
add_executable(hps_final_state_test tests/state_test.cpp ${HPS_FINAL_SOURCES})
target_include_directories(hps_final_state_test PRIVATE src include)
target_compile_definitions(hps_final_state_test PRIVATE HPS_FINAL_BUILD)
target_compile_options(hps_final_state_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME final_state_and_detectors COMMAND hps_final_state_test)

# CLI integration tests launch the executable directly using POSIX process APIs.
# The failed-output fixture uses a read-only standard-output descriptor.
if(UNIX)
    foreach(test IN ITEMS cli_output cli_input)
        add_executable(hps_final_${test}_test tests/${test}_test.cpp)
        target_compile_options(hps_final_${test}_test PRIVATE -Wall -Wextra -Wpedantic)
        target_link_libraries(hps_final_${test}_test PRIVATE Threads::Threads)
        add_dependencies(hps_final_${test}_test hps_final_cli)
    endforeach()
    add_test(NAME final_cli_output
        COMMAND hps_final_cli_output_test $<TARGET_FILE:hps_final_cli>)
    add_test(NAME final_cli_input
        COMMAND hps_final_cli_input_test $<TARGET_FILE:hps_final_cli>)
    set_tests_properties(final_cli_output final_cli_input PROPERTIES TIMEOUT 120)
endif()
