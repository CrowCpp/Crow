#Findasio.cmake
#
# Finds the asio library
#
# from https://think-async.com/Asio/
#
# This will define the following variables
#
#    ASIO_FOUND
#    ASIO_INCLUDE_DIR
#
# and the following imported targets
#
#     asio::asio
#

find_package(Threads QUIET)
if (Threads_FOUND)
  find_path(ASIO_INCLUDE_DIR asio.hpp)

  mark_as_advanced(ASIO_FOUND ASIO_INCLUDE_DIR)

  include(FindPackageHandleStandardArgs)
  find_package_handle_standard_args(asio
    FOUND_VAR ASIO_FOUND
    REQUIRED_VARS ASIO_INCLUDE_DIR
  )

  if(ASIO_FOUND AND NOT TARGET asio::asio)
      add_library(asio::asio INTERFACE IMPORTED)
      target_include_directories(asio::asio
        INTERFACE
          ${ASIO_INCLUDE_DIR}
      )
      target_compile_definitions(asio::asio
        INTERFACE
          "ASIO_STANDALONE"
      )
      target_link_libraries(asio::asio
        INTERFACE
          Threads::Threads
      )
  endif()
else()
  if(asio_FIND_REQUIRED)
    message(FATAL_ERROR "asio requires Threads, which couldn't be found.")
  elseif(asio_FIND_QUIETLY)
    message(STATUS "asio requires Threads, which couldn't be found.")
  endif()
endif()
