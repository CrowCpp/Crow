cmake_minimum_required(VERSION 3.15)
project(crow_examples)

include(${CMAKE_SOURCE_DIR}/cmake/compiler_options.cmake)

add_executable(helloworld helloworld.cpp)
add_warnings_optimizations(helloworld)
target_link_libraries(helloworld PUBLIC Crow::Crow)

# If compression is enabled, the example will be built
if("compression" IN_LIST CROW_FEATURES)
  add_executable(example_compression example_compression.cpp)
  add_warnings_optimizations(example_compression)
  target_link_libraries(example_compression Crow::Crow)
else()
  message(STATUS "example_compression example deactivated")
endif()

# If SSL is enabled, the example will be built
if("ssl" IN_LIST CROW_FEATURES)
  add_executable(example_ssl ssl/example_ssl.cpp)
  add_warnings_optimizations(example_ssl)
  target_link_libraries(example_ssl PUBLIC Crow::Crow)
else()
  message(STATUS "example_ssl example deactivated")
endif()

add_executable(example_websocket websocket/example_ws.cpp)
add_warnings_optimizations(example_websocket)
target_link_libraries(example_websocket PUBLIC Crow::Crow)
add_custom_command(OUTPUT ws.html
  COMMAND ${CMAKE_COMMAND} -E
  copy ${PROJECT_SOURCE_DIR}/websocket/templates/ws.html ${CMAKE_CURRENT_BINARY_DIR}/templates/ws.html
  DEPENDS ${PROJECT_SOURCE_DIR}/websocket/templates/ws.html
)
add_custom_target(example_ws_copy ALL DEPENDS ws.html)

add_executable(basic_example example.cpp)
add_warnings_optimizations(basic_example)
target_link_libraries(basic_example PUBLIC Crow::Crow)

if(CROW_AMALGAMATE)
  add_executable(example_with_all example_with_all.cpp)
  add_dependencies(example_with_all crow_amalgamated)
  add_warnings_optimizations(example_with_all)
  target_link_libraries(example_with_all PUBLIC Crow::Crow)
  target_include_directories(example_with_all PUBLIC ${CMAKE_BINARY_DIR})
endif()

add_executable(example_chat example_chat.cpp)
add_warnings_optimizations(example_chat)
target_link_libraries(example_chat PUBLIC Crow::Crow)
add_custom_command(OUTPUT example_chat.html
  COMMAND ${CMAKE_COMMAND} -E
  copy ${PROJECT_SOURCE_DIR}/example_chat.html ${CMAKE_CURRENT_BINARY_DIR}/templates/example_chat.html
  DEPENDS ${PROJECT_SOURCE_DIR}/example_chat.html
)
add_custom_target(example_chat_copy ALL DEPENDS example_chat.html)

add_executable(example_static_file example_static_file.cpp)
add_warnings_optimizations(example_static_file)
target_link_libraries(example_static_file PUBLIC Crow::Crow)

add_executable(example_catchall example_catchall.cpp)
add_warnings_optimizations(example_catchall)
target_link_libraries(example_catchall PUBLIC Crow::Crow)

add_executable(example_json_map example_json_map.cpp)
add_warnings_optimizations(example_json_map)
target_link_libraries(example_json_map PUBLIC Crow::Crow)

add_executable(example_blueprint example_blueprint.cpp)
add_warnings_optimizations(example_blueprint)
target_link_libraries(example_blueprint PUBLIC Crow::Crow)

add_executable(example_middleware example_middleware.cpp)
add_warnings_optimizations(example_middleware)
target_link_libraries(example_middleware PUBLIC Crow::Crow)

add_executable(example_cors middlewares/example_cors.cpp)
add_warnings_optimizations(example_cors)
target_link_libraries(example_cors PUBLIC Crow::Crow)

add_executable(example_cookies middlewares/example_cookies.cpp)
add_warnings_optimizations(example_cookies)
target_link_libraries(example_cookies PUBLIC Crow::Crow)

add_executable(example_session middlewares/example_session.cpp)
add_warnings_optimizations(example_session)
target_link_libraries(example_session PUBLIC Crow::Crow)

add_executable(example_file_upload example_file_upload.cpp)
add_warnings_optimizations(example_file_upload)
target_link_libraries(example_file_upload PUBLIC Crow::Crow)

if(MSVC)
  add_executable(example_vs example_vs.cpp)
  add_warnings_optimizations(example_vs)
  target_link_libraries(example_vs Crow::Crow)
endif()
