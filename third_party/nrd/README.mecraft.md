# Mecraft NRD 固定依赖说明

本目录固定 NVIDIA NRD `v4.17.3`，上游提交为
`792eff196afdd350fd9c3f862119017ccb438a0e`。项目构建只编译仓库内源码和已生成的
SPIR-V Blob，不执行网络下载，也不在普通构建中运行 ShaderMake 或 DXC。

固定生成工具：

- ShaderMake：`18f5a344e7ca8fa65daaf079d07bc8ce38453e05`
- MathLib：`v11`，提交 `974e1387ba936740c7cdc494792d2641bc127e86`
- DirectXShaderCompiler：`v1.8.2505`，Linux 发布日期标识 `2025_05_24`

固定 NRD 配置：

- `NRD_STATIC_LIBRARY=ON`
- `NRD_NRI=OFF`
- `NRD_SUPPORTS_VIEWPORT_OFFSET=ON`
- `NRD_SUPPORTS_CHECKERBOARD=ON`
- `NRD_SUPPORTS_HISTORY_CONFIDENCE=ON`
- `NRD_SUPPORTS_DISOCCLUSION_THRESHOLD_MIX=ON`
- `NRD_SUPPORTS_ANTIFIREFLY=ON`
- `NRD_EMBEDS_SPIRV_SHADERS=ON`
- `NRD_NORMAL_ENCODING=2`
- `NRD_ROUGHNESS_ENCODING=1`
- SPIR-V Binding Offset：Sampler `0`、Constant Buffer `2`、Storage `3`、Texture `20`

生成产物的逐文件 SHA-256 位于 `Generated/SPIRV-SHA256.txt`。NRD、ShaderMake 和
MathLib 的许可证文本均随源码保存；应用发布仍须遵守 `LICENSE.txt` 的 Object Code
分发要求及最终用户条款要求。
