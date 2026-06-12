# WARNING
- 
- 如需自行编译，需先注入 MSVC 环境再执行 cmake：
  ```powershell
  # 注入 MSVC 2025 x64 环境
  $vsPath = "C:\Program Files\Microsoft Visual Studio\18\Community"
  $devShellDll = "$vsPath\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
  if (Test-Path $devShellDll) {
      Import-Module $devShellDll
      Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -Arch amd64 -HostArch amd64
  }
# 参考指令
  # 编译主程序
  & "D:\JetBrain\CLion\bin\cmake\win\x64\bin\cmake.exe" --build "D:\project\mecraft\cmake-build-release" --target mecraft -j 18
  # 编译测试
  & "D:\JetBrain\CLion\bin\cmake\win\x64\bin\cmake.exe" --build "D:\project\mecraft\cmake-build-release" --target chunk_save_serializer_test -j 18
  # 运行测试
  cd d:/project/mecraft/cmake-build-release && ./chunk_save_serializer_test.exe
  ```
# 文档
参考save-system-design.md进行开发
# 语言使用
- 代码注释必须使用英文，且必须清晰描述函数目的、参数意义、返回值含义、算法思路等关键信息。禁止使用中文注释或不规范的缩写。
- 给用户的汇报和文档必须使用中文。
# 汇报文档
在用户没有明确要求写汇报文档时，不要写任何文档，通过会话给用户报告即可。
