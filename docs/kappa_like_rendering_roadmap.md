# Mecraft DerivativeMain-like 光影重构路线图

> 目标：把当前 Mecraft 的 OpenGL 4.5 Hybrid Deferred 管线，逐步重构为以 `DerivativeMain` 为参考的高性价比 Minecraft 光影管线。本文取代旧的 Kappa-like roadmap。Kappa 分析仍可作为阴影与调色的历史参考，但后续主目标改为 DerivativeMain 的低成本分层渲染架构。

## 1. 结论摘要

DerivativeMain 与当前管线的适配度 **中高**。它不是一个“直接移植 shader 文件”的目标，而是一个非常适合拆成阶段实现的架构参考。

最适合优先吸收的部分：

- 四次方阴影畸变、PCSS、屏幕空间接触阴影、云影。
- 半分辨率体积雾、体积光、深度感知上采样。
- 使用 2D noise 模拟 3D 噪声的体积云、雾和水波。
- Cook-Torrance + Hammon diffuse 的轻量 PBR 光照模型。
- Blackbody 方块光、矿石高亮检测、发光材质进入 HDR/Bloom。
- 水体的程序化波浪、精确 Fresnel、屏幕空间折射、低步数 SSR。
- ACES/AgX、Purkinje、Bloom mip 链、CAS/dither 后处理。

不建议短期完整复刻的部分：

- Bruneton 全套预计算大气 LUT 管线。当前 DerivativeMain 解包资源已包含 LUT 文件，但接入仍涉及纹理格式、3D/2D 查询映射、天空捕获和光照数据流重构。
- 完整 TAA、运动向量、历史帧 reprojection。
- DerivativeMain 的 8 个 `colortex` 原样布局。
- 完整 LabPBR 资源包兼容、POM 全覆盖。
- RSM GI、完整体积云时间超分辨率、Distant Horizons 适配。

推荐策略：**先做 DerivativeMain-inspired 版本，再按效果瓶颈升级**。也就是保留当前项目的 Hybrid Deferred 骨架，增加必要的 scene/light/translucent/volumetric/reflect/post targets，而不是一步替换为 shaderpack 的多 pass 结构。

## 2. 当前渲染管线基线

当前项目已经具备继续推进的关键基础：

- OpenGL 4.5 core，可使用现代 GLSL 和 DSA 风格 FBO 管理。
- 地形 opaque/cutout 已进入 G-buffer，透明和水体仍走前向路径。
- 当前 G-buffer：
  - `GAlbedoMaterial`: `RGBA8`，RGB 为 linear albedo，A 为 emissive hint。
  - `GNormalAo`: `RGBA16F`，RGB 为 world normal，A 为 vertex AO。
  - `GVoxelLight`: `RG8`，R 为 skylight，G 为 block light。
  - `Depth`: `DEPTH_COMPONENT32F`。
- 当前 deferred 阶段：
  - GBuffer。
  - 单张太阳 shadow map。
  - SSAO。
  - deferred lighting 直接写回捕获 framebuffer。
  - 透明、水、破坏框、outline 在 deferred 后继续前向绘制。
- 当前后处理：
  - `PostProcessRenderer` 使用 `RGBA16F` scene color。
  - 半分辨率 bloom extract + ping-pong blur。
  - tonemap、Kappa-like grading、dither、水下 tint、简单 sun rays。
- 当前阴影：
  - 单 shadow depth。
  - light-space texel snapping。
  - radial warp。
  - 24 tap R2 PCF。
  - normal/slope/constant bias。
- 当前工程化：
  - 已有 GPU timer，但只覆盖 GBuffer、Shadow、SSAO、Lighting、Transparent。
  - Dashboard/setting 已有基础可调参数。

这些基础与 DerivativeMain 的“延迟主光照 + 半分辨率高级效果 + 后处理合成”方向一致，所以不需要推倒重来。

## 3. DerivativeMain 目标拆解

DerivativeMain 的核心不是某一个单独 shader，而是一组低成本组合策略：

- **多缓冲延迟管线**：先存几何信息，再分阶段计算天空、云、GI/AO、反射、体积雾和后处理。
- **分辨率分层**：GI/AO、体积雾、云、Bloom、SSR 都可以 half-res 或低频更新。
- **时间复用**：TAA、云层棋盘格、天空历史、GI 历史都用历史帧提升质量。
- **物理启发但可调**：Bruneton 大气、HG/CS 相函数、Fresnel、GGX、Hammon diffuse、Blackbody 光源。
- **Minecraft 特化**：voxel lightmap、方块材质 ID、树叶/植物 SSS、矿石高亮、下界/末地差异、天气和云影。

