# Compiler options with hardening flags

if(MSVC)

  list(APPEND compiler_options 
    /W4
    /permissive-
    $<$<CONFIG:RELEASE>:/O2 /Ob2>
    $<$<CONFIG:MINSIZEREL>:/O1 /Ob1>
    $<$<CONFIG:RELWITHDEBINFO>:/Zi /O2 /Ob1>
    $<$<CONFIG:DEBUG>:/Zi /Ob0 /Od /RTC1>)

else(MSVC)

  list(APPEND compiler_options 
    -Wall
    -Wextra
    -Wpedantic
    $<$<CONFIG:RELEASE>:-O2>
    $<$<CONFIG:DEBUG>:-O0 -g -p -pg>)

endif()
