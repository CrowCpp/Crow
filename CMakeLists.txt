#####################################
# Define Project-Wide Settings
#####################################
cmake_minimum_required(VERSION 3.15.0 FATAL_ERROR)

# Define the project name and language
project(Crow
	LANGUAGES CXX
	VERSION 1.2.1
)

# Make sure Findasio.cmake module is found
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# Check if Crow is the main project
set(CROW_IS_MAIN_PROJECT OFF)
if (CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  set(CROW_IS_MAIN_PROJECT ON)
endif()

# Set required C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

# Default to build type "Release" unless tests are being built
if(NOT CMAKE_BUILD_TYPE)
	if (NOT CROW_BUILD_TESTS)
		message(STATUS "No build type selected, default to Release")
		set(CMAKE_BUILD_TYPE "Release")
	else()
		message(STATUS "No build type selected but tests are being built, default to Debug")
		set(CMAKE_BUILD_TYPE "Debug")
	endif()
endif()
if (MSVC)
  add_compile_options(/bigobj)
endif ()

include(FindPython3)
find_package(Python3)

#####################################
# Define Options
#####################################
option(CROW_BUILD_EXAMPLES     "Build the examples in the project"      ${CROW_IS_MAIN_PROJECT})
option(CROW_BUILD_TESTS        "Build the tests in the project"         ${CROW_IS_MAIN_PROJECT})
option(CROW_BUILD_FUZZER       "Instrument and build Crow fuzzer"       OFF)
option(CROW_AMALGAMATE         "Combine all headers into one"           OFF)
option(CROW_INSTALL            "Add install step for Crow"              ON )
option(CROW_USE_BOOST          "Use Boost.Asio for Crow"                OFF)
option( CROW_RETURNS_OK_ON_HTTP_OPTIONS_REQUEST
		"Returns HTTP status code OK (200) instead of 204 for OPTIONS request"
		OFF )

option(CROW_ENABLE_SSL "Enable Crow's SSL feature for supporting https" OFF)
option(CROW_ENABLE_COMPRESSION "Enable Crow's Compression feature for supporting compressed http content" OFF)


#####################################
# Define Targets
#####################################
add_library(Crow INTERFACE)
add_library(Crow::Crow ALIAS Crow)

target_include_directories(Crow
	INTERFACE
		$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
		$<INSTALL_INTERFACE:include>
)

if(CROW_USE_BOOST)
	if(POLICY CMP0167)
		# Use Boost CMake module from Boost instead of the one from CMake
		cmake_policy(SET CMP0167 NEW)
	endif()
	find_package(Boost 1.64 COMPONENTS system date_time REQUIRED)
	target_link_libraries(Crow
		INTERFACE
			Boost::boost Boost::system Boost::date_time
	)
	target_compile_definitions(Crow INTERFACE CROW_USE_BOOST)
else()
    find_package(asio REQUIRED)
	target_link_libraries(Crow
		INTERFACE
			asio::asio
	)
endif()

target_compile_definitions(Crow INTERFACE "")

if(CROW_ENABLE_COMPRESSION)
	find_package(ZLIB REQUIRED)
	target_link_libraries(Crow INTERFACE ZLIB::ZLIB)
	target_compile_definitions(Crow INTERFACE CROW_ENABLE_COMPRESSION)
endif()

if(CROW_ENABLE_SSL)
	find_package(OpenSSL REQUIRED)
	target_link_libraries(Crow INTERFACE OpenSSL::SSL)
	target_compile_definitions(Crow INTERFACE CROW_ENABLE_SSL)
endif()

if(CROW_AMALGAMATE)
	set(CROW_AMALGAMATED_HEADERS
		include/crow.h
		include/crow/app.h
		include/crow/ci_map.h
		include/crow/common.h
		include/crow/compression.h
		include/crow/exceptions.h
		include/crow/http_connection.h
		include/crow/http_parser_merged.h
		include/crow/http_request.h
		include/crow/http_response.h
		include/crow/http_server.h
		include/crow/json.h
		include/crow/logging.h
		include/crow/middleware.h
		include/crow/middleware_context.h
		include/crow/mime_types.h
		include/crow/multipart.h
		include/crow/multipart_view.h
		include/crow/mustache.h
		include/crow/parser.h
		include/crow/query_string.h
		include/crow/returnable.h
		include/crow/routing.h
		include/crow/settings.h
		include/crow/socket_adaptors.h
		include/crow/task_timer.h
		include/crow/utility.h
		include/crow/version.h
		include/crow/websocket.h
		include/crow/middlewares/cookie_parser.h
		include/crow/middlewares/cors.h
		include/crow/middlewares/session.h
		include/crow/middlewares/utf-8.h
	)

	add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/crow_all.h
		COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/merge_all.py
		${CMAKE_CURRENT_SOURCE_DIR}/include
		${CMAKE_CURRENT_BINARY_DIR}/crow_all.h
		WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
		DEPENDS ${CROW_AMALGAMATED_HEADERS}
	)

	add_custom_target(crow_amalgamated ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/crow_all.h)
