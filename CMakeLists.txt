cmake_minimum_required(VERSION 3.15)
project (crow_all LANGUAGES CXX)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/")

# Set required C++ Standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
# Generate the compile_commands.json file
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

option(BUILD_EXAMPLES "Builds the examples in the project" OFF)
option(BUILD_TESTING  "Builds the tests in the project"    ON)

find_package(Tcmalloc)
find_package(Threads)

find_program(CCACHE_FOUND ccache)
if(CCACHE_FOUND)
  message("Found ccache ${CCACHE_FOUND}")
  message("Using ccache to speed up compilation")
  set(ENV{CCACHE_CPP2} "yes")
  set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
  set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK ccache)
endif(CCACHE_FOUND)

if (NOT CMAKE_BUILD_TYPE)
	message(STATUS "No build type selected, default to Release")
	set(CMAKE_BUILD_TYPE "Release")
endif()

if (MSVC)
	set(Boost_USE_STATIC_LIBS "On")
	find_package( Boost 1.52 COMPONENTS system thread regex REQUIRED )
else()
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -pedantic -Wextra")
	find_package( Boost 1.52 COMPONENTS system thread REQUIRED )
endif()

include_directories(${Boost_INCLUDE_DIR})

set(PROJECT_INCLUDE_DIR
  ${PROJECT_SOURCE_DIR}/include
)

include_directories("${PROJECT_INCLUDE_DIR}")
include_directories("${PROJECT_SOURCE_DIR}")
include_directories("${CMAKE_CURRENT_BINARY_DIR}")  # To include crow_all.h

if(BUILD_EXAMPLES)
  # OpenSSL is required at runtime dynamically
  find_package(OpenSSL)
  if(OPENSSL_FOUND)
    include_directories(${OPENSSL_INCLUDE_DIR})
  endif()
 
  add_subdirectory(examples)
endif()

if (NOT MSVC AND BUILD_TESTING)
	add_subdirectory(tests)

	enable_testing()
	add_test(NAME crow_test COMMAND ${CMAKE_CURRENT_BINARY_DIR}/tests/unittest)
	add_test(NAME template_test COMMAND ${CMAKE_CURRENT_BINARY_DIR}/tests/template/test.py WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tests/template)
endif()

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/crow_all.h
	COMMAND python ${PROJECT_SOURCE_DIR}/scripts/merge_all.py
	${PROJECT_SOURCE_DIR}/include
	${CMAKE_CURRENT_BINARY_DIR}/crow_all.h
	WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
	DEPENDS ${PROJECT_SOURCE_DIR}/include/*.h ${PROJECT_SOURCE_DIR}/include/crow/*.h ${PROJECT_SOURCE_DIR}/include/crow/middlewares/*.h
)

add_custom_target(amalgamation ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/crow_all.h)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/crow_all.h DESTINATION include)
