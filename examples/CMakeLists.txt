cmake_minimum_required(VERSION 3.15)
project (crow_examples)

if (MSVC)
  add_executable(example_vs example_vs.cpp)
  target_link_libraries(example_vs ${Boost_LIBRARIES})
  target_link_libraries(example_vs ${CMAKE_THREAD_LIBS_INIT})
  target_link_libraries(example_vs ${ZLIB_LIBRARIES})
else ()
  add_executable(helloworld helloworld.cpp)
  target_link_libraries(helloworld ${Boost_LIBRARIES})
  target_link_libraries(helloworld ${CMAKE_THREAD_LIBS_INIT})
  target_link_libraries(helloworld ${ZLIB_LIBRARIES})

add_executable(example_static_file example_static_file.cpp)
target_link_libraries(example_static_file ${Boost_LIBRARIES})
target_link_libraries(example_static_file ${CMAKE_THREAD_LIBS_INIT})
target_link_libraries(example_static_file ${ZLIB_LIBRARIES})

add_executable(example_compression example_compression.cpp)
target_link_libraries(example_compression ${Boost_LIBRARIES})
target_link_libraries(example_compression ${CMAKE_THREAD_LIBS_INIT})
target_link_libraries(example_compression ${ZLIB_LIBRARIES})

if (OPENSSL_FOUND)
  add_executable(example_ssl ssl/example_ssl.cpp)
  target_link_libraries(example_ssl ${Boost_LIBRARIES})
  target_link_libraries(example_ssl ${CMAKE_THREAD_LIBS_INIT} ${OPENSSL_LIBRARIES})
  target_link_libraries(example_ssl ${ZLIB_LIBRARIES})
endif()

add_executable(example_websocket websocket/example_ws.cpp)
target_link_libraries(example_websocket ${Boost_LIBRARIES})
target_link_libraries(example_websocket ${CMAKE_THREAD_LIBS_INIT} ${OPENSSL_LIBRARIES})
target_link_libraries(example_websocket ${ZLIB_LIBRARIES})
add_custom_command(OUTPUT ws.html
  COMMAND ${CMAKE_COMMAND} -E
  copy ${PROJECT_SOURCE_DIR}/websocket/templates/ws.html ${CMAKE_CURRENT_BINARY_DIR}/templates/ws.html
  DEPENDS ${PROJECT_SOURCE_DIR}/websocket/templates/ws.html
)
add_custom_target(example_ws_copy ALL DEPENDS ws.html)

add_executable(basic_example example.cpp)
target_link_libraries(basic_example ${Boost_LIBRARIES})
target_link_libraries(basic_example ${CMAKE_THREAD_LIBS_INIT})
target_link_libraries(basic_example ${ZLIB_LIBRARIES})

if (Tcmalloc_FOUND)
  target_link_libraries(basic_example ${Tcmalloc_LIBRARIES})
endif(Tcmalloc_FOUND)

add_executable(example_with_all example_with_all.cpp)
add_dependencies(example_with_all amalgamation)
target_link_libraries(example_with_all ${Boost_LIBRARIES})
target_link_libraries(example_with_all ${CMAKE_THREAD_LIBS_INIT})
target_link_libraries(example_with_all ${ZLIB_LIBRARIES})

add_custom_command(OUTPUT example_test.py
  COMMAND ${CMAKE_COMMAND} -E
  copy ${PROJECT_SOURCE_DIR}/example_test.py ${CMAKE_CURRENT_BINARY_DIR}/example_test.py
  DEPENDS ${PROJECT_SOURCE_DIR}/example_test.py 
)
add_custom_target(example_copy ALL DEPENDS example_test.py)

add_executable(example_chat example_chat.cpp)
target_link_libraries(example_chat ${Boost_LIBRARIES})
target_link_libraries(example_chat ${CMAKE_THREAD_LIBS_INIT})
target_link_libraries(example_chat ${ZLIB_LIBRARIES})
add_custom_command(OUTPUT example_chat.html
  COMMAND ${CMAKE_COMMAND} -E
  copy ${PROJECT_SOURCE_DIR}/example_chat.html ${CMAKE_CURRENT_BINARY_DIR}/example_chat.html
  DEPENDS ${PROJECT_SOURCE_DIR}/example_chat.html 
)
add_custom_target(example_chat_copy ALL DEPENDS example_chat.html)

endif()
