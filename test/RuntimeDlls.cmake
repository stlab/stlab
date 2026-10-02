# Deploys runtime DLLs beside a test executable; static targets need no copy.
function(stlab_copy_runtime_dlls target)
  if(WIN32)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND "${CMAKE_COMMAND}"
        "-Druntime_dlls=$<TARGET_RUNTIME_DLLS:${target}>"
        "-Ddestination=$<TARGET_FILE_DIR:${target}>"
        -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/copy_runtime_dlls.cmake"
      VERBATIM)
  endif()
endfunction()