当前 Mecraft 应采用以下简化目标：

- 不照搬 `colortex0-7`，而是建立项目自己的 `DeferredRenderTargets V2`。
- 不立即做完整历史帧 TAA，而是先做稳定的空间效果和可选单帧低成本版本。
- 不立即依赖完整 PBR 贴图，而是先做 block material registry + 少量代表材质。
- 不立即做完整 Bruneton LUT 管线，而是先做低分辨率天空捕获与解析散射近似；由于 LUT 资源已经具备，阶段 3 可并行做加载/采样验证。

## 4. 适配度评估

| 系统 | 当前基础 | 适配度 | 重构幅度 | 说明 |
| --- | --- | --- | --- | --- |
| 延迟管线架构 | 已有 Hybrid Deferred | 高 | 中 | 需要从“lighting 直接写屏”改为 scene lighting target，再进入 composite/post。 |
| G-buffer | 已有 albedo/normal/light/depth | 中高 | 中 | 需要增加 material packed target，法线可从 RGB 改 oct encoding 作为后续优化。 |
| 阴影 | 已有单 shadow map + warp + PCF | 高 | 小到中 | 可直接升级为 quartic warp、PCSS blocker search、屏幕空间阴影。 |
| 大气/天空 | 有 GameplaySkyRenderer 和 fog color，DerivativeMain LUT 资源已具备 | 中高 | 中到大 | 先做解析散射和天空捕获，同时验证 LUT 加载；完整 Bruneton 查询与光照耦合后置。 |
| 体积云 | 当前无真实体积云 | 中 | 中到大 | 可用现有 `noise2D.png` 做伪 3D 噪声，先 half-res 无历史。 |
| 体积雾/光 | 当前只有距离雾/大气透视近似 | 中高 | 中 | 半分辨率 ray marching 很适合当前 deferred depth。 |
| 水体 | 当前水为前向透明基础效果 | 中 | 中到大 | 需要 scene color/depth 读回、折射、SSR、专用水 shader。 |
| 光照/PBR | 当前为经验 diffuse + voxel light | 中 | 中 | 先扩展材质参数，再引入 BRDF；不用立刻 LabPBR。 |
| GI/AO | 有 SSAO 与 fake bounce | 中 | 中到大 | SSAO 可先升级，RSM GI 依赖 shadow color/normal target，后置。 |
| 后处理 | 有 HDR scene、bloom、tonemap | 高 | 小到中 | 可快速替换为 ACES/AgX、Bloom mip chain、Purkinje、CAS。 |
| TAA/历史帧 | 当前无 motion vector/history | 低 | 大 | 对稳定性有帮助，但工程牵引大，放到后期。 |
| 维度差异 | 当前主要主世界逻辑 | 中 | 中 | 下界/末地可用配置分支实现，不必照搬 shaderpack 宏体系。 |

总体重构级别：**中大型，建议 6 到 10 个阶段渐进实施**。

最小可见收益版本：阶段 0 到阶段 4。

完整 DerivativeMain-like V1：阶段 0 到阶段 9。

## 5. 总体架构方向

目标管线建议演进为：

```text
World HDR Scene FBO
  |
  +-- Sky / atmosphere background
  |
  +-- GBuffer terrain opaque + cutout
  |     albedo/material
  |     normal/ao
  |     voxel light/material id
  |     material packed
  |     depth
  |
  +-- Shadow map
  |     depth
  |     optional color/normal for colored shadow and RSM
  |
  +-- SSAO / DSSAO
  |
  +-- Sky capture / irradiance
  |
  +-- Deferred lighting -> SceneLightingTex
  |
  +-- SSR / reflection half-res or full-res
  |
  +-- Volumetric fog half-res -> VolumetricTex
  |
  +-- Transparent / water composite
  |
  +-- Postprocess
        auto exposure
        bloom mip chain
        ACES/AgX grade
        CAS + dither
        UI after world tonemap
```

关键改动：

