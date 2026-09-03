# Verifies that every smoke scenario CMake registers as a test still exists in
# the binary's registry.
#
# The two lists are written in different languages and nothing but care keeps
# them together: renaming a scenario in game/src/smoke_scenarios.cpp leaves the
# CMake registration pointing at a name that no longer exists. That failure does
# surface on its own, but only when the test runs, and the smoke tests need a
# GPU, so a headless machine would report a green suite either way. This check
# needs no GPU, because --list-scenarios returns before a window is opened.
#
# The check is deliberately one-directional. A scenario the binary offers but
# CMake does not register is not a defect: most of them are capture aids meant
# to be run by hand.
#
# Invoked as:
#   cmake -DBINARY=<exe> -DEXPECTED=<file> -P check-registered-scenarios.cmake

if(NOT DEFINED BINARY OR NOT DEFINED EXPECTED)
    message(FATAL_ERROR "BINARY and EXPECTED must both be defined.")
endif()

execute_process(
    COMMAND "${BINARY}" --list-scenarios
    OUTPUT_VARIABLE available_output
    ERROR_VARIABLE listing_error
    RESULT_VARIABLE listing_result
)
if(NOT listing_result EQUAL 0)
    message(FATAL_ERROR
        "'${BINARY} --list-scenarios' failed with ${listing_result}: ${listing_error}")
endif()

string(REPLACE "\r" "" available_output "${available_output}")
string(REPLACE "\n" ";" available "${available_output}")
list(REMOVE_ITEM available "")

file(READ "${EXPECTED}" expected_contents)
string(REPLACE "\r" "" expected_contents "${expected_contents}")
string(REPLACE "\n" ";" expected "${expected_contents}")
list(REMOVE_ITEM expected "")

if(available STREQUAL "")
    message(FATAL_ERROR "The binary reported no smoke scenarios at all.")
endif()

set(missing "")
foreach(name IN LISTS expected)
    if(NOT name IN_LIST available)
        list(APPEND missing "${name}")
    endif()
endforeach()

if(NOT missing STREQUAL "")
    string(REPLACE ";" ", " missing_text "${missing}")
    string(REPLACE ";" ", " available_text "${available}")
    message(FATAL_ERROR
        "CMake registers smoke scenario(s) the binary does not offer: ${missing_text}.\n"
        "The registry holds: ${available_text}.\n"
        "Either the scenario was renamed in game/src/smoke_scenarios.cpp without "
        "updating its ic2de_add_smoke_test call, or it was removed.")
endif()

list(LENGTH expected expected_count)
message(STATUS "All ${expected_count} registered smoke scenario(s) exist in the binary.")
