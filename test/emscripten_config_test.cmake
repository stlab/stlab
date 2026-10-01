# Each incompatible configuration must fail with the intended diagnostic, not a toolchain error.
foreach(scenario IN ITEMS task main threads pool compiler)
  if(scenario STREQUAL "task")
    set(option -DSTLAB_TASK_SYSTEM=portable)
    set(diagnostic "Threadless Emscripten requires")
  elseif(scenario STREQUAL "main")
    set(option -DSTLAB_MAIN_EXECUTOR=none)
    set(diagnostic "Threadless Emscripten requires")
  elseif(scenario STREQUAL "threads")
    set(option -DSTLAB_THREAD_SYSTEM=pthread-emscripten)
    set(diagnostic "Threadless Emscripten requires")
  elseif(scenario STREQUAL "pool")
    set(option -DSTLAB_TASK_POOL_MAXIMUM=1)
    set(diagnostic "Threadless Emscripten does not support")
  else()
    set(option -DCMAKE_CXX_FLAGS=-pthread)
    set(diagnostic "requires a toolchain without -pthread")
  endif()
  execute_process(
    COMMAND "${CMAKE_COMMAND}" --preset=debug-emscripten-threadless
      -B "${binary_dir}/${scenario}" "-DEM_CONFIG_EXECUTABLE=${em_config}"
      -DBUILD_TESTING=OFF ${option}
    WORKING_DIRECTORY "${source_dir}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error
    TIMEOUT 90)
  if(result STREQUAL "0" OR NOT "${output}${error}" MATCHES "${diagnostic}")
    message(FATAL_ERROR "${scenario}: wrong configuration result: ${result}\n${output}${error}")
  endif()
endforeach()
