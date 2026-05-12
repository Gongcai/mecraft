# DerivativeMain 内置渲染管线完整差异分析报告

> 范围：本报告以根目录 `DerivativeMain/` 解包源码为唯一权威，对照当前 Mecraft C++/OpenGL Hybrid Deferred 管线、`src/renderer/` 与 `assets/shaders/`。  
> 当前目标：**主世界 `world0` + 原版 Minecraft 材质包 + 内置 DerivativeMain 光影复刻**。Nether/End、Distant Horizons、Physics Oceans、外部 PBR 材质包、LabPBR/POM 等作为 DerivativeMain 源码中存在的非目标分支记录，不列入当前必须完成项。

## 0. 移植原则

本项目的 DerivativeMain 复刻必须遵循以下原则：

1. **DerivativeMain 源码是权威实现。**
   - shader 数学公式、采样顺序、bias、dither、宏默认值、材质 ID 语义、buffer 语义均以 DerivativeMain 为准。
   - 当前引擎只允许做 OpenGL/FBO/材质系统/资源路径层面的适配。

2. **禁止“看起来等价”的公式改写。**
   - 典型事故：DerivativeMain `sqrt2(x)` 是 `sqrt(sqrt(x))`，即四次根；曾误写成 `sqrt(x)`，导致 Derivative shadow warp 读取端与写入端不一致，出现一整团随摄像机/时间漂移的 shadow-depth 形状阴影。
   - 结论：基础函数必须逐字复刻，不能凭直觉化简。

3. **先还原 DerivativeMain 数据流，再优化性能。**
   - 如果当前 C++ pass graph 与 shaderpack `colortex` 不同，必须先建立明确映射。
   - 不允许为了当前资源布局方便而改变 DerivativeMain 的采样语义。

4. **每个移植函数都必须标注来源。**
   - 推荐在 shader 注释中写明：`DerivativeMain/lib/...` 或 `DerivativeMain/program/...` 的函数名/行意图。
   - 报告中的状态分为：`已照抄/已适配/近似实现/缺失/非目标`。

## 1. 当前总状态

当前 Mecraft 已具备承载 DerivativeMain 的 Hybrid Deferred 骨架：

- C++ 端已有 GBuffer、Shadow、SSAO、DeferredLighting、Reflection、Cloud、SceneComposite、VolumetricFog、TAA、MotionBlur、DoF、Water/Transparent、Post 等 pass。
- Render target 已覆盖 GBuffer、shadow depth/color/normal、scene lighting/composite/resolved、half-res fog/cloud、reflection、sky capture、velocity、history、Atmosphere LUT。
- Shader 端已有 DerivativeMain 风格材质 ID、roughness/f0/emission/SSS、BRDF、PCSS shadow、sky capture、atmosphere LUT、SSR、水雾、体积雾、TAA、AgX/ACES 后处理入口。

但当前实现仍不是完整 DerivativeMain。最大差距不是“有没有 pass”，而是：

- 很多 shader 仍是 DerivativeMain-inspired 近似，不是逐函数移植。
- 当前 FBO 布局与 shaderpack `colortex0-7` 不同，需要稳定映射表。
- `shaders.properties` 中的 blend、flip、scale、program toggle、自定义 uniform 平滑规则没有完整内置等价层。
- 大气、云、体积雾、水、SSR、后处理、GI/AO 等大量核心函数仍未逐文件端口。

结论：**架构地基已基本可用；后续工作必须从“补效果”转为“按 DerivativeMain 文件逐函数收敛”。**

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

仍缺：

- `Material.inc` 未逐项端口，很多 fallback 值仍是项目自定义。
- Terrain.frag 中原版 atlas alpha、tint、lightmap、mip/alpha 处理尚未逐行对齐。
- Block/Entities/Hand/HandWater/Textured/Basic/Damagedblock/Beaconbeam/Spidereyes/ArmorGlint/Weather 没有完整 GBuffer 等价 pass。
- PBR/LabPBR/POM 路径为非目标，但原版材质 fallback 必须对齐。

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

### 5.4 当前仍未完整等价

- DerivativeMain 使用 shadow sampler/OptiFine shadowtex 语义；当前是普通 depth texture 手写比较，需要继续审计所有边界、过滤、bias 行为。
- `shadowcolor0` colored shadow 有资源，但彩色玻璃/透明体的写入与读取还未完整等价。
- 水/透明 shadow caster 当前不应贸然写入 depth，否则会把大片水面当完全遮挡者；后续必须按 `Shadow.frag` 的水/透明逻辑完整端口。
- `screenSpaceShadow` 当前默认不应作为主阴影稳定性前提；应等主 shadow map 完全稳定后再开启调参。
- RSM GI 使用 shadow color/normal 的间接光仍缺失。
- DH shadow 为非目标。