- `Renderer::renderDeferredLightingPass()` 不再直接写到 captured framebuffer，而是写入 `SceneLightingTex`。
- 透明、水、体积雾、SSR 等 composite pass 统一读 `SceneLightingTex + depth + G-buffer`。
- `PostProcessRenderer` 应逐步与 deferred targets 协同，避免“世界 deferred”和“全局 postprocess scene FBO”互相绕路。
- 资源缺失策略保持明确：必需 shaderpack/noise/LUT 资源缺失时报错，调试资源可有 fallback。

## 6. 阶段 0：管线资源重构与诊断

目标：先把高级效果需要的资源槽、pass 顺序、debug 能力建好。

关键任务：

- 重构 `DeferredRenderTargets` 为 V2：
  - 保留当前 G-buffer。
  - 新增 `sceneLightingTex`，建议 `R11F_G11F_B10F` 或 `RGBA16F`。
  - 新增 `materialTex`，建议 `RGBA8` 或 `RG16F`，存 roughness/f0/emission/material id。
  - 新增 `halfResTex` 或专用 `volumetricTex`。
  - 为 SSR/反射预留 `reflectionTex`。
- 建立显式 pass 函数：
  - `renderSkyAtmospherePass`
  - `renderGBufferPass`
  - `renderShadowPass`
  - `renderSsaoPass`
  - `renderDeferredLightingPass`
  - `renderVolumetricPass`
  - `renderWaterCompositePass`
  - `renderPostProcessPass`
- 扩展 GPU timer：
  - GBuffer、Shadow、SSAO、Lighting、Volumetric、SSR、Water、Bloom、Tonemap、Transparent。
- 增加 debug views：
  - albedo、normal、voxel light、material、depth。
  - shadow depth、shadow factor、PCSS radius。
  - SSAO、volumetric RGB/A、reflection hit mask。
  - exposure、bloom mip、tonemap input/output。
- 整理 settings：
  - `AtmosphereSettings`
  - `CloudSettings`
  - `ShadowSettings`
  - `LightingSettings`
  - `WaterSettings`
  - `PostSettings`
  - `QualityPreset`

验收标准：

- resize 后所有 FBO complete。
- deferred lighting 可以输出到独立 HDR target，再由 postprocess 显示。
- 所有新增 pass 可独立关闭并回退到当前可用画面。

## 7. 阶段 1：DerivativeMain 阴影 V1

目标：优先吸收 DerivativeMain 性价比最高的阴影技术。

关键任务：

- 当前正式阴影路径保持 No Warp：
  - radial warp 与 DerivativeMain quartic warp 只作为 debug/研究模式保留。
  - 不再把 quartic warp 直接应用到当前单 shadow camera 的 clip-space 投影上；在高阴影距离下，这会让阴影边界出现随摄像机移动的异常弯曲。
- 后续补齐完整 shadowProjection 体系后，再恢复 DerivativeMain quartic warp：
  - 建立稳定的 light-space shadow projection，区分 shadow model-view、shadow projection、shadow projection inverse 与 runtime 采样坐标。
  - projection snapping 要在最终 shadowProjection/warped projection 域内完成，避免采样格点随摄像机旋转漂移。
  - warp distortion factor 需要参与 bias、normal offset、PCSS blocker search radius、PCF radius 和 shadow edge fade 的计算。
  - 需要明确 coverage 边界处理：cascade-like 覆盖、projection fade 或等价机制，不能让 warped projection 边界在地面上形成可见曲线。
  - 采样端和 shadow depth pass 必须使用同一套 shadowProjection 与 warp 参数，不允许各自近似。
  - 验证标准：低/高 shadow distance 下，方块直线阴影不会随视角弯曲，边缘不会出现跟随摄像机移动的凸点/拐点。
- PCSS 初版：
  - 8 tap blocker search。
  - 16 到 24 tap PCF。
  - 半影半径随 blocker/receiver 距离变化。
- 屏幕空间接触阴影：
  - 沿太阳方向 6 到 12 步。
  - 只补近距离细节，避免替代主 shadow map。
- 云影预留：
  - 先用 2D noise 投射到地面。
  - 后续接体积云 density。
- 植物/树叶透光：
  - 基于 material id 或 block registry 给 leaves/grass SSS factor。
  - 背光时提高绿色/暖色透射，阴影中不死黑。

验收标准：

- 方块接触处清晰，远离遮挡物边缘更柔和。
- 树叶和草地阴影不再大片纯黑。
- 阴影 pass 时间可量化，PCSS 可按 Low/Medium/High 调采样数。

