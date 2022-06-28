cmake_minimum_required(VERSION 3.16)
project(crow_test)

include(${CMAKE_SOURCE_DIR}/cmake/compiler_options.cmake)

set(TEST_SRCS
  src/app.cpp
  src/black_magic.cpp
  src/blueprints.cpp
  src/json.cpp
  src/middlewares.cpp
  src/query_string.cpp
  src/requests.cpp
  src/responses.cpp
  src/routing.cpp
  src/task_timer.cpp
  src/templates.cpp
  src/utility.cpp
  src/websocket.cpp

  src/catch.cpp
)

if("ssl" IN_LIST CROW_FEATURES)
  list(APPEND TEST_SRCS "src/ssl.cpp")
endif()
if("compression" IN_LIST CROW_FEATURES)
  list(APPEND TEST_SRCS "src/compression.cpp")
endif()

add_executable(unittest ${TEST_SRCS})
target_include_directories(unittest PRIVATE include)
target_link_libraries(unittest Crow::Crow)
target_precompile_headers(unittest PRIVATE include/catch.hpp)
add_warnings_optimizations(unittest)
set_source_files_properties(src/catch.cpp
  PROPERTIES
    SKIP_PRECOMPILE_HEADERS ON
)


if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  set_target_properties(unittest PROPERTIES COMPILE_FLAGS "--coverage -fprofile-arcs -ftest-coverage")
  target_link_libraries(unittest gcov)
endif()
