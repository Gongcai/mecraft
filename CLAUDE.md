# WARNING
- 当前环境在控制台中编译困难且缓慢，如果你需要编译构建代码，请通知我，由我通过ide编译，将结果反馈给你。 
- 项目在实现我的实际光影包的效果，目标是把根目录下的DerivativeMain光影包做成内置光影，算法实现/数值调整都应该忠实遵从DerivativeMain光影包，shader端算法的实现，常量数值的使用，C++端引擎的渲染链路，都应该完全遵循DerivativeMain光影包的实现，在写代码之前先查看DerivativeMain光影包的实现，根据DerivativeMain的实现在项目内实现对应效果，禁止快速原型或者简单实现，需要补齐引擎能力就补齐引擎能力。

# DerivativeMain 移植原则（强制）

1. **DerivativeMain 源码是权威实现。** shader 数学公式、采样顺序、bias、dither、宏默认值、材质 ID 语义、buffer 语义均以 DerivativeMain 为准。当前引擎只允许做 OpenGL/FBO/材质系统/资源路径层面的适配。

2. **禁止"看起来等价"的公式改写。** 典型事故：DerivativeMain `sqrt2(x)` 是 `sqrt(sqrt(x))`，即四次根；曾误写成 `sqrt(x)`，导致 Derivative shadow warp 读取端与写入端不一致。结论：基础函数必须逐字复刻，不能凭直觉化简。

3. **先还原 DerivativeMain 数据流，再优化性能。** 不允许为了当前资源布局方便而改变 DerivativeMain 的采样语义。

4. **每个移植函数都必须标注来源。** 在 shader 注释中写明：`DerivativeMain/lib/...` 或 `DerivativeMain/program/...` 的函数名/行意图。

5. **阴影失真等核心函数必须使用公共 include。** 所有 shadow distortion / bias 函数统一在 `derivative_shadow.glsl` 中定义，shadow/deferred/volumetric/debug 全部引用同一 include，禁止重复内联。