cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED MECRAFT_SOURCE_DIR OR NOT IS_DIRECTORY "${MECRAFT_SOURCE_DIR}")
    message(FATAL_ERROR "MECRAFT_SOURCE_DIR must identify the Mecraft source directory")
endif()

if(NOT MECRAFT_CLANG_FORMAT_MODE STREQUAL "FORMAT" AND
   NOT MECRAFT_CLANG_FORMAT_MODE STREQUAL "CHECK")
    message(FATAL_ERROR "MECRAFT_CLANG_FORMAT_MODE must be FORMAT or CHECK")
endif()

find_program(MECRAFT_CLANG_FORMAT_EXECUTABLE NAMES clang-format REQUIRED)
execute_process(
        COMMAND "${MECRAFT_CLANG_FORMAT_EXECUTABLE}" --version
        RESULT_VARIABLE MECRAFT_CLANG_FORMAT_VERSION_RESULT
        OUTPUT_VARIABLE MECRAFT_CLANG_FORMAT_VERSION_OUTPUT
        ERROR_VARIABLE MECRAFT_CLANG_FORMAT_VERSION_ERROR
        OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT MECRAFT_CLANG_FORMAT_VERSION_RESULT EQUAL 0)
    message(FATAL_ERROR
            "Unable to query clang-format: ${MECRAFT_CLANG_FORMAT_VERSION_ERROR}")
endif()
if(NOT MECRAFT_CLANG_FORMAT_VERSION_OUTPUT MATCHES "version 22\\.")
    message(FATAL_ERROR
            "Mecraft requires clang-format 22, found: ${MECRAFT_CLANG_FORMAT_VERSION_OUTPUT}")
endif()

# The project owns these source roots. External SDK and generated build files are intentionally excluded.
file(GLOB_RECURSE MECRAFT_CLANG_FORMAT_FILES
        LIST_DIRECTORIES false
        "${MECRAFT_SOURCE_DIR}/src/*.c"
        "${MECRAFT_SOURCE_DIR}/src/*.cc"
        "${MECRAFT_SOURCE_DIR}/src/*.cpp"
        "${MECRAFT_SOURCE_DIR}/src/*.cxx"
        "${MECRAFT_SOURCE_DIR}/src/*.h"
        "${MECRAFT_SOURCE_DIR}/src/*.hh"
        "${MECRAFT_SOURCE_DIR}/src/*.hpp"
        "${MECRAFT_SOURCE_DIR}/src/*.hxx"
        "${MECRAFT_SOURCE_DIR}/tests/*.c"
        "${MECRAFT_SOURCE_DIR}/tests/*.cc"
        "${MECRAFT_SOURCE_DIR}/tests/*.cpp"
        "${MECRAFT_SOURCE_DIR}/tests/*.cxx"
        "${MECRAFT_SOURCE_DIR}/tests/*.h"
        "${MECRAFT_SOURCE_DIR}/tests/*.hh"
        "${MECRAFT_SOURCE_DIR}/tests/*.hpp"
        "${MECRAFT_SOURCE_DIR}/tests/*.hxx"
)
list(APPEND MECRAFT_CLANG_FORMAT_FILES
        "${MECRAFT_SOURCE_DIR}/main.cpp"
        "${MECRAFT_SOURCE_DIR}/dedicated_server.cpp"
)
list(SORT MECRAFT_CLANG_FORMAT_FILES)

set(MECRAFT_CLANG_FORMAT_STYLE "--style=file:${MECRAFT_SOURCE_DIR}/.clang-format")
set(MECRAFT_CLANG_FORMAT_CHECK_FAILED FALSE)

foreach(MECRAFT_SOURCE_FILE IN LISTS MECRAFT_CLANG_FORMAT_FILES)
    if(MECRAFT_CLANG_FORMAT_MODE STREQUAL "FORMAT")
        execute_process(
                COMMAND "${MECRAFT_CLANG_FORMAT_EXECUTABLE}"
                        "${MECRAFT_CLANG_FORMAT_STYLE}" -i "${MECRAFT_SOURCE_FILE}"
                RESULT_VARIABLE MECRAFT_CLANG_FORMAT_RESULT
                ERROR_VARIABLE MECRAFT_CLANG_FORMAT_ERROR
        )
        if(NOT MECRAFT_CLANG_FORMAT_RESULT EQUAL 0)
            message(FATAL_ERROR
                    "clang-format failed for ${MECRAFT_SOURCE_FILE}:\n${MECRAFT_CLANG_FORMAT_ERROR}")
        endif()
    else()
        execute_process(
                COMMAND "${MECRAFT_CLANG_FORMAT_EXECUTABLE}"
                        "${MECRAFT_CLANG_FORMAT_STYLE}" --dry-run --Werror "${MECRAFT_SOURCE_FILE}"
                RESULT_VARIABLE MECRAFT_CLANG_FORMAT_RESULT
                ERROR_VARIABLE MECRAFT_CLANG_FORMAT_ERROR
        )
        if(NOT MECRAFT_CLANG_FORMAT_RESULT EQUAL 0)
            set(MECRAFT_CLANG_FORMAT_CHECK_FAILED TRUE)
            message(STATUS "Formatting differs: ${MECRAFT_SOURCE_FILE}")
        endif()
    endif()
endforeach()

if(MECRAFT_CLANG_FORMAT_CHECK_FAILED)
    message(FATAL_ERROR "Source formatting check failed; run the format target")
endif()

list(LENGTH MECRAFT_CLANG_FORMAT_FILES MECRAFT_CLANG_FORMAT_FILE_COUNT)
message(STATUS
        "clang-format ${MECRAFT_CLANG_FORMAT_MODE} completed for ${MECRAFT_CLANG_FORMAT_FILE_COUNT} files")
