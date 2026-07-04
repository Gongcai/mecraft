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
  
# 语言使用

- 代码注释必须使用英文，且必须清晰描述函数目的、参数意义、返回值含义、算法思路等关键信息。禁止使用中文注释或不规范的缩写。
- 给用户的汇报和文档必须使用中文。

# 汇报文档

在用户没有明确要求写汇报文档时，不要写任何文档，通过会话给用户报告即可。

# 代码规范

- 禁止使用任何fallback代码或兜底代码，出现bug必须修复bug，而不是绕过bug
- 代码规范须符合C++17标准，多使用现代C++特性
- 除非用户要求，禁止写一切的回退代码。优先查找真正的发生原因。这是一条严格执行的规则，回退是正常被禁止的。
- 严令禁止各种回退逻辑，包括“xxx才会退回”、“降级处理”、“优先 再”、“如果没有 就”这种字眼，绝对禁止！！！绝对禁止！！！这种就是兜底！出现一次严肃惩罚！
- 本项目需要高性能开发，严禁使用try catch等异常捕获方法，使用std::optinal或错误码进行处理

