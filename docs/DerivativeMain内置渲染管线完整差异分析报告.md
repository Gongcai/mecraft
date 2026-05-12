# DerivativeMain 内置渲染管线完整差异分析报告

> 范围：对照 `DerivativeMain/` shaderpack 权威实现、现有专题技术分析文档、当前 Mecraft C++/OpenGL Hybrid Deferred 管线与 `assets/shaders/`。目标是找出“DerivativeMain 已实现什么、当前内置管线已转译什么、还差什么”，并覆盖 DerivativeMain 的主要实现面。
>
> 当前项目目标口径：**主世界（Overworld/world0）+ 原版 Minecraft 材质包 + 内置 DerivativeMain 风格光影**。不考虑 PBR 材质包、Distant Horizons 模组、Physics Ocean/PhysicsOceans 模组，也不实现 Nether/End 多维度专用管线。因此 LabPBR/POM/外部 PBR texture workflow、DH pass、PhysicsOceans、world-1/world1 只作为 DerivativeMain 源码中存在的非目标分支记录，不计入当前必须补齐缺口。

## 0. 总结结论

当前 Mecraft 已经不是旧的“前向 + 单阴影 + 基础后处理”状态，而是具备完整 Hybrid Deferred pass graph 的内置光影地基。实际代码中已经存在：

- `GBuffer -> Velocity -> Shadow(depth/color/normal) -> SSAO/filter -> DeferredLighting -> Reflection/filter -> Cloud -> SceneComposite -> VolumetricFog/composite -> TAA -> MotionBlur/DoF -> Water/Transparent -> History -> Post` 的闭合链路。
- `DeferredRenderTargets` 已有 GBuffer、ShadowColor/ShadowNormal、SceneLighting、SceneComposite、SceneResolved、TransparentComposite、HalfRes、Reflection、Cloud、Velocity、History、Atmosphere 3D LUT 等资源。
- Shader 侧已有 DerivativeMain 风格材质 ID、roughness/f0/emission/SSS、PCSS/contact/cloud shadow、shadow color tint、Atmosphere LUT 采样、SSR、half-res cloud/fog、水体 composite、TAA、AgX/ACES 后处理等入口。

但它仍不是“照抄 DerivativeMain”的视觉实现。主要差距已经从“有没有管线”转移为：

1. DerivativeMain 的 Iris/OptiFine 多 `colortex` 语义没有逐 pass 等价转译，当前是项目自有 FBO/资源图。
2. 大气只接入了 `Final.lut` 3D LUT 和自有 `atmosphere_lut.glsl`，没有完整复刻 DerivativeMain 的 Bruneton `Transmittance/Scattering/Irradiance/Final` 查询体系与天空辐照度数据流。
3. GBuffer 契约已接近；在“原版材质包”目标下，重点差距是法线/材质 ID/发光/SSS/透明分类与实体/手/天气/破坏方块等 shaderpack 分类，LabPBR/POM/PBR 材质包路径为非目标。
4. 光照模型已做 DerivativeMain-inspired 近似，但不是 `Deferred5 + SunLighting + BlockLighting + GlobalIllumination + AmbientOcclusion` 的完整等价实现，尤其缺 RSM GI、SH 天空光完整预计算、DerivativeMain 精确 block light/SSS/SSS shadow 逻辑。
5. 阴影资源已有 depth/color/normal，但 DerivativeMain 的 quartic distortion、PCSS blocker 细节、屏幕空间阴影、透明阴影、RSM 复用仍未完整照搬；DH shadow 为非目标。
6. 云、体积雾、水、SSR、后处理都有 pass 壳和部分算法，但大多是低成本/近似版本，不是 `program/` 与 `lib/` 的逐函数移植。
7. 主世界 world0 的 Iris properties 中 blend/scale/flip/conditional pass 还没有完整内置对应；world-1/world1 多维度分支与 Distant Horizons 分支为非目标。

因此当前状态可以定义为：**架构完成度高，DerivativeMain 权威算法覆盖度中等，视觉等价度仍偏低到中等**。下一阶段应从“补 pass”转为“按 DerivativeMain 文件逐函数收敛 shader 语义”。

## 1. DerivativeMain 权威实现清单

### 1.1 顶层配置与资源

权威配置在 `DerivativeMain/shaders.properties`：

- 全局兼容语义：`oldHandLight=false`、`oldLighting=false`、`separateAo=true`、`shadow.culling=false`。
- 核心纹理资源：
  - `texture/Noise2D.png`
  - `texture/Bayer256.png`
  - `texture/RippleNormal.png`
  - `texture/Atmosphere/Final.lut`，声明为 `TEXTURE_3D RGBA32F 256 128 33 RGBA FLOAT`
  - 同目录还存在 `Transmittance.lut`、`Scattering.lut`、`Irradiance.lut`
- GBuffer/Shadow/Weather/Post 的 blend 由 properties 显式配置；DH 相关配置存在于 DerivativeMain，但当前非目标：
  - 大多数 GBuffer pass `blend=off`
  - 天气粒子为加法 `ONE ONE ONE ONE`
  - armor glint 使用颜色乘法
  - 部分 `gbuffers_* .colortex6` 使用 alpha 混合
