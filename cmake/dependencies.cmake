# Dependencies

if(BUILD_EXAMPLES OR BUILD_TESTING)
  find_program(CCACHE_FOUND ccache)
  if(CCACHE_FOUND)
    message("Found ccache ${CCACHE_FOUND}")
    message("Using ccache to speed up compilation")
    set(ENV{CCACHE_CPP2} "yes")
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK ccache)
  endif(CCACHE_FOUND)

  find_package(Tcmalloc)
  find_package(Threads)

  if (MSVC)
    set(Boost_USE_STATIC_LIBS ON)
    find_package( Boost 1.64.0 COMPONENTS system thread regex REQUIRED )
  else()
    find_package( Boost 1.64.0 COMPONENTS system thread REQUIRED )
  endif()

  if(Boost_FOUND)
    include_directories(${Boost_INCLUDE_DIR})
  endif()
endif()

if(BUILD_EXAMPLES)
  # OpenSSL is needed at runtime dynamically by some examples
  # if it isn't installed, the examples won't be built
  find_package(OpenSSL)
  if(OPENSSL_FOUND)
    include_directories(${OPENSSL_INCLUDE_DIR})
  endif()
endif()
