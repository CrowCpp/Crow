cmake_minimum_required(VERSION 3.15)
project(template_test)

set(PROJECT_INCLUDE_DIR
  ${PROJECT_SOURCE_DIR}/include
)

set(TEST_SRCS
  mustachetest.cpp
)

add_executable(mustachetest ${TEST_SRCS})
target_link_libraries(mustachetest Crow::Crow)
add_warnings_optimizations(mustachetest)

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  set_target_properties(mustachetest PROPERTIES COMPILE_FLAGS "--coverage -fprofile-arcs -ftest-coverage")
  target_link_libraries(mustachetest gcov)
endif()

if(NOT MSVC)
  # there is a bug in the python script, it does not find the path
  file(COPY DIRECTORY . DESTINATION ${CMAKE_CURRENT_BINARY_DIR} FILES_MATCHING PATTERN "*.json")

  add_custom_command(OUTPUT test.py
    COMMAND ${CMAKE_COMMAND} -E
    copy ${PROJECT_SOURCE_DIR}/test.py ${CMAKE_CURRENT_BINARY_DIR}/test.py
    DEPENDS ${PROJECT_SOURCE_DIR}/test.py
  )

  add_custom_target(template_test_copy ALL DEPENDS test.py)

  add_test(
    NAME template_test
    COMMAND python3 ${CMAKE_CURRENT_BINARY_DIR}/test.py
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  )
endif()