- 自定义 uniform 计算：
  - `worldLightVector/worldSunVector`
  - `taaOffset`
  - `waterAbsorption`
  - `eyeSkylightFix`
  - `wetnessCustom/weatherSnowySmooth`
  - `BiomeSandstorm/BiomeGreenShift`
  - `worldTimeCounter/worldTimeChanged`
  - `volFogWind/volFogDensity`
  - `timeNoon/timeMidnight/timeSunrise/timeSunset`
- Conditional pass：
  - `gbuffers_spidereyes` 受 `ENTITY_EYES_LIGHTING` 控制
  - `composite2` 受 `DOF_ENABLED`
  - `composite10/12/13` 受 `BLOOM_ENABLED`
  - `composite14` 受 `MOTION_BLUR`
  - `world-1/composite` 禁用（非目标维度，仅记录）
- Buffer flip：
  - `flip.deferred6.colortex0=false`
  - `flip.deferred6.colortex1=false`
  - `flip.composite15.colortex0=true`
  - `flip.composite15.colortex1=true`
  - `flip.composite1.colortex5=true`

当前 Mecraft 差异：

- 已加载 `DerivativeMain/texture/Atmosphere/Final.lut`，并作为 `GL_RGBA32F 256x128x33` 3D texture 接入。
- 未建立 `shaders.properties` 等价层；blend/scale/flip/conditional pass 都是 C++ 手写逻辑。
- `Noise2D/Bayer/RippleNormal` 没有完整按 DerivativeMain 资源名、wrap/filter、缺失策略统一管理。
- `wetness/biome/weather/time phase` 有项目自有参数，但未按 properties 中的 smoothing 与 biome 规则逐项复刻。

### 1.2 DerivativeMain program pass 清单

`DerivativeMain/program/` 包含：

- Deferred：`Deferred0.glsl`、`Deferred1.glsl`、`Deferred2.glsl`
- Filter：`SpatialFilter.frag/.comp`、`ReflectionFilter.frag/.comp`
- GBuffer：`ArmorGlint`、`Basic`、`Beaconbeam`、`Block`、`Damagedblock`、`Entities`、`Hand`、`HandWater`、`Spidereyes`、`Terrain`、`Textured`、`Water`
- Shadow：`Shadow.frag/.vert`
- DH：`DH/Shadow`、`DH/Terrain`、`DH/Water`（非目标分支，仅记录）
- Post：`BlurH`、`BlurV`、`DoF`、`DownSample`、`DownSample0`、`Final`、`Grade`、`MotionBlur`、`Temporal`

`DerivativeMain/world0/` 将这些 include 成 OptiFine/Iris 标准 pass：

- `gbuffers_*`
- `shadow`
- `dh_*`（非目标分支，仅记录）
- `deferred` 到 `deferred8`
- `composite` 到 `composite15`
- `final`

当前 Mecraft 差异：

- 没有直接执行 shaderpack 标准 pass 名；而是 C++ 显式函数：`renderSkyCapturePass`、`renderGBuffer`、`renderVelocityPass`、`renderShadowMap`、`renderSsaoPass`、`renderDeferredLightingPass`、`renderReflectionPass`、`renderCloudPass`、`renderSceneCompositePass`、`renderVolumetricFogPass`、`renderTemporalResolvePass`、`renderWaterCompositePass` 等。
- Terrain/Water/Shadow/Post 的功能入口已对应；当前必须关注的缺口是 `ArmorGlint/Beaconbeam/Damagedblock/Entities/Hand/HandWater/Spidereyes/Textured/Weather` 的主世界等价 pass。DH 不纳入当前目标。
- Compute shader 版本的 `SpatialFilter.comp`、`ReflectionFilter.comp` 没有原样移植；当前主要用 fullscreen fragment pass。

## 2. Render Target 与 GBuffer 差异

### 2.1 DerivativeMain colortex 布局

专题文档总结的权威布局：

| Buffer | 格式 | DerivativeMain 用途 |
| --- | --- | --- |
| `colortex0` | RGBA16F | GI/AO temporal、spatial filter 输入输出、部分材质/反射数据 |
| `colortex1` | RGBA16F | 体积雾/光、天空 temporal |
| `colortex2` | RGBA16F | SSR/反射、云渲染数据 |
| `colortex3` | RGBA16 | TAA 后最终不透明颜色；半透明 normal/albedo packed |
| `colortex4` | R11F_G11F_B10F | 延迟光照 HDR scene；Bloom tile chain；也绑定 atmosphere LUT 资源槽 |
| `colortex5` | RGBA16F | sky capture、irradiance/weather rows、TAA history、exposure |
| `colortex6` | RGB8 | albedo；Bloom fog transmittance |
| `colortex7` | RGB8/RGBA8 | mc lightmap + material ID；DerivativeMain 还可放 POM shadow；同时绑定 RippleNormal |

权威 GBuffer：

- `gbuffers_terrain`：
  - `colortex6.rgb = albedo`
  - `colortex7.rg = mcLightmap`
  - `colortex7.b = materialID / 255`
  - `colortex7.a = POM shadow`（POM 为 PBR/高级材质路径，当前非目标）
  - `colortex3.xy = oct normal`
  - `colortex3.zw = PackUnorm2x8(specular.rg / specular.ba)`