#### 5.4.1 TODO：升级为 sampler2DShadow 硬件 PCF（P1 后续升级）

DerivativeMain 使用两个 shadow sampler：

- `sampler2D shadowtex0` — raw depth，用于 `BlockerSearch` 的 `texelFetch` 读取原始深度值
- `sampler2DShadow shadowtex1` — comparison mode，用于 `PercentageCloserFilter` 的 `textureLod(shadowtex1, vec3(uv, refZ), 0)` 硬件自动比较+双线性插值

当前 Mecraft 只有一个 `sampler2D uShadowMap`，PCF 使用 `compareShadowBilinear` 手动模拟（~15 条指令/样本 vs 硬件 1 条）。

**升级方案：`glTextureView` 零拷贝双视图**

```
m_shadowDepth (原始纹理, GL_DEPTH_COMPONENT32F)
       │
       ├── sampler2D       view → uShadowMapRaw   (texelFetch 读原始深度，用于 blockerSearch)
       │
       └── sampler2DShadow view → uShadowMap      (texture() 硬件比较，用于 PCF)
```

C++ 改动（~15 行）：

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

Shader 改动：

```glsl
uniform sampler2D       uShadowMapRaw;   // blockerSearch: texelFetch 读原始深度
uniform sampler2DShadow uShadowMap;       // PCF: texture() 硬件比较

// PCF 从 compareShadowBilinear(...) 变为一行：
float lit = texture(uShadowMap, vec3(sampleCoord, shadowProjPos.z));

// blockerSearch 仍用 texelFetch：
float depthSample = texelFetch(uShadowMapRaw, texelCoord, 0).x;
```

**收益**：PCF 每样本 ~15 条指令 → 1 条；硬件精确双线性；`compareShadowBilinear` 等 ~40 行可全部删除；轻松支持 colored shadows。

**前置条件**：当前手动双线性在视觉上等价，不阻塞 P1。升级时机为 P1 完成、shadow map 稳定后。

### 5.5 下一步阴影任务

1. ~~将 `ShadowDistortion.glsl` 抽成项目公共 include，shadow/deferred/volumetric/debug 全部引用同一函数，避免再次分叉。~~ ✅ 已完成（`derivative_shadow.glsl`）
2. ~~将 `SunLighting.glsl` 的纯数学函数端口为 include。~~ ✅ 已完成（`derivative_sunlight.glsl` — HG phase / fake bounce / SSS 计算）
3. 升级 shadow map sampler 架构：`sampler2D` → `sampler2DShadow` + `glTextureView` 双视图（见 5.4.1）。
4. 将 `Shadow.frag` 的 `shadowcolor0/1` 写入逐分支复刻，包括水、透明、normal、lightmap、height aux。
5. 再接 colored shadows 与 RSM GI。

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

已适配：

- `DiffuseHammon`、GGX NDF、Smith visibility、Schlick Fresnel、`SpecularBRDF` 已有 DerivativeMain 方向实现。
- deferred lighting 主流程已接入 direct/sky illuminance、shadow、cloud shadow、shadow tint、SSS、emissive material。
- 多数 emissive material id 已在 `BlockLighting.glsl` 方向做了规则覆盖。
- SSAO 与 bilateral filter 已有独立 pass。

未完整等价：

- `deferred5.fsh` 的主流程仍不是逐行端口，尤其 sceneData 累积顺序、water tint、eye skylight fix、material roughness/metal 调整、indirectData 与 GI/AO 混合仍有差异。
- `BlockLighting.glsl` 没有完整移植，当前发光规则仍有项目近似。
- `GlobalIllumination.glsl` RSM GI 缺失。
- `AmbientOcclusion.glsl` 的 AO/GI temporal + spatial filter 未完整。
- SH sky lighting 未完全按 `deferred5.vsh` 从 sky capture 构建。

下一步：

- 将 `BRDF.glsl`、`BlockLighting.glsl`、`SunLighting.glsl` 拆为项目 include，保持函数名和公式一致。
- `deferred_lighting.fs` 主函数按 `world0/deferred5.fsh` 分段重排。

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

未完整等价：

