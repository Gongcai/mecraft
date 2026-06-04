# Load MSVC environment
$vsPath = "C:\Program Files\Microsoft Visual Studio\18\Community"
$devShellDll = "$vsPath\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"

if (Test-Path $devShellDll) {
    Import-Module $devShellDll
    Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -Arch amd64 -HostArch amd64
}

# Build the project
& "D:\JetBrain\CLion\bin\cmake\win\x64\bin\cmake.exe" --build "D:\project\mecraft\cmake-build-release" --target mecraft -j 18