- `gbuffers_water`：
  - `colortex7 = lightmap + materialID`
  - `colortex2 = reflection data`
  - `colortex3.xy = normal`
  - `colortex3.zw = translucent albedo`

### 2.2 当前 Mecraft 资源布局

当前 `DeferredRenderTargets`：

| 当前资源 | 格式 | 当前用途 |
| --- | --- | --- |
| `GAlbedoMaterial` | RGBA8 | linear albedo.rgb + emissive hint.a |
| `GNormalAo` | RGBA16F | world normal.rgb + vertex AO.a |
| `GVoxelLight` | RG8 | sky light + block light |
| `GMaterial` | RGBA8 | roughness/f0/emission/sss |
| `GMaterialAux` | RGBA8 | material kind/wetness/porosity/metalness |
| `Depth` | DEPTH32F | reconstruction/SSAO/透明/体积 |
| `ShadowDepth` | DEPTH32F | shadow map |
| `ShadowColor` | RGBA8 | colored shadow / caustics albedo |
| `ShadowNormal` | RG16F | encoded normal |
| `SSAO/SSAOFiltered` | R8 | AO |
| `SceneLighting` | RGBA16F | deferred lighting HDR |
| `SceneComposite` | RGBA16F | opaque scene + reflection/cloud |
| `SceneResolved` | RGBA16F | fog/TAA/透明前后的最终 HDR scene |
| `TransparentComposite` | RGBA16F + DEPTH32F | 水/普通透明 scratch |
| `HalfRes` | RGBA16F | half-res volumetric |
| `Reflection` | RGBA16F | SSR/reflection |
| `Cloud` | RGBA16F half-res | cloud color/transmittance placeholder |
| `SkyCapture` | RGBA16F 256x? | sky capture + irradiance rows |
| `Velocity` | RG16F | screen-space velocity |
| `HistoryScene/Depth/Reflection/Cloud` | ping-pong | temporal reuse |
| `AtmosphereLut3D` | RGBA32F 256x128x33 | Final.lut |

差异：

- 当前使用项目语义资源名，而非 `colortex0-7` 原样布局。这对 C++ 管线更清晰，但不能直接照搬 shaderpack 代码。
- 当前 normal 存 world-space RGB16F，不是 DerivativeMain `colortex3.xy` oct encoding。质量更直接、带宽更高；与 DerivativeMain 函数接口不等价。
- 当前 material 拆成 `GMaterial/GMaterialAux`，不是 `PackUnorm2x8` 双通道压缩。这降低解码误差，但移植时必须适配 `Material.inc`。
- 当前 `colortex5` 的 sky capture 概念已存在，但行布局、sky/cloud 双层、irradiance/weather rows 只部分对应。
- DerivativeMain 的 `colortex0/1/2` 历史复用语义非常密集，当前拆成独立 history/reflection/cloud，更清楚但与原 pass flip 机制不同。

## 3. GBuffer 与材质系统差异

### 3.1 已对齐部分

当前 `gbuffer_contract.glsl` 已显式写成 DerivativeMain-compatible contract：

- Material IDs 覆盖：
  - 草/植物：1-6
  - leaves：7
  - SSS snow/ice：10
  - lava/fire/emissive：15、19-34、36
  - stained glass/water/ice：16/17/18
  - ore/nether ore：57/58
- `derivativeFragmentMaterialId` 将 1-5 合并到 6，贴近 DerivativeMain 的 GBuffer 合同。
- `SurfaceMaterial` 包含 roughness/f0/emission/sss。
- `SurfaceMaterialAux` 包含 materialKind/wetnessMask/porosity/metalness。
- `chunk_gbuffer.fs` 写入 albedo、voxel light、material、material aux。

### 3.2 主要缺口

- DerivativeMain `program/Gbuffers/Terrain.frag` 包含 LabPBR/POM/PBR 材质包路径，但当前目标是原版材质包，这部分不作为必须移植项。
- 在原版材质目标下，仍需要补的是：
  - 原版方块/实体/手持物/天气/破坏方块的 material ID 与透明分类。
  - 无 PBR 贴图时的 roughness/f0/emission/SSS fallback 是否与 DerivativeMain 一致。
  - 原版 atlas/tint/lightmap/alpha cutout 对 GBuffer 的写入细节。
- DerivativeMain grass/plant vertex animation 在 `Terrain.vert` 和材质 ID 中绑定，当前只部分有材质 ID，动画规则未完整照搬。
- DerivativeMain `Block/Entities/Hand/HandWater/Basic/Textured/Damagedblock/Beaconbeam/Spidereyes/Weather` 各自有 GBuffer 行为；当前多数实体、手持物、掉落物、UI/outline 仍走项目已有 forward shader 或简化路径。
- DerivativeMain alpha/cutout/translucent 的 `colortex6` blend 语义没有完全转译，当前水体从普通透明拆出，但玻璃/冰/植物半透明还偏 forward。

## 4. 延迟 pass 架构差异

### 4.1 DerivativeMain 权威 pass 顺序

主世界核心：

