# DerivativeMain 内置渲染管线完整差异分析报告

> 范围：本报告以根目录 `DerivativeMain/` 解包源码作为视觉与算法参考，对照当前 Mecraft C++/OpenGL Hybrid Deferred 管线、`src/renderer/` 与 `assets/shaders/`。  
> 当前目标：**主世界 `world0` + 原版 Minecraft 材质包 + 内置 DerivativeMain-like 光影效果**。Mecraft 引擎端拥有自己的 Renderer Contract；DerivativeMain 的大气、光照、色调、HDR、水、体积雾、材质风格作为移植目标，但不再强制复现 Iris/OptiFine shaderpack 宿主 ABI。Nether/End、Distant Horizons、Physics Oceans、外部 PBR 材质包、LabPBR/POM、任意 shaderpack 替换能力均不列入当前必须完成项。
> 2026-05-14 范围修订：`SKY_GROUND` 行星地面渲染、`AURORA/AURORA_STRENGTH`、染色玻璃彩色阴影不纳入当前目标；DerivativeMain 的非线性 `shadow warp` 已确认与 Mecraft greedy meshing 不适配，正式阴影路线维持 Mecraft 自有 CSM contract。
> 2026-05-14 源码同步：当前工作区已修复 SkyCapture raw/cloudy atlas normalized UV，并新增 `lighting_environment.glsl` 统一读取入口、SkyCapture directional debug、体积雾 High/Ultra 雏形。后续优先级从"先修 SkyCapture UV"调整为"移植 FromSH、收口云/水光照单来源、修 SSS、参数化体积雾云海"。
> 2026-05-15 源码同步：`FromSH` skylight 已实现（`sky_sh.glsl`，L1 SH 25 方向采样）并接入 deferred lighting；SkyCapture raw sky 已剥离天体盘；云/水光照已统一到 `LightingEnvironment` 单来源并按 DerivativeMain 做了能量倍率审计（volumetric sun=22.0/sky=0.15，planar sun=120.0，water fog sun=28.0*directIlluminance，sun reflection clamp 2000）；云合成已修正为 premultiplied strength 混合。Phase 0 亮度链路收口基本完成，后续转向 SSS、体积雾参数化、Tonemap 对齐。
> 2026-05-16 体积雾同步：`volumetric_fog.fs` 已按 DerivativeMain `VolumetricFog.glsl` 主积分形态收口，使用 `sunlightSample/skylightSample/transmittance` 循环累积，循环后统一应用 `directIlluminance/skyIlluminance` 与 `fogSunColor*20 + fogSkyColor*2`。Low/Medium Cornette-Shanks phase、High/Ultra sunlight OD、多瓣 HG、powder、`TIME_FADE`、Bloomy Fog 与 debug 64/65 均已落地；旧 `uVolumetricLightStrength/uVolumetricPhaseG` 失效路径已清理。剩余重点是 `SEA_LEVEL/FALLOFF/samples`、High/Ultra 原始密度公式、天气光照联动与水下体积光。
> 2026-05-15 后处理曝光同步：`PostProcessRenderer::updateAutoExposure()` 已恢复 DerivativeMain `Temporal.vert` 的自动曝光公式：无 `averageLum >= 0.02` 地板、无 target exposure min/max clamp、适应速度为 `target < prev ? 1.5 : 1.0`。`autoExposureMin/Max` 仅作为 legacy UI/settings 字段保留，不再作为 DerivativeMain-like 标准路径的调参依据。
> 2026-05-18 TAA/VFog 时间管线重写：完全重写 `temporal_resolve.fs` 对齐 DerivativeMain `Temporal.frag`——variance clip（mean ± 1.25σ）、固定 0.97 history weight + 子像素覆盖调制、Reinhard 亮度加权混合、CatmullRom history sampling（sharpness=0.7）、taaOffset × 0.5 采样偏移、无 sky 特判统一流程。重写 `velocity_resolve.fs`——3×3 closest fragment 搜索、远平面 reprojection velocity（无 sky early return）、raw projection path（无手动 jitter 减法）。GBuffer 投影注入 TAA jitter（`gl_Position.xy += taaOffset * gl_Position.w` 等价），所有读取 depth 的 pass 统一使用 jitteredInvViewProj。渲染顺序重排为 VFog → TAA（DerivativeMain 原序）。VFog 恢复 R1 时间抖动（golden ratio）与旋转 spatial upscale bias。新增 `TemporalCurrent` scratch RT 避免 TAA 读写 history 冲突。新增 debug 67/68/69（TAA current scratch、current-history delta、velocity sky highlight）与 Dashboard A/B 开关（Freeze R1、Freeze Bias、Force Zero Velocity、Freeze TAA Jitter）。
> 2026-05-18 源码全面审计：对 `Renderer.cpp`（3997 行）、`DeferredRenderTargets`（917 行）、`PostProcessRenderer`（549 行）、`GameplaySkyRenderer`（1152 行）和全部 75 个 shader 文件做逐文件扫描。确认管线完整 pass 顺序为 SkyCapture → GBuffer → Velocity → Shadow → SSAO+Filter → DeferredLighting → Reflection+Filter → Cloud → SceneComposite → WaterComposite(pre-TAA) → VFog+Composite → TemporalResolve → MotionBlur → DoF → HistoryUpdate。新发现：`uIsEyeInWater` 硬编码为 0（`Renderer.cpp:348`）；前向路径 `chunk_lit_common.fs` 无 shadow/SSAO/SSR、使用简化 BRDF（hammonDiffuseApprox）；`cloud_target.fs` 和 `water_composite.fs` 已统一读 `LightingEnvironment`；实体/手/掉落物仍纯 forward 渲染；GBuffer 无 per-pixel 法线贴图（仅面法线）；roughness/f0 完全靠材质 ID 硬编码。
> 2026-05-19 源码同步：体积雾系统全面确认完成——`SEA_LEVEL`（`uVFogCenterHeight` 默认 63.0）、`FALLOFF`（`uVFogHeightSpread` 默认 100.0）、动态步数 `getFogSteps()`、High/Ultra 密度公式（4/5 octave FBM）全部对齐 DerivativeMain。21 个 debug mode、CSM shadow、cloud shadow、phase functions、powder、optical depth march、TIME_FADE、R1 dither、Bloomy Fog alpha passthrough 全部落地。水下检测链路完整（PhysicsSystem → ECS → Renderer::m_eyeInWater），deferred lighting 已动态绑定（`Renderer.cpp:1805`），但 water composite 仍硬编码为 0（`Renderer.cpp:348`）。SSS 仍为死代码：`outSssDepth` 恒为 0，PCSS blocker depth 未传递到 `ShadowSample` 结构体。

> 2026-05-13 路线修订：阴影 ghosting 已通过 `Debug Disable Greedy Meshing` 验证为 **非线性 shadow warp 与 Mecraft 贪婪合并大面片之间的插值不兼容**。开启 1x1 terrain face 后 ghosting 消失；因此当前目标不再是让 Mecraft 完整复刻 Iris/OptiFine contract，而是建立 Mecraft 自有阴影/材质/GBuffer contract，并让内置 DerivativeMain-like shader 适配该 contract。

## 0. 移植原则

本项目的 DerivativeMain-like 内置光影必须遵循以下原则：

1. **Mecraft Renderer Contract 是宿主权威，DerivativeMain 是视觉/算法参考。**
   - 大气、BRDF、色调、HDR、水、体积雾、材质风格优先参考 DerivativeMain。
   - 当 DerivativeMain/Iris 运行假设与 Mecraft 引擎基础设施冲突时，以 Mecraft contract 为准。例如：DerivativeMain 的非线性 shadow warp 默认假设 Minecraft/Iris 小 quad terrain；Mecraft 当前主渲染依赖 greedy meshing + MDI，因此不能为了逐字复刻 shadow warp 而全局关闭 greedy meshing。
   - `SKY_GROUND`、Aurora、染色玻璃彩色阴影属于可选/非目标特性；报告中只保留为差异说明，不进入当前开发路线。
   - shaderpack sampler 名、`colortex*`、`shadowtex*`、OptiFine/Iris uniform 只作为映射参考，不再要求完整 ABI 等价。

2. **禁止"看起来等价"的公式改写。**
   - 典型事故：DerivativeMain `sqrt2(x)` 是 `sqrt(sqrt(x))`，即四次根；曾误写成 `sqrt(x)`，导致 Derivative shadow warp 读取端与写入端不一致，出现一整团随摄像机/时间漂移的 shadow-depth 形状阴影。
   - 结论：基础函数必须逐字复刻，不能凭直觉化简。

3. **先定义 Mecraft 数据流，再吸收 DerivativeMain 效果。**
   - 如果当前 C++ pass graph 与 shaderpack `colortex` 不同，必须先建立明确映射。
   - 不再为了对齐 shaderpack ABI 而反向扭曲引擎架构；应把 DerivativeMain 的算法改写为消费 Mecraft 的 GBuffer、shadow、history、material contract。
   - 性能路径必须服务当前项目已有优势：greedy meshing、MDI、多类 draw list、固定内置 shader，而不是为任意 shaderpack loader 预留过高复杂度。

4. **每个移植函数都必须标注来源。**
   - 推荐在 shader 注释中写明：`DerivativeMain/lib/...` 或 `DerivativeMain/program/...` 的函数名/行意图。
   - 报告中的状态分为：`已照抄/已适配/近似实现/缺失/非目标`。

## 1. 当前总状态

当前 Mecraft 已具备承载 DerivativeMain 的 Hybrid Deferred 骨架：

- C++ 端已有 GBuffer、Shadow、SSAO、DeferredLighting、Reflection、Cloud、SceneComposite、VolumetricFog、TAA、MotionBlur、DoF、Water/Transparent、Post 等 17 个 pass。
- Render target 已覆盖 GBuffer 5 MRT、shadow depth CSM array + comparison view、scene lighting/composite/resolved、half-res fog/cloud、reflection、sky capture 256×514、velocity、history ping-pong、Atmosphere LUT 3D。
- Shader 端已有 DerivativeMain 风格材质 ID 33+ 种、roughness/f0/emission/SSS、BRDF、PCSS shadow、sky capture、atmosphere LUT、SSR、水雾、体积雾 4 tier、TAA DerivativeMain parity、AgX/ACES 后处理入口。

但当前实现仍不是完整 DerivativeMain。最大差距不是"有没有 pass"，而是：

