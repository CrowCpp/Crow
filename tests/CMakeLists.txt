cmake_minimum_required(VERSION 3.21)
project(crow_test)

set(CMAKE_POLICY_DEFAULT_CMP0077 new)
set(CMAKE_POLICY_WARNING_CMP0126 new)

include(${CMAKE_SOURCE_DIR}/cmake/compiler_options.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/CPM.cmake)


CPMAddPackage(Catch2
  VERSION 3.7.0
  GITHUB_REPOSITORY catchorg/Catch2
  OPTIONS
  "CATCH_INSTALL_DOCS Off"
  "CATCH_INSTALL_EXTRAS Off"
  "CATCH_ENABLE_REPRODUCIBLE_BUILD Off"
)
  
enable_testing()

set(TEST_SRCS
  unittest.cpp
  query_string_tests.cpp
)

add_executable(unittest ${TEST_SRCS})
target_link_libraries(unittest Crow::Crow Catch2::Catch2WithMain)
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
  if (CROW_ENABLE_SSL)
    add_subdirectory(ssl)
  endif()
endif()
add_subdirectory(img)
