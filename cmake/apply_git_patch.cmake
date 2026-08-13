if(NOT DEFINED SOURCE_DIR OR NOT DEFINED PATCH_FILE)
    message(FATAL_ERROR "SOURCE_DIR and PATCH_FILE are required")
endif()

find_program(GIT_EXECUTABLE git REQUIRED)

execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply --check --whitespace=nowarn "${PATCH_FILE}"
    WORKING_DIRECTORY "${SOURCE_DIR}"
    RESULT_VARIABLE patch_needed
    ERROR_VARIABLE patch_error)

if(patch_needed EQUAL 0)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply --whitespace=nowarn "${PATCH_FILE}"
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE patch_result
        ERROR_VARIABLE patch_error)
    if(NOT patch_result EQUAL 0)
        message(FATAL_ERROR "Failed to apply ${PATCH_FILE}:\n${patch_error}")
    endif()
    return()
endif()

execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply --reverse --check --whitespace=nowarn "${PATCH_FILE}"
    WORKING_DIRECTORY "${SOURCE_DIR}"
    RESULT_VARIABLE already_applied
    ERROR_VARIABLE reverse_error)

if(NOT already_applied EQUAL 0)
    message(FATAL_ERROR
        "Patch does not apply cleanly and is not already applied: ${PATCH_FILE}\n"
        "Apply check: ${patch_error}\n"
        "Reverse check: ${reverse_error}")
endif()