- 很多 shader 仍是 DerivativeMain-inspired 近似，不是逐函数移植。
- 当前 FBO 布局与 shaderpack `colortex0-7` 不同，需要稳定映射表。
- `shaders.properties` 中的 blend、flip、scale、program toggle、自定义 uniform 平滑规则没有完整内置等价层。
- 大气、云、体积雾、水、SSR、后处理、GI/AO 等大量核心函数仍未逐文件端口。
- 前向路径 `chunk_lit_common.fs` 无 shadow/SSAO/SSR/SH skylight，使用简化 BRDF（`hammonDiffuseApprox` 非 `DiffuseHammon`）。
- `uIsEyeInWater` 硬编码为 0（`Renderer.cpp:348`），水下渲染路径未激活。
- GBuffer 无 per-pixel 法线贴图，roughness/f0 完全靠材质 ID 硬编码。
- 实体/手/掉落物仍纯 forward 渲染（`steve.fs`/`block_item_lit.fs`）。

结论：**架构地基已基本可用；后续工作必须从"复刻 Iris 宿主"转为"稳定 Mecraft Renderer Contract，并按 DerivativeMain 视觉目标收敛"。**

## 2. 已扫描的 DerivativeMain 权威文件

### 2.1 顶层配置

- `DerivativeMain/shaders.properties`
- `DerivativeMain/Settings.glsl`

关键权威设置：

- Vanilla settings：
  - `oldHandLight=false`
  - `oldLighting=false`
  - `separateAo=true`
  - `clouds=off`
  - `particles.before.deferred=true`
  - `shadow.culling=false` 在 GI 分支下启用
- Custom textures：
  - `texture/Noise2D.png`
  - `texture/RippleNormal.png`
  - `texture/Atmosphere/Final.lut TEXTURE_3D RGBA32F 256 128 33`
- GBuffer blend：
  - 大多数 GBuffer pass `blend=off`
  - `gbuffers_weather = ONE ONE ONE ONE`
  - armor glint 特殊 blend
  - `gbuffers_block/entities/hand/spidereyes.colortex6` 有 alpha blend
- Custom uniforms：
  - `worldLightVector`
  - `worldSunVector`
  - `screenSize/screenPixelSize`
  - `taaOffset`
  - `waterAbsorption`
  - `eyeSkylightFix`
  - `wetnessCustom`
  - `BiomeSandstorm/BiomeGreenShift`
  - `worldTimeCounter/worldTimeChanged`
  - `volFogWind/volFogDensity`
  - `timeNoon/timeMidnight/timeSunrise/timeSunset`
- Program toggles：
  - `gbuffers_spidereyes` 受 `ENTITY_EYES_LIGHTING` 控制
  - `composite2` 受 `DOF_ENABLED` 控制
  - `composite10/12/13` 受 `BLOOM_ENABLED` 控制
  - `composite14` 受 `MOTION_BLUR` 控制
  - `deferred4/deferred8` 在新版 MC 分支分别对应 GI/AO 与 ReflectionFilter compute

当前 Mecraft：

- 资源与 pass 由 C++ 手写控制，没有 properties 等价解释层。
- 已加载 `Final.lut`，但没有使用完整 `Transmittance/Scattering/Irradiance` 多 LUT 文件体系。
- 自定义 uniform 仅部分转为 C++/shader 参数，平滑规则不等价。

### 2.2 主世界 `world0` pass

已扫描 `DerivativeMain/world0/` 中：

- `gbuffers_*`
- `shadow`
- `deferred` 到 `deferred8`
- `composite` 到 `composite15`
- `final`
- `dh_*` 仅记录为非目标

当前 Mecraft 没有直接执行 shaderpack pass 名，而是对应到：

- `renderSkyCapturePass`
- `renderGBufferTerrain`
- `renderVelocityPass`
- `renderShadowMap`
- `renderSsaoPass/renderSsaoFilterPass`
- `renderDeferredLightingPass`
- `renderReflectionPass/renderReflectionFilterPass`
- `renderCloudPass`
- `renderSceneCompositePass`
- `renderVolumetricFogPass/compositeVolumetricFogPass`
- `renderTemporalResolvePass`
- `renderMotionBlurPass`
- `renderDofPass`
- `renderWaterCompositePass`
- `PostProcessRenderer`

因此本项目不是 shaderpack pass 名原样复用，而是 C++ pass graph 适配。后续移植必须保证 shader 内算法与数据语义等价。

## 3. 当前 Mecraft Render Target 对照

当前 `DeferredRenderTargets`：

| Mecraft target | 格式 | 当前用途 | DerivativeMain 近似语义 |
| --- | --- | --- | --- |
| `GAlbedo` | RGBA8 | albedo.rgb + emissive hint.a | `colortex6.rgb` |
| `GNormalAo` | RGBA16F | world normal.rgb + AO.a | `colortex3.xy` normal，当前未压 oct |
| `GVoxelLight` | RG8 | sky light + block light | `colortex7.rg` |
| `GMaterial` | RGBA8 | roughness/f0/emission/sss | `colortex3.zw` packed spec |
| `GMaterialAux` | RGBA8 | material id/wetness/porosity/metal | `colortex7.b` + Material.inc 扩展 |
| `GDepth` | DEPTH32F | opaque depth | `depthtex0` |
| `ShadowDepth` | DEPTH32F | shadow map depth | `shadowtex0/shadowtex1` 的基础深度 |
| `ShadowColor` | RGBA8 | colored shadow / caustics | `shadowcolor0` |
| `ShadowNormal` | RGBA16F | encoded normal.rg + skylight.b + aux/height.a | `shadowcolor1` |
| `SSAO/SSAOFiltered` | R8 | SSAO | DerivativeMain AO/GI filter 的简化替代 |
| `SceneLighting` | RGBA16F | deferred lighting HDR | `colortex4` |
| `Reflection` | RGBA16F | SSR/reflection | `colortex2` |
| `Cloud` | RGBA16F half-res | cloud color/transmittance | `colortex2/1` 部分语义 |
| `SkyCapture` | RGBA16F 256x514 | sky + cloudy sky + metadata rows | `colortex5` |
| `HalfRes` | RGBA16F | volumetric fog | `colortex1`/half-res fog |
| `Velocity` | RG16F | screen velocity | Temporal pass velocity |
| `History*` | RGBA16F/Depth | temporal history | DerivativeMain flip/history colortex |
| `AtmosphereLut3D` | RGBA32F 256x128x33 | `Final.lut` | `texture.deferred.colortex4.1` |

差异：

- 当前资源拆分更清晰，但不等同于 DerivativeMain `colortex` 复用。
- 移植 shader 时，不能直接照搬 sampler 名；必须先查映射。
- `ShadowNormal` 已调整为 `RGBA16F`，与 `shadowcolor1` 语义更接近。

## 4. GBuffer 与材质系统

### 4.1 DerivativeMain 权威文件

- `program/Gbuffers/Terrain.vert/.frag`
- `program/Gbuffers/Water.vert/.frag`
- `program/Gbuffers/Block.vert/.frag`
- `program/Gbuffers/Entities.vert/.frag`
- `program/Gbuffers/Hand.vert/.frag`
- `program/Gbuffers/HandWater.vert/.frag`
- `program/Gbuffers/Textured.vert/.frag`
- `program/Gbuffers/Basic.vert/.frag`
- `program/Gbuffers/Damagedblock.vert/.frag`
- `program/Gbuffers/Beaconbeam.vert/.frag`
- `program/Gbuffers/Spidereyes.vert/.frag`
- `program/Gbuffers/ArmorGlint.vert/.frag`
- `program/Gbuffers/Weather` 由 world0 pass 包装
- `lib/Head/Material.inc`
- `lib/Surface/Parallax.glsl`（PBR/POM 非目标）
- `lib/Surface/ManualTBN.glsl`

### 4.2 当前 Mecraft 对应

- `assets/shaders/chunk_gbuffer.vs/fs`
- `assets/shaders/gbuffer_contract.glsl`
- `src/renderer/ChunkMesher.cpp`
- `src/world/Block.h/Block.cpp`
- `src/renderer/HumanoidRenderer.*`
- `src/renderer/FirstPersonHeldItemRenderer.*`
- `src/renderer/DropRenderer.*`
- `assets/shaders/item_model.*`
- `assets/shaders/steve.*`

### 4.3 当前状态

已适配：

- 原版方块材质 ID 已进入顶点 packed 数据，并写入 `GMaterialAux`。
- `gbuffer_contract.glsl` 已包含 DerivativeMain 风格 material id、roughness/f0/emission/sss、translucent mask。
- terrain chunk 的 albedo、voxel light、normal、AO、material、aux 已写入 GBuffer。
- `chunk_gbuffer.vs` 解码 `tintPacked`（tintKind/materialKind/tintUV），`chunk_gbuffer.fs` 基于 `albedoRaw` 亮度 + blockLight 生成 `emissiveHint`。
- 半透明 mask 解码 — water/ice/stained_glass。

仍缺：

- `Material.inc` 未逐项端口，很多 fallback 值仍是项目自定义。
- Terrain.frag 中原版 atlas alpha、tint、lightmap、mip/alpha 处理尚未逐行对齐。
- Block/Entities/Hand/HandWater/Textured/Basic/Damagedblock/Beaconbeam/Spidereyes/ArmorGlint/Weather 没有完整 GBuffer 等价 pass。
- PBR/LabPBR/POM 路径为非目标，但原版材质 fallback 必须对齐。
- **无 per-pixel 法线贴图**：GBuffer 仅存面法线，`chunk_gbuffer.fs` 无 normal map 采样。DerivativeMain `Terrain.frag` 有完整 tangent-space normal map + anisotropic filtering。
- **roughness/f0 完全靠材质 ID 硬编码**：无 PBR specular map 输入，`surfaceMaterialForKind` / `surfaceMaterialAuxForKind` 返回固定值。水面/冰面/玻璃的 porosity/metalness 未设置。
- **实体/手/掉落物/天气粒子 GBuffer**：`HumanoidRenderer`（`steve.fs`）、`FirstPersonHeldItemRenderer`（`block_item_lit.fs`）、`DropRenderer` 均为纯 forward，无 GBuffer 输出。

建议下一步：

- 建立 `gbuffer_contract.glsl` 与 `Material.inc` 的逐 ID 对照表。
- 将实体、手、掉落物、破坏方块、天气粒子全部纳入同一 DerivativeMain material contract。

## 5. 阴影系统

### 5.1 DerivativeMain 权威文件

- `program/Shadow/Shadow.vert`
- `program/Shadow/Shadow.frag`
- `lib/Lighting/ShadowDistortion.glsl`
- `lib/Lighting/SunLighting.glsl`
- `world0/deferred5.fsh`
- `lib/Atmosphere/VolumetricFog.glsl`
- `lib/Lighting/GlobalIllumination.glsl`

