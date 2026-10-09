execute_process(COMMAND "${PROGRAM}" ${PREFIX} "--headless-crt-probe=${KIND}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 4)
if(NOT result STREQUAL "${EXPECTED}" OR NOT errors MATCHES "Headless CRT")
    message(FATAL_ERROR "Headless failure probe ${KIND} failed: exit=${result}, expected=${EXPECTED}, output=${output}, errors=${errors}")
endif()
message(STATUS "${PROGRAM}: ${KIND} exited ${result} with stderr evidence and no workflow/window.")
