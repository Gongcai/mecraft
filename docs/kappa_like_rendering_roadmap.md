# Mecraft Kappa-like 光影技术路线设计大纲

> 目标：在当前 OpenGL 4.5 Hybrid Deferred 管线基础上，逐步逼近 Kappa/BSL/Complementary 这类 Minecraft shader pack 的完整视觉效果。本文作为后续渲染工作的长期设计大纲，不要求一步到位，而是按“先打基础、再叠高级效果”的顺序推进。

## 1. 当前基线

当前项目已经从纯前向渲染推进到混合延迟渲染，具备继续实现 shader pack 效果的基础：

- OpenGL 4.5 core 作为目标运行环境，新 shader 可使用 `#version 450 core`。
- Terrain opaque/cutout 已进入 G-buffer，透明、水、粒子、手持物、UI 保留前向路径。
- 已有 HDR scene color、SSAO、Bloom、Tonemap/Postprocess、深度雾、水下色调等基础 pass。
- 已实现单张近景太阳阴影，并完成稳定化：
  - light-space texel snapping。
  - Kappa-like radial shadow warp。
  - R2 low-discrepancy PCF。
  - 手动 bilinear PCF。
  - normal offset、slope/constant bias 调参。
- 已实现 Kappa-like 程序化色彩链路 V1：
  - 线性 HDR 工作流。
  - ACES-like highlight compression。
  - RRT sweeteners 近似。
  - film toe/mid/shoulder 近似。
  - split tone、vignette、vibrance、shadow/sun/sky tint。
- 已有 `assets/textures/shaderpacks/noise2D.png`，可作为通用噪声/抖动/云雾采样资源。

参考文档：

- `docs/Kappa光影阴影系统技术分析.md`
- `docs/Kappa光影HDR与色调映射技术分析.md`
- `docs/render_pipeline_notes.md`

## 2. 与完整 Kappa 效果的主要差距

当前画面已经有早期 Minecraft 光影效果，但距离 Kappa 仍有明显差距，核心原因不是单个后处理参数，而是整条光照、天空、大气、材质、水体和后处理链路还不完整。

主要缺口按影响排序：

1. 天空、大气和云层仍偏基础，缺少 shader pack 的 horizon haze、太阳方向散射、云层受光、远景空气感。
2. Bloom/exposure 还不是完整 HDR 摄影链路，缺少自动曝光、分层 bloom、太阳/天空 glare 的统一控制。
3. 色彩链路只完成 Kappa-like 近似，尚未建立完整 ACES/AP1 工作空间、Kappa 分段曲线和环境相关调色。
4. 阴影已经稳定，但还缺少 VPS/PCSS 级别半影、半透明彩色阴影、云影、植物透光和更精细接触阴影。
5. 水体仍是基础前向效果，缺少 Kappa-style 程序化水波高度场、有限差分法线、视差、Fresnel、吸收散射、SSR/反射和焦散。
6. 材质仍接近原版，没有 PBR/Specular/Normal/Roughness/Emission/Porosity 等材质信息。
7. 间接光仍主要来自 voxel lightmap、SSAO 和调色，缺少 fake bounce、SSGI 或 voxel GI 近似。
8. 透明、实体、手持物、掉落物尚未完全统一到世界光照和大气链路。
9. 缺少系统化 debug view、GPU timer、质量预设与视觉回归场景。

## 3. 总体技术柱

完整 Kappa-like 效果建议由以下技术柱组成：

- **Hybrid Deferred Core**：保持当前混合延迟架构，地形和大部分不透明几何走 G-buffer，透明和特殊对象保留前向合成。
- **Physically Inspired Lighting**：不追求严格物理正确，但所有颜色、阴影、雾、天空和材质要围绕线性 HDR 光照统一。
- **Atmosphere First**：Minecraft 光影包的“味道”很大一部分来自天空、大气透视、云和太阳方向散射，应优先于 PBR/SSR。
- **Procedural Color Pipeline**：Kappa 没有依赖 3D LUT，主要通过程序化 ACES/film/grading 链路实现调色，因此本项目也优先走 shader 内程序化色彩。
- **Material Upgrade Later**：PBR 是后期质感核心，但必须等 G-buffer、光照、天空和后处理稳定后再扩展，否则容易放大当前基础问题。
- **Quality Scalable**：所有昂贵效果必须可单独关闭，并逐步建立 Low/Medium/High/Ultra 质量层级。

