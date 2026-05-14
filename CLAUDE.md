# WARNING
- 当前环境在控制台中编译困难且缓慢，如果你需要编译构建代码，请通知我，由我通过ide编译，将结果反馈给你。 
- 当前目标是：**主世界 `world0` + 原版 Minecraft 材质包 + 内置 DerivativeMain-like 光影效果**。Mecraft 引擎端拥有自己的 Renderer Contract；DerivativeMain 的大气、光照、色调、HDR、水、体积雾、材质风格作为视觉与算法参考，但不再要求 C++ 宿主完整复现 Iris/OptiFine shaderpack ABI，也不要求任意 shaderpack 替换能力。
- 2026-05-13 阴影路线修订：`Debug Disable Greedy Meshing` 已验证 terrain ghosting 根因是 **Derivative/Radial 非线性 shadow warp 与 Mecraft greedy 大 quad 插值不兼容**。正式路线应保留主渲染 greedy meshing + MDI，将默认阴影切到 MecraftShadow Linear/No Warp，后续评估 CSM、PCF/PCSS、contact shadow；Derivative/Radial warp 只保留为 debug/研究模式，不作为默认画面完成标准。

# Mecraft Renderer Contract 与 DerivativeMain-like 移植原则（强制）

1. **Mecraft Renderer Contract 是宿主权威，DerivativeMain 是视觉/算法参考。** 当 DerivativeMain/Iris 的运行假设与 Mecraft 引擎基础设施冲突时，以 Mecraft contract 为准。例如不能为了完整复刻非线性 shadow warp 而全局关闭 greedy meshing；应把 DerivativeMain-like shader 改写为消费 Mecraft 的 GBuffer、shadow、history、material contract。

2. **禁止"看起来等价"的基础公式改写。** 典型事故：DerivativeMain `sqrt2(x)` 是 `sqrt(sqrt(x))`，即四次根；曾误写成 `sqrt(x)`，导致 Derivative shadow warp 读取端与写入端不一致。结论：被移植的基础数学函数、BRDF/SSS/HG phase 等核心函数必须逐字核对，不能凭直觉化简。

3. **先定义 Mecraft 数据流，再吸收 DerivativeMain 效果。** 不再为了对齐 shaderpack ABI 而反向扭曲引擎架构。需要补齐引擎能力时，应优先补齐 Mecraft 自有 contract 层，例如 `MecraftRenderContract`、`MecraftTextureContract`、`MecraftRenderPhase`、`MecraftFrameUniformState`。

4. **每个移植函数都必须标注来源与适配边界。** 在 shader 注释中写明：`DerivativeMain/lib/...` 或 `DerivativeMain/program/...` 的函数名/行意图；若因 Mecraft contract 改写了采样、buffer、shadow projection、透明语义，必须标注为 Mecraft adaptation，不能伪装成完整 shaderpack ABI 等价。

5. **核心函数必须使用公共 include。** shadow distortion/bias、BRDF、SSS/HG phase 等 DerivativeMain 参考核心函数统一在 `derivative_shadow.glsl`、`derivative_brdf.glsl`、`derivative_sunlight.glsl` 中定义，禁止在消费文件中重复内联。消费文件通过 local wrapper 模式绑定 uniform。

6. **DerivativeMain 函数命名使用 PascalCase。** `FresnelSchlick`、`DiffuseHammon`、`SpecularBRDF`、`CalculateSubsurfaceScattering`、`HenyeyGreensteinPhase` 等必须使用 DerivativeMain 原名，不得改为 camelCase（如 `fresnelSchlick`、`diffuseHammon`）。同名是防止歧义和搜索困难。

7. **DerivativeMain Common.inc 辅助宏必须使用共享定义。** `oneMinus`、`saturate`、`max0`、`fastExp`、`rcp`、`pow5`、`pow4`、`pow16`、`dotSelf`、`cossin`、`GetLuminance` 等由 `derivative_shadow.glsl` 统一提供，消费文件不得自行定义（`#ifndef` 保护允许安全共存）。

# 参考文档
## 文档位置及内容
- 根目录下DerivativeMain文件夹，此为光影包解包文件，包含光影包内所有着色器文件和使用的素材，供移植参考。
- docs/文件夹根目录下，*技术分析.md，此为对光影包的各项实现的分析报告，包含对核心算法的分析和伪代码实现，供移植参考。
- docs/文件夹根目录下，*差异报告.md，此为对光影包和Mecraft渲染管线差异的分析报告，包含对每个差异点的分析和建议，供移植参考。
- docs/文件夹根目录下，渲染管线实现状态分析与开发路线图.md，此为对光影包移植的计划和路线图，包含每个阶段的目标和任务，供移植参考。



