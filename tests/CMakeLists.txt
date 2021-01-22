cmake_minimum_required(VERSION 3.15)
project(crow_test)

set(TEST_SRCS
  unittest.cpp
)

add_executable(unittest ${TEST_SRCS})
target_link_libraries(unittest ${Boost_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} z)
# set target compile options as defined in the cmake/compiler_options.cmake Module
target_compile_options(unittest PRIVATE ${compiler_options})

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  set_target_properties(unittest PROPERTIES COMPILE_FLAGS "--coverage -fprofile-arcs -ftest-coverage")
  target_link_libraries(unittest gcov)
endif()

add_subdirectory(template)
add_subdirectory(img)