## 4. 阶段 0：稳固当前渲染基础

目标：保证已有 Hybrid Deferred 管线可靠、可调、可观察，为后续高级效果提供稳定地基。

关键任务：

- 整理 `RenderPipelineSettings`，把 shadow、color、atmosphere、water、bloom、debug 设置分组。
- 为主要 pass 增加或完善 GPU timer：
  - GBuffer
  - Shadow
  - SSAO
  - DeferredLighting
  - TransparentForward
  - Bloom
  - Tonemap
  - UI
- 增加 debug views：
  - G-buffer albedo/normal/light/depth。
  - shadow map、shadow factor、PCF radius。
  - SSAO、contact shadow。
  - exposure/luminance、bloom mask。
  - aerial perspective/fog factor。
- 明确资源缺失策略：shaderpack 必需资源缺失时直接报错，不做隐藏 fallback。
- 保留 `ForwardLegacy` 或调试路径，用于视觉对比和回归定位。

验收标准：

- 任意窗口 resize 后 FBO complete，无黑屏和资源泄漏。
- 各 pass 可在 dashboard 中单独开关。
- 视觉问题能通过 debug view 快速定位到 shadow、lighting、fog、postprocess 或 texture 阶段。

## 5. 阶段 1：天空、大气与云层 V2

目标：先把画面的“空气”和“时间感”做出来，这是最接近 shader pack 观感的下一步。

关键任务：

- 重构天空颜色模型：
  - 更深的 zenith blue。
  - 暖色 horizon haze。
  - 日出/日落橙紫色散射。
  - 夜间冷色环境光和月光。
- 统一太阳/月亮方向数据：
  - sun direction。
  - moon direction。
  - sun visibility。
  - day/night factor。
  - sunset factor。
- 增强 aerial perspective：
  - 按距离、视线高度、视线方向、太阳方向混入大气色。
  - 地形、水面、透明物、实体尽量使用同一套远景 haze。
  - 避免只在后处理阶段简单按深度变白。
- 云层 V2：
  - 使用 `noise2D.png` 构造多 octave 云形态。
  - 云顶受太阳暖光，云底偏冷/偏暗。
  - 远处云混入 horizon haze。
  - 先做 2D/pseudo-volume 云层，不做真正体积云。
- 太阳视觉：
  - 太阳盘亮度进入 HDR。
  - 太阳附近轻微 glare。
  - 日出日落时太阳周围色温明显变化。

建议新增/修改资源：

- 当前 `noise2D.png` 可用于云形和雾噪声。
- 若后续要更接近 Kappa 体积云，可再准备 tileable blue noise 或 3D noise，但本阶段不强制。

验收标准：

- 正午画面不只是“原版蓝天”，而是具有清晰的深蓝天顶和远处空气透视。
- 日出/日落时远景山体、水面、云层出现暖色 haze，近景仍保留材质颜色。
- 夜晚暗部偏冷但不死黑，天空和地形色调统一。

## 6. 阶段 2：曝光、Bloom 与 Glare V2

目标：把 HDR 亮度链路从“固定参数后处理”推进到 shader pack 摄影感。

关键任务：

- 实现 luminance downsample chain：
  - 从 HDR scene 计算平均/中位亮度近似。
  - 支持 log luminance，避免太阳或火把单点把曝光拉崩。
- 自动曝光：
  - temporal adaptation。
  - min/max exposure clamp。
  - indoor/outdoor 可接受的曝光范围。
  - dashboard 支持 auto/manual 切换。
- Bloom V2：
  - 多尺度 bloom pyramid。
  - pre-tonemap HDR 混合。
  - threshold/knee 控制。
  - 火把、天空、太阳、水面高光分别可调。
- Glare/Sun rays 初版：
  - 根据太阳屏幕位置和遮挡深度做轻量太阳眩光。
  - 可先做 screen-space radial blur 的低成本版本。

