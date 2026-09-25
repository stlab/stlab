if(NOT DEFINED build_dir)
  message(FATAL_ERROR "build_dir is required")
endif()

set(command "${CMAKE_COMMAND}" --build "${build_dir}" --target stlab.test.task_abi_mismatch)
if(DEFINED config AND NOT config STREQUAL "")
  list(APPEND command --config "${config}")
endif()

execute_process(
  COMMAND ${command}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr)

string(CONCAT output "${stdout}" "\n" "${stderr}")

if(result EQUAL 0)
  message(FATAL_ERROR "Mismatched task storage ABI linked successfully:\n${output}")
endif()

if(NOT output MATCHES "task_storage_abi_guard")
  message(FATAL_ERROR
    "Mismatch target failed for an unexpected reason; guard symbol was absent:\n${output}")
endif()

message("${output}")