### 5.2 当前 Mecraft 对应

- `assets/shaders/shadow_depth.vs`
- `assets/shaders/shadow_depth.fs`
- `assets/shaders/deferred_lighting.fs`
- `assets/shaders/volumetric_fog.fs`
- `src/renderer/Renderer::renderShadowMap`
- `src/renderer/DeferredRenderTargets`

### 5.3 已完成/已修复

已照抄或接近照抄：

- Derivative quartic distortion：
  - `DistortionFactor = (x^4 + y^4)^(1/4) * 0.9 + 0.1`
  - `xy /= distortion`
  - `z *= 0.2`
- 读取端与写入端的 Derivative warp 已修正为一致。
- Shadow MRT：
  - depth
  - `shadowcolor0` 类 albedo/caustics
  - `shadowcolor1` 类 encoded normal/skylight/aux
- `BlockerSearch`、`PercentageCloserFilter`、`ScreenSpaceShadow` 已按 `SunLighting.glsl` 方向重写。
- PCF 边界采样已补普通 depth texture 的适配保护，避免越界/背景深度误当 blocker。

重要事故记录：

- 曾将 DerivativeMain `quarticLength` 写成 `sqrt(x^4+y^4)`，正确实现应是 `(x^4+y^4)^(1/4)`。
- 该错误只影响 Derivative warp；radial/no warp 正常。
- 现象为：出现一整团形似 shadow depth 的阴影，跟随摄像机移动，并随时间/太阳方向漂移。
- 结论：此类基础函数必须字面核对 DerivativeMain，不允许自行化简。

**2026-05-13 阴影系统重构（已完成）：**

阴影系统已从 DerivativeMain/Iris shadow warp 路线迁移到 Mecraft 自有 CSM 级联阴影。以下为本次完成的全部工作：

1. **CSM 级联阴影**：4 cascade depth texture array，per-cascade matrix/split/texel snapping，线性正交投影，适配 greedy mesh。
2. **ShadowRenderer 模块拆分**：从 `Renderer` 中提取 `shadow::ShadowRenderer`，拥有 cascade 数据、光照方向、uniform 绑定。`Renderer` 保留 shadow pass 编排。
3. **mecraft_shadow.glsl CSM contract**：`CsmCascade`、`ShadowSample`、cascade 选择、world→CSM 投影、bias、PCF 3x3、`sampleCsmShadow()`。
4. **Cascade transition fade**：cascade 边界最后 12% 范围内 smoothstep 混合两级采样，消除 split 边界硬切。
5. **Cascade-specific PCF radius**：cascade 0 半径 2.0x（柔化），cascade 3 半径 1.0x（锐利）。
6. **PCSS 近 cascade**：blocker search（8 tap Poisson）→ penumbra estimation → variable 4x4 PCF，仅 cascade 0 启用。
7. **Contact shadow 调优**：12→16 步，zTolerance 0.025→0.015，距离自适应步长。
8. **Bias/normal offset 透传**：从 `RenderPipelineSettings` 通过 `ShadowRenderer::BiasSettings` 传入 GLSL。
9. **Water/stained glass discard**：水和彩色玻璃在 CSM depth-only pass 中 `discard`，不写 hard depth。
10. **Warp 代码清理**：删除 `shadowWarpMode`/`shadowWarpCutoff`/`derivativeExactShadow` 设置、uniform 绑定、shader 分支。`derivative_shadow.glsl` 中 warp 函数保留为共享数学参考。
11. **Debug views**：CSM Cascade、CSM Depth 0-3、Cascade Info（texel world size + split 可视化）。
12. **Hardware comparison sampler**：`sampler2DArrayShadow` + `csmShadowDepthComparisonTexture()` + `glProgramUniform1i()` 预设。

**Cutout/SSS 阴影白化/黑斑（已修复）：**
- 原因：`BlockerSearch.y` 从 DerivativeMain 的 `sssDepth * shadowProjectionInverse[2].z` 被改成正的 world scale，导致 SSS `fastExp(coeff * sssDepth)` 在 leaves/grass 上指数爆亮。
- 修复：SSS depth 符号已修正为匹配 DerivativeMain ortho 约定。CSM 路径下 cutout alpha test、tint、mip alpha 在 GBuffer 与 shadow pass 间使用同一 helper。

### 5.4 当前 Mecraft CSM 阴影架构

阴影系统已迁移到 Mecraft 自有 CSM 架构：

**C++ 端：**
- `src/renderer/shadow/ShadowRenderer.h/.cpp` — cascade 数据、光照方向、uniform 绑定
- `src/renderer/shadow/ShadowMatrices.h/.cpp` — cascade 矩阵计算、texel snapping
- `src/renderer/shadow/ShadowCasterCuller.h/.cpp` — Box-culler 距离域剔除
- `src/renderer/shadow/ShadowRenderContext.h` — 数据契约结构体
- `src/renderer/DeferredRenderTargets` — `GL_TEXTURE_2D_ARRAY` DEPTH32F × 4 layers + `sampler2DArrayShadow` comparison view

**GLSL 端：**
- `assets/shaders/mecraft_shadow.glsl` — CSM 完整 contract：cascade 选择、投影、bias、PCF 3x3、PCSS、cascade transition fade、`sampleCsmShadow()`
- `assets/shaders/shadow_depth.vs` — 线性 CSM 投影，无 warp
- `assets/shaders/shadow_depth.fs` — opaque+cutout 写 depth，water/stained glass discard

**仍需验收：**
- `shadowcolor0` colored shadow 有资源，但彩色玻璃/透明体的写入与读取还需按 Mecraft 透明材质模型重新定义。
- RSM GI 使用 shadow color/normal 的间接光仍缺失。
- DH shadow 为非目标。

#### 5.4.1 已完成：sampler2DShadow 硬件 PCF 基础架构

DerivativeMain 使用两个 shadow sampler：

- `sampler2D shadowtex0` — raw depth，用于 `BlockerSearch` 的 `texelFetch` 读取原始深度值
- `sampler2DShadow shadowtex1` — comparison mode，用于 `PercentageCloserFilter` 的 `textureLod(shadowtex1, vec3(uv, refZ), 0)` 硬件自动比较+双线性插值

Mecraft 已完成 raw depth + comparison view 的双视图基础架构：

**当前方案：`glTextureView` 零拷贝双视图**

```
m_shadowDepth (原始纹理, GL_DEPTH_COMPONENT32F)
       │
       ├── sampler2D       view → uShadowMapRaw   (texelFetch 读原始深度，用于 blockerSearch)
       │
       └── sampler2DShadow view → uShadowMap      (texture() 硬件比较，用于 PCF)
```

C++ 关键配置：

```cpp
// 在 DeferredRenderTargets.cpp ensureSize() 中，创建 m_shadowDepth 之后：
GLuint m_shadowDepthComparison = 0;
glGenTextures(1, &m_shadowDepthComparison);
glTextureView(m_shadowDepthComparison, GL_DEPTH_COMPONENT32F,
              m_shadowDepth, GL_DEPTH_COMPONENT32F, 0, 1, 0, 1);
glTextureParameteri(m_shadowDepthComparison, GL_TEXTURE_COMPARE_MODE,  GL_COMPARE_REF_TO_TEXTURE);
glTextureParameteri(m_shadowDepthComparison, GL_TEXTURE_COMPARE_FUNC,  GL_LEQUAL);
glTextureParameteri(m_shadowDepthComparison, GL_TEXTURE_MIN_FILTER,    GL_NEAREST);
glTextureParameteri(m_shadowDepthComparison, GL_TEXTURE_MAG_FILTER,    GL_NEAREST);
glTextureParameteri(m_shadowDepthComparison, GL_TEXTURE_WRAP_S,        GL_CLAMP_TO_BORDER);
glTextureParameteri(m_shadowDepthComparison, GL_TEXTURE_WRAP_T,        GL_CLAMP_TO_BORDER);
constexpr float kBorderColor[] = {1.0f, 1.0f, 1.0f, 1.0f};
glTextureParameterfv(m_shadowDepthComparison, GL_TEXTURE_BORDER_COLOR, kBorderColor);
```

Shader 关键接口：

```glsl
uniform sampler2D       uShadowMapRaw;   // blockerSearch: texelFetch 读原始深度
uniform sampler2DShadow uShadowMap;       // PCF: texture() 硬件比较

// PCF 从 compareShadowBilinear(...) 变为一行：
float lit = texture(uShadowMap, vec3(sampleCoord, shadowProjPos.z));

// blockerSearch 仍用 texelFetch：
float depthSample = texelFetch(uShadowMapRaw, texelCoord, 0).x;
```

**收益**：PCF 每样本从手写比较转为硬件比较；raw depth 仍可用于 blocker search/debug；colored shadow 可继续通过 Mecraft 的 ShadowColor 语义扩展。

**注意**：该架构只解决 sampler/compare 的基础设施问题，不解决非线性 warp 与 greedy mesh 的 ghosting。正式默认阴影仍应走 MecraftShadow Linear/CSM 路线。

### 5.5 阴影任务完成状态

1. ~~将 `ShadowDistortion.glsl` 抽成项目公共 include~~ ✅（`derivative_shadow.glsl`）
2. ~~将 `SunLighting.glsl` 的纯数学函数端口为 include~~ ✅（`derivative_sunlight.glsl`）
3. ~~升级 shadow map sampler 架构~~ ✅（`sampler2DShadow` + `glTextureView` 双视图）
4. ~~将正式阴影路线改为 MecraftShadow~~ ✅
   - CSM 4 cascade 级联阴影已实现，线性正交投影，适配 greedy mesh
   - 旧 Derivative/Radial warp 已从正式路径删除，`derivative_shadow.glsl` 中保留为数学参考
   - ShadowRenderer 已从 Renderer 拆分
5. ~~`Shadow.frag` 的 `shadowcolor0/1` 写入验收~~ ✅
   - Water/stained glass 在 CSM depth-only pass 中 discard，不写 hard depth
   - Cutout leaves/grass 作为 opaque caster 写 depth
6. ~~Cutout/SSS 阴影白化修复~~ ✅ — SSS depth 符号修正
7. RSM GI 使用 shadow color/normal 的间接光仍缺失（P3/P4）
8. shadow-only bounded fine caster mesh 未实现（低优先级，当前 CSM 质量已足够）

## 6. 主光照、BRDF、Block Light、SSS

### 6.1 DerivativeMain 权威文件

