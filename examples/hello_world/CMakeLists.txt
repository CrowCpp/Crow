cmake_minimum_required(VERSION 3.15)
project(HelloWorld CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Paths relative to this file: ../../include and ../../third_party
get_filename_component(CROW_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(ASIO_VENDOR "${CROW_ROOT}/third_party")

add_executable(hello_world main.cpp)

target_include_directories(hello_world PRIVATE
    "${CROW_ROOT}/include"
    "${ASIO_VENDOR}"
)

target_compile_definitions(hello_world PRIVATE ASIO_STANDALONE)

find_package(Threads REQUIRED)
target_link_libraries(hello_world PRIVATE Threads::Threads)