## 8. 阶段 2：G-buffer 材质化与光照模型

目标：为 DerivativeMain 的 PBR、水、湿润、反射和自发光打底。

关键任务：

- 建立 `BlockMaterial` 注册表：
  - material id。
  - roughness。
  - f0/specular。
  - metallic。
  - emission。
  - sss/translucency。
  - porosity/wetness factor。
  - reflection flag。
- 扩展 G-buffer：
  - 当前 `GAlbedoMaterial.a` 的 emissive hint 不够，需要独立 material packed。
  - `GVoxelLight.b/a` 可考虑存 material id 或保留给 future data。
- 在 `deferred_lighting.fs` 引入：
  - Cook-Torrance GGX specular。
  - Hammon diffuse 或保守 Oren-Nayar/Hammon 可切换。
  - Blackbody 方块光色温。
  - emission 进入 HDR scene 和 bloom。
- Minecraft 特化：
  - 火把、岩浆、发光方块使用 registry emission。
  - 矿石高亮可先用颜色通道对比度检测，后续改 registry。
  - leaves/grass 使用 SSS factor。

验收标准：

- 石头、泥土、草、树叶、水、玻璃、矿石、火把在同一光照下响应不同。
- 无 PBR 贴图时仍由 registry 给出稳定默认值。
- 旧资源不会出现随机金属感或过曝自发光。

## 9. 阶段 3：天空、大气与天空捕获

目标：先做低成本 DerivativeMain-inspired 大气，同时把已解包的 Bruneton LUT 资源纳入加载验证；不在第一步就强行接完整 LUT 光照管线。

关键任务：

- 统一天空数据：
  - sun direction。
  - moon direction。
  - sun/moon visibility。
  - day/night factor。
  - rain/wetness/cloudiness。
  - horizon factor。
- 天空捕获：
  - 增加低分辨率 sky capture texture，例如 256x128 或 255x256。
  - 存天空辐射度，供环境光、反射、SSR miss、雾色使用。
- 解析大气散射 V1：
  - Rayleigh + Mie 近似。
  - HG phase，太阳方向前向散射。
  - horizon haze、日落暖色、夜间冷色。
  - Purkinje 夜视响应接后处理。
- 天体：
  - HDR 太阳盘。
  - 月亮盘与月光强度。
  - 程序化星空，Blackbody 色温可后置。
- 天空光照：
  - 先用 4 到 9 方向近似天空 irradiance。
  - 后续升级为 DerivativeMain 的 25 方向 SH L1/L2。
- LUT 资源验证：
  - 读取 `DerivativeMain/texture/Atmosphere/Transmittance.lut`。
  - 读取 `DerivativeMain/texture/Atmosphere/Scattering.lut`。
  - 读取 `DerivativeMain/texture/Atmosphere/Irradiance.lut`。
  - 读取 `DerivativeMain/texture/Atmosphere/Final.lut`。
  - 明确每个 LUT 的尺寸、格式、OpenGL texture 类型和采样坐标映射。

Bruneton LUT 后置条件：

- 当前解析大气无法满足日出/地平线/高空一致性，或需要与 DerivativeMain 的天空/雾/水反射进一步对齐。
- sky capture 和 lighting path 已稳定。
- 已确认 LUT 加载、格式、精度、采样映射和调试视图。

验收标准：

- 太阳方向、地平线、远景 haze、阴影色调统一。
- SSR miss 和水面反射能采样天空颜色。
- 夜间不只是降低亮度，而有月光、星空和蓝绿色暗视觉倾向。

## 10. 阶段 4：后处理 V2

目标：把当前后处理从“基础 bloom + Kappa-like grade”改成 DerivativeMain 风格的摄影链。

关键任务：

- Bloom mip chain：
  - 由当前 half-res ping-pong blur 升级为多级 downsample/upsample。
  - 标准 bloom 与雾 bloom 权重分离。
- 自动曝光：
  - luminance downsample。
  - log luminance。
  - temporal adaptation。
  - 变暗速度快于变亮速度。
  - manual/auto 切换。
  - 当前 4C 初版采用 GPU 小 mip 链降采样到 1x1，再 CPU 读回单个曝光数据做 temporal adaptation；后续如出现读回 stall，再升级为 GPU history texture 或 histogram/compute path。
