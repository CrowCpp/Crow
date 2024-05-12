cmake_minimum_required(VERSION 3.15)
project(crow_test)

include(${CMAKE_SOURCE_DIR}/cmake/compiler_options.cmake)

enable_testing()

set(TEST_SRCS
  unittest.cpp
  query_string_tests.cpp
)

add_executable(unittest ${TEST_SRCS})
target_link_libraries(unittest Crow::Crow)
add_warnings_optimizations(unittest)

if("${CMAKE_SYSTEM_NAME}" STREQUAL "Android")
  target_link_libraries(unittest log)
endif()

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  set_target_properties(unittest PROPERTIES COMPILE_FLAGS "--coverage -fprofile-arcs -ftest-coverage")
  target_link_libraries(unittest gcov)
endif()

add_subdirectory(template)
add_subdirectory(multi_file)
add_subdirectory(external_definition)
if(NOT MSVC)
  if ("ssl" IN_LIST CROW_FEATURES)
    add_subdirectory(ssl)
  endif()
endif()
add_subdirectory(img)
