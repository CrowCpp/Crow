cmake_minimum_required(VERSION 3.15)
project (crow_ssl_test)

include(${CMAKE_SOURCE_DIR}/cmake/compiler_options.cmake)

set(TEST_SRCS
  ssltest.cpp
)

add_executable(ssltest ${TEST_SRCS})
target_link_libraries(ssltest Crow::Crow)
add_warnings_optimizations(ssltest)

if("${CMAKE_SYSTEM_NAME}" STREQUAL "Android")
  target_link_libraries(ssltest log)
endif()

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  set_target_properties(ssltest PROPERTIES COMPILE_FLAGS "--coverage -fprofile-arcs -ftest-coverage")
  target_link_libraries(ssltest gcov)
endif()

