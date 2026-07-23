if(NOT DEFINED PROGRAM OR NOT DEFINED EXPECTED_EXIT_CODE)
  message(FATAL_ERROR "PROGRAM and EXPECTED_EXIT_CODE are required")
endif()

execute_process(
  COMMAND "${PROGRAM}" ${ARGUMENTS}
  RESULT_VARIABLE actual_exit_code
  OUTPUT_QUIET
  ERROR_QUIET
)

if(NOT actual_exit_code EQUAL EXPECTED_EXIT_CODE)
  message(FATAL_ERROR
    "Expected exit code ${EXPECTED_EXIT_CODE}, got ${actual_exit_code}"
  )
endif()
