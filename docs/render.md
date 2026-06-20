       Mecraft 渲染系统调查报告

       调查范围：src/renderer/ 及
       assets/shaders/。结论先行：这是一个完成度较高的延迟渲染管线，老文档标记为「缺失」的两点中，实体 GBuffer
       已实现（不再是纯 forward），但真正的全局光照(RSM/GI)仍然缺失（只有 fake bounce 近似）。

       1. 渲染 Pass 列表（src/renderer/passes/）

       每个 Pass 继承自 RenderPass（基类 passes/RenderPass.h，提供 init/shutdown/name 接口和 fullscreen triangle
       绘制辅助）。共 19 个具体 Pass 类：

       ┌─────────────────────────────┬──────────────────────┬──────────────────────────────────────────────────────
       ───────┐
       │            文件             │          类          │                     职责（取自类注释）
             │
       ├─────────────────────────────┼──────────────────────┼──────────────────────────────────────────────────────
       ───────┤
       │ SkyCapturePass.h/.cpp       │ SkyCapturePass       │ 渲染等距柱状(equirectangular)天空辐射度和云数据，供
       IBL     │
       │                             │                      │ 与光照使用
             │
       ├─────────────────────────────┼──────────────────────┼──────────────────────────────────────────────────────
       ───────┤
       │ GBufferPass.h/.cpp          │ GBufferPass          │ 实体/掉落物/坠落方块的 GBuffer 渲染（地形 GBuffer
             │
       │                             │                      │ 由地形管线单独处理）
             │
       ├─────────────────────────────┼──────────────────────┼──────────────────────────────────────────────────────
       ───────┤
       │ ShadowPass.h/.cpp           │ ShadowPass           │ 渲染所有级联的 CSM
             │
       │                             │                      │
       阴影图；处理不透明地形、cutout、实体、掉落物、透明阴影投射  │
       ├─────────────────────────────┼──────────────────────┼──────────────────────────────────────────────────────
       ───────┤
       │ VelocityPass.h/.cpp         │ VelocityPass         │ 屏幕空间速度：重投影深度计算逐像素速度
             │
       ├─────────────────────────────┼──────────────────────┼──────────────────────────────────────────────────────
       ───────┤
       │ SsaoPass.h/.cpp             │ SsaoPass             │ SSAO：半分辨率原始 → 双边滤波 → 深度感知上采样 →
       时域重投影 │
       ├─────────────────────────────┼──────────────────────┼──────────────────────────────────────────────────────
       ───────┤
       │ DeferredLightingPass.h/.cpp │ DeferredLightingPass │ 从 GBuffer、阴影、SSAO、大气计算全场景光照
             │
       ├─────────────────────────────┼──────────────────────┼──────────────────────────────────────────────────────
       ───────┤
       │ ReflectionPass.h/.cpp       │ ReflectionPass       │ SSR 反射探针 + 双边滤波 + 时域重投影
             │
       ├─────────────────────────────┼──────────────────────┼──────────────────────────────────────────────────────
       ───────┤
       │ CloudPass.h/.cpp            │ CloudPass            │ 光线步进体积云 + 时域重投影
             │
       ├─────────────────────────────┼──────────────────────┼──────────────────────────────────────────────────────
       ───────┤
       │ SceneCompositePass.h/.cpp   │ SceneCompositePass   │ 合成光照、反射、云、大气为最终 HDR 场景
             │
       ├─────────────────────────────┼──────────────────────┼──────────────────────────────────────────────────────
       ───────┤
       │ VolumetricPass.h/.cpp       │ VolumetricPass       │ 体积雾光线步进 + 时域重投影 + 场景合成
             │
       ├─────────────────────────────┼──────────────────────┼──────────────────────────────────────────────────────
       ───────┤
       │ WaterCompositePass.h/.cpp   │ WaterCompositePass   │
       水面渲染（延迟兼容着色），含深度软化、体积雾、天空捕获反射  │
       ├─────────────────────────────┼──────────────────────┼──────────────────────────────────────────────────────
       ───────┤
       │ TemporalResolvePass.h/.cpp  │ TemporalResolvePass  │ TAA：当前帧与重投影历史帧混合
             │
       ├─────────────────────────────┼──────────────────────┼──────────────────────────────────────────────────────
       ───────┤
       │ MotionBlurPass.h/.cpp       │ MotionBlurPass       │ 基于速度的逐像素运动模糊
             │
       ├─────────────────────────────┼──────────────────────┼──────────────────────────────────────────────────────
       ───────┤
       │ DepthOfFieldPass.h/.cpp     │ DepthOfFieldPass     │ 基于弥散圆(CoC)的景深模糊
             │
       ├─────────────────────────────┼──────────────────────┼──────────────────────────────────────────────────────
       ───────┤
       │ PostProcessPass.h/.cpp      │ PostProcessPass      │
       前向/延迟共用：bloom、自动曝光、tonemap、色彩分级、水下效果 │
       ├─────────────────────────────┼──────────────────────┼──────────────────────────────────────────────────────
       ───────┤
       │ Fsr1Pass.h/.cpp             │ Fsr1Pass             │ FSR1 超分辨率上采样（EASU/RCAS）
             │
       ├─────────────────────────────┼──────────────────────┼──────────────────────────────────────────────────────
       ───────┤
       │ DebugPass.h/.cpp            │ DebugPass            │ 调试可视化：GBuffer/光照/阴影/SSAO 各中间目标
             │
       ├─────────────────────────────┼──────────────────────┼──────────────────────────────────────────────────────
       ───────┤
       │ RenderPass.h/.cpp           │ RenderPass           │ 抽象基类
             │
       └─────────────────────────────┴──────────────────────┴──────────────────────────────────────────────────────
       ───────┘

       2. 渲染管线编排（src/renderer/core/）

       没有名为 Renderer 的类，编排入口是 RenderScene（core/RenderScene.h/.cpp）。

       架构：
       - RenderPipeline（core/RenderPipeline.h）— 抽象接口，含 renderFrame() / supportsDeferred() /
       supportsDebugView()。
       - ForwardPipeline 和 DeferredPipeline 各自实现该接口。
       - RenderScene 持有两个管线的 unique_ptr（m_forwardPipeline / m_deferredPipeline），通过 m_activePipeline
       指针切换，并拥有共用的 PostProcessPass m_postProcessPass 和 Fsr1Pass。
       - 两管线共享 SharedRenderResources 结构体（地形、目标、子渲染器等非拥有指针）。

       前向与延迟是否已解耦完成：是，已完全解耦。 两者是独立的类，各有 init/shutdown/renderFrame：
       - ForwardPipeline（core/ForwardPipeline.cpp）：极简 5 步 — 天空→不透明/cutout
       地形→实体与粒子→透明地形→输出。它显式调用 setForwardMode(true) 切换子渲染器到 vanilla 着色器，且
       buildFrameOutput 设置 hasDeferredInputs=false; skipPostProcess=true（跳过 bloom/曝光/分级）。
       - DeferredPipeline（core/DeferredPipeline.cpp:144 renderFrame）：完整 pass 编排顺序为
       SkyCapture → GBuffer(地形+实体+掉落+坠落方块) → Velocity → Shadow → SSAO → DeferredLighting → Reflection →
       Cloud → SceneComposite → WaterComposite(pre-TAA) → 通用透明 → 粒子 → Volumetric → TAA → MotionBlur → DoF →
       历史更新+blit → Debug。每个 pass 都包裹了 ScopedDebugGroup 和 ScopedGpuTimer。

       3. 延迟渲染 GBuffer 与材质 Contract

       GBuffer = 5 个固定 MRT（targets/DeferredRenderTargets.h:149-154，kGBufferAttachmentCount = 5）：

       ┌────────────┬─────────┬─────────────────────────────────────────────────────────────────┐
       │ Attachment │  格式   │                              内容                               │
       ├────────────┼─────────┼─────────────────────────────────────────────────────────────────┤
       │ 0          │ RGBA8   │ 线性 albedo.rgb + emissive hint.a                               │
       ├────────────┼─────────┼─────────────────────────────────────────────────────────────────┤
       │ 1          │ RGBA16F │ 编码世界法线.rgb + 顶点 AO.a                                    │
       ├────────────┼─────────┼─────────────────────────────────────────────────────────────────┤
       │ 2          │ RG8     │ 天空光.r + 方块光.g (voxel light)                               │
       ├────────────┼─────────┼─────────────────────────────────────────────────────────────────┤
       │ 3          │ RGBA8   │ roughness.r / f0.g / emission.b / subsurface.a                  │
       ├────────────┼─────────┼─────────────────────────────────────────────────────────────────┤
       │ 4          │ RGBA8   │ DerivativeMain 材质 id.r / wetness.g / porosity.b / metalness.a │
       └────────────┴─────────┴─────────────────────────────────────────────────────────────────┘

       另有运行时临时第 6 个附件（COLOR_ATTACHMENT5，RG16F per-object velocity），由
       attachPerObjectVelocityToGBuffer() 在实体/掉落渲染时临时挂载。

       材质 ID：约 33 种有效 ID（assets/shaders/gbuffer_contract.glsl:53-88）。ID 范围
       0-63（MATERIAL_ID_MAX=63），映射自 DerivativeMain block.properties（OptiFine mc_Entity id 减 10000）。关键
       ID：植物 1-7、Banner SSS=9、雪冰 SSS=10、熔岩 15、染色玻璃 16、水 17、冰 18、各类发光体 19-36、矿石
       57-58，以及 Mecraft 自有扩展 MATERIAL_SKIN=60（实体皮肤）。

       4. 关键效果实现状态

       ┌───────────────────────────────────┬────────────────────────────────────┬──────────────────────────────────
       ─────┐
       │               效果                │                状态                │               主要文件
           │
       ├───────────────────────────────────┼────────────────────────────────────┼──────────────────────────────────
       ─────┤
       │                                   │                                    │ passes/ShadowPass,
           │
       │ CSM 级联阴影                      │ ✅ 已实现，4 级联                  │ shadow/ShadowMatrices,
           │
       │                                   │ (kShadowCascadeCount=4)            │ derivative_shadow.glsl,
           │
       │                                   │                                    │ MecraftTextureContract.h
           │
       ├───────────────────────────────────┼────────────────────────────────────┼──────────────────────────────────
       ─────┤
       │ PBR BRDF                          │ ✅ 已实现                          │
       assets/shaders/derivative_brdf.glsl,  │
       │                                   │                                    │ deferred_lighting.frag
           │
       ├───────────────────────────────────┼────────────────────────────────────┼──────────────────────────────────
       ─────┤
       │ 球谐天光 SH                       │ ✅ 已实现（L1 SH，25               │ assets/shaders/sky_sh.glsl
       (ToSH/From │
       │                                   │ 方向累积，移植自 DerivativeMain）  │ SH/buildSkySH/evaluateSkySH)
           │
       ├───────────────────────────────────┼────────────────────────────────────┼──────────────────────────────────
       ─────┤
       │                                   │ ✅ 已实现（256×128×33 RGBA32F 3D   │
       assets/shaders/atmosphere_lut.glsl,   │
       │ 大气散射 LUT                      │ 纹理，含 transmittance/scattering/ │
       DeferredRenderTargets::loadAtmosphere │
       │                                   │ irradiance）                       │ Lut
           │
       ├───────────────────────────────────┼────────────────────────────────────┼──────────────────────────────────
       ─────┤
       │ 体积云                            │ ✅ 已实现（光线步进+时域）         │ passes/CloudPass,
       cloud_density.glsl, │
       │                                   │                                    │  cloud_target.frag
           │
       ├───────────────────────────────────┼────────────────────────────────────┼──────────────────────────────────
       ─────┤
       │ SSR 屏幕空间反射                  │ ✅ 已实现（探针+滤波+时域）        │ passes/ReflectionPass,
           │
       │                                   │                                    │
       reflection_probe/filter/temporal.frag │
       ├───────────────────────────────────┼────────────────────────────────────┼──────────────────────────────────
       ─────┤
       │ SSAO                              │ ✅ 已实现（半分辨率+双边+时域）    │ passes/SsaoPass, ssao*.frag
           │
       ├───────────────────────────────────┼────────────────────────────────────┼──────────────────────────────────
       ─────┤
       │ SSS 次表面散射                    │ ✅ 已实现（按材质 ID 硬编码：草    │ gbuffer_contract.glsl:166,
           │
       │                                   │ 0.45/叶雪 0.70/旗 0.65/皮肤 0.35） │ deferred_lighting.frag
           │
       ├───────────────────────────────────┼────────────────────────────────────┼──────────────────────────────────
       ─────┤
       │ TAA                               │ ✅ 已实现                          │ passes/TemporalResolvePass,
           │
       │                                   │                                    │ temporal_resolve.frag
           │
       ├───────────────────────────────────┼────────────────────────────────────┼──────────────────────────────────
       ─────┤
       │ 水体渲染                          │ ✅                                 │ passes/WaterCompositePass,
           │
       │                                   │ 已实现（深度软化、反射、体积雾）   │ water_composite.frag
           │
       ├───────────────────────────────────┼────────────────────────────────────┼──────────────────────────────────
       ─────┤
       │ 体积雾                            │ ✅ 已实现                          │ passes/VolumetricPass,
           │
       │                                   │                                    │
       volumetric_composite/temporal.frag    │
       ├───────────────────────────────────┼────────────────────────────────────┼──────────────────────────────────
       ─────┤
       │                                   │                                    │ passes/PostProcessPass
           │
       │ HDR 后处理 Bloom/Tonemap/色彩分级 │ ✅ 全部实现                        │ (bloom/曝光/tonemap/分级),
           │
       │ /运动模糊/景深                    │                                    │ MotionBlurPass+motion_blur.frag,
           │
       │                                   │                                    │ DepthOfFieldPass+dof.frag
           │
       └───────────────────────────────────┴────────────────────────────────────┴──────────────────────────────────
       ─────┘

       5. DerivativeMain 移植 — 两个老「缺失」点核实

       移植着色器确实存在：assets/shaders/ 下有
       derivative_brdf.glsl、derivative_shadow.glsl、derivative_sunlight.glsl、derivative_weather.glsl，以及大量带
       DerivativeMain 移植注释的文件（gbuffer_contract.glsl、sky_sh.glsl、atmosphere_lut.glsl）。

       核实点 A — 全局光照(RSM/GI)：仍然缺失（真实状态确认）。
       - 全代码库 src/renderer/ 下搜索 RSM / reflective shadow / global illumination / GI pass / indirect bounce /
       voxel cone / path trac 零匹配。
       - deferred_lighting.frag 中唯一的「GI」是 CalculateFakeBouncedLight（第 818 行，移植自 DerivativeMain
       SunLighting.glsl），即 fake bounce 假反弹近似，由 uFakeBounceStrength 控制，注释明确标为 "fake bounce"。第
       823 行注释 "GI / AO" 实际只是 AO 乘到漫反射上。
       - 结论：没有真正的 RSM 或多次反弹 GI，只有单次假反弹 + AO，与老文档标记一致。

       核实点 B — 实体渲染：不再是纯 forward，实体 GBuffer 已实现（状态已更新）。
       - 存在专用 shader assets/shaders/entity_gbuffer.frag，由 GBufferPass.cpp:13 getShader("entity_gbuffer")
       加载。
       - DeferredPipeline::renderFrame（DeferredPipeline.cpp:202-214）在 GBuffer 阶段显式调用 m_gbufferPass->execut
       eEntities(...)、executeDrops(...)、executeFallingBlocks(...)，将人形实体、掉落物、坠落方块写入延迟 GBuffer。
       - 材质 contract 为实体新增了 MATERIAL_SKIN=60（皮肤专属
       roughness=0.65、SSS=0.35、porosity=0.3），证明实体已纳入延迟光照路径。
       - 纯 forward 实体渲染只在 ForwardPipeline 中保留（renderEntitiesAndParticles），那是独立的 vanilla
       前向管线。
       - 结论：延迟管线下实体已是 GBuffer 渲染，老文档「实体纯 forward」的标记已过时。

       6. 着色器组织（assets/shaders/）

       单层扁平目录（仅一个子目录 ffx-fsr/ 放 AMD FSR1 头文件）。约 100 个着色器文件，扩展名约定：
       - .vert — 顶点着色器
       - .frag — 片段着色器
       - .glsl — 共享 include 库（如 gbuffer_contract.glsl、derivative_*.glsl、sky_sh.glsl、atmosphere_lut.glsl、re
       nder_contract.glsl、fogs.glsl、lighting_environment.glsl）

       注意：项目未使用 .vsh/.fsh 扩展名（那是 OptiFine/Iris 的约定，这里是移植来源 DerivativeMain
       的格式）。每个效果通常有 forward 与 deferred 两个变体（如 gameplay_sky.frag vs
       gameplay_sky_forward.frag、steve.frag vs steve_forward.frag、drop_block.frag vs
       drop_block_forward.frag），对应两条管线。

       ---
       给 README 的核心结论：Mecraft 是一套移植自 DerivativeMain 光影包的内置延迟渲染管线，前向/延迟双管线已通过
       RenderPipeline 接口完全解耦。GBuffer 5-MRT、4 级 CSM、PBR、SH 天光、大气
       LUT、体积云/雾、SSR、SSAO、TAA、完整 HDR 后处理均已落地，实体已纳入延迟
       GBuffer。唯一显著缺口是真正的全局光照（RSM/多次反弹 GI）仍未实现，当前以 fake bounce 近似替代。