1. GBuffer：terrain/block/entities/water/hand/weather 等写入 colortex。
2. Shadow。DH shadow 为非目标分支。
3. `deferred0`：天空捕获、大气透射、sky map、irradiance/weather rows。
4. `deferred1`：体积云/平面云/极光，输出 `colortex2`。
5. `deferred2`：GI/AO 或天空数据 temporal accumulation。
6. `deferred3/4`：GI/AO spatial filter，部分为 compute。
7. `deferred5`：主光照核心，输出 `colortex4` scene lighting 与 `colortex0` specular/material data。
8. `deferred6`：opaque SSR，输出 `colortex2`。
9. `deferred7/8`：reflection spatial filter，fragment/compute 两种路径。
10. `composite`：透明合成 + 体积雾。
11. `composite1`：折射、反射、雾效、水雾合成。
12. `composite2`：DoF。
13. `composite3`：TAA + velocity + exposure。
14. `composite10/12/13`：Bloom downsample/blur。
15. `composite14`：Motion blur。
16. `composite15`：Grade/tonemap。
17. `final`：CAS + dither 输出。

### 4.2 当前 Mecraft pass 顺序

代码中的 Hybrid Deferred 大致为：

1. `renderSkyCapturePass`
2. `bindGBuffer` + opaque/cutout chunk draw
3. `renderVelocityPass`
4. `renderShadowMap`
5. `renderSsaoPass` + `renderSsaoFilterPass`
6. `copyFramebufferColorToSceneLighting`
7. `renderDeferredLightingPass`
8. `renderReflectionPass`
9. `renderReflectionFilterPass`
10. `renderCloudPass`
11. `renderSceneCompositePass`
12. `renderVolumetricFogPass` + `compositeVolumetricFogPass`
13. `renderTemporalResolvePass`
14. `renderMotionBlurPass`
15. `renderDofPass`
16. `renderWaterCompositePass`
17. generic transparent
18. history copy/swap
19. `PostProcessRenderer`

差异：

- Mecraft 把 sky capture 放在 GBuffer 前；DerivativeMain `deferred0` 在 GBuffer/shadow 后的 deferred 阶段执行。
- Mecraft 将 reflection/cloud/fog 拆成专用资源；DerivativeMain 复用 `colortex2/1/4/5` 并依赖 flip。
- Mecraft TAA 已是独立 pass；DerivativeMain TAA 在 composite3 中同时承担 motion vector/exposure/temporal。
- Mecraft water 是 deferred 后的独立 forward/composite；DerivativeMain 水先在 `gbuffers_water` 写 reflection/normal/albedo，后在 `composite1` 合成。
- Mecraft postprocess 是项目全局 post；DerivativeMain `composite10-15/final` 完全在 shaderpack 多 buffer 内完成。

## 5. 阴影系统差异

### 5.1 DerivativeMain 权威实现

DerivativeMain 阴影包含：

- `program/Shadow/Shadow.vert/.frag`
- shadow depth + `shadowcolor0`，透明/彩色阴影信息
- shadow normal/color 用于 colored shadow、SSS、RSM/GI
- quartic shadow distortion：近处分辨率集中
- `SHADOW_MAP_BIAS`、normal offset、PCF depth bias
- PCSS：
  - blocker search
  - penumbra radius
  - rotated spiral PCF
  - dither/temporal sampling
- screen-space shadow/contact shadow
- SSS 双层吸收衰减
- RSM global illumination
- cloud shadow
- Distant Horizons shadow（非目标分支，仅记录）

### 5.2 当前状态与差距

已具备：

- `ShadowDepth`、`ShadowColor`、`ShadowNormal` 资源。
- `ShadowProjectionData`：modelView/projection/inverse/viewProj/extent/texelWorldSize。
- deferred lighting 中有 PCF/PCSS/contact/cloud shadow、normal offset、shadow color tint 的入口。
- shadow pass 会绑定 color/normal attachment。

差异：

- 当前默认 no-warp，quartic/radial warp 只保留研究/debug 方向；DerivativeMain 是权威 distortion。
- 当前 PCSS 是近似版本，blocker search 半径、sample pattern、depth bias 与 DerivativeMain 不完全一致。
- 屏幕空间阴影不是 DerivativeMain `SunLighting.glsl` 中 12 步沿光方向算法的完整端口。
- colored shadow 资源存在，但透明材质写入/读取逻辑不完整，彩色玻璃、实体、手、水等没有全链路覆盖。
- RSM GI 未完整实现；shadow color/normal 尚未作为 GI 采样源重建间接光。
- DH shadow 没有实现，但按当前目标不计入必须补齐项。
- SSS 与 shadow 的双层吸收/厚度估算只近似存在。

## 6. 光照、BRDF、GI 与 AO 差异

### 6.1 DerivativeMain 权威实现

相关文件：

- `lib/Surface/BRDF.glsl`
- `lib/Lighting/SunLighting.glsl`
- `lib/Lighting/BlockLighting.glsl`
- `lib/Lighting/AmbientOcclusion.glsl`
- `lib/Lighting/GlobalIllumination.glsl`
- `lib/Head/Material.inc`

核心特性：