endif()

# Examples
if(CROW_BUILD_EXAMPLES)
	add_subdirectory(examples)
endif()

# Tests
if(CROW_BUILD_TESTS)

  add_subdirectory(tests)
  enable_testing()
  add_test(
    NAME crow_test
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/tests/unittest
  )

  if(NOT CROW_ENABLE_COMPRESSION)
    message(STATUS "Compression tests are omitted. (Configure with CROW_ENABLE_COMPRESSION to enable them)")
  endif()
  if(NOT CROW_ENABLE_SSL)
    message(STATUS "SSL tests are omitted. (Configure with CROW_ENABLE_SSL to enable them)")
  else()
    if(NOT MSVC)
      add_test(
	NAME ssl_test
	COMMAND ${CMAKE_CURRENT_BINARY_DIR}/tests/ssl/ssltest
      )
    endif()
  endif()
endif()

# Fuzzers
if (CROW_BUILD_FUZZER)
	add_subdirectory(tests/fuzz)
endif()

#####################################
# Install Files
#####################################
if(CROW_INSTALL)
	include(GNUInstallDirs)
	install(TARGETS Crow EXPORT CrowTargets)
	install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
		DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
	)

	install(EXPORT CrowTargets
		FILE CrowTargets.cmake
		NAMESPACE Crow::
		DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Crow"
	)

	include(CMakePackageConfigHelpers)
	configure_package_config_file(
		"${CMAKE_CURRENT_SOURCE_DIR}/cmake/CrowConfig.cmake.in"
		"${CMAKE_CURRENT_BINARY_DIR}/CrowConfig.cmake"
		INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Crow"
	)
	install(FILES
		"${CMAKE_CURRENT_SOURCE_DIR}/cmake/Findasio.cmake"
		"${CMAKE_CURRENT_BINARY_DIR}/CrowConfig.cmake"
		DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Crow"
	)
endif()

if(WIN32 AND NOT CYGWIN)
	set(CPACK_GENERATOR NSIS ZIP)
endif(WIN32 AND NOT CYGWIN)
if(APPLE)
	set(CPACK_GENERATOR DragNDrop TGZ)
endif(APPLE)
if (UNIX AND NOT APPLE AND NOT WIN32)
	set(CPACK_GENERATOR DEB TGZ)
endif (UNIX AND NOT APPLE AND NOT WIN32)

set(CPACK_PACKAGE_NAME "Crow")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "CrowCpp")
set(CPACK_PACKAGE_VENDOR "CrowCpp")
set(CPACK_PACKAGE_DESCRIPTION "A Fast and Easy to use C++ microframework for the web.")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://crowcpp.org")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "all")
set(CPACK_DEBIAN_PACKAGE_DEBUG OFF)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libasio-dev")
set(CPACK_DEBIAN_PACKAGE_SECTION "libdevel")

include(CPack)

#####################################
# Uninstall Files
#####################################
if(NOT TARGET uninstall)
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
    IMMEDIATE @ONLY)

  add_custom_target(uninstall
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)
endif()
