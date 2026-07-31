# C/C++ 代码格式化

项目使用 `clang-format 22` 和仓库根目录的 `.clang-format` 统一 C++17 代码格式。
执行前可用 `clang-format --version` 确认工具的主版本号。格式化目标会拒绝其他主版本，避免不同版本生成不一致的代码布局。

格式化范围包括 `src/`、`tests/`、`main.cpp` 和 `dedicated_server.cpp`。`third_party/` 中的外部依赖代码不属于格式化范围。

已有构建目录配置完成后，在 Linux 上执行：

```bash
cmake --build cmake-build-linux-manifest --target format
cmake --build cmake-build-linux-manifest --target format-check
```

Windows 使用对应的 CMake 构建目录：

```powershell
cmake --build cmake-build-fsr31-relwithdebinfo --target format
cmake --build cmake-build-fsr31-relwithdebinfo --target format-check
```

`format` 会原地修改不符合规则的文件；`format-check` 只检查，在发现格式差异时返回非零退出码。