- `world0/deferred5.fsh`
- `world0/deferred5.vsh`
- `lib/Surface/BRDF.glsl`
- `lib/Lighting/SunLighting.glsl`
- `lib/Lighting/BlockLighting.glsl`
- `lib/Lighting/AmbientOcclusion.glsl`
- `lib/Lighting/GlobalIllumination.glsl`

### 6.2 当前 Mecraft 对应

- `assets/shaders/deferred_lighting.fs`
- `assets/shaders/ssao.fs`
- `assets/shaders/ssao_filter.fs`
- `assets/shaders/render_contract.glsl`
- `assets/shaders/atmosphere_lut.glsl`

### 6.3 当前状态

#### 已照抄（P0/P1 已完成）

- ✅ `derivative_shadow.glsl` — `Common.inc` 基础数学辅助宏全量端口：
  - `PI/rPI/TAU/rTAU/rLOG2`、`rcp/oneMinus/saturate/max0/fastExp/clamp16F`
  - `sqr/cube/pow4/pow5/pow16/curve/dotSelf/sincos/cossin/remap/GetLuminance`
  - `maxOf/minOf`（vec2/vec3 重载）
  - `GetBlocklightFalloff` — DerivativeMain `Functions.inc:4-7`，block light 通道非线性 remap
  - `LinearToSRGB/SRGBtoLinear` — 颜色空间转换
  - `sqrt2/quarticLength/DistortionFactor/DistortShadowSpace/WorldPosToShadowProjPosBias` — shadow distortion 全量
  - shadow bias/normal offset/convenience 函数

- ✅ `derivative_brdf.glsl` — `BRDF.glsl` 逐字复刻：
  - `DiffuseHammon` — Disney diffuse + retro-reflection + roughness wrap
  - `SpecularBRDF` — GGX NDF + Smith visibility + Schlick Fresnel

- ✅ `derivative_sunlight.glsl` — `SunLighting.glsl` 核心光照函数全量端口：
  - HG phase function（`CalculateSunHgPhase`）
  - Fake bounce indirect（`CalculateFakeBounce`）
  - SSS（`CalculateSubsurfaceScattering`）

- ✅ `BlockLighting.glsl` 完整端口（inline 于 `deferred_lighting.fs`）：
  - `GetBlocklightFalloff(mcLightmapR)` — block light 前处理非线性 remap
  - 所有 materialKind emissive 规则改用 `albedoRaw = LinearToSRGB(albedo)` 替代之前的 `pow(albedo, 1/2.2)` 近似
  - Redstone 补全顶部/底部区分：`fract(worldPos.y) > 0.18` 判断
  - Blocklight falloff 使用经 `GetBlocklightFalloff` remap 后的 `mcLightmapR`
  - Held torchlight 精确公式：`fma(NdotV, 0.8, 0.2)` + `ssao * oneMinus(falloff) + falloff`
  - Emission Mode 1：`materialEmission * 1.5 * uBlockLightStrength`
  - Emissive ores：`LinearToSRGB(pow5(max0(albedoRaw - 0.1)))` — DerivativeMain 精确公式
  - Beacon Core / Middle Glowing 的 `fract(worldPos)` 逻辑已确认正确（Mecraft 的 worldPos 已含 cameraPosition）
  - 最后一行常量加法已对齐

- ✅ `deferred5.fsh` 主流程已按 DerivativeMain 参考重排（`deferred_lighting.fs`）：
  - `sceneData = vec3(0.0)` 初始化，`BASIC_BRIGHTNESS (0.018)` 移至 skylight 之后
  - `diffuse = vec3(1.0)` 初始化，仅在 shadow > 0 时乘 `DiffuseHammon`
  - `shadow` 改为 `vec3` 类型，匹配 DerivativeMain 的 vec3 shadow
  - **`shadow *= saturate(voxelLight.r * 1e6)`** — 天空光遮蔽太阳光，室内无直接阳光
  - **`shadow *= sunlightMult`** — shadow 包含 sunlightMult（DerivativeMain 精确顺序）
  - **`specular *= 1.0 + uWeatherWetness`** — SPECULAR_HIGHLIGHT_BRIGHTNESS + wetnessCustom
  - `sssContrib *= outdoorSkyMask` — eyeSkylightFix
  - Compositing 顺序：`sceneData += shadow * diffuse` → `sceneData *= albedo` → `sceneData *= oneMinus(metalMask)` → `sceneData += shadow * specular`
  - Shadow desaturation 使用原始 `sunShadow` 标量（Mecraft 扩展，非 DerivativeMain 原生）
  - 删除旧的自定义 back-scatter SSS（已由 `CalculateSubsurfaceScattering` 替代）

#### 仍需完善（P1 剩余 / P2+）

- `deferred5.fsh` 中 shadow/SSS/colored shadow 必须按 MecraftShadow contract 重新验收；water tint、underwater 视觉处理也仍需继续收敛。
- `GlobalIllumination.glsl` RSM GI 缺失。
- `AmbientOcclusion.glsl` 的 AO/GI temporal + spatial filter 未完整。
- ~~SH sky lighting 未完全按 `deferred5.vsh` 从 sky capture 构建。~~ ✅ 已完成：`sky_sh.glsl` 实现 ToSH/FromSH/buildSkySH，`deferred_lighting.fs` 已接入。
- `BlockLighting.glsl` 中的 Nether/End 分支为非目标，但部分函数被 world0 依赖时仍需处理。
- `EMISSION_CURVE` 当前近似为 1.0（无 pow），因 Mecraft 预烘焙 emissiveness 值；后续如需精确匹配需确认曲线参数。

## 7. 大气与 Sky Capture

### 7.1 DerivativeMain 权威文件

- `lib/Atmosphere/Atmosphere.glsl`
- `program/Deferred0.glsl`
- `world0/deferred.fsh/.vsh`
- `world0/deferred5.vsh`
- `texture/Atmosphere/Final.lut`
- `texture/Atmosphere/Transmittance.lut`
- `texture/Atmosphere/Scattering.lut`
- `texture/Atmosphere/Irradiance.lut`

### 7.2 当前 Mecraft 对应

- `assets/shaders/atmosphere_lut.glsl`
- `assets/shaders/gameplay_sky.fs`
- `src/renderer/GameplaySkyRenderer.*`
- `src/renderer/Renderer::renderSkyCapturePass`
- `assets/shaders/render_contract.glsl`

### 7.3 当前状态

已适配：

- `Final.lut` 已按 `RGBA32F 256x128x33` 载入。
- `atmosphere_lut.glsl` 已实现多种 DerivativeMain 风格查询函数。
- SkyCapture 为 `256x514`，包含 raw sky rows、cloudy sky rows、metadata rows。
- metadata row 已包含 direct/sky/sun/moon illuminance 与 cloud dynamic weather。

未完整适配：

- DerivativeMain 的 Bruneton 查询体系依赖多个 LUT 语义；当前主要从 `Final.lut` 采样并自行映射。
- `Atmosphere.glsl` 中 planet parameters、ProjectSky/UnprojectSky、sun/moon disk、night eye、stars、land atmospheric scattering 仍需逐函数端口；`AURORA/AURORA_STRENGTH` 不纳入当前目标。
- `shaders.properties` 中 time/biome/weather 平滑 uniform 未完整。

下一步：

- 审计 `atmosphere_lut.glsl` 是否逐函数匹配 `Atmosphere.glsl`，不匹配处标注"适配"或重写。
- 如果后续要提升一致性，可同步加载并使用 `Transmittance/Scattering/Irradiance/Final` 的完整资源体系；这属于 DerivativeMain-like 视觉收敛，不是 shaderpack ABI 复刻要求。

## 8. 云系统

### 8.1 DerivativeMain 权威文件

- `program/Deferred0.glsl`
- `program/Deferred1.glsl`
- `lib/Atmosphere/VolumetricClouds.glsl`
- `lib/Atmosphere/PlanarClouds.glsl`
- `lib/Atmosphere/Aurora.glsl`
- `world0/deferred1.fsh`
- `world0/deferred2.fsh`

### 8.2 当前 Mecraft 对应

- `assets/shaders/cloud_target.fs`
- `src/renderer/Renderer::renderCloudPass`
- `Cloud` half-res target
- `HistoryCloud`
- `scene_composite.fs`

### 8.3 当前状态

已适配：

- 有 planar/cirrocumulus/volumetric cloud 的入口。
- 有 half-res cloud target 与 cloudy sky capture。
- cloud composite 已进入 scene composite。

未完整适配：

- `VolumetricClouds.glsl` 的 marching、multi-scattering、powder、sun optical depth、temporal upscaling 未完整。
- `PlanarClouds.glsl` 双层 cirrus/cirrocumulus 仍是近似。
- `CLOUDS_SHADOW` 当前不是 DerivativeMain 光学深度式云影。
- Aurora 未完整。

备注：

- 用户验证 cloud shadow 开关对本次 Derivative/Radial warp 阴影 bug 无影响，说明该 bug 根因不是云影，而是非线性 shadow warp 与 greedy mesh 的插值不兼容。

## 9. 体积雾与体积光

### 9.1 DerivativeMain 权威文件

- `world0/composite.fsh`
- `lib/Atmosphere/VolumetricFog.glsl`
- `lib/Atmosphere/VolumetricFogEnd.glsl`（End 非目标）
- `lib/Water/WaterFog.glsl`

### 9.2 当前 Mecraft 对应

- `assets/shaders/volumetric_fog.fs`
- `assets/shaders/volumetric_composite.fs`
- `Renderer::renderVolumetricFogPass`
- `Renderer::compositeVolumetricFogPass`

### 9.3 当前状态

已适配：