- Cook-Torrance specular
- GGX NDF
- Smith GGX visibility
- Schlick Fresnel
- Hammon diffuse
- direct sun/moon illuminance 来自 atmosphere LUT/sky capture
- block light 非物理 Minecraft 衰减与 blackbody/材质发光规则
- held light
- ore glow/color detection
- spiral SSAO
- RSM GI
- temporal GI/AO accumulation
- spatial bilateral filter
- SH sky lighting

### 6.2 当前状态与差距

已具备：

- `deferred_lighting.fs` 中有 GGX、Smith、Fresnel、Hammon-like diffuse、rough terminator fill。
- 已覆盖大量 DerivativeMain material ID 的 emissive 规则，包括 torch/fire/glowstone/sea lantern/redstone/soul fire/amethyst/glowberry/rails/beacon/sculk/glow lichen/ore。
- 有 SSAO 和 5x5 bilateral filter。
- 有 sky capture 与 atmosphere LUT 参与 lighting。
- 有 wetness 对 roughness/f0 的影响。

差异：

- BRDF 不是 `lib/Surface/BRDF.glsl` 的逐函数精确移植，能量标定、diffuse/specular 权重与 DerivativeMain 不一致。
- `SunLighting.glsl` 中 SSS、fake bounce、cloud shadow、screen-space shadow 的顺序与数值未完全照搬。
- `BlockLighting.glsl` 的衰减和材质发光虽然大体覆盖，但不是逐式等价，blackbody 方块光仍需校准。
- `GlobalIllumination.glsl` RSM GI 缺失。
- `AmbientOcclusion.glsl` 的 spiral SSAO + temporal accumulation + spatial filter 只部分对应。
- SH sky lighting 未按 DerivativeMain `deferred5.vsh` 从 sky capture 多方向采样构建完整 SHR/SHG/SHB。

## 7. 大气散射与 LUT 差异

### 7.1 DerivativeMain 权威实现

相关文件：

- `lib/Atmosphere/Atmosphere.glsl`
- `texture/Atmosphere/Transmittance.lut`
- `texture/Atmosphere/Scattering.lut`
- `texture/Atmosphere/Irradiance.lut`
- `texture/Atmosphere/Final.lut`
- `program/Deferred0.glsl`
- `world0/deferred5.vsh`
- `world0/composite1.fsh`

核心特性：

- Bruneton precomputed atmosphere。
- Earth-like atmosphere parameters：
  - Rayleigh/Mie scattering
  - ozone/absorption
  - ground albedo
  - custom sun angular radius
  - Mie phase g
- nonlinear LUT coordinate mapping。
- `GetSkyRadiance`、`GetSkyRadianceToPoint`、`GetSunAndSkyIrradiance`、sun transmittance。
- sky capture 分辨率与 `ProjectSky` 映射。
- sky capture 特殊 rows 存 direct/sky/sun/moon illuminance 与 cloud weather。
- sun/moon/star/aurora。
- land atmospheric scattering fallback。
- weather/rain desaturation 与 biome fog。

### 7.2 当前状态与差距

已具备：

- `DeferredRenderTargets::loadAtmosphereLut` 加载 `Final.lut` 为 3D texture。
- `assets/shaders/atmosphere_lut.glsl` 作为项目统一采样接口。
- Sky capture pass 已写天空与 cloudy sky capture，并写 illuminance/weather rows。
- `deferred_lighting`、`reflection`、`cloud`、`volumetric_fog`、`water_composite` 均绑定 atmosphere LUT。

差异：

- 只接入 `Final.lut`，没有按 DerivativeMain 的完整 `Transmittance/Scattering/Irradiance` 数据模型重建 Bruneton 查询。
- `Atmosphere.glsl` 的物理参数、坐标映射、太阳/月亮光通量、aerial perspective 函数未完整端口。
- sky capture 分辨率/行布局和 DerivativeMain 接近，但不是完全相同的 `skyCaptureRes` 与 `colortex5` 语义。
- Aurora、stars、moon disk、sun disk、night Purkinje/mesopic 大气联动没有完全照搬。
- `LAND_ATMOSPHERIC_SCATTERING` 与主世界 border fog 还不是 DerivativeMain 的完整分支；Nether/End fog 为非目标。

## 8. 云系统差异

### 8.1 DerivativeMain 权威实现

相关文件：

- `lib/Atmosphere/VolumetricClouds.glsl`
- `lib/Atmosphere/PlanarClouds.glsl`
- `lib/Atmosphere/Aurora.glsl`
- `program/Deferred1.glsl`
- `program/Deferred2.glsl`
- `world0/deferred5.fsh`

核心特性：

- Cumulus volumetric clouds。
- Cirrus planar clouds。
- Cirrocumulus planar clouds。
- 3D noise/FBM density。
- planet curvature。
- temporal upscaling。
- depth culling。
- Beer-Lambert transmittance。
- multi-scattering approximation。
- multi-lobe phase function。
- powder effect。
- sun optical depth sampling。
- cloudDynamicWeather。
- cloud shadows 进入 lighting。
- atmosphere aerial perspective 融合。

### 8.2 当前状态与差距

已具备：

