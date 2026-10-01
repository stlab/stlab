execute_process(
  COMMAND ${emulator} "${executable}" "${scenario}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
  TIMEOUT 30)
if(NOT DEFINED expected)
  set(expected "EXPECTED_STLAB_TERMINATE")
endif()
if(result STREQUAL "0" OR NOT "${output}${error}" MATCHES "${expected}")
  message(FATAL_ERROR "Expected terminate handler was not called: ${result}\n${output}${error}")
endif()