- Tonemap：
  - ACES AcademyFit。
  - AgX Minimal。
  - 保留当前 ACES/Reinhard/Filmic 作为 debug。
- 色彩处理：
  - Purkinje effect。
  - 白平衡或 color temperature。
  - 高光去饱和。
  - 暗角可保守保留。
- Final：
  - CAS 锐化。
  - Bayer 或 noise dither。

暂不做：

- 完整 DoF。
- 运动模糊。
- TAA。

验收标准：

- 洞穴到室外曝光平滑，不白屏。
- 火把、太阳、水面高光能进入 bloom，但不污染整屏。
- UI 仍在世界 tonemap 之后绘制，不受 HDR 后处理污染。

## 11. 阶段 5：体积雾与体积光

目标：实现 DerivativeMain 高性价比的半分辨率体积雾/光。

阶段 5A 基础层：

- 先把现有 distance fog/aerial perspective 收敛成统一函数。
- deferred 不透明与 forward 透明/水共用同一套天空捕获雾色、日/月方向散射和户外 skylight mask。
- 仍依赖现有 fog 开关与距离参数；洞穴通过 outdoor sky mask 抑制空气透视，避免室内凭空泛白。
- 本阶段不是完整 half-res ray marching，后续再接散射/transmittance target。

阶段 5B Low：

- 使用已预留的 half-res HDR target 输出 RGB scattering / A transmittance。
- 基于 depth 重建 world position，做低成本指数高度雾和日/月方向散射。
- 合成到 scene lighting：`scene.rgb = scene.rgb * transmittance + scattering`。
- 本阶段暂不采样 shadow map，不加入 noise2D 伪 3D 噪声，不做深度感知双边上采样；这些进入 5C/5D。

阶段 5C Medium：

- `volumetric_fog.fs` 从单次高度雾升级为低步数屏幕空间 ray marching。
- 使用 `shader_noise2d` 构造两层 pseudo-3D density，并加入低速风场偏移。
- 雾输出仍保持 RGB scattering / A transmittance，合成公式不变。
- `volumetric_composite.fs` 加入基于 full-res depth 的 5-tap 空间上采样，降低 half-res 雾在方块边缘的漏光和白糊。
- 本阶段仍不采样 shadow map；shadowed volumetric light / 丁达尔光束进入 5D。

关键任务：

- 新增 half-res volumetric target：
  - RGB 为散射。
  - A 为 transmittance。
- 屏幕空间 ray marching：
  - Low：指数高度雾，无噪声。
  - Medium：高度雾 + 2 层伪 3D 噪声。
  - High：4 层 FBM。
  - Ultra：5 层 FBM，后置。
- 使用 `noise2D.png` 伪 3D 噪声：
  - Z 轴用偏移和双通道插值。
  - 风场随时间移动。
- 体积光：
  - 每步采样 shadow map。
  - Rayleigh/HG phase。
  - 4 级多重散射近似可做 High 模式。
- 上采样：
  - 深度感知双边上采样。
  - 先空间上采样，暂不依赖 TAA。
- 合成：
  - `scene.rgb = scene.rgb * transmittance + scattering`。
  - 输出雾透射率给 Bloom fog 作为后续增强。

验收标准：

- 日出/日落、森林阴影、水边有可见光束和空气厚度。
- half-res 边缘不会在方块边界明显漏光。
- Low/Medium/High 成本差异清晰。

## 12. 阶段 6：体积云与云影

目标：实现比 2D 云更强的 DerivativeMain-like 云层，同时保持性能可控。

推荐顺序：

1. 平面高空云。
2. 低步数 pseudo-volume 积云。
3. 云影接入地面光照。
4. 时间超分辨率/TAA 后置。

关键任务：

- 平面云：
  - Cirrus/Cirrocumulus 两类 2D 投影。
  - 黄金角旋转 FBM。
  - 自身阴影用 2 到 3 步光照采样。
- 体积云 V1：
  - 半分辨率或 quarter-res。
  - 16 到 24 步主 ray march。
  - 2 到 4 步 sunlight optical depth。
  - HG 前向 + 后向 + CS 峰值近似。
  - Beer-Powder 散射。
  - 低透射率早退。
- 天气系统：
  - 基于 world day/time 的平滑随机天气。
  - cloud coverage、wetness、storm factor。
- 云影：
  - 在 deferred lighting 中采样云 density。
  - 先 2D/两层采样，后续接真实体积云数据。

