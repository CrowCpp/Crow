# Utilized by OSSFuzz to build the harness(es) for continuous fuzz-testing
# OSSFuzz defines the following environment variables, that this target relies upon:
# CXX, CFLAGS, LIB_FUZZING_ENGINE, OUT
cmake_minimum_required(VERSION 3.14)

function(define_fuzzer executable_name)
    add_executable(${executable_name} ${executable_name}.cpp)
    target_link_libraries(${executable_name} PRIVATE Crow $ENV{LIB_FUZZING_ENGINE})
    target_compile_features(${executable_name} PRIVATE cxx_std_17)

    if (DEFINED ENV{OUT})
        install(TARGETS ${executable_name} DESTINATION $ENV{OUT})
    else ()
        message(WARNING "Cannot install if $OUT is not defined!")
    endif ()
endfunction()

add_definitions(-DNDEBUG)  # Do not want assertions

if (DEFINED ENV{CFLAGS})
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} $ENV{CFLAGS}")
endif ()
if (DEFINED ENV{CXXFLAGS})
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} $ENV{CXXFLAGS}")
endif ()

define_fuzzer(template_fuzzer)
define_fuzzer(request_fuzzer)
define_fuzzer(b64_fuzzer)