- DerivativeMain 的 Bruneton 查询体系依赖多个 LUT 语义；当前主要从 `Final.lut` 采样并自行映射。
- `Atmosphere.glsl` 中 planet parameters、ProjectSky/UnprojectSky、sun/moon disk、night eye、aurora/stars、land atmospheric scattering 仍需逐函数端口。
- `shaders.properties` 中 time/biome/weather 平滑 uniform 未完整。

下一步：

- 审计 `atmosphere_lut.glsl` 是否逐函数匹配 `Atmosphere.glsl`，不匹配处标注“适配”或重写。
- 如果要完全权威，应同步加载并使用 `Transmittance/Scattering/Irradiance/Final` 的完整资源体系。

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

未完整等价：

- `VolumetricClouds.glsl` 的 marching、multi-scattering、powder、sun optical depth、temporal upscaling 未完整。
- `PlanarClouds.glsl` 双层 cirrus/cirrocumulus 仍是近似。
- `CLOUDS_SHADOW` 当前不是 DerivativeMain 光学深度式云影。
- Aurora 未完整。

备注：

- 用户验证 cloud shadow 开关对本次 Derivative warp 阴影 bug 无影响，说明该 bug 根因不是云影，而是 shadow distortion 不一致。

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

- Half-res volumetric target。
- 绑定 depth、sky capture、noise、shadow depth/color、atmosphere LUT。
- 有 height density、weather haze、phase、shadow visibility、depth-aware composite。
- Derivative warp 在 volumetric 端使用 `(x^4+y^4)^(1/4)` 公式，与 shadow pass 一致。

未完整等价：

- `CalculateVolumetricFog` 未逐行端口。
- Low/Medium/High/Ultra density mode、volFogWind、volFogDensity、BiomeSandstorm/GreenShift 未完整。
- colored shadow 体积雾采样不完整。
- underwater volumetric light 未完整。
- Bloomy fog transmittance 未接 DerivativeMain `colortex6` 链路。

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
- WaterFog/UnderwaterFog 已有 DerivativeMain 注释来源。
- 水面 SSR、sky reflection、sun reflection、wave/parallax、absorption 有入口。

未完整等价：

- DerivativeMain 水先写 GBuffer，再在 composite/composite1 中合成；当前是 deferred 后 forward/composite。
- `WaterWave.glsl` 未完整逐函数端口。
- `Refraction.glsl`、raytraced refraction、dispersion 未完整。
- `RainEffect.glsl` 与 `RippleNormal.png` 未完整。
- Foam/caustics/underwater light 不完整。
- 玻璃/冰/彩色透明与水的统一半透明管线缺失。

注意：

- 当前不应把水/透明几何直接作为完全遮挡的 shadow depth caster，必须先按 DerivativeMain `Shadow.frag` 的 colored shadow/water 分支完整端口。

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

未完整等价：

- `ScreenSpaceReflections.glsl` 的 ray steps、hit validation、real sky reflection、VNDF rough reflection 未完整。
- `ReflectionFilter.glsl` 与 compute path 未完整。
- wetness/rain splash/puddle 对 reflection 的影响未完整。
- DerivativeMain 的 reflection temporal reject 未完整。

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

- TAA 有 reprojection、history depth、CatmullRom/YCoCg/variance clamp/Reinhard blend。
- Motion blur、DoF、Bloom、AgX/ACES/Filmic、sharpen/dither 均有入口。
- PostProcessRenderer 已有 DerivativeMain sigmoid exposure response 注释来源。