验收标准：

- 从洞穴走到室外时曝光平滑适应，不突然白屏或黑屏。
- 太阳和高亮天空有柔和溢光，但不会污染整屏。
- 火把 bloom 有局部温暖感，不把夜晚整体染黄。

## 7. 阶段 3：Kappa-like 色彩链路 V2/V3

目标：从当前近似调色推进到更接近 Kappa 的程序化 film/ACES 链路。

Kappa 的关键点：

- Kappa 不依赖 3D LUT，而是在 shader 内通过色彩空间矩阵、曲线、film emulation、RRT sweeteners 和后置 grading 实现风格。
- LUT 仍然可用于其他 shader pack 风格，但不是复刻 Kappa 的必要条件。

关键任务：

- 引入更完整的色彩空间变换：
  - linear sRGB → AP1/ACEScg 近似。
  - AP1 grading。
  - AP1 → output linear/sRGB。
- 更贴近 Kappa 的 tonemap operator：
  - ACES compression LMT。
  - RRT sweeteners。
  - 分段曲线 toe/linear/shoulder。
  - 高光 desaturation。
- 环境相关调色：
  - 正午：干净、通透、稍暖。
  - 日落：暖高光、紫冷阴影、低太阳角度增强 haze。
  - 夜晚：暗部冷色、低饱和、保留火把暖色。
  - 洞穴：低照度下轻微 Purkinje effect 或冷暗部响应。
- 保留可调参数，但降低“直接拉饱和度/对比度”的权重：
  - vibrance 只对低饱和颜色更敏感。
  - contrast 更接近 film curve，而不是简单乘法。
  - saturation 不应成为主要风格来源。
- 加入轻量 dithering：
  - 使用 `noise2D.png` 或 blue-noise 风格采样。
  - 主要用于天空、雾、tonemap 后的 banding 抑制。

验收标准：

- 关闭 shaderpack grading 时接近当前基础画面。
- 开启后不是简单高饱和/高对比，而是高光柔和、暗部有颜色、远景有空气层次。
- UI、物品栏和 dashboard 不受世界 tonemap 错误影响。

## 8. 阶段 4：阴影 V4：半影、彩色阴影与植物透光

目标：在当前稳定阴影基础上补齐 Kappa 阴影系统的高级观感。

关键任务：

- VPS/PCSS-like 半影：
  - blocker search 估计遮挡距离。
  - 根据 blocker/receiver 距离扩大 PCF radius。
  - 近接触阴影保持清晰，远离遮挡物逐渐柔化。
- Contact Shadow V2：
  - 屏幕空间沿光方向 ray march。
  - 只补小尺度细节，不替代主阴影。
  - 对草、花、树叶、水边台阶做特殊限制，避免闪烁和过黑。
- 半透明彩色阴影：
  - 水、彩色玻璃、树叶可投射带颜色/透射的阴影。
  - 需要额外 shadow color target 或透明 shadow pass。
- 植物和树叶透光：
  - leaves/grass 使用 translucency 或 SSS 近似。
  - 背光时边缘透亮，阴影内仍有绿色/暖色穿透。
- 云影：
  - 云层噪声投射到地面，随风缓慢移动。
  - 与太阳角度和天气强度绑定。

验收标准：

- 方块阴影接触点清楚，远端边缘柔和。
- 树叶下方不再是一整片硬黑，而有透光和颜色变化。
- 水/玻璃对阴影有可见影响，但不引入明显闪烁。

## 9. 阶段 5：水体 V2/V3

目标：把当前水面从基础透明/雾效果升级到 Kappa-style 程序化水体。Kappa 没有依赖传统 `water_normal_0/1.png` 滚动法线贴图，而是以 `noise2D.png` 驱动水波高度场，再从高度场生成法线，并在后续 pass 中叠加反射、折射、体积水和焦散。

已具备资源：

- `assets/textures/shaderpacks/noise2D.png`

Kappa-like 关键路线：

