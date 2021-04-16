cmake_minimum_required(VERSION 3.15)
project (ssl_test)

set(PROJECT_INCLUDE_DIR 
  ${PROJECT_SOURCE_DIR}/include
)

set(TEST_SRCS
  ssltest.cpp
)

add_executable(ssltest ${TEST_SRCS})
target_link_libraries(ssltest ${Boost_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} ${OPENSSL_LIBRARIES} z)
target_compile_options(ssltest PRIVATE "${compiler_options}")

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  set_target_properties(ssltest PROPERTIES COMPILE_FLAGS "--coverage -fprofile-arcs -ftest-coverage")
  target_link_libraries(ssltest gcov)
endif()