验收标准：

- 云体受太阳方向影响，有亮边、暗底和大气透视。
- 地面云影缓慢移动，直射光随云层减弱。
- Medium 预设下成本可接受，Low 可回退平面云。

## 13. 阶段 7：水体 V2

目标：把当前透明水升级为 DerivativeMain-inspired 程序化水体。

关键任务：

- 水体专用渲染路径：
  - 保留透明前向排序，但水 shader 必须读取 scene color/depth。
  - 或建立 `WaterCompositePass` 在 opaque scene 后合成。
- 程序化波浪：
  - 内置噪声波浪优先。
  - 4 层 `WaterHeight()`，使用 `noise2D.png`。
  - `dFdx/dFdy` 或距离衰减降低远处高频。
  - Gerstner/PhysicsOceans 风格非线性波浪后置。
- 法线与视差：
  - 有限差分水面法线。
  - 低步数水面 parallax，近距离启用。
- Fresnel：
  - 使用精确 dielectric Fresnel，IOR 1.33。
  - 掠射角反射增强。
- 折射：
  - 基于水面法线偏移 screen coord。
  - 深度验证，避免采样水面前物体。
- SSR V1：
  - 8 到 16 步 ray march。
  - 6 步二分细化可选。
  - miss fallback 到 sky capture。
- 水雾/水下：
  - RGB 吸收，红光吸收更强。
  - 水下 tint、visibility、forward scattering。
- 焦散后置：
  - 初版用水面法线投影 + depth mask。
  - 高级版再考虑 shadow pass 写 caustics。

验收标准：

- 水面近处有非重复波纹，远处不闪烁。
- 低视角有明显 Fresnel 和天空反射。
- 岸边浅水、深水、水下颜色有层次。
- SSR miss 不出现黑块或屏幕边缘拉伸。

## 14. 阶段 8：SSR、反射与湿润材质

目标：让水、玻璃、金属和雨天表面共享一套反射逻辑。

关键任务：

- SSR 通用化：
  - depth/normal ray march。
  - roughness 控制步长、采样方向和滤波半径。
  - hit mask/debug view。
- GGX VNDF：
  - 先为粗糙反射使用低成本扰动。
  - 高质量模式使用 VNDF importance sampling。
- 反射滤波：
  - normal + depth + spatial 三权重双边滤波。
  - half-res 反射可选。
- 湿润效果：
  - wetness 分布噪声。
  - porous material 反照率变暗。
  - roughness 降低、F0 提高。
  - 雨滴涟漪使用可选 ripple texture，资源缺失时关闭该子功能。

验收标准：

- 雨天石头、草地、木头、叶子、水面质感明显变化。
- 反射边缘稳定，不在深度断层处大面积泄漏。
- 反射成本可通过采样数、分辨率、滤波开关控制。

## 15. 阶段 9：GI 与高级 AO

目标：补足阴影内的环境反弹和洞穴入口层次。

推荐顺序：

1. SSAO/DSSAO V2。
2. fake bounced light V2。
3. RSM GI 原型。
4. RSM GI spatial filter。
5. 历史帧 GI 后置。

关键任务：

- SSAO 升级：
  - 黄金角螺旋采样。
  - depth/normal aware。
  - half-res + bilateral upsample 可选。
- fake bounce：
  - 参考 DerivativeMain 的反向法线 + sun/up vector。
  - 根据 albedo/sky light 给阴影区少量彩色反弹。
- RSM GI 条件：
  - shadow pass 需要写 albedo/normal/skylight。
  - 阴影贴图空间采样周围点。
  - GI radius/samples/brightness 可调。
- 空间滤波：
  - normal/depth/spatial 三权重。
  - 先无历史帧。

验收标准：

- 树下、洞口、山坡背光面有环境色和反弹，不只是变黑。
- GI 可独立关闭，关闭后 fake bounce 仍提供稳定低成本回退。

## 16. 阶段 10：TAA、历史帧与高级后处理

目标：在主要效果稳定后，再引入时间复用。

前置条件：

- scene lighting target 已稳定。
- depth/G-buffer 历史可保留。
- 相机 jitter 和 motion vector 方案明确。
- UI 与 world postprocess 边界清晰。

关键任务：

- motion vector：
  - 当前/上一帧 viewProj。
  - dynamic objects 后续补齐。