- 程序化水波高度场：
  - 实现 `waterWaves(pos, matID)` 风格函数。
  - V2 使用多 octave Gerstner 波，叠加 `noise2D` domain warp，降低重复感。
  - 保留噪声波模式作为质量/风格选项。
  - 波长、振幅、方向旋转、天气强度和距离衰减全部可调。
- 水面顶点扰动：
  - 水面 vertex shader 中加入低频 Y 位移。
  - 近处提供真实轮廓起伏，远处降低幅度，避免 chunk/water mesh 接缝明显。
  - 冰或静态水材质可关闭动画。
- 水面法线：
  - 不采样水法线贴图，改为对 `waterWaves()` 做有限差分。
  - 差分步长随视距增大，远处自动过滤高频细节，减少闪烁。
  - 法线强度与波浪高度分开调参，避免水面过碎。
- 水面视差：
  - 实现 6~8 步高度场 parallax。
  - 只在近中距离启用，远处关闭或降低步数。
  - 低视角增强波纹深度，但必须限制采样偏移避免破边。
- Fresnel：
  - 低视角反射增强。
  - 正视角更透明。
- 深度吸收：
  - 根据水深从浅蓝/绿色过渡到深色。
  - 岸边浅水保留清晰边界。
- 水下：
  - underwater fog/tint。
  - 视距衰减。
  - 体积水初版使用 RGB 吸收 + 散射，红光吸收更强，形成蓝绿色水体。
- 反射：
  - 初版 screen-space reflection。
  - SSR 失败时明确使用 sky/atmosphere reflection 或环境色，不做隐藏贴图 fallback。
  - 后续可加入低分辨率 reflection capture。
- 折射：
  - 使用水折射率约 1.33 的屏幕空间折射。
  - 使用表面扰动法线和几何法线双限制，避免折射采样跑到水面前方。
- 焦散：
  - 后置到 V3。
  - 基于水面法线和太阳方向做投影焦散，优先在浅水/岸边可见。
- 岸边泡沫：
  - depth edge foam。
  - 噪声扰动。

验收标准：

- 水面近处有程序化非重复波纹，远处不透明消失问题不复现。
- 移动时波纹稳定，不出现明显网格接缝或高频闪烁。
- 低视角有明显 Fresnel 反射。
- 水体颜色与天空/雾/调色一致，不再像独立贴片。
- 不需要额外水 normal 贴图即可达到 Kappa-style 水波基础效果。

## 10. 阶段 6：PBR 材质基础

目标：让材质从“原版贴图 + 光照”升级到 shader pack 的材质响应。

关键设计：

- 扩展 block material registry：
  - roughness。
  - metallic/specular。
  - emission。
  - normal strength。
  - porosity/wetness 可后置。
- 扩展 G-buffer：
  - 保留 albedo、normal、voxel light、depth。
  - 增加 material 参数 target 或打包到现有 target。
  - 明确每个通道定义，避免后续无法兼容。
- 支持资源包式 PBR：
  - 可参考 LabPBR/OldPBR 约定。
  - 若项目暂时没有 PBR 贴图，则使用 registry 默认值，且默认值应显式可查。
- 光照模型：
  - diffuse 使用 Lambert 或 Oren-Nayar 近似。
  - specular 使用 GGX/Schlick Fresnel。
  - emission 进入 HDR 和 bloom。

验收标准：

- 石头、泥土、草、水、玻璃、金属类方块在同一光照下有不同响应。
- 火把/发光方块能自然进入 bloom。
- 旧材质在未配置 PBR 参数时仍可预测，不出现随机质感。

## 11. 阶段 7：间接光与 GI 近似

目标：补足 shader pack 中常见的环境反弹光、洞穴暗部层次和局部色彩反射。

可选路线：

- SSAO/DSSAO 增强：
  - depth/normal aware。
  - horizon-based AO。
  - temporal accumulation 后置。
- Fake bounce：
  - 根据地表 albedo、sky visibility、sun visibility 给阴影区少量暖/冷反弹。
  - 草地附近阴影略带绿色，沙地略暖。
- SSGI：
  - screen-space ray march。
  - half-res + bilateral upsample。
  - temporal denoise 可后置。
