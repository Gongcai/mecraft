cmake_minimum_required(VERSION 3.20)

set(MECRAFT_NRD_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(MECRAFT_NRD_GENERATED_DIR "${MECRAFT_NRD_ROOT}/Generated/SPIRV")

file(STRINGS "${MECRAFT_NRD_ROOT}/Include/NRD.h" MECRAFT_NRD_VERSION_DEFINES
        REGEX "^#define NRD_VERSION_(MAJOR|MINOR|BUILD) ")
string(JOIN ";" MECRAFT_NRD_VERSION_TEXT ${MECRAFT_NRD_VERSION_DEFINES})
if(NOT MECRAFT_NRD_VERSION_TEXT MATCHES "NRD_VERSION_MAJOR 4" OR
   NOT MECRAFT_NRD_VERSION_TEXT MATCHES "NRD_VERSION_MINOR 17" OR
   NOT MECRAFT_NRD_VERSION_TEXT MATCHES "NRD_VERSION_BUILD 3")
    message(FATAL_ERROR "third_party/nrd must contain NVIDIA NRD 4.17.3")
endif()
set(MECRAFT_NRD_REQUIRED_FILES
        "${MECRAFT_NRD_ROOT}/LICENSE.txt"
        "${MECRAFT_NRD_ROOT}/SOURCE_NOTICE.txt"
        "${MECRAFT_NRD_ROOT}/Shaders/NRDConfig.hlsli"
        "${MECRAFT_NRD_GENERATED_DIR}/Clear.cs.spirv.h"
        "${MECRAFT_NRD_GENERATED_DIR}/REBLUR_TemporalAccumulation.cs.spirv.h"
        "${MECRAFT_NRD_GENERATED_DIR}/RELAX_TemporalAccumulation.cs.spirv.h"
        "${MECRAFT_NRD_ROOT}/Dependencies/ShaderMake/LICENSE.txt"
        "${MECRAFT_NRD_ROOT}/Dependencies/MathLib/LICENSE.txt"
)
foreach(MECRAFT_NRD_REQUIRED_FILE IN LISTS MECRAFT_NRD_REQUIRED_FILES)
    if(NOT EXISTS "${MECRAFT_NRD_REQUIRED_FILE}")
        message(FATAL_ERROR "Missing fixed NRD dependency file: ${MECRAFT_NRD_REQUIRED_FILE}")
    endif()
endforeach()

add_library(mecraft_nrd STATIC
        Source/InstanceImpl.cpp
        Source/Reblur.cpp
        Source/Reference.cpp
        Source/Relax.cpp
        Source/Sigma.cpp
        Source/Timer.cpp
        Source/Wrapper.cpp
        Dependencies/ShaderMake/ShaderMake/ShaderBlob.cpp
)
target_compile_features(mecraft_nrd PUBLIC cxx_std_17)
set_target_properties(mecraft_nrd PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(mecraft_nrd SYSTEM
        PUBLIC
            "${MECRAFT_NRD_ROOT}/Include"
        PRIVATE
            "${MECRAFT_NRD_ROOT}/Source"
            "${MECRAFT_NRD_ROOT}/Shaders"
            "${MECRAFT_NRD_GENERATED_DIR}"
            "${MECRAFT_NRD_ROOT}/Dependencies/ShaderMake"
            "${MECRAFT_NRD_ROOT}/Dependencies/MathLib"
)
target_compile_definitions(mecraft_nrd
        PUBLIC
            NRD_STATIC_LIBRARY=1
        PRIVATE
            WIN32_LEAN_AND_MEAN
            NOMINMAX
            _CRT_SECURE_NO_WARNINGS
            NRD_EMBEDS_SPIRV_SHADERS=1
            NRD_EMBEDS_DXIL_SHADERS=0
            NRD_EMBEDS_DXBC_SHADERS=0
            NRD_SUPPORTS_VIEWPORT_OFFSET=1
            NRD_SUPPORTS_CHECKERBOARD=1
            NRD_SUPPORTS_HISTORY_CONFIDENCE=1
            NRD_SUPPORTS_DISOCCLUSION_THRESHOLD_MIX=1
            NRD_SUPPORTS_ANTIFIREFLY=1
            REBLUR_PERFORMANCE_MODE=0
            NRD_NORMAL_ENCODING=2
            NRD_ROUGHNESS_ENCODING=1
            SPIRV_SREG_OFFSET=0
            SPIRV_BREG_OFFSET=2
            SPIRV_UREG_OFFSET=3
            SPIRV_TREG_OFFSET=20
)
if(MSVC)
    target_compile_options(mecraft_nrd PRIVATE /W0 /GR- /EHs-c-)
else()
    target_compile_options(mecraft_nrd PRIVATE -w -fno-rtti -fno-exceptions)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
        target_compile_options(mecraft_nrd PRIVATE -mssse3)
    endif()
endif()