- TAA：
  - Catmull-Rom 重采样。
  - YCoCgR 或亮度/色度分离。
  - 方差裁剪。
  - Reinhard 域混合。
- 云/体积雾时间复用：
  - checkerboard 或 interleaved sampling。
  - 历史帧按 motion/depth 重投影。
- 高级后处理：
  - DoF。
  - motion blur。
  - chromatic aberration 可选。

验收标准：

- TAA 不造成明显拖影、UI 污染、手持物模糊。
- 云、SSR、体积雾噪声明显降低。
- 可一键关闭 TAA 回到空间方案。

## 17. 阶段 11：维度、天气与内容整合

目标：让光影系统从“晴天主世界 demo”变成可长期扩展的世界渲染层。

关键任务：

- 天气：
  - rain strength。
  - wetness。
  - thunder factor。
  - cloud density。
  - fog density。
- 下界：
  - 无太阳阴影或极弱方向光。
  - 生物群系雾色。
  - 方块光/岩浆更主导。
- 末地：
  - 紫黄天空渐变。
  - 更强环境雾。
  - boss/dark mode 后置。
- 实体/掉落物/手持物：
  - 接同一套 sun/sky/fog/post settings。
  - 可接收 shadow factor 或低成本环境阴影。
  - emission/bloom 标记。
- 透明物：
  - water/glass/leaves/particles 明确各自材质模型。

验收标准：

- 白天、夜晚、雨天、洞穴、水下、下界、末地都有明确差异。
- 手持物、掉落物和实体不再像后贴的 LDR 图层。

## 18. 质量预设

建议建立四级预设：

| 功能 | Low | Medium | High | Ultra |
| --- | --- | --- | --- | --- |
| 阴影 | 硬阴影/低 PCF | PCF | PCSS | PCSS 高采样 + SSS |
| SSAO | 关闭或低采样 | 全分辨率低采样 | half-res DSSAO | DSSAO + filter |
| 天空 | 解析天空 | sky capture + LUT 加载验证 | SH 天空光 | Bruneton LUT 查询接入 |
| 云 | 平面云 | 低步体积云 | half-res 体积云 | 时间超分辨率 |
| 体积雾 | 关闭 | Low/Medium | High half-res | High + temporal |
| 水体 | Fresnel + fog | 折射 + 波浪 | SSR | SSR + caustics |
| 反射 | 关闭 | 水面 SSR | 通用 SSR | 滤波/粗糙反射 |
| 后处理 | Tonemap | Bloom mip + ACES | Auto exposure + AgX | CAS + advanced effects |
| TAA | 关闭 | 关闭 | 可选 | 可选 |

## 19. 推荐实施顺序

短期最优先级：

1. 阶段 0：管线资源重构与诊断。
2. 阶段 1：DerivativeMain 阴影 V1。
3. 阶段 2：G-buffer 材质化与光照模型。
4. 阶段 4：后处理 V2。
5. 阶段 3：天空、大气与天空捕获。

中期：

6. 阶段 5：体积雾与体积光。
7. 阶段 7：水体 V2。
8. 阶段 6：体积云与云影。
9. 阶段 8：SSR、反射与湿润材质。

后期：

10. 阶段 9：GI 与高级 AO。
11. 阶段 10：TAA、历史帧与高级后处理。
12. 阶段 11：维度、天气与内容整合。

原因：

- 先重构 pass/target，否则后续高级效果会互相绕路。
- 阴影、材质光照、后处理能最快提升观感，且风险低于体积云/TAA。
- 天空捕获是 SSR、水体、雾、环境光的共同依赖，应早于水体和体积效果稳定下来。
- TAA 是稳定性工具，但会牵动运动向量、历史资源、UI 边界和动态物体，应该后置。

## 20. 资源需求

已具备：

- `DerivativeMain/texture/Noise2D.png`：云、体积雾、水波、湿润分布等通用噪声。当前项目已有 `assets/textures/shaderpacks/noise2D.png`，后续需确认是否与 DerivativeMain 版本一致。
- `DerivativeMain/texture/Bayer256.png`：有序抖动、采样旋转、低成本 dither，可替代路线中原先提到的必需 blue-noise 贴图。
- `DerivativeMain/texture/RippleNormal.png`：雨滴涟漪法线，可用于水面和湿润表面。
- `DerivativeMain/texture/Atmosphere/Transmittance.lut`
- `DerivativeMain/texture/Atmosphere/Scattering.lut`
- `DerivativeMain/texture/Atmosphere/Irradiance.lut`
- `DerivativeMain/texture/Atmosphere/Final.lut`