- Half-res volumetric target + depth-aware composite。
- 绑定 depth、SkyCapture metadata、noise、CSM shadow depth/color、atmosphere LUT。
- 主积分已改为 DerivativeMain 形态：循环内累积 `sunlightSample/skylightSample/transmittance`，循环后统一构造 `fogSunColor/fogSkyColor`。
- 输出公式已对齐：`fogColor = fogSunColor * 20.0 + fogSkyColor * 2.0`。
- Low/Medium：Cornette-Shanks phase 作用到 `mistDensity`。
- High/Ultra：太阳方向 4 步 optical-depth、powderSun、多瓣 HG phase、`FOG_TYPE^2` scale 已落地。
- `TIME_FADE` 已移植并提供 Dashboard 开关；默认开启，验证 Clear 正午压雾、晨昏出体积光。
- `VOLUMETRIC_LIGHT` 清空气溶胶路径已接入 Rayleigh-phase `airDensity`，颜色来自 SkyCapture `directIlluminance`。
- Bloomy Fog 已完成：volumetric alpha 输出 transmittance，TAA/motion_blur/dof 保留 alpha，postprocess `CalculateBloomFog()` 按 DerivativeMain 双套权重混合 `fogBloom`。
- Debug 64/65 已加入：`VFog Sun/Sky Ratio` 与 `VFog Beam Modulation`。
- 旧 `uVolumetricLightStrength/uVolumetricPhaseG` 与 UI 失效滑条已清理/标记 deprecated。
- **R1 时间抖动已恢复（2026-05-18）：** `volumetric_fog.fs` 使用 DerivativeMain `R1(frameCounter, noiseTexel.a)` 准随机序列（golden ratio），每帧偏移 `1/PHI`，TAA 逐帧积累。Dashboard `Freeze R1 Dither` 可冻结为静态 per-pixel hash。
- **旋转 spatial upscale bias 已恢复（2026-05-18）：** `volumetric_composite.fs` 使用 `ivec2(fullCoord + frameCounter) % 2`，每帧采样不同 2×2 quarter。Dashboard `Freeze Upscale Bias` 可冻结为静态 bias。
- **渲染顺序已重排为 VFog → TAA（2026-05-18）：** 与 DerivativeMain `composite.fsh → Temporal.fsh` 一致，VFog 参与 TAA temporal accumulation。`updateDeferredHistoryTargets` 在 TAA 之后执行，history 存储 VFog+TAA 结果。
- **深度重建使用 raw invViewProj（2026-05-18）：** 匹配 DerivativeMain `ScreenToViewSpaceRaw`（不做 taaOffset 减法），jittered screen ray 由 TAA 平均。

已完整对齐（2026-05-19 确认）：

- `SEA_LEVEL` 通过 `uVFogCenterHeight`（默认 63.0）参数化，替代所有 DerivativeMain `SEA_LEVEL` 引用。
- `FALLOFF` 通过 `uVFogHeightSpread`（默认 100.0）参数化，指数 `falloffExp = 1.0 / max(uVFogHeightSpread, 1.0)`。
- 动态步数 `getFogSteps()` 返回 `min(20, 20*0.4 + rayLength*0.1)`，匹配 DerivativeMain `VOLUMETRIC_FOG_SAMPLES` contract。
- High（tier 2）密度公式完整对齐：对称 `exp2(-abs(y - SEA_LEVEL) * falloffExp)` + 4-octave FBM，`clamp(noise * 12.0 * falloff - 4.5, 0.0, 1.0) * 9.0`。
- Ultra（tier 3）密度公式完整对齐：对称 falloff + 5-octave loop（`weight *= 0.5`, `p = (p + wind) * 4.0`），`clamp(falloff * noise * 400.0 - 170.0, 0.0, 1.0) * 48.0`。
- 21 个 debug mode 已实现（576-824 行）。

仍缺：

- `volFogWind/volFogDensity`、BiomeSandstorm/GreenShift 未完整。
- colored shadow 体积雾采样不完整。
- underwater volumetric light（`UW_VOLUMETRIC_LIGHT`）未实现。

## 10. 水体、透明、折射

### 10.1 DerivativeMain 权威文件

- `program/Gbuffers/Water.vert/.frag`
- `program/Gbuffers/HandWater.vert/.frag`
- `lib/Water/WaterWave.glsl`
- `lib/Water/WaterFog.glsl`
- `lib/Surface/Refraction.glsl`
- `lib/Surface/ScreenSpaceReflections.glsl`
- `lib/Surface/RainEffect.glsl`
- `world0/composite.fsh`
- `world0/composite1.fsh`

### 10.2 当前 Mecraft 对应

- `assets/shaders/water_composite.fs`
- `Renderer::renderWaterCompositePass`
- `transparent_composite.fs`
- `reflection_probe.fs`

### 10.3 当前状态

已适配：

- 水从 generic transparent 中拆出，有独立 composite。
- WaterFog/UnderwaterFog 已有 DerivativeMain 注释来源，光照已统一到 `LightingEnvironment`。
- 水面 SSR、sky reflection、sun reflection、wave/parallax、absorption 有入口。
- 水面在 TAA 之前渲染（`Renderer.cpp:1481-1486`），与 GBuffer 共享 jittered depth。
- 太阳反射已改读 `env.sunIlluminance`，月亮反射用 `env.moonIlluminance`。
- `water_composite.fs` 体积雾集成使用 depth-aware upsample + premultiplied alpha 混合。

未完整适配：

- DerivativeMain 水先写 GBuffer，再在 composite/composite1 中合成；当前是 deferred 后 forward/composite。
- `WaterWave.glsl` 已移植到 `water_composite.fs`（4 octave textureSmooth、distance attenuation），但雨滴涟漪仍为占位噪声（注释"no real rain ripple texture/system yet"），非 DerivativeMain `RippleNormal.png`。
- `Refraction.glsl`、raytraced refraction、dispersion 未完整。
- `RainEffect.glsl` 与 `RippleNormal.png` 未完整。
- Foam/caustics/underwater light 不完整。水面 caustics 函数已在 `shadow_depth.fs:60-89` 定义，但水面在 CSM pass 中 discard，caustics 被阻止。
- 玻璃/冰/彩色透明与水的统一半透明管线缺失。
- **`uIsEyeInWater` 在 water composite pass 仍硬编码为 0**（`Renderer.cpp:348`），水下渲染路径未激活；deferred lighting pass 已动态绑定 `m_eyeInWater`（`Renderer.cpp:1805`）。水下检测链路完整（PhysicsSystem → ECS → Game.cpp → Renderer::m_eyeInWater），只需修复 water composite pass 的一行绑定。
- 水下时反转法线但不做波浪计算（`water_composite.fs:448`）。

注意：

- 当前不应把水/透明几何直接作为完全遮挡的 shadow depth caster；应参考 DerivativeMain `Shadow.frag` 的 colored shadow/water 分支，在 Mecraft 透明阴影 contract 中定义自己的写入/读取语义。

## 11. SSR、反射、湿润表面

### 11.1 DerivativeMain 权威文件

- `world0/deferred6.fsh`
- `world0/deferred7.fsh`
- `world0/deferred8.csh`
- `program/ReflectionFilter.frag/.comp`
- `lib/Surface/ScreenSpaceReflections.glsl`
- `lib/Surface/ReflectionFilter.glsl`
- `lib/Surface/RainEffect.glsl`

### 11.2 当前 Mecraft 对应

- `assets/shaders/reflection_probe.fs`
- `assets/shaders/reflection_filter.fs`
- `Reflection` target
- `HistoryReflection`

### 11.3 当前状态

已适配：

- SSR ray march 与 sky fallback 有基础实现。
- Reflection bilateral filter 有基础实现。
- roughness/metal/translucent mask 已参与。
- 湿润表面影响：`ComputePixelWetness` 修改 normal/roughness/f0，`wetReflectBoost` 增强 Fresnel。
- Premultiplied 输出：DerivativeMain convention `rgb*specular, a=1-specular`。
- 天空回退：cloudy sky + wet-boosted sky weight。
- Filter：roughness-based bilateral filter + luma-chroma sharpening。

未完整适配：

- `ScreenSpaceReflections.glsl` 的 GGX VNDF importance sampling、rough cone widening、hit validation、real sky reflection 未完整。当前 28 步线性 ray march，无 VNDF。
- `ReflectionFilter.glsl` 与 compute path 未完整。
- wetness/rain splash/puddle 对 reflection 的影响部分实现（`weather_surface.glsl` 已端口），但无 puddle rendering。
- DerivativeMain 的 reflection temporal reprojection 未完成（`HistoryReflection` 纹理已绑定但未使用）。
- 无 half-resolution SSR pass。

## 12. TAA、Motion Blur、DoF、Bloom、Grade、Final

### 12.1 DerivativeMain 权威文件

- `program/Post/Temporal.vert/.frag`
- `program/Post/DoF.glsl`
- `program/Post/MotionBlur.glsl`
- `program/Post/DownSample0.glsl`
- `program/Post/DownSample.glsl`
- `program/Post/BlurH.glsl`
- `program/Post/BlurV.glsl`
- `program/Post/Grade.glsl`
- `program/Post/Final.glsl`
- `lib/Post/ACES.glsl`
- `lib/Post/AgX.glsl`

### 12.2 当前 Mecraft 对应

- `assets/shaders/temporal_resolve.fs`
- `assets/shaders/motion_blur.fs`
- `assets/shaders/dof.fs`
- `assets/shaders/bloom_extract.fs`
- `assets/shaders/bloom_blur.fs`
- `assets/shaders/postprocess.fs`
- `src/renderer/PostProcessRenderer.*`

### 12.3 当前状态

已适配：

- **TAA temporal resolve 已对齐 DerivativeMain（2026-05-18 重写）：**
  - Variance clip（mean ± 1.25σ）替代旧 AABB 12.5% expansion。
  - 固定 0.97 history weight + 子像素覆盖调制（`sqrt(pixelVelocity.x * pixelVelocity.y) * 0.25 + 0.75`）。
  - Reinhard 亮度加权混合：`invReinhard(mix(reinhard(current), reinhard(history), weight))`。
  - CatmullRom history sampling（5-tap SMAA 近似，sharpness=0.7）。
  - `taaOffset × 0.5` 应用到当前采样坐标。
  - 无 sky 特判：所有像素（包括 sky、VFog）统一走 variance clip + 0.97 blend。
  - `TemporalCurrent` scratch RT 避免 TAA 读写 history ping-pong 冲突。
- **Velocity resolve 已对齐 DerivativeMain（2026-05-18 重写）：**
  - 3×3 closest fragment 搜索（`GetClosestFragment` 等价）。
  - 远平面 reprojection velocity（无 sky early return，depth=1 也做 reprojection）。
  - Raw projection path（无手动 jitter 减法，矩阵本身编码 jitter）。
- **GBuffer TAA jitter 已注入（2026-05-18）：** `jitteredViewProj` 等价于 `gl_Position.xy += taaOffset * gl_Position.w`，所有读取 depth 的 pass（deferred lighting、reflection、cloud、VFog、water composite）统一使用 jitteredInvViewProj。
- **Debug 诊断已补齐（2026-05-18）：** Debug 67=TAA Current Scratch、68=TAA Current-History Delta、69=Velocity Sky Highlight。Dashboard A/B 开关：Freeze R1、Freeze Bias、Force Zero Velocity、Freeze TAA Jitter。
- Motion blur、DoF、Bloom、AgX/ACES/Filmic、sharpen/dither 均有入口。
- PostProcessRenderer 已有 DerivativeMain sigmoid exposure response 注释来源。