- Voxel GI：
  - 远期路线。
  - 复用 chunk/voxel 数据构造低分辨率 radiance volume。
  - 成本和复杂度明显高于 SSGI。

建议顺序：

1. Fake bounce。
2. SSAO/DSSAO V2。
3. SSGI 初版。
4. Voxel GI 研究。

验收标准：

- 阴影区域不再只是变暗，而有环境色和轻微反弹光。
- 洞穴入口、树下、山坡背光面有层次，不死黑也不灰白。

## 12. 阶段 8：SSR、反射与高光系统

目标：为水面、湿润方块、金属/玻璃类材质提供 shader pack 级反射。

关键任务：

- SSR ray march：
  - 基于 depth/normal。
  - hierarchical depth 或 mip depth 可后置。
  - roughness 控制步长和模糊。
- 反射 fallback 策略必须显式：
  - SSR hit：使用屏幕空间反射。
  - SSR miss：使用天空/环境反射颜色。
  - 不隐藏使用错误资源。
- Reflection composition：
  - 水面 Fresnel。
  - 玻璃弱反射。
  - 金属/PBR 材质高光。
- Specular anti-aliasing：
  - 对 blocky normal 和低分辨率 normal map 限制闪烁。

验收标准：

- 水面能反射天空和近处地形轮廓。
- 反射失败区域不出现黑洞、断层或明显屏幕边缘拉伸。

## 13. 阶段 9：天气、体积雾与体积光

目标：让世界状态影响整体光照和空气，而不是只有晴天画面。

关键任务：

- 天气参数：
  - rain strength。
  - wetness。
  - thunder factor。
  - cloud density。
- 体积雾初版：
  - froxel 或 screen-space ray marching 二选一。
  - 阴影采样参与雾体光照。
  - 日出/日落低太阳角度增强体积感。
- God rays：
  - 从太阳方向和深度遮挡生成。
  - 可先做低成本 screen-space radial light shafts。
- 云影与天气联动：
  - 多云时降低直射光。
  - 雨天提高天空漫反射、降低对比。

验收标准：

- 雨天/阴天/黄昏的光照和色调有明显差异。
- 体积效果不会在普通移动时产生明显条纹和抖动。

## 14. 阶段 10：实体、手持物与透明路径统一

目标：避免世界已经是 shader pack 风格，但手持物、掉落物、实体仍像原版直接贴图。

关键任务：

- 为实体、掉落物、手持物添加专用 shader：
  - 接收同一套 sun/sky/fog/color settings。
  - 支持基础阴影或至少接收 shadow factor。
  - 支持 emission/bloom 标记。
- 透明物统一：
  - water/glass/leaves/particles 明确各自光照模型。
  - 透明排序仍保留前向路径，但颜色、雾和曝光要与世界一致。
- 第一人称特殊处理：
  - 避免手持物被世界雾完全吞掉。
  - 允许轻微独立曝光/亮度补偿，但要可调。

验收标准：

- 手持方块在白天、夜晚、洞穴中都与世界光照一致。
- 掉落物和粒子不再像后贴的 LDR 元素。

## 15. 阶段 11：性能、质量预设与工程化

目标：在效果逐步完整后，系统化控制成本，避免后续优化无从下手。

关键任务：

- GPU timer 常驻：
  - Dashboard 显示 pass 时间。
  - 支持平均值/峰值。
- 质量预设：
  - Low：基础阴影、无 SSR、低 bloom、基础天空。
  - Medium：软阴影、SSAO、Bloom V2、天空 V2。
  - High：VPS、云影、水 normal、SSR half-res、自动曝光。
  - Ultra：更高采样、更远阴影、更高质量体积雾/SSGI。
- 动态分辨率或 half-res pass：
  - SSAO。
  - SSR。
  - Volumetric fog。
  - Bloom。
- 视觉回归场景：
  - 正午森林。
  - 日落海岸。
  - 洞穴火把。
  - 高山远景。
  - 水下。
  - 雨天/夜晚。

验收标准：

- 1080p/1440p、render distance 16 下每个效果的成本可量化。
- 任意昂贵效果能单独关闭并稳定回退到可用画面。