- `CloudTarget` half-res 资源。
- `renderCloudPass`。
- `cloud_target.fs` 绑定 depth、skyCapture、noise、atmosphere LUT。
- lighting 中已有 procedural cloud shadow factor。
- history cloud 资源已存在。

差异：

- 当前云更像 placeholder/低成本 half-res pass，不是 `VolumetricClouds.glsl` 的 32 步体积云。
- 3D density、sun optical depth、multi-scattering、powder、planet curvature 未完整端口。
- Temporal upscaling/reprojection 资源有，但云 history reject、cloud depth/transmittance 还缺。
- Cirrus/Cirrocumulus 平面云未完整按 DerivativeMain 双层逻辑实现。
- Aurora 只有资源/文档层认识，当前无完整体渲染实现。

## 9. 体积雾与体积光差异

### 9.1 DerivativeMain 权威实现

相关文件：

- `lib/Atmosphere/VolumetricFog.glsl`
- `lib/Atmosphere/VolumetricFogEnd.glsl`（End 维度非目标，仅记录）
- `world0/composite.fsh`
- `world0/composite1.fsh`

核心特性：

- half-res volumetric fog/light。
- Low/Medium/High/Ultra 密度模式。
- 3D noise + wind animation。
- biome/green/sandstorm/rain 驱动。
- phase function 组合。
- multi-scattering approximation。
- powder effect。
- sun optical depth。
- shadow sampling + colored shadow。
- cloud shadow。
- underwater volumetric light。
- End-specific volumetric fog（非目标）。
- depth-aware bilateral upsample。
- fog transmittance 输出给 bloom fog。

### 9.2 当前状态与差距

已具备：

- `HalfRes` RGBA16F。
- `renderVolumetricFogPass` + `compositeVolumetricFogPass`。
- `volumetric_fog.fs` 绑定 depth、skyCapture、noise、shadowDepth、shadowColor、atmosphere LUT。
- 有 density/phase/shadow/cloud/weather 参数入口。

差异：

- 密度模式、FBM 层级、BiomeSandstorm/BiomeGreenShift、worldTime smoothing 未完整照搬。
- Shadow sampling 只近似，未完全复用 DerivativeMain shadow distortion/colored shadow/SSS。
- depth-aware upsample 与 temporal accumulation 不完整。
- Bloom fog transmittance 没有完全复刻 `colortex6` 雾透射率通路。
- underwater volumetric light 不完整；End fog 与 DH depth support 为非目标。

## 10. 水体、折射与透明差异

### 10.1 DerivativeMain 权威实现

相关文件：

- `program/Gbuffers/Water.frag/.vert`
- `lib/Water/WaterWave.glsl`
- `lib/Water/WaterFog.glsl`
- `lib/Water/PhysicsOceans.glsl`（Physics Ocean 非目标，仅记录）
- `lib/Surface/Refraction.glsl`
- `lib/Surface/ScreenSpaceReflections.glsl`
- `lib/Surface/RainEffect.glsl`
- `world0/composite1.fsh`

核心特性：

- 水体先在 GBuffer 写 normal/albedo/reflection data。
- `WaterHeight` 程序化多层水波。
- Water parallax。
- PhysicsOceans 可选，但当前目标明确不考虑。
- RippleNormal 雨滴涟漪。
- Fresnel/GGX/VNDF SSR。
- Screen-space refraction。
- raytraced refraction optional。
- chromatic dispersion optional。
- WaterFog/UnderwaterFog。
- underwater volumetric light。
- caustics。
- foam。
- sky reflection from sky capture。

### 10.2 当前状态与差距

已具备：

- 水体已从 generic transparent 独立为 `renderWaterCompositePass`。
- `water_composite.fs` 绑定 scene/depth/noise/reflection/skyCapture/atmosphere LUT，且有 water absorption、IOR、wave、weather、underwater 等参数。
- 已实现水深、WaterFog/UnderwaterFog 风格函数、screen-space reflection、sky reflection、sun reflection、水波/parallax 的部分逻辑。

差异：

- 当前是 deferred 后 forward/composite 水；DerivativeMain 是 `gbuffers_water` 先写 reflection/normal/albedo，再 `composite1` 合成。
- `PhysicsOceans` 未接，但按当前目标不需要接。
- RippleNormal 雨滴涟漪未按 `texture/RippleNormal.png` 完整接入。
- SSR/GGX VNDF 与 DerivativeMain `CalculateSpecularReflections` 未完全一致。
- Refraction、raytraced refraction、dispersion 未完整；DH depth support 为非目标。
- Foam、caustics、underwater volumetric light 未完整。
- 玻璃/冰/彩色透明与水体一样的透明材质系统未完整统一。

## 11. SSR、反射与湿润表面差异

### 11.1 DerivativeMain 权威实现

相关文件：

- `lib/Surface/ScreenSpaceReflections.glsl`
- `lib/Surface/ReflectionFilter.glsl`
- `program/ReflectionFilter.frag/.comp`
- `world0/deferred6.fsh`
- `world0/deferred7.fsh`
- `world0/deferred8.csh`

核心特性：