未完整适配：

- DerivativeMain exposure 与 TAA 在 `Temporal.vert/.frag` 里高度耦合；当前拆分后顺序不同。
- Bloom 不是 DerivativeMain tile/mip downsample + H/V blur + Grade 合成链。
- Grade 中 Purkinje、white balance、cinematic、vignette、wet lens、fog bloom 等未完整。
- Final CAS 与 dither 未完全按 `Final.glsl`。

## 13. 非目标分支

以下 DerivativeMain 源码存在，但当前目标明确不要求完整实现：

- `world-1/` Nether。
- `world1/` End。
- `program/DH/*` 与 `world0/dh_*` Distant Horizons。
- `lib/Water/PhysicsOceans.glsl`。
- LabPBR/PBR texture workflow。
- POM/parallax heightmap 作为外部 PBR 材质包路径。

注意：非目标分支中的公共函数如果被 world0 使用，仍需按 world0 依赖移植。

## 14. 文件级覆盖矩阵

| DerivativeMain 模块 | Mecraft 当前对应 | 状态 |
| --- | --- | --- |
| `shaders.properties` | C++ settings/manual pass graph | 近似；缺等价解释层 |
| `Settings.glsl` | `Renderer::PipelineSettings` + uniforms | 部分；宏默认值未完整 |
| `lib/Head/Common.inc` | `derivative_shadow.glsl` | ✅ 已照抄；基础数学宏+辅助函数全量端口 |
| `lib/Head/Functions.inc` | `derivative_shadow.glsl` | ✅ `GetBlocklightFalloff` 已端口；其余散落 |
| `lib/Head/Uniforms.inc` | C++ uniforms | 部分 |
| `lib/Head/Noise.inc` | `uNoiseTex` + local functions | 部分 |
| `lib/Head/Mask.inc` | `gbuffer_contract.glsl` translucent mask | 部分 |
| `lib/Head/Material.inc` | `gbuffer_contract.glsl` | ✅ EMISSIVE_CURVE=2.2 已复刻；需逐 ID 审计 |
| `program/Gbuffers/Terrain` | `chunk_gbuffer.vs/fs` | 中等；原版 fallback 需继续对齐；无 per-pixel normal map、无 PBR specular map |
| `program/Gbuffers/Water` | `water_composite.fs` + water mesh | 架构不同，算法部分；水面 pre-TAA 已实现 |
| `program/Gbuffers/Block/Entities/Hand/...` | forward/entity/item shaders | 低；`steve.fs`/`block_item_lit.fs`/`item_model.fs` 无 GBuffer 输出 |
| `program/Shadow` | `shadow_depth.vs/fs` | 部分完成；Derivative warp 与 sampler2DShadow 双视图已实现，但 ShadowColor.a 透明标志不是 DerivativeMain 双 shadowtex 语义，colored shadow/透明投射者需重新验收；RSM 仍缺 |
| `lib/Lighting/ShadowDistortion.glsl` | `derivative_shadow.glsl` | ✅ 已照抄；公共 include |
| `lib/Lighting/SunLighting.glsl` | `derivative_sunlight.glsl` + `derivative_shadow.glsl` + `deferred_lighting.fs` | 部分完成；HG phase/fake bounce/SSS 纯函数已端口，但 BlockerSearch/PCF/SSS depth/colored shadow 读取链路需逐行复核并通过 cutout/transparent 验收 |
| `lib/Lighting/BlockLighting.glsl` | `deferred_lighting.fs` inline | ✅ 已完整端口（GetBlocklightFalloff、Redstone top/bottom、emissive ores、held torchlight、emission mode 1） |
| `lib/Lighting/AmbientOcclusion.glsl` | `ssao.fs/filter` | 部分 |
| `lib/Lighting/GlobalIllumination.glsl` | 无完整 RSM GI | 缺失 |
| `lib/Surface/BRDF.glsl` | `derivative_brdf.glsl` | ✅ 已照抄（DiffuseHammon + SpecularBRDF 逐字复刻） |
| `lib/Surface/ScreenSpaceReflections.glsl` | `reflection_probe.fs/water_composite.fs` | 部分；28步线性ray march，无VNDF/rough cone，history纹理已绑定未用 |
| `lib/Surface/ReflectionFilter.glsl` | `reflection_filter.fs` | 部分；roughness-based bilateral + luma-chroma sharpen |
| `lib/Surface/Refraction.glsl` | `water_composite.fs` | 低到中 |
| `lib/Surface/RainEffect.glsl` | wetness params | 低 |
| `lib/Atmosphere/Atmosphere.glsl` | `atmosphere_lut.glsl/gameplay_sky.fs` | 中等；LUT 体系不完整 |
| `lib/Atmosphere/VolumetricClouds.glsl` | `cloud_target.fs` | 部分近似；光照已统一读LightingEnvironment |
| `lib/Atmosphere/PlanarClouds.glsl` | `cloud_target.fs` | 部分近似 |
| `lib/Atmosphere/VolumetricFog.glsl` | `volumetric_fog.fs` | ✅ 已完整对齐（4 tier/SEA_LEVEL/FALLOFF/动态步数/High(Ultra FBM/phase/OD/TIME_FADE/Bloomy Fog/21 debug modes） |
| `lib/Water/WaterWave.glsl` | `water_composite.fs/shadow_depth.fs` | 部分；4 octave textureSmooth 已移植，雨涟漪为占位 |
| `lib/Water/WaterFog.glsl` | `water_composite.fs` | 部分较高；光照已统一到LightingEnvironment |
| `program/Post/Temporal` | `temporal_resolve.fs` | 高；variance clip/CatmullRom/0.97 weight/Reinhard luminance blend/3×3 velocity 已完整对齐 |
| `program/Post/DoF` | `dof.fs` | 部分 |
| `program/Post/MotionBlur` | `motion_blur.fs` | 部分 |
| `program/Post/DownSample/Blur/Grade` | bloom/postprocess | 部分；6 tonemap modes + Bloomy Fog + Kappa grading 已实现 |
| `program/Post/Final` | `postprocess.fs` | 部分；CAS sharpening + dithering 已实现 |
| `program/DH/*` | 无 | 非目标 |
| `world-1/world1` | 无完整维度管线 | 非目标 |
| `world0/deferred5.fsh` | `deferred_lighting.fs` | 部分完成；主流程大体重排、water/underwater、vec3 shadow 与 sampler2DShadow 已接入，但 2026-05-13 cutout/SSS bug 证明不能标记逐行对齐，需重新审计 shadow/SSS 相关数据流 |

## 15. 优先级路线图

### P0：防止再次发生基础语义分叉 ✅ 已完成

1. ~~抽出 `derivative_shadow.glsl`~~ ✅ 已完成
   - `pow2/pow4/pow5/pow16/sqrt2/sqr/cube/curve/dotSelf/sincos/cossin/remap/GetLuminance`
   - `quarticLength`
   - `DistortionFactor`
   - `DistortShadowSpace`
   - `WorldPosToShadowProjPosBias`
   - `GetBlocklightFalloff`、`LinearToSRGB/SRGBtoLinear`
   - `maxOf/minOf`（vec2/vec3 重载）
2. ~~shadow/deferred/volumetric/debug 必须引用同一 include~~ ✅ 已完成
3. 建立 `DerivativeMainPortingIndex.md` — 待建立
4. ~~把"禁止近似改写 DerivativeMain 公式"写入项目开发约定~~ ✅ 已在本文档 §0 明确

### P1：主光照和 MecraftShadow 阴影收敛 ✅ 已完成

1. ✅ `derivative_sunlight.glsl`（HG phase / fake bounce / SSS）
2. ✅ `derivative_brdf.glsl`（DiffuseHammon + SpecularBRDF 逐字复刻）
3. ✅ `BlockLighting.glsl` 完整端口
4. ✅ `deferred_lighting.fs` 主流程按 `deferred5.fsh` 重排
5. ✅ Mecraft 透明阴影 contract：water/stained glass discard，cutout opaque caster
6. ✅ Water/underwater `isEyeInWater` 分支对齐
7. ✅ `EMISSION_CURVE` = 2.2
8. ✅ Shadow sampler 升级：`sampler2DShadow` + `glTextureView` 双视图
9. ✅ MecraftShadow 默认路径：CSM 4 cascade，线性投影，greedy mesh 稳定
10. ✅ Cascade transition fade、cascade-specific PCF、PCSS 近 cascade
11. ✅ Contact shadow 16 步调优
12. ✅ Cutout/SSS 阴影白化修复（SSS depth 符号修正）
13. ✅ ShadowRenderer 模块拆分
14. ✅ Warp 代码清理（shadowWarpMode/derivativeExactShadow 删除）
15. ✅ Bias/normal offset 从 pipeline settings 透传

**P1 仍需后续验收：**
- RSM GI 使用 shadow color/normal 的间接光（P3/P4）
- `COLORED_SHADOWS` 透明 shadow 语义按 Mecraft contract 重新定义（当前 ShadowColor.a 是适配层）
- shadow-only bounded fine caster mesh（低优先级）

### P2：GBuffer 与材质合同

1. `Material.inc` 逐 ID 对齐。
2. Terrain 原版材质 fallback 对齐。
3. Entities/Hand/HandWater/Weather/Beacon/Damaged/Glint 进入 DerivativeMain GBuffer 合同。

### P3：Atmosphere/Cloud/Fog/Water

1. `Atmosphere.glsl` 完整审计。
2. `VolumetricClouds.glsl` 与 `PlanarClouds.glsl` 移植。
3. ~~`VolumetricFog.glsl` 移植。~~ ✅ 已完成：SEA_LEVEL/FALLOFF/动态步数/High(Ultra 密度公式/phase/OD/TIME_FADE/Bloomy Fog/21 debug modes 全部对齐。
4. `WaterWave/WaterFog/Refraction/RainEffect` 移植。

### P4：Post 与长期完整性

1. Temporal/exposure 与 DerivativeMain composite3 对齐。
2. Bloom downsample/blur/grade 链对齐。
3. CAS/dither/AgX/ACES/Grade 完整端口。
4. RSM GI 与 temporal GI/AO。

## 16. 当前结论

当前 Mecraft 已有完整的内置 deferred 管线骨架。阴影系统已完成从 DerivativeMain/Iris shadow warp 到 Mecraft 自有 CSM 级联阴影的迁移。

**P0 基础语义防分叉 ✅ 已完成**：`derivative_shadow.glsl` 统一 include、基础数学宏全量端口。

**P1 主光照/MecraftShadow ✅ 已完成**：
- BRDF/SunLighting/BlockLighting 完整端口
- CSM 4 cascade 级联阴影 + ShadowRenderer 模块拆分
- Cascade transition fade、PCSS、contact shadow
- Cutout/SSS 阴影修复、water/stained glass discard
- Warp 代码清理

**P3 体积雾 ✅ 已完成**：
- SEA_LEVEL/FALLOFF/动态步数/High(Ultra 密度公式全部对齐 DerivativeMain
- 21 个 debug mode、CSM shadow、cloud shadow、phase functions、powder、optical depth march、TIME_FADE、R1 dither、Bloomy Fog alpha passthrough 全部落地

**下一步进入 P2/P3**：
- P2：GBuffer Material 合同（Material.inc 逐 ID 对齐、实体/手/掉落物进 GBuffer）
- P3：Atmosphere/Cloud/Water 大规模视觉收敛；修复 water composite `uIsEyeInWater` 硬编码；实现 `UW_VOLUMETRIC_LIGHT`
- P1：修复 SSS depth（`outSssDepth` 恒为 0）
- P3/P4：RSM GI、temporal AO

后续实现继续参考 DerivativeMain 源码，但不再要求宿主 ABI 逐字复刻。

## 17. 2026-05-14 全量复扫补遗

> 本节是按用户要求对 `DerivativeMain/` 光影包实现重新做的一轮"从配置到 pass 到 lib"的完整复核。它不以既有报告为准，而是重新从 `shaders.properties`、`Settings.glsl`、`world0/*` wrapper、`program/*`、`lib/*` 扫描后归纳。结论会修正前文里若干"完成"措辞：Mecraft 当前是**管线骨架和若干核心函数可用**，不是 DerivativeMain 视觉完整移植。

### 17.1 本轮确认的 DerivativeMain 主世界 pass 顺序

主世界 `world0` 不是单一 deferred pass，而是多阶段复用 `colortex`：

| DerivativeMain pass | 核心职责 | Mecraft 当前对应 | 复扫结论 |
| --- | --- | --- | --- |
| `gbuffers_*` | 写 `colortex6/7/3`：albedo、lightmap/material、packed normal/spec/SSS | `chunk_gbuffer.*` + forward entity/item | 只覆盖 terrain 主路径；实体、手、掉落物、天气、glint、beacon 等未进同一 GBuffer 生态 |
| `shadow` | 写 depth + `shadowcolor0/1`，用 `shadowtex0/1` 区分透明/不透明 caster | CSM depth array + `shadow_depth.*` | CSM 稳定，但 DerivativeMain 双 shadowtex 透明语义、彩色阴影、水焦散、RSM 数据链未等价 |
| `deferred0` | 生成 sky capture raw sky、cloudy sky、metadata；写 `colortex4/5` | `renderSkyCapturePass`/`gameplay_sky.fs` | SkyCapture normalized UV 已修；metadata 由 GPU LUT pass 写入；仍需让所有后续 pass 完全消费同一 metadata |
| `deferred1` | 云 half/full 分辨率渲染，支持 temporal upscaling/checkerboard | `cloud_target.fs` | 云体公式近似较多；缺 shaderpack 的 checkerboard temporal upscale/history 语义 |
| `deferred4`/`SpatialFilter` | GI/AO half-res filter 或 compute 分支 | `ssao.fs/filter` | SSAO 是简化实现；RSM GI 缺失 |
| `deferred5` | 主光照、sky/cloud 合成、BRDF、SH skylight、BlockLighting、SSS | `deferred_lighting.fs` + `scene_composite.fs` | 直射主项已接 SkyCapture metadata；云只在天空像素合成已修；SH skylight、SSS depth、colored shadow、云合成能量仍需验收 |
| `deferred6/7/8` | SSR + reflection filter，rough VNDF、sky fallback | `reflection_probe.fs/filter` | 只有线性 SSR/简化 filter；rough VNDF/rough cone/时序/Hi-Z 未达 DerivativeMain |
| `composite` | 半分辨率体积雾/体积光 + 水雾预处理 | `volumetric_fog.fs` | ✅ 已完整对齐：SEA_LEVEL/FALLOFF/动态步数/High(Ultra FBM 密度公式/Low/Medium phase/High(Ultra OD/多瓣相函数/air density/TIME_FADE/Bloomy Fog/21 debug modes；缺 UW_VOLUMETRIC_LIGHT 水下体积光 |
| `composite1` | 透明/折射/水、land scattering、体积雾合成、CommonFog、Bloomy Fog mask | `scene_composite.fs` + `water_composite.fs` + `volumetric_composite.fs` | Bloomy Fog alpha 链路已完成；透明/水合成顺序按 Mecraft pass 拆分，CommonFog/特殊介质 fallback 仍未完整 |
| `composite10/12/13/15/final` | bloom downsample/blur、grade、final/TAA history | `PostProcessRenderer` + `postprocess.fs` | tonemap/grade 有移植，但 bloom/exposure/TAA history 布局不等价 |

### 17.2 必须修正的"完成"判断

| 系统 | 旧判断风险 | 复扫后状态 |
| --- | --- | --- |
| 阴影 | "完成"容易误导 | Mecraft CSM 稳定完成；DerivativeMain `COLORED_SHADOWS`、water caustics、dual `shadowtex0/1` 透明语义、RSM GI 输入仍缺 |
| 光照 | BRDF/BlockLighting 完成不等于主光照完成 | 直射主项大体对齐；SH skylight、SSS blocker depth、colored shadow tint、GI、cloud shadow 光学深度仍缺 |
| GBuffer 材质 | "33 种 ID 完成"只适用于 terrain fallback | `Material.inc` 本体很小，但 DerivativeMain 的 GBuffer 生态包含 entities/hand/water/weather/glint/PBR/rain wetness；Mecraft 未完整覆盖 |
| 大气 | LUT 函数完成不等于 sky pipeline 完成 | SkyCapture raw/cloudy atlas normalized UV 已修；`lighting_environment.glsl` 已建立；`FromSH` skylight 已接入；天气对 SkyCapture/光照的联动仍不足 |
| 云 | 32 步 ray march 完成不等于 DerivativeMain 云完成 | 光照单来源与 premultiplied 合成已修；仍缺 temporal upscaling/checkerboard/history 与完整天气动态云 |
| 水 | WaterWave/WaterFog 已高覆盖，但不应标满 | 折射/水雾/反射可用；shadow caustics、colored shadow、水 GBuffer/HandWater 语义仍缺 |
| 后处理/TAA | 视觉功能有，但不是包级等价 | TAA 缺 closest-depth velocity 同构、可选 CatmullRom、variance sigma 细节；bloom/grade 有能量和布局差异 |

### 17.3 当前画面问题的真实链路

这轮复扫后，当前"自动曝光后受光面亮、背光面黑、天空灰、体积雾像前向 fog、阳光颜色不像"的原因不是一个开关，而是下面几条链路叠加：

1. **SkyCapture atlas 采样错误已修。** `render_contract.glsl` 现在按 256x514 真实 normalized texture layout 映射 raw sky rows 0..257、cloudy sky rows 258..513，旧 `uv.y + 1.0` clamp 问题不再成立；仍需用 debug 41-44 做视觉验收。
2. **illuminance 单来源已基本完成。** `lighting_environment.glsl` 已统一从 SkyCapture metadata 读光照，deferred lighting、volumetric fog、水体、云散射已全部接入；`uSunLightColor` 已降级为 forward fallback/debug 用途。
3. **阳光颜色全链已统一到 SkyCapture metadata。** 地表直射主项、体积雾、水雾、太阳反射均从 LUT metadata 获取颜色。旧 `uSunLightColor` 和 `artisticSunIlluminance()` 不再主导 deferred 路径。
4. **Skylight 已实现 `FromSH`。** `sky_sh.glsl` 已实现 ToSH/FromSH/buildSkySH（L1 SH，25 方向采样），`deferred_lighting.fs` 已接入。SkyCapture raw sky 已剥离太阳/月亮盘（避免 SH 采到天体盘导致暴白）。
5. **阴影 shaping 默认已中性。** `shadowContrast=1.0`、`shadowMinLight=0.0` 已避免额外压暗；`shapeShadowVisibility()` 仍在正式路径，建议降级为 debug/extra。
6. **体积雾已全面完成。** `SEA_LEVEL`（`uVFogCenterHeight`）、`FALLOFF`（`uVFogHeightSpread`）、动态步数、High/Ultra 密度公式全部对齐 DerivativeMain。Bloomy Fog、TIME_FADE、R1 dither、21 个 debug mode 已落地。下一步转向 `UW_VOLUMETRIC_LIGHT` 水下体积光。
7. **自动曝光公式已回到 DerivativeMain。** 当前 `PostProcessRenderer` 已移除 Mecraft 自加的夜间亮度地板、曝光上限和改速逻辑；如果后续仍出现夜间阴影关系不对，应继续查 Grade/Tonemap 集合、Bloomy Fog、Purkinje Shift 和 deferred lighting extension，而不是继续用 exposure clamp 补偿。
8. **`uIsEyeInWater` 硬编码为 0。** 水下渲染路径未激活，影响水下体积光、水下 fog、eye-in-water 视觉。
9. **前向路径实体无 deferred 效果。** `chunk_lit_common.fs` 无 shadow/SSAO/SSR/SH skylight，实体和掉落物在 forward 路径下缺少 deferred 管线提供的光影效果。

### 17.4 体积雾/体积光复扫结论

DerivativeMain 默认 `Settings.glsl` 是 `FOG_TYPE=1`（Cloudy Fog Lite），但 `zh_cn.lang` 明确把 `FOG_TYPE` 暴露为视觉预设而不是采样质量：Low=无噪声、Medium=小型团雾、High=大型团雾、Ultra=云海。采样精度由 `VOLUMETRIC_FOG_SAMPLES` 单独控制。

- Low：纯高度雾。
- Medium：高度雾 + 2 层 3D noise，晴天中午退回轻雾。
- High：以 `SEA_LEVEL` 为中心的 4 层 FBM，输出 `* 9`。
- Ultra：5 层 FBM，频率每层 `*4`，`falloff * noise * 400 - 170` 后输出 `* 48`，这才是用户说的云海感来源。

`SEA_LEVEL` 是体积雾高度（海拔），`VOLUMETRIC_FOG_FALLOFF/FALLOFF_START` 控制高度衰减曲线。Mecraft 早期实现把高度中心写死在 shader 中，缺少可调海拔与衰减入口，因此调强度只能得到一层白雾，不能复现 DerivativeMain Ultra 的低空云海。

Mecraft 当前 `volumetric_fog.fs` 已全面完成：主积分基线、独立 air-density 体积光、`TIME_FADE`、Bloomy Fog、`SEA_LEVEL`（`uVFogCenterHeight`）、`FALLOFF`（`uVFogHeightSpread`）、动态步数（`getFogSteps()`）、High/Ultra 原始 FBM 密度公式全部对齐 DerivativeMain。21 个 debug mode 已实现。DerivativeMain 的独立 `VOLUMETRIC_LIGHT` 参考公式为：

```glsl
airDensity = VOLUMETRIC_LIGHT_STRENGTH * RayleighPhase(LdotV) * (3.0 / far);
fogColor = directIlluminance * sunlightSample * 20.0 + skyIlluminance * skylightSample * 2.0;
```

也就是说，即便没有厚雾，清空气溶胶也会产生体积光。Mecraft 目前已加入 Rayleigh-phase `airDensity`，颜色也改为 SkyCapture metadata 的 `directIlluminance`；Debug 65 已验证阴影调制成立。Clear 正午体积光弱、晨昏明显是 DerivativeMain `TIME_FADE` 默认行为，不应再误判为 shadow 失效。

此外，DerivativeMain 还有独立的 `UW_VOLUMETRIC_LIGHT` 水下体积光：`UnderwaterVolumetricLight()` 使用水吸收系数、折射后的太阳方向、shadow/translucent shadow 采样和双 forward HG 相函数积分。Mecraft 当前 `water_composite.fs` 只有 `WaterFog/UnderwaterFog` 的水雾吸收混合，且 `uIsEyeInWater` 仍有 TODO 检测缺口，没有水下体积光积分。

### 17.5 后续优先级修正

如果目标是先追 DerivativeMain 视觉，而不是继续堆 pass，优先级应调整为：

1. **移植 `deferred5.vsh` 的 sky SH 构建和 `FromSH()`。** 这是背光面不死黑、天空照明有方向性的核心。
2. **完成云/水 sunlight 单来源尾项。** `directIlluminance/sunIlluminance` 应成为云散射、水雾、太阳反射、空气透视的共同太阳光源；`uSunLightColor` 降级为 UI/旧 forward fallback/debug。
3. **去掉或旁路标准路径中的 shadow shaping。** 默认参数已中性，但标准 DerivativeMain-like 路径应直接消费 CSM shadow；风格化 contrast/minLight 作为 debug/extra。
4. **修复 SSS depth。** 当前 CSM depth 稳定，但 `shadowSssDepth` 仍为 0；需要从 CSM/PCSS blocker search 暴露 Mecraft-native SSS depth。
5. ~~**收紧体积雾云海参数化。**~~ ✅ 已完成：`SEA_LEVEL`/`FALLOFF`/动态步数/High(Ultra 密度公式/Bloomy Fog 全部对齐 DerivativeMain。
6. **补水下体积光与水体光学链。** water composite pass 的 `uIsEyeInWater` 仍硬编码为 0（`Renderer.cpp:348`），需改为 `m_eyeInWater ? 1 : 0`；deferred lighting 已动态绑定。再移植 `UW_VOLUMETRIC_LIGHT_STRENGTH/LENGTH` 对应的水下积分；水焦散可作为水体增强项，染色玻璃彩色阴影不列入当前目标。
7. **SSS/RSM 另行评估。** 当前 CSM depth 稳定，但 DerivativeMain 的 `shadowcolor0/1` 数据链不再作为完整 shaderpack ABI 目标；只保留对树叶/草 SSS、RSM 输入的 Mecraft-native 方案评估。

### 17.6 材质 ID 与实体分类漏项

DerivativeMain 的材质语义不只在 `Material.inc`。真正的分类入口是：

- `block.properties`：把方块映射到 `10001..10058`，shader 内再减去 `10000` 得到 material id。
- `entity.properties`：把 lightning、slime、boat、projectile、experience orb、end crystal、warden 等映射到实体 material id。
- `program/Gbuffers/*`：terrain、block、entities、hand、hand_water、textured、basic、weather、spidereyes、armor_glint 分别写不同 `DRAWBUFFERS`。

Mecraft 当前 `gbuffer_contract.glsl` 的 material id 常量覆盖了主要原版类别，但缺少 DerivativeMain 的 properties 映射层和实体分类层。结果是：

| 领域 | DerivativeMain | Mecraft 当前 |
| --- | --- | --- |
| Terrain 方块 | `block.properties` 大表分类，植物动画/SSS/发光/透明/矿石都经 material id | 由 Mecraft block 配置和 `vMaterialKind` 推断，覆盖主要原版类别，但不是 properties 等价 |
| 实体 | `entity.properties` + `program/Gbuffers/Entities.frag` 写 `colortex6/7/3` | mob/player/drop/hand 多数 forward，不能获得 deferred light/SSAO/SSR/SSS |
| 发光眼 | `gbuffers_spidereyes` 可由 `ENTITY_EYES_LIGHTING` 控制 | 无等价实体眼部 GBuffer/发光 pass |
| 天气粒子 | `gbuffers_weather` 使用 additive blend，且 `particles.before.deferred=true` | 粒子 forward，未纳入 deferred/体积/雾的同一语义 |
| Armor glint / damaged block / beacon | 独立 pass 和 blend 规则 | 未完整复刻 shaderpack 语义 |

因此"Material.inc 已对齐"只能理解为 **terrain fallback 材质结构可用**，不能理解为 DerivativeMain 的 GBuffer 生态已完成。

### 17.7 大气与 SkyCapture 追加差异

本轮复扫 `Atmosphere.glsl` 与 `atmosphere_lut.glsl/gameplay_sky.fs` 后，除 SkyCapture atlas UV 外，还确认这些差异：

- **观察高度不完全一致。** DerivativeMain 所有大气查询使用 `eyeAltitude`；Mecraft `gameplay_sky.fs` metadata 用 `uCameraAltitude + 100`，但 `atmGetSkyRadiance()` 内部写死 `atmPlanetRadius + 100.0`。低空/高空、验证程序 `Alt=100m` 与游戏相机高度可能因此不一致。
- **`SKY_GROUND` 不纳入当前目标。** DerivativeMain 源码支持 ground diffuse 分支，但默认被注释；Mecraft 也基本未实现 ground sky radiance。它不是当前蓝天灰白的主因，后续不作为 DerivativeMain-like 必做项。
- **MoonFlux/night eye 近似不同。** DerivativeMain `MoonFlux = abs(moonPhase - 4) * 0.25 + 0.2` 再乘 `NIGHT_BRIGHTNESS + nightVision*0.02`；Mecraft 用 `uMoonPhaseFlux`，需要确认 C++ 绑定是否完全等价。
- **星空链路可后续评估，Aurora 不纳入当前目标。** DerivativeMain `RenderStars/Aurora` 可参与 sky 和 skylight；Mecraft `gameplay_sky.fs` 有自定义 starField，但不是 DerivativeMain 的星空/极光实现。按当前目标，`AURORA/AURORA_STRENGTH` 只记录为非目标差异。
- **Debug 路径不等价。** DerivativeMain 的 sky capture 是后续采样源；Mecraft debug mode 直接预览 atlas 时会混入 raw/cloudy/metadata 布局，不能证明主画面方向采样正确。

### 17.8 水、折射、雨湿追加差异

Mecraft 的 `water_composite.fs` 对 `WaterWave.glsl`、水面 parallax、Fresnel、水雾、雨涟漪做了较多端口，但仍有几个视觉相关差异：

- **水雾颜色来源部分已迁移，太阳项仍需收口。** `WaterFog/UnderwaterFog` 已使用 `LightingEnvironment` 的 sky base；但太阳散射 multiplier 和太阳反射仍用 `uSunLightColor`，需要改为 SkyCapture metadata 的 `directIlluminance/sunIlluminance`。
- **折射实现是近似。** DerivativeMain 默认非 raytraced refraction 使用 view-space normal/up vector 与 `depthtex1` rejection；Mecraft forward water pass 用 tangent slope 近似 screen offset，深度 gap 也来自自有场景深度。
- **Shadow caustics 未接通。** DerivativeMain `Shadow.frag` 对 water 写 `shadowcolor0Out = sqrt2(caustics)`、`shadowcolor1Out.w = water height`；Mecraft CSM 当前对 water discard，避免硬阴影，但也丢失水焦散和水深 colored shadow 数据。
- **雨湿表面不完整。** DerivativeMain terrain GBuffer 在雨天会通过 `GetRainWetness()` 改 albedo、specularData、normal；Mecraft water 有雨涟漪，但 terrain wetness 与 PBR specular/wet albedo 路径不完整。

### 17.9 后处理、Bloom、Final 追加差异

DerivativeMain post 链路的关键不是"有 tonemapper"，而是 buffer/history 布局和 bloom/fog/exposure 的耦合：

- `Temporal.vert` 在 shader 内从 `colortex4` mip LOD 计算 exposure，并把上一帧 exposure 存在 `colortex5(0).a`。Mecraft 用独立 exposure downsample FBO + CPU `glReadPixels`，公式接近但历史位置和执行阶段不同。
- DerivativeMain bloom 是 tiled mip layout：`DownSample0/DownSample/BlurH/BlurV/Grade` 复用 `colortex4/5`，`CalculateBloomFog()` 还会读取 `colortex6` 的 fog transmittance 做 `BLOOMY_FOG`。Mecraft bloom 有 mip chain（separate textures），`CalculateBloomFog()` 已按 DerivativeMain 双套权重实现，fogTransmittance 通过 alpha 链路贯通 TAA/motion_blur/dof 到 postprocess，Bloomy Fog 已生效。bloom_extract.fs HDR clamp 已移除。
- DerivativeMain `Final.glsl` 在最后对 `colortex3` 做 CAS，或在非 1.0 render quality 下 CatmullRom upsample，再加 Bayer dither。Mecraft final/post 里有 CAS/dither/tonemap，但 pass 边界和输入 buffer 不同。
- DerivativeMain `Grade.glsl` 还包含 Purkinje shift（已实现，默认关闭）、white balance chromatic adaptation、rain/snow fog bloom 加权；Mecraft grading 参数已对齐，tonemap 集合已完整移植。

结论：后处理当前能用，但若要视觉追 DerivativeMain，必须把 **Bloomy Fog、exposure 输入阶段、bloom tiled energy、final CAS/dither** 当成一组一起验收。
