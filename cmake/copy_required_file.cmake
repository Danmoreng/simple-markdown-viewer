if(NOT DEFINED SOURCE_FILE OR NOT EXISTS "${SOURCE_FILE}")
    message(FATAL_ERROR "Required runtime file is missing: ${SOURCE_FILE}")
endif()

if(NOT DEFINED DESTINATION_FILE)
    message(FATAL_ERROR "DESTINATION_FILE was not provided")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${SOURCE_FILE}" "${DESTINATION_FILE}"
    COMMAND_ERROR_IS_FATAL ANY)
