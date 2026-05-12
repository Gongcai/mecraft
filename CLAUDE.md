# WARNING
- 当前环境在控制台中编译困难且缓慢，如果你需要编译构建代码，请通知我，由我通过ide编译，将结果反馈给你。 
- 项目在实现我的实际光影包的效果，目标是把根目录下的DerivativeMain光影包做成内置光影，算法实现/数值调整都应该忠实遵从DerivativeMain光影包，shader端算法的实现，常量数值的使用，C++端引擎的渲染链路，都应该完全遵循DerivativeMain光影包的实现，在写代码之前先查看DerivativeMain光影包的实现，根据DerivativeMain的实现在项目内实现对应效果，禁止快速原型或者简单实现，需要补齐引擎能力就补齐引擎能力。

# DerivativeMain 移植原则（强制）

1. **DerivativeMain 源码是权威实现。** shader 数学公式、采样顺序、bias、dither、宏默认值、材质 ID 语义、buffer 语义均以 DerivativeMain 为准。当前引擎只允许做 OpenGL/FBO/材质系统/资源路径层面的适配。

2. **禁止"看起来等价"的公式改写。** 典型事故：DerivativeMain `sqrt2(x)` 是 `sqrt(sqrt(x))`，即四次根；曾误写成 `sqrt(x)`，导致 Derivative shadow warp 读取端与写入端不一致。结论：基础函数必须逐字复刻，不能凭直觉化简。

3. **先还原 DerivativeMain 数据流，再优化性能。** 不允许为了当前资源布局方便而改变 DerivativeMain 的采样语义。

4. **每个移植函数都必须标注来源。** 在 shader 注释中写明：`DerivativeMain/lib/...` 或 `DerivativeMain/program/...` 的函数名/行意图。

5. **核心函数必须使用公共 include。** shadow distortion/bias、BRDF、SSS/HG phase 等 DerivativeMain 核心函数统一在 `derivative_shadow.glsl`、`derivative_brdf.glsl`、`derivative_sunlight.glsl` 中定义，禁止在消费文件中重复内联。消费文件通过 local wrapper 模式绑定 uniform。

6. **DerivativeMain 函数命名使用 PascalCase。** `FresnelSchlick`、`DiffuseHammon`、`SpecularBRDF`、`CalculateSubsurfaceScattering`、`HenyeyGreensteinPhase` 等必须使用 DerivativeMain 原名，不得改为 camelCase（如 `fresnelSchlick`、`diffuseHammon`）。同名是防止歧义和搜索困难。

7. **DerivativeMain Common.inc 辅助宏必须使用共享定义。** `oneMinus`、`saturate`、`max0`、`fastExp`、`rcp`、`pow5`、`pow4`、`pow16`、`dotSelf`、`cossin`、`GetLuminance` 等由 `derivative_shadow.glsl` 统一提供，消费文件不得自行定义（`#ifndef` 保护允许安全共存）。