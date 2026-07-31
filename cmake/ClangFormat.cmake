# Registers explicit formatting targets without making clang-format a build dependency.
add_custom_target(format
        COMMAND "${CMAKE_COMMAND}"
                "-DMECRAFT_CLANG_FORMAT_MODE=FORMAT"
                "-DMECRAFT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                -P "${PROJECT_SOURCE_DIR}/cmake/RunClangFormat.cmake"
        COMMENT "Formatting Mecraft C and C++ sources"
        VERBATIM
)

add_custom_target(format-check
        COMMAND "${CMAKE_COMMAND}"
                "-DMECRAFT_CLANG_FORMAT_MODE=CHECK"
                "-DMECRAFT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                -P "${PROJECT_SOURCE_DIR}/cmake/RunClangFormat.cmake"
        COMMENT "Checking Mecraft C and C++ source formatting"
        VERBATIM
)