未完整等价：

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
| `lib/Head/Common.inc` | scattered helpers | 部分；需建立公共 include |
| `lib/Head/Functions.inc` | scattered helpers | 部分 |
| `lib/Head/Uniforms.inc` | C++ uniforms | 部分 |
| `lib/Head/Noise.inc` | `uNoiseTex` + local functions | 部分 |
| `lib/Head/Mask.inc` | `gbuffer_contract.glsl` translucent mask | 部分 |
| `lib/Head/Material.inc` | `gbuffer_contract.glsl` | 中等；需逐 ID 审计 |
| `program/Gbuffers/Terrain` | `chunk_gbuffer.vs/fs` | 中等；原版 fallback 需继续对齐 |
| `program/Gbuffers/Water` | `water_composite.fs` + water mesh | 架构不同，算法部分 |
| `program/Gbuffers/Block/Entities/Hand/...` | forward/entity/item shaders | 低 |
| `program/Shadow` | `shadow_depth.vs/fs` | 中高；Derivative warp 已修，透明/colored/RSM 仍缺 |
| `lib/Lighting/ShadowDistortion.glsl` | duplicated shader functions | 已适配；建议抽公共 include |
| `lib/Lighting/SunLighting.glsl` | `deferred_lighting.fs` | 中等；部分函数已端口 |
| `lib/Lighting/BlockLighting.glsl` | `deferred_lighting.fs` emissive rules | 中等偏低 |
| `lib/Lighting/AmbientOcclusion.glsl` | `ssao.fs/filter` | 部分 |
| `lib/Lighting/GlobalIllumination.glsl` | 无完整 RSM GI | 缺失 |
| `lib/Surface/BRDF.glsl` | `deferred_lighting.fs` | 中高；需逐式复核 |
| `lib/Surface/ScreenSpaceReflections.glsl` | `reflection_probe.fs/water_composite.fs` | 部分 |
| `lib/Surface/ReflectionFilter.glsl` | `reflection_filter.fs` | 部分 |
| `lib/Surface/Refraction.glsl` | `water_composite.fs` | 低到中 |
| `lib/Surface/RainEffect.glsl` | wetness params | 低 |
| `lib/Atmosphere/Atmosphere.glsl` | `atmosphere_lut.glsl/gameplay_sky.fs` | 中等；LUT 体系不完整 |
| `lib/Atmosphere/VolumetricClouds.glsl` | `cloud_target.fs` | 部分近似 |
| `lib/Atmosphere/PlanarClouds.glsl` | `cloud_target.fs` | 部分近似 |
| `lib/Atmosphere/VolumetricFog.glsl` | `volumetric_fog.fs` | 部分 |
| `lib/Water/WaterWave.glsl` | `water_composite.fs/shadow_depth.fs` | 部分 |
| `lib/Water/WaterFog.glsl` | `water_composite.fs` | 部分较高 |
| `program/Post/Temporal` | `temporal_resolve.fs` | 中等 |
| `program/Post/DoF` | `dof.fs` | 部分 |
| `program/Post/MotionBlur` | `motion_blur.fs` | 部分 |
| `program/Post/DownSample/Blur/Grade` | bloom/postprocess | 部分 |
| `program/Post/Final` | `postprocess.fs` | 部分 |
| `program/DH/*` | 无 | 非目标 |
| `world-1/world1` | 无完整维度管线 | 非目标 |

## 15. 优先级路线图

### P0：防止再次发生基础语义分叉

1. 抽出 `derivative_shadow.glsl`：
   - `pow2/pow4/sqrt2`
   - `quarticLength`
   - `DistortionFactor`
   - `DistortShadowSpace`
   - `WorldPosToShadowProjPosBias`
2. shadow/deferred/volumetric/debug 必须引用同一 include。
3. 建立 `DerivativeMainPortingIndex.md`，记录每个当前函数来源。
4. 把“禁止近似改写 DerivativeMain 公式”写入项目开发约定。

### P1：主光照和阴影完全收敛

1. 完整端口 `SunLighting.glsl`。
2. 完整端口 `BRDF.glsl`。
3. 完整端口 `BlockLighting.glsl`。
4. 按 `world0/deferred5.fsh` 重排 `deferred_lighting.fs` 主流程。
5. 完整实现 colored shadow 与透明 shadow，不再自造透明 caster 行为。

### P2：GBuffer 与材质合同

1. `Material.inc` 逐 ID 对齐。
2. Terrain 原版材质 fallback 对齐。
3. Entities/Hand/HandWater/Weather/Beacon/Damaged/Glint 进入 DerivativeMain GBuffer 合同。

### P3：Atmosphere/Cloud/Fog/Water

1. `Atmosphere.glsl` 完整审计。
2. `VolumetricClouds.glsl` 与 `PlanarClouds.glsl` 移植。
3. `VolumetricFog.glsl` 移植。
4. `WaterWave/WaterFog/Refraction/RainEffect` 移植。

### P4：Post 与长期完整性

1. Temporal/exposure 与 DerivativeMain composite3 对齐。
2. Bloom downsample/blur/grade 链对齐。
3. CAS/dither/AgX/ACES/Grade 完整端口。
4. RSM GI 与 temporal GI/AO。

## 16. 当前结论

当前 Mecraft 已经有完整的内置 deferred 管线骨架，且阴影系统经过 Derivative warp 修复后，已经确认最危险的坐标变换分叉被修掉。

但从 DerivativeMain 权威实现角度看，当前项目仍处于：

**骨架完整、部分核心函数已端口、许多模块仍是近似适配。**

后续所有实现必须按 DerivativeMain 源码逐文件收敛。尤其 shadow、BRDF、SunLighting、BlockLighting、Atmosphere、Water、Post 这些基础库，不能再“按效果重写”，只能“照抄后适配”。
