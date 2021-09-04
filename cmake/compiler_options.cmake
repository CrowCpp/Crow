# Compiler options with hardening flags

function(add_warnings_optimizations target_name)
  if(MSVC)
    target_compile_options(${target_name}
      PRIVATE
        /W4
        /permissive-
        $<$<CONFIG:RELEASE>:/O2 /Ob2>
        $<$<CONFIG:MINSIZEREL>:/O1 /Ob1>
        $<$<CONFIG:RELWITHDEBINFO>:/Zi /O2 /Ob1>
        $<$<CONFIG:DEBUG>:/Zi /Ob0 /Od /RTC1>
    )
  else()
    target_compile_options(${target_name}
      PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        $<$<CONFIG:RELEASE>:-O2>
        $<$<CONFIG:DEBUG>:-O0 -g -p -pg>
    )
  endif()
endfunction()
