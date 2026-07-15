param(
    [string]$BuildDirectory = "cmake-build-fsr31-relwithdebinfo",
    [string]$Target = "mecraft",
    [int]$Jobs = 18,
    [switch]$EnableStreamline,
    [string]$StreamlineRoot = ".cache\streamline\sdk-v2.12.0",
    [uint32]$StreamlineApplicationId = 0,
    [switch]$ConfigureOnly
)

# Load MSVC environment
$vsPath = "C:\Program Files\Microsoft Visual Studio\18\Community"
$devShellDll = "$vsPath\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"

if (Test-Path $devShellDll) {
    Import-Module $devShellDll
    Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -Arch amd64 -HostArch amd64
}

$cmake = "D:\JetBrain\CLion\bin\cmake\win\x64\bin\cmake.exe"
$sourceDirectory = $PSScriptRoot
$buildPath = Join-Path $sourceDirectory $BuildDirectory

$configureArguments = @(
    "-S", $sourceDirectory,
    "-B", $buildPath,
    "-G", "Ninja",
    "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
    "-DMECRAFT_BUILD_TESTS=ON",
    "-DMECRAFT_DEFAULT_RHI_BACKEND=Vulkan",
    "-DMECRAFT_ENABLE_FSR31=ON"
)
if ($EnableStreamline) {
    $streamlinePath = Join-Path $sourceDirectory $StreamlineRoot
    $configureArguments += "-DMECRAFT_ENABLE_STREAMLINE=ON"
    $configureArguments += "-DMECRAFT_STREAMLINE_ROOT=$streamlinePath"
    $configureArguments += "-DMECRAFT_STREAMLINE_APPLICATION_ID=$StreamlineApplicationId"
} else {
    $configureArguments += "-DMECRAFT_ENABLE_STREAMLINE=OFF"
}

& $cmake @configureArguments
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

if ($ConfigureOnly) {
    exit 0
}

# Build the Vulkan and FSR 3.1 baseline target.
& $cmake --build $buildPath --target $Target -j $Jobs
exit $LASTEXITCODE