## 16. 推荐实施顺序

建议实际推进顺序如下：

1. 阶段 0：debug/timer/settings 整理。
2. 阶段 1：天空、大气、云层 V2。
3. 阶段 2：自动曝光、Bloom V2、太阳 glare。
4. 阶段 3：Kappa-like 色彩链路 V2/V3。
5. 阶段 4：阴影 VPS/彩色阴影/植物透光。
6. 阶段 5：水体 V2/V3。
7. 阶段 6：PBR 材质基础。
8. 阶段 7：Fake bounce、DSSAO、SSGI。
9. 阶段 8：SSR 与反射系统。
10. 阶段 9：天气、体积雾、体积光。
11. 阶段 10：实体、手持物、透明路径统一。
12. 阶段 11：质量预设、性能调优、回归场景。

短期最优先级：

1. 天空/大气/云层 V2。
2. 自动曝光 + Bloom V2。
3. 色彩链路 V2。
4. 阴影 VPS 和植物透光。

原因：这些最能改变“原版 + 阴影”的观感，且不依赖完整 PBR 资源。

## 17. 资源需求清单

已具备：

- `assets/textures/shaderpacks/noise2D.png`

后续建议补充：

- 水体：
  - Kappa-like 主路线不需要传统水 normal 贴图，优先复用 `noise2D.png` 生成高度场和法线。
  - 若未来想支持其他 shader pack 风格，可选加入 `water_normal_0.png`、`water_normal_1.png` 作为替代水面模式。
- 可选噪声：
  - blue noise texture，用于 dithering、SSR/SSGI/volumetric 随机采样。
  - 3D noise 或 tiled volume noise，用于更高质量体积云/体积雾。
- PBR：
  - 若走资源包兼容路线，需要约定 normal/specular/emission texture 命名。
  - 若先走内置材质表，不需要新贴图，但需要完整 block material 参数表。

## 18. 非目标与延期项

以下内容不建议在基础完成前优先做：

- 完整 CSM：当前 Kappa 分析显示单级联也能做高质量，先继续完善单 shadow map。
- 完整体积云：先做 pseudo-volume 云层，等天空和曝光稳定后再升级。
- 完整 voxel GI：工程量大，应在 SSGI/fake bounce 之后评估。
- 全材质 PBR 资源包：先建立材质接口和少量代表方块，再扩展覆盖面。
- TAA：能改善 SSR/SSGI/volumetric 稳定性，但会显著影响运动清晰度和工程复杂度，后置。

## 19. 主要风险

- **色彩过度调参化**：如果依赖 saturation/contrast/exposure 手调，会很难稳定复刻 shader pack。应优先完善 HDR、sky、atmosphere、film curve。
- **透明路径割裂**：水、玻璃、手持物、掉落物若不接收统一雾和色彩，会破坏整体观感。
- **阴影采样成本膨胀**：VPS、contact shadow、透明阴影都可能增加 lighting pass 成本，需要质量开关和 GPU timer。
- **资源规范不清**：PBR、程序化水波噪声、可选水 normal、其他噪声纹理必须明确加载路径、格式、wrap/filter 和缺失报错策略。
- **后处理污染 UI**：世界 HDR 后处理必须和 UI/LDR 绘制顺序保持清晰边界。

## 20. 阶段完成判断

当以下条件同时满足时，可以认为项目达到“完整 Kappa-like V1”：

- 天空、云、远景 haze 和太阳方向散射成为画面主导风格，而不是只靠阴影。
- 阴影稳定、柔和、有接触感，并支持树叶/植物透光或彩色阴影的基础版本。
- HDR 自动曝光、Bloom 和 tonemap 能在白天、日落、夜晚、洞穴、水下保持自然。
- 水体具备程序化高度场法线、Fresnel、深度吸收、远景雾和基础反射。
- 主要方块拥有基础材质参数，发光、粗糙、反射响应可区分。
- 透明、实体、掉落物、手持物与世界光照、雾、色彩保持一致。
- Dashboard 可以定位和关闭每个昂贵效果，GPU timer 能解释性能变化。
