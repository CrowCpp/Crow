cmake_minimum_required(VERSION 3.15)
project (crow_examples)

# Define Required libraries 
list(APPEND REQUIRED_LIBRARIES 
  ${Boost_LIBRARIES}
  ${CMAKE_THREAD_LIBS_INIT}
)

if (MSVC)
  add_executable(example_vs example_vs.cpp)
  target_link_libraries(example_vs )
else ()
  add_executable(helloworld helloworld.cpp)
  target_link_libraries(helloworld PUBLIC ${REQUIRED_LIBRARIES})

  if (OPENSSL_FOUND)
    add_executable(example_ssl ssl/example_ssl.cpp)
    target_link_libraries(example_ssl PUBLIC ${REQUIRED_LIBRARIES} ${OPENSSL_LIBRARIES})
  endif()

  add_executable(example_websocket websocket/example_ws.cpp)
  target_link_libraries(example_websocket )
  target_link_libraries(example_websocket PUBLIC ${REQUIRED_LIBRARIES})
  add_custom_command(OUTPUT ws.html
    COMMAND ${CMAKE_COMMAND} -E
    copy ${PROJECT_SOURCE_DIR}/websocket/templates/ws.html ${CMAKE_CURRENT_BINARY_DIR}/templates/ws.html
    DEPENDS ${PROJECT_SOURCE_DIR}/websocket/templates/ws.html
  )
  add_custom_target(example_ws_copy ALL DEPENDS ws.html)

  add_executable(basic_example example.cpp)
  target_link_libraries(basic_example PUBLIC ${REQUIRED_LIBRARIES})

  if (Tcmalloc_FOUND)
    target_link_libraries(basic_example ${Tcmalloc_LIBRARIES})
  endif(Tcmalloc_FOUND)

  add_executable(example_with_all example_with_all.cpp)
  add_dependencies(example_with_all amalgamation)
  target_link_libraries(example_with_all PUBLIC ${REQUIRED_LIBRARIES})

  add_custom_command(OUTPUT example_test.py
    COMMAND ${CMAKE_COMMAND} -E
    copy ${PROJECT_SOURCE_DIR}/example_test.py ${CMAKE_CURRENT_BINARY_DIR}/example_test.py
    DEPENDS ${PROJECT_SOURCE_DIR}/example_test.py 
  )
  add_custom_target(example_copy ALL DEPENDS example_test.py)

  add_executable(example_chat example_chat.cpp)
  target_link_libraries(example_chat PUBLIC ${REQUIRED_LIBRARIES})

  add_custom_command(OUTPUT example_chat.html
    COMMAND ${CMAKE_COMMAND} -E
    copy ${PROJECT_SOURCE_DIR}/example_chat.html ${CMAKE_CURRENT_BINARY_DIR}/example_chat.html
    DEPENDS ${PROJECT_SOURCE_DIR}/example_chat.html 
  )
  add_custom_target(example_chat_copy ALL DEPENDS example_chat.html)

endif()