可选新增：

- `assets/textures/shaderpacks/blue_noise.png`：不是 DerivativeMain 必需资源。若后续 SSR、体积雾、云在运动中出现明显条纹或有序抖动痕迹，再作为质量优化加入。
- 若不直接引用 `DerivativeMain/texture`，需要把 `Bayer256.png`、`Noise2D.png`、`RippleNormal.png` 和 Atmosphere LUT 复制或导入到项目资源目录，并在 `ResourceMgr` 中建立稳定资源名。

不急需：

- 完整 LabPBR 贴图资源包。
- 3D noise texture。
- PhysicsOceans 相关贴图。
- Distant Horizons 深度/阴影资源。

## 21. 主要风险

- **一次性照搬 shaderpack 架构**：DerivativeMain 的多 `colortex` 布局依赖 Iris/OptiFine 运行模型，原样移植会与当前 C++ renderer 资源管理冲突。应转译为本项目自己的 FBO/pass。
- **历史帧过早引入**：TAA、云历史、GI 历史都需要运动向量和 reprojection。过早做会掩盖基础 pass 的错误。
- **G-buffer 通道不足**：材质、SSS、emission、roughness、f0、material id 会很快挤爆当前布局。阶段 0/2 必须先明确通道。
- **透明路径割裂**：水、玻璃、树叶、粒子如果继续只走旧前向光照，会和 deferred 世界明显不一致。
- **体积效果成本膨胀**：云、雾、SSR 都是采样密集型，必须 half-res、早退、质量预设、GPU timer 同步推进。
- **色彩链路重复补偿**：曝光、tonemap、天空亮度、雾色、Bloom 不应互相用魔法参数补偿。所有世界颜色保持 linear HDR，UI 后绘制。
- **资源规范不清**：Noise2D、Bayer256、Atmosphere LUT、RippleNormal、PBR 资源必须明确路径、格式、wrap/filter、缺失行为。blue noise 只是可选增强，不应成为阶段 0 到阶段 5 的阻塞项。

## 22. DerivativeMain-like V1 完成标准

达到以下条件，可认为完成 DerivativeMain-like V1：

- 管线已从“deferred lighting 直接写屏”升级为 scene lighting/composite/post 分离。
- G-buffer 包含稳定的材质参数，能区分 roughness、f0、emission、SSS、material id。
- 阴影有 quartic warp、PCSS/contact shadow、植物透光和云影基础版本。
- 天空有解析散射或 sky capture，环境光、反射、雾和水体共享同一天空数据。
- 后处理有自动曝光、Bloom mip chain、ACES/AgX、Purkinje、dither/CAS。
- 体积雾以 half-res 渲染，并能与阴影、太阳方向、天气联动。
- 水体具备程序化波浪、Fresnel、折射、深度吸收、水下雾和 SSR fallback。
- 至少 Medium 预设能在目标分辨率下稳定运行，且每个昂贵效果可单独关闭。
- UI、手持物、实体、透明物与世界 HDR/雾/光照边界明确，不出现明显 LDR 贴片感。

## 23. 参考文档

DerivativeMain 分析文档：

- `docs/accomplished/延迟渲染管线架构技术分析.md`
- `docs/accomplished/大气散射系统技术分析.md`
- `docs/accomplished/体积云渲染系统技术分析.md`
- `docs/accomplished/体积光与体积雾系统技术分析.md`
- `docs/accomplished/水体渲染系统技术分析.md`
- `docs/accomplished/阴影映射系统技术分析.md`
- `docs/accomplished/光照模型与全局照明技术分析.md`
- `docs/accomplished/后处理管线技术分析.md`
- `docs/accomplished/表面渲染与材质系统技术分析.md`

当前项目管线参考：

- `docs/render_pipeline_notes.md`
- `src/renderer/Renderer.cpp`
- `src/renderer/Renderer.h`
- `src/renderer/DeferredRenderTargets.cpp`
- `src/renderer/PostProcessRenderer.cpp`
- `assets/shaders/chunk_gbuffer.fs`
- `assets/shaders/deferred_lighting.fs`
- `assets/shaders/postprocess.fs`
