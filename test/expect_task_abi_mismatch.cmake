if(NOT DEFINED build_dir)
  message(FATAL_ERROR "build_dir is required")
endif()

function(stlab_output_has_expected_guard_diagnostic outvar output_text)
  set(found FALSE)
  set(in_apple_undefined_block FALSE)
  string(REPLACE "\r\n" "\n" normalized "${output_text}")
  string(REPLACE "\r" "\n" normalized "${normalized}")
  string(REPLACE "\n" ";" output_lines "${normalized}")

  foreach(line IN LISTS output_lines)
    if(line MATCHES "Undefined symbols for architecture")
      set(in_apple_undefined_block TRUE)
    endif()

    if(line MATCHES "task_storage_abi_guard")
      if(line MATCHES "LNK20(01|19): unresolved external symbol"
         OR line MATCHES "undefined reference to"
         OR line MATCHES "undefined symbol:")
        set(found TRUE)
        break()
      endif()

      if(in_apple_undefined_block)
        set(found TRUE)
        break()
      endif()
    endif()

    if(line STREQUAL ""
       OR line MATCHES "ld: symbol\\(s\\) not found"
       OR line MATCHES "collect2: error:"
       OR line MATCHES "clang\\+\\+: error:"
       OR line MATCHES "em\\+\\+: error:")
      set(in_apple_undefined_block FALSE)
    endif()
  endforeach()

  set(${outvar} ${found} PARENT_SCOPE)
endfunction()

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

stlab_output_has_expected_guard_diagnostic(has_expected_guard_diagnostic "${output}")

if(NOT has_expected_guard_diagnostic)
  message(FATAL_ERROR
    "Mismatch target failed for an unexpected reason; unresolved/undefined guard diagnostic was absent:\n${output}")
endif()

message("${output}")