- view-space ray marching。
- depthtex1 hit testing。
- GGX VNDF importance sampling。
- roughness-aware reflection。
- real sky fallback。
- bilateral reflection filter。
- luma/chroma sharpening。
- compute shader optimized path。
- transparent/water reflection data shared through `colortex2`。

### 11.2 当前状态与差距

已具备：

- `Reflection` RGBA16F resource。
- `renderReflectionPass` and `renderReflectionFilterPass`。
- `reflection_probe.fs` 绑定 scene/depth/normal/material/materialAux/skyCapture/atmosphere LUT。
- `reflection_filter.fs` 有双边滤波与锐化风格。
- history reflection 资源存在。

差异：

- SSR trace、step/refinement/hit rules 与 DerivativeMain 不完全一致。
- `Material.inc` 的 `hasReflections/isRough/isMetal` 逻辑没有逐项端口。
- compute filter 路径未实现。
- roughness-aware temporal reject 未完整。
- sky fallback 与 atmosphere/sky capture 的权重需按 DerivativeMain 校准。
- wetness/puddles/rain splash 未完整进入材质与 reflection。

## 12. 后处理、HDR、Exposure、TAA 差异

### 12.1 DerivativeMain 权威实现

相关文件：

- `program/Post/Temporal.frag/.vert`
- `program/Post/DoF.glsl`
- `program/Post/DownSample0.glsl`
- `program/Post/DownSample.glsl`
- `program/Post/BlurH.glsl`
- `program/Post/BlurV.glsl`
- `program/Post/Grade.glsl`
- `program/Post/MotionBlur.glsl`
- `program/Post/Final.glsl`
- `lib/Post/ACES.glsl`
- `lib/Post/AgX.glsl`

核心特性：

- TAA：
  - exposure calculation
  - velocity buffer
  - temporal reprojection
  - CatmullRom/bicubic sampling
  - Reinhard domain blending
  - YCoCg/variance style clamp
- DoF：
  - physical CoC
  - focus modes
  - anamorphic bokeh
  - magnification chromatic aberration
- Bloom：
  - tile/mip downsample chain
  - H/V Gaussian blur
  - fog-bloom weights
  - exposure-modulated bloom composite
- Grade：
  - ACES AcademyFit/AcademyFull
  - AgX Minimal/Full
  - color grading
  - Purkinje shift
  - white balance
  - vignette
  - cinematic mode
- Final：
  - CAS sharpening
  - dithering

### 12.2 当前状态与差距

已具备：

- `temporal_resolve.fs` 有 velocity、history depth、CatmullRom/reprojection、YCoCgR/variance clipping、Reinhard domain blend。
- `motion_blur.fs`、`dof.fs` 存在。
- `postprocess.fs` 有 Reinhard/ACES/Filmic/AgX、bloom composite、underwater tint、vignette/noise dither/sharpen。
- `PostProcessRenderer` 使用 HDR scene 与 bloom extract/blur。

差异：

- DerivativeMain 的 TAA 在 composite3 中连带 exposure 与 motion vector；当前拆分后尚未完全等价。
- Auto exposure 不是 DerivativeMain `Temporal.vert`/Grade 流程的完整实现，也缺稳定 luminance pyramid/历史适应策略。
- Bloom 仍偏基础 extract/blur，不是 `DownSample0/DownSample/BlurH/BlurV/Grade` 的 tile/mip/fog bloom 链。
- ACES/AgX 只是近似实现，未完整端口 `lib/Post/ACES.glsl` 与 `AgX.glsl` 的全部变体和色彩空间转换。
- Purkinje、white balance、film/cinematic、21:9、CAS/dither 的参数与顺序未完全照搬。
- UI/手持物/透明物在 HDR/post 边界上仍需明确 DerivativeMain 等价策略。

## 13. 主世界范围与非目标分支

### 13.1 DerivativeMain 权威实现

DerivativeMain 有三套 world，但当前只实现主世界 `world0`：

- `world0` 主世界：完整大气、体积云、天空、GI/AO、SSR、水、后处理。这是当前唯一权威参照。
- `world-1` 下界：非目标。
- `world1` 末地：非目标。

DerivativeMain 还包含 Distant Horizons：

- `dh_terrain`
- `dh_water`
- `dh_shadow`
- DH depth support in SSR/refraction/fog/shadow。

这些 DH pass 当前全部为非目标，仅作为源码中存在的分支记录。

### 13.2 当前状态与差距

- 当前 Mecraft 的单主世界方向与目标一致，不需要为 Nether/End 做维度参数化。
- Distant Horizons 没有内置等价系统，但按当前目标不需要实现。
- `gbuffers_weather`、`armor_glint`、`beaconbeam`、`spidereyes`、`hand_water`、`damagedblock` 等主世界特殊 pass 仍需要建立权威对应。

## 14. 文件级覆盖矩阵

