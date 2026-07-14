@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem Generate the FidelityFX SDK v1.1.4 FSR 3.1.4 Vulkan permutation headers.
rem Usage: generate_fsr31_vulkan_shaders.bat SDK_ROOT OUTPUT_DIRECTORY

if "%~1"=="" (
    echo Missing FidelityFX SDK root.
    exit /b 2
)
if "%~2"=="" (
    echo Missing output directory.
    exit /b 2
)

set "SDK_ROOT=%~f1"
set "OUTPUT_DIR=%~f2"
set "TOOLS_DIR=%SDK_ROOT%\sdk\tools\binary_store"
set "SC=%TOOLS_DIR%\FidelityFX_SC.exe"
set "GLSLANG=%TOOLS_DIR%\glslangValidator.exe"
set "GPU_DIR=%SDK_ROOT%\sdk\include\FidelityFX\gpu"
set "SHADER_DIR=%SDK_ROOT%\sdk\src\backends\vk\shaders\fsr3upscaler"
set "SDK_VERSION_HEADER=%SDK_ROOT%\sdk\include\FidelityFX\host\ffx_interface.h"
set "FSR_VERSION_HEADER=%SDK_ROOT%\sdk\include\FidelityFX\host\ffx_fsr3upscaler.h"

if not exist "%SC%" (
    echo FidelityFX_SC.exe was not found at "%SC%".
    exit /b 3
)
if not exist "%GLSLANG%" (
    echo glslangValidator.exe was not found at "%GLSLANG%".
    exit /b 3
)
if not exist "%SDK_VERSION_HEADER%" (
    echo FidelityFX SDK headers were not found under "%SDK_ROOT%".
    exit /b 3
)

findstr /C:"#define FFX_SDK_VERSION_MAJOR (1)" "%SDK_VERSION_HEADER%" >nul || exit /b 4
findstr /C:"#define FFX_SDK_VERSION_MINOR (1)" "%SDK_VERSION_HEADER%" >nul || exit /b 4
findstr /C:"#define FFX_SDK_VERSION_PATCH (4)" "%SDK_VERSION_HEADER%" >nul || exit /b 4
findstr /C:"#define FFX_FSR3UPSCALER_VERSION_MAJOR      (3)" "%FSR_VERSION_HEADER%" >nul || exit /b 4
findstr /C:"#define FFX_FSR3UPSCALER_VERSION_MINOR      (1)" "%FSR_VERSION_HEADER%" >nul || exit /b 4
findstr /C:"#define FFX_FSR3UPSCALER_VERSION_PATCH      (4)" "%FSR_VERSION_HEADER%" >nul || exit /b 4

if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"
if errorlevel 1 exit /b 5

set BASE_ARGS=-reflection -deps=gcc -DFFX_GPU=1
set BASE_ARGS=!BASE_ARGS! -DFFX_FSR3UPSCALER_OPTION_UPSAMPLE_SAMPLERS_USE_DATA_HALF=0
set BASE_ARGS=!BASE_ARGS! -DFFX_FSR3UPSCALER_OPTION_ACCUMULATE_SAMPLERS_USE_DATA_HALF=0
set BASE_ARGS=!BASE_ARGS! -DFFX_FSR3UPSCALER_OPTION_REPROJECT_SAMPLERS_USE_DATA_HALF=1
set BASE_ARGS=!BASE_ARGS! -DFFX_FSR3UPSCALER_OPTION_POSTPROCESSLOCKSTATUS_SAMPLERS_USE_DATA_HALF=0
set BASE_ARGS=!BASE_ARGS! -DFFX_FSR3UPSCALER_OPTION_UPSAMPLE_USE_LANCZOS_TYPE=2
set BASE_ARGS=!BASE_ARGS! -compiler=glslang -glslangexe="%GLSLANG%" -e CS --target-env vulkan1.2 -S comp -Os -DFFX_GLSL=1

set PERMUTATION_ARGS=-DFFX_FSR3UPSCALER_OPTION_REPROJECT_USE_LANCZOS_TYPE={0,1}
set PERMUTATION_ARGS=!PERMUTATION_ARGS! -DFFX_FSR3UPSCALER_OPTION_HDR_COLOR_INPUT={0,1}
set PERMUTATION_ARGS=!PERMUTATION_ARGS! -DFFX_FSR3UPSCALER_OPTION_LOW_RESOLUTION_MOTION_VECTORS={0,1}
set PERMUTATION_ARGS=!PERMUTATION_ARGS! -DFFX_FSR3UPSCALER_OPTION_JITTERED_MOTION_VECTORS={0,1}
set PERMUTATION_ARGS=!PERMUTATION_ARGS! -DFFX_FSR3UPSCALER_OPTION_INVERTED_DEPTH={0,1}
set PERMUTATION_ARGS=!PERMUTATION_ARGS! -DFFX_FSR3UPSCALER_OPTION_APPLY_SHARPENING={0,1}

set INCLUDE_ARGS=-I"%GPU_DIR%" -I"%GPU_DIR%\fsr3upscaler"

set SHADERS=^
ffx_fsr3upscaler_accumulate_pass ^
ffx_fsr3upscaler_autogen_reactive_pass ^
ffx_fsr3upscaler_debug_view_pass ^
ffx_fsr3upscaler_luma_instability_pass ^
ffx_fsr3upscaler_luma_pyramid_pass ^
ffx_fsr3upscaler_prepare_inputs_pass ^
ffx_fsr3upscaler_prepare_reactivity_pass ^
ffx_fsr3upscaler_rcas_pass ^
ffx_fsr3upscaler_shading_change_pass ^
ffx_fsr3upscaler_shading_change_pyramid_pass

for %%S in (%SHADERS%) do (
    call :compile %%S %%S 0
    if errorlevel 1 exit /b !errorlevel!
    call :compile %%S %%S_wave64 0
    if errorlevel 1 exit /b !errorlevel!
    call :compile %%S %%S_16bit 1
    if errorlevel 1 exit /b !errorlevel!
    call :compile %%S %%S_wave64_16bit 1
    if errorlevel 1 exit /b !errorlevel!
)

set GENERATED_COUNT=0
for /f %%C in ('dir /b /a-d "%OUTPUT_DIR%\ffx_fsr3upscaler*_permutations.h" 2^>nul ^| find /c /v ""') do set GENERATED_COUNT=%%C
if not "%GENERATED_COUNT%"=="40" (
    echo Expected 40 permutation headers but found %GENERATED_COUNT%.
    exit /b 7
)

echo Generated 40 FSR 3.1.4 Vulkan permutation headers in "%OUTPUT_DIR%".
exit /b 0

:compile
set "SOURCE_NAME=%~1"
set "OUTPUT_NAME=%~2"
set "HALF_MODE=%~3"
echo Compiling %OUTPUT_NAME%...
"%SC%" %BASE_ARGS% %PERMUTATION_ARGS% -name=%OUTPUT_NAME% -DFFX_HALF=%HALF_MODE% %INCLUDE_ARGS% -output="%OUTPUT_DIR%" "%SHADER_DIR%\%SOURCE_NAME%.glsl"
if errorlevel 1 (
    echo FidelityFX_SC failed while compiling %OUTPUT_NAME%.
    exit /b 6
)
exit /b 0