| DerivativeMain 模块 | 当前 Mecraft 对应 | 覆盖状态 |
| --- | --- | --- |
| `shaders.properties` | C++ settings + manual pass graph | 部分；缺 properties 等价语义 |
| `Settings.glsl` | C++ pipeline settings + shader uniforms | 部分；宏/预设未完整 |
| `lib/Head/Common/Functions/Uniforms/Mask` | `render_contract.glsl`、`atmosphere_lut.glsl`、C++ uniforms | 部分 |
| `lib/Head/Material.inc` | `gbuffer_contract.glsl` | 中高；原版材质 fallback 需继续对齐；LabPBR/PBR 非目标 |
| `program/Gbuffers/Terrain` | `chunk_gbuffer.vs/fs` | 中等；需对齐原版材质 ID/tint/lightmap/alpha/fallback；LabPBR/POM 非目标 |
| `program/Gbuffers/Water` | `water_composite.fs` + transparent water mesh | 中等；架构不同 |
| `program/Gbuffers/Block/Entities/Hand/...` | forward/item/entity shaders | 低到中 |
| `program/Shadow` | `shadow_depth.vs/fs` | 中等；资源有，算法未全等价 |
| `program/DH/*` | 无直接对应 | 非目标 |
| `program/Deferred0` | `GameplaySkyRenderer` + `renderSkyCapturePass` | 中等；LUT/rows 部分 |
| `program/Deferred1` | `cloud_target.fs` | 低到中；正式体积云缺 |
| `program/Deferred2` | `temporal_resolve` + history resources + SSAO | 部分；GI/AO/sky temporal 不等价 |
| `SpatialFilter` | `ssao_filter.fs` | 部分 |
| `world0/deferred5` | `deferred_lighting.fs` | 中等；核心近似最多 |
| `world0/deferred6` | `reflection_probe.fs` | 中等 |
| `ReflectionFilter` | `reflection_filter.fs` | 中等；compute 缺 |
| `world0/composite` | `volumetric_fog.fs` + composite | 中等 |
| `world0/composite1` | `scene_composite.fs` + `water_composite.fs` | 中等；折射/水/雾不完整 |
| `Post/Temporal` | `temporal_resolve.fs` | 中高；参数/曝光不等价 |
| `Post/DoF` | `dof.fs` | 部分 |
| `Post/DownSample/Blur/Grade` | `PostProcessRenderer` + `postprocess.fs` + bloom shaders | 部分 |
| `Post/Final` | `postprocess.fs` sharpen/dither | 部分 |
| `lib/Atmosphere/*` | `atmosphere_lut.glsl` + sky/cloud/fog shaders | 部分 |
| `lib/Water/*` | `water_composite.fs` | 部分 |
| `lib/Surface/*` | `deferred_lighting/reflection/water` scattered functions | 部分 |
| `world-1/world1` | no full dimension-specific pipeline | 非目标 |

## 15. 优先级建议

### P0：先固定权威合同

1. 建立 `DerivativeMainPorting.md` 或 shader 注释索引：每个当前 shader 函数对应 DerivativeMain 哪个文件/函数。
2. 明确“不原样 colortex，但语义等价”的映射表，并在 shader include 中统一。
3. 将 `Noise2D/Bayer256/RippleNormal/Atmosphere LUT` 资源纳入正式 ResourceMgr 名称、格式、wrap/filter、缺失策略。

### P1：视觉收益最高、风险可控

1. `deferred_lighting.fs` 对齐 `SunLighting.glsl`、`BlockLighting.glsl`、`BRDF.glsl` 的公式和顺序。
2. Shadow PCSS/contact/color shadow 按 DerivativeMain 逐函数重写；保持 no-warp 直到 projection 与 sampling 统一稳定。
3. Post Grade 对齐 `Grade.glsl + ACES.glsl + AgX.glsl`，先把色彩链路统一。
4. Bloom 改成 DerivativeMain downsample/blur/grade 合成模型。

### P2：中期关键观感

1. 水体：把 `WaterWave/WaterFog/Refraction/SSR/RainEffect` 逐函数移入 `water_composite.fs` 或拆 include。
2. SSR/reflection：对齐 `ScreenSpaceReflections.glsl` 与 `ReflectionFilter.glsl`，补 roughness-aware resolve 和 temporal reject。
3. 大气：决定是否完整接 `Atmosphere.glsl` 的 Bruneton 查询。如果追求权威，不能只采 `Final.lut`。
4. Volumetric fog：补 density modes、shadow/cloud shadow、depth-aware upsample、bloom fog transmittance。

### P3：完整性与长期扩展

1. 体积云完整移植 `VolumetricClouds.glsl` 与 `PlanarClouds.glsl`。
2. RSM GI 与 temporal GI/AO。
3. 特殊 GBuffer pass：entities、hand、hand water、weather、beacon、glint、spidereyes、damagedblock。

## 16. 最终判断

如果目标是“DerivativeMain-inspired 内置光影”，当前架构已经足够继续做视觉迭代。

如果目标是用户要求的“DerivativeMain 的实现方式是权威，所有效果应该照抄 DerivativeMain”，当前还需要一次系统性 shader 收敛：不要再只按效果名做近似，而要按 `DerivativeMain/program` 与 `DerivativeMain/lib` 的文件逐个建立端口，保留当前 C++ FBO 架构，但让 shader 内的公式、数据流、参数、采样顺序尽量与 DerivativeMain 一致。

最关键的转折点是：**当前 Mecraft 已经有承载 DerivativeMain 的管线骨架，但还没有完成 DerivativeMain 的算法语义复制。**
