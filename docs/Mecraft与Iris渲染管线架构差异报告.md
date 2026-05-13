# Mecraft 与 Iris/OptiFine 渲染管线架构差异报告

> 目标：记录当前 Mecraft C++/OpenGL 渲染宿主环境与 Iris/OptiFine 的差异，避免调试内置光影时误把 Iris shaderpack ABI 当作 Mecraft 必须复刻的目标。  
> 范围：当前 Mecraft `src/renderer/`、`assets/shaders/`、内置 `DerivativeMain/` 光影包、根目录 `Iris-26.1/` 源码。  
> 重点：本报告不是 DerivativeMain shader 文件逐项移植清单，也不再是“把 Mecraft 改造成 Iris”的实施计划；它是 **Mecraft 引擎端宿主行为与 Iris 宿主行为的差异/风险报告**。

> 2026-05-13 路线修订：项目目标已从“复现 Iris/OptiFine contract”调整为“建立 Mecraft Renderer Contract，并让内置 DerivativeMain-like 光影适配该 contract”。Iris 继续作为重要参照，用于理解 shaderpack 默认假设和定位 bug，但不作为最终架构硬目标。

## 目标边界

当前目标是：**主世界 `world0` + 原版 Minecraft 材质包 + 内置 DerivativeMain-like 视觉效果**。Mecraft 引擎端定义自己的 renderer contract；DerivativeMain 的大气、光照、色调、HDR、水、体积雾、材质风格作为实现参考，而不是要求 C++ 宿主完整复刻 Iris/OptiFine shaderpack ABI。

本报告中的 `ShaderpackDirectives`、`ShaderpackTextureContract`、`RenderPhase` 等旧建议，保留为历史参考。后续命名和实现应逐步改为 `MecraftRenderContract`、`MecraftTextureContract`、`MecraftRenderPhase` 一类项目自有概念，避免继续被 Iris/OptiFine 对号入座误导。

明确非目标：

- 不做通用 shaderpack loader。
- 不承诺加载任意 OptiFine/Iris shaderpack。
- 不以完整复刻 Iris/OptiFine uniform/sampler/pass ABI 为目标。
- 不以 PBR/LabPBR/POM/高级材质包为当前效果目标。
- 不优先实现 Distant Horizons、Physics Oceans、外部材质扩展等 DerivativeMain 可选分支。

架构上仍需保留清晰 contract 层，但该 contract 应由 Mecraft 定义。DerivativeMain-like shader 适配 Mecraft，而不是 Mecraft 为任意 shaderpack 适配自己。

## 0. 当前结论

当前 Mecraft 已经有一套可运行的 Hybrid Deferred 管线。它不是 Iris/OptiFine shaderpack 宿主，也不需要成为完整 shaderpack 宿主。很多 pass、uniform、render target、sampler、阴影 culling、透明/实体提交规则是 Mecraft 手写约定；后续需要把这些约定整理成稳定的 Mecraft Renderer Contract，而不是继续散落在 Renderer 和各个 shader 中。

这会导致一种非常容易误判的问题：**shader 代码局部看起来已经和 DerivativeMain 对上了，但 shader 所依赖的是 Iris/OptiFine 默认宿主假设，而 Mecraft 的 mesh、pass、buffer、sampler contract 与之不同。**

近期阴影问题就是典型症状：

- `No Warp` 不出现鬼影。
- `Radial Debug` 和 `Derivative` 都出现鬼影。
- debug view 中的 shadow visibility 与最终画面一致。
- 缩小阴影距离会加重鬼影。
- cloud shadow 对该问题无影响。
- 继续做接收端 OOB 保护、局部距离限制后，鬼影仍存在且行为一致。

这组现象后续已被进一步验证：开启 `Debug Disable Greedy Meshing` 后 ghosting 消失。根因不是 C++ caster 域单独不一致，而是 **DerivativeMain/Radial 非线性 shadow warp 与 Mecraft 贪婪合并大面片不兼容**。Iris/Sodium terrain 不会触发该问题，是因为它保留 Minecraft block model 的小 quad 粒度，并通过压缩顶点格式、region batching、索引/MDI 等方式获得性能，而不是把大量相邻方块面合并成超大 quad。

因此本报告的工程结论已调整：不建议为了复刻 Iris contract 而全局关闭 Mecraft greedy meshing；正式路线应保留 greedy + MDI，并将阴影改为适配 Mecraft mesh contract 的稳定方案（No Warp/linear shadow、CSM、PCF/PCSS/contact shadow，必要时提供 shadow-only bounded fine caster mesh）。

## 1. Mecraft 当前渲染管线概览

当前 Mecraft 主链路集中在 `Renderer` 巨型类中：

- `src/renderer/Renderer.cpp:1285`：`renderWorldDeferred`
- `src/renderer/Renderer.cpp:1408`：`renderGBufferTerrain`
- `src/renderer/Renderer.cpp:1463`：`renderShadowMap`
- `src/renderer/Renderer.cpp:1625`：`renderDeferredLightingPass`
- `src/renderer/Renderer.cpp:2222`：`renderDeferredDebugView`
- `src/renderer/Renderer.cpp:2399`：`buildShadowProjectionData`
- `src/renderer/Renderer.cpp:2553`：`renderOpaqueChunksAndCollectPasses`
- `src/renderer/Renderer.cpp:1117`：`bindShadowFrameUniforms`

现有大致顺序：

1. begin frame / 更新 camera、天空、大气、时间、history。
2. GBuffer terrain pass。
3. velocity pass。
4. shadow map pass。
5. SSAO / AO filter。
6. deferred lighting。
7. reflection / reflection filter。
8. cloud。
9. scene composite。
10. volumetric fog。
11. temporal resolve。
12. motion blur / DoF。
13. water / transparent composite。
14. post process / debug view。

这个架构适合项目自有光照管线，但与 Iris 最大差别是：Mecraft 当前以“自己定义的一组 pass 和资源”为中心；Iris 以“shaderpack 声明 + Minecraft 渲染阶段 + OptiFine 兼容 uniform/texture contract”为中心。

## 2. Iris/OptiFine 宿主行为概览

Iris 不是简单地把 shader 编译后按固定顺序调用。它在 shaderpack 和 Minecraft 渲染器之间提供一整层宿主兼容系统。

关键组成：

- `Iris-26.1/common/src/main/java/net/irisshaders/iris/shaderpack/properties/PackShadowDirectives.java`
  - 解析 `shadowDistance`、`shadowDistanceRenderMul`、`shadowIntervalSize`、shadow target、实体/方块实体/透明阴影等 directives。
  - 默认 `distanceRenderMul = -1.0f`，表示使用 Iris/视频设置逻辑，而不是直接等同 shadow projection 半径。
  - `shadowDistanceRenderMul` 在源码中有专门 directive 解析入口。
- `Iris-26.1/common/src/main/java/net/irisshaders/iris/shadows/ShadowMatrices.java`
  - 提供 OptiFine/Iris 风格 shadow matrices。
  - `NEAR = -100.05f`，`FAR = 156.0f`。
  - 包含 `snapModelViewToGrid` 和 `createModelViewMatrix`。
- `Iris-26.1/common/src/main/java/net/irisshaders/iris/shadows/ShadowRenderer.java`
  - `ShadowMatrices.createModelViewMatrix(...)` 创建 shadow modelView。
  - `createShadowFrustum(...)` 生成 terrain/entity shadow frustum。
  - `renderDistance = (halfPlaneLength * renderDistanceMultiplier) / 16`。
  - terrain 阶段通过 `levelRenderer.invokeCullTerrain(playerCamera, terrainFrustumHolder.getFrustum(), false)` 重新执行 terrain setup/culling。
  - entity 阶段可使用独立 `entityShadowDistanceMultiplier`。
  - debug 中会输出 terrain/entity distance info 与 culling info。
- `Iris-26.1/common/src/main/java/net/irisshaders/iris/shadows/ShadowRenderTargets.java`
  - 管理 `shadowtex*`、`shadowcolor*` 及其采样、mipmap、feature flag。

换句话说，Iris 阴影不是“把所有当前可见 chunk 用 shadow shader 画一遍”。它会建立一个 shaderpack 指令驱动的 shadow render context，并用专门的 frustum/culling/render target/uniform 状态运行一轮 Minecraft 渲染。

## 3. 高风险差异总表

| 领域 | Iris/OptiFine 行为 | Mecraft 当前行为 | 风险 |
| --- | --- | --- | --- |
| Shaderpack directives | 从 pack properties/const directives 生成管线参数 | C++ `PipelineSettings` 手写参数 + shader 常量散落 | shader 以为自己在 Iris 环境，实际参数来源不完整 |
| Pass 架构 | `gbuffers_*`、`shadow`、`deferred*`、`composite*`、`final` 与 Minecraft phase 绑定 | 自定义 Hybrid Deferred pass graph | pass 名不同不是问题，资源/时序/语义不同才是问题 |
| Shadow renderer | 独立 `ShadowRenderer`，有 shadow context、targets、frustum、directives | `Renderer::renderShadowMap` 内嵌在主 Renderer | shadow 状态易和主相机/主 frustum/主 chunk 列表耦合 |
| Shadow culling | terrain/entity/block entity 分离，专门 shadow frustum | 近期加入简单 camera XZ 距离限制，仍不是 Iris frustum | warping 后 far caster 仍可能折入 shadow map，造成 ghosting |
| Shadow matrix | `ShadowMatrices` 统一生成，snap、near/far、intervalSize 受 directives 管理 | 已接近 Iris 数值，但散落在 `Renderer` | 数值对上不等于提交范围、坐标契约也对上 |
| Shadow distance render mul | pack directive 决定阴影 caster 渲染距离 | 当前基本使用 `shadowDistance` 或最小 64 | 缩短 shadowDistance 时 ghosting 变重，说明 render 域与 warp 域仍未闭合 |
| Render targets | Iris 管理 `shadowtex*`、`shadowcolor*`、`colortex*`、depthtex、flip/mipmap/filter | Mecraft 使用自定义 `DeferredRenderTargets` | shader 移植时 sampler 名和 buffer 语义容易“看起来对号”但不等价 |
| Sampler/filter/mipmap | properties/directives 可控制 | C++ 固定 texture 参数为主 | PCF/soft shadow/历史 buffer/噪声采样出现偏差 |
| Cutout/alpha | Minecraft terrain render type + shaderpack alpha test 语义 | chunk pass 自行分类 cutout/transparent | 树叶/草在 shadow、GBuffer、透明 pass 中语义可能分裂 |
| Entity/block entity/player shadow | directives 可分别开关和距离限制 | 当前重点是 chunk terrain | 后续实体阴影、手持物、掉落物会继续偏离 |
| Uniform contract | OptiFine/Iris 全量兼容 uniform，时间、相机、矩阵、上一帧状态严格定义 | 当前只上传项目 shader 用到的 uniform | DerivativeMain 文件逐步移植时会不断撞到缺失或近似 uniform |
| 坐标空间 | Minecraft/Iris 明确区分 camera-relative、absolute、unshifted camera | Mecraft 多处自定义世界坐标/相机坐标 | shadow warp、TAA、体积雾、SSR、云影容易出现视角相关漂移 |
| 状态管理 | 每个 pass 有明确 GL state/blend/depth/cull 规则 | 多数状态由 C++ pass 手动设置 | alpha/cutout/背面阴影/透明阴影非常容易残留状态 |

## 4. 阴影系统差异：当前鬼影最相关

### 4.1 已对齐但仍不充分的部分

Mecraft 近期已经做了几项正确方向的工作：

- `shadowDistance` 默认/目标值与 DerivativeMain `lib/Lighting/SunLighting.glsl` 中 `shadowDistance = 192.0` 对齐。
- DerivativeMain 中 `shadowDistanceRenderMul = 1.0` 已被识别为关键常量。
- `ShadowMatrices` 风格 near/far 已采用类似 `-100.05 / 156`。
- shadow angle 已改为接近 Iris/OptiFine 的 sky angle 模型。
- shadow modelView snap、sunPathRotation、interval size 已向 Iris 逻辑靠拢。
- `uShadowLightDirection` 已从 shadow modelView 反推，避免与天空颜色方向分裂。
- 写入端 shadow vertex 已使用 DerivativeMain `DistortShadowSpace`。
- 读取端 debug/final/volumetric 使用同一套 shadow projection helper。

这些修复能减少“数学公式不一致”类问题，但不能自动解决 caster 提交域问题。

### 4.2 仍不一致的核心：shadow caster 提交不是 Iris 模型

Iris shadow pass 会：

1. 根据 `PackShadowDirectives` 得到 `halfPlaneLength`、`renderDistanceMultiplier`、`intervalSize`、near/far、实体距离、透明阴影开关。
2. 根据 shadow matrices 创建 shadow projection。
3. 创建 shadow frustum。
4. 用 shadow frustum 重新执行 terrain setup/culling。
5. 单独处理 terrain、translucent、entities、block entities、player。
6. 输出 debug distance/culling 信息。

Mecraft 当前 shadow pass 仍更像：

1. 在主 Renderer 中计算 shadow matrices。
2. 切换到 shadow FBO。
3. 临时把 `m_chunkShader` 改成 shadow shader。
4. 调用 `renderOpaqueChunksAndCollectPasses(...)` 复用 chunk 绘制链路。
5. 当前已尝试关闭主 frustum cull，并增加基于 camera XZ 距离的 max distance。

这比最初更接近正确方向，但仍只是 Mecraft shadow culling 的早期近似。Iris 的 `createShadowFrustum + invokeCullTerrain` 可作为参考，但当前已确认 ghosting 主因不是 caster 域本身，而是非线性 shadow warp 与 greedy 大面片插值不兼容。因此后续不应把“变得更像 Iris”作为完成标准，而应把 culling/debug 纳入 MecraftShadow contract。

### 4.3 为什么“缩短阴影距离反而加重鬼影”很重要

如果问题只是接收端采样越界，缩短 shadow distance 通常会让投影范围变小、错误范围变明显，但不一定导致方向相关 ghost caster 一致存在。

当时的表现曾被判断为：

- shadow projection 半径缩小后，shadow warp 的有效域变得更紧。
- 某些 caster 提交可能没有按预期剔除。
- 这些 caster 经过 DerivativeMain/Radial warp 后被压入 shadow map 可采样区域。
- 因为 caster 集合、chunk collect、LOD/visible set 或 warp 插值误差都可能随视角变化，所以不同视角 ghost 不同。
- `No Warp` 消失，说明普通线性 shadow projection 下这些 caster 没有被折入或折入不明显。
- debug view 和 final 一致，说明错误已经存在于 shadow visibility/深度关系中，不是后续 lighting、cloud shadow 或 tone mapping 引入。

该判断已被后续实验修正：BoxCuller 与 caster 计数调试只能证明提交域可观测，不能解释 ghosting 根因。`Debug Disable Greedy Meshing` 开启后 ghosting 消失，说明首要问题是 Mecraft greedy 大面片与非线性 shadow warp 不兼容。下一步不应继续把 shadow pass 逼近 Iris，而应把正式阴影路线改为 Mecraft 自有稳定 contract：默认线性/CSM 阴影，Derivative/Radial warp 仅保留为 debug 或研究模式。

### 4.4 DerivativeMain 的 `shadow.culling = false` 不等于 Mecraft 应提交所有 chunk

`DerivativeMain/shaders.properties` 中存在：

```properties
shadow.culling = false
```

这个值容易误导。它表达的是 shaderpack/Iris 对特定 culling 策略的要求，不等价于“引擎可以把任意远处 chunk 都送进 shadow map”。

Iris 仍然有：

- shadow render distance；
- terrain setup/culling；
- render distance multiplier；
- entity shadow distance multiplier；
- chunk/section 可见性与 render region 管理；
- voxelization 与 culling fallback 逻辑。

所以 Mecraft 不能把 `shadow.culling=false` 简化成“shadow pass 不做任何剔除”。正确做法是：**在 MecraftShadow contract 中显式定义 terrain/entity caster 的提交边界**；Iris 在该 directive 组合下的最终提交集合可作为参照和风险检查，但不再作为必须逐项复刻的目标。

## 5. Render Target 与 sampler 差异

Mecraft 当前 `DeferredRenderTargets` 已经有清晰的现代化资源拆分，例如：

- GBuffer albedo/normal/material/materialAux/voxelLight/depth。
- shadow depth/color/normal。
- scene lighting/composite/resolved。
- reflection/cloud/half-res fog。
- velocity/history。

这对自研 renderer 是好事，但对移植 shaderpack 是风险源。DerivativeMain 文件中很多 sampler 名来自 OptiFine/Iris 语义：

- `colortex0-7`
- `depthtex0/1/2`
- `shadowtex0/1`
- `shadowcolor0/1`
- `noisetex`
- custom texture / LUT

当前 Mecraft 把这些语义映射到自己的 target 名上。问题不是“名字不同”，而是以下行为必须也一起映射：

- 颜色格式是否一致。
- depth compare sampler 与普通 depth sampler 是否一致。
- nearest/linear filter 是否一致。
- mipmap 是否生成，何时生成。
- shadow depth 是否有 hardware comparison。
- shadowcolor 是否在 shadow pass 中完整写入。
- half-res buffer 的 scale/viewport 是否符合 shaderpack pass。
- history/flip buffer 是否符合 `flip.*` 语义。
- blend/depth/cull 状态是否符合 `shaders.properties`。

建议建立一个固定表：`MecraftTextureContract`，将 DerivativeMain/Iris 参考 sampler 映射到 Mecraft target、格式、filter、wrap、mipmap、写入 pass、读取 pass。没有这张表，后续每移植一个效果都可能出现“采样到了看似合理但语义错误的 buffer”。

## 6. Pass/phase 架构差异

Iris 的 shaderpack pass 有明确语义：

- `gbuffers_*`：按 Minecraft render type 写入基础材质/法线/光照/深度。
- `shadow`：从 light 视角渲染 shadow terrain/entities/translucent 等。
- `deferred*`：延迟光照、AO/GI、反射等。
- `composite*`：云、体积、bloom、TAA、后处理链。
- `final`：最终输出。

Mecraft 当前 pass graph 不直接使用这些名字，而是按自身功能拆分。这可以保留，但必须补一个 `MecraftRenderPhase` / `MecraftRenderContract` 层，使每个 Mecraft pass 明确声明：

- 该 pass 的 Mecraft 语义，以及可选的 DerivativeMain/Iris 参考 phase。
- 当前绑定哪些 color/depth attachments。
- 当前允许读取哪些 texture。
- 当前写入哪些 texture。
- viewport/scale 是全分辨率、半分辨率还是自定义。
- GL blend/depth/cull/color mask 状态。
- 必须上传哪些 Mecraft frame/material/shadow uniform。
- 是否需要 previous/current matrix、camera、time。

没有这个 contract 层，Renderer 会越来越像“能跑但难以证明行为边界”的状态机。阴影 bug 只是第一个放大器；后续 TAA、SSR、体积云、透明、水、手持物也会遇到同类问题。

## 7. Uniform 与时间/坐标契约差异

DerivativeMain 依赖大量 OptiFine/Iris uniform 语义。Mecraft 当前只上传项目 shader 已经用到的 uniform，例如 shadow matrices、camera、time、noise、sky 等。

需要补齐一个统一的 `MecraftFrameUniformState`：

- 当前帧与上一帧 camera position。
- camera-relative 与 absolute/unshifted camera position。
- modelView/projection 与 inverse。
- previous modelView/projection。
- shadowModelView/shadowProjection 与 inverse。
- sun/moon/world light vector。
- world time、day progress、frameTimeCounter、frameCounter、tickDelta。
- viewport、view size、pixel size。
- near/far、rain/wetness、eye brightness、held item、dimension。
- TAA jitter 与历史矩阵。

特别注意：时间类 uniform 不能简单等同于 `animationTime`。之前“树影草影抖动随时间速度变快”的现象说明，shadow matrix、wind animation、frameTimeCounter、world time 之间存在耦合风险。Iris/OptiFine 中：

- 天体角度影响 shadow matrix。
- `frameTimeCounter` 影响动画/noise。
- tickDelta 影响插值。
- paused/frozen time 与世界时间有不同语义。

Mecraft 应拆开这些时间源，避免一个调试加速参数同时驱动“太阳角度 + 植被动画 + temporal noise + cloud/fog wind”，否则稳定性排错会被时间耦合污染。

## 8. Cutout/alpha/透明链路差异

用户最初提到树叶/草等 cutout 物体在 sun shadow 下出现大片白色/少量黑色，关闭 soft shadow 可以减轻，PCSS 开关无影响。这个问题也不应只看 lighting shader。

Iris/Minecraft 中 terrain/cutout/translucent 有 render type、alpha test、mip/atlas、material id、lightmap、tint、entity/hand 等完整路径。Mecraft 当前 chunk GBuffer 和 shadow pass 复用自己的 chunk 分类：

- opaque chunk；
- cutout entries；
- transparent entries；
- water/transparent composite；
- shadow pass 临时收集 cutout/transparent。

需要确认每个路径在 shadow pass 中由 Mecraft contract 明确定义，并参考 DerivativeMain 的材质意图：

- cutout alpha discard 阈值。
- mip alpha 处理。
- 树叶/草 tint 是否参与 shadowcolor。
- shadow depth 是否写入 alpha-tested geometry。
- shadow normal/color 是否对 cutout 写入一致。
- backface culling 是否符合 `SHADOW_BACKFACE_CULLING`。
- translucent shadow 是否按 DerivativeMain 预期启用。

白/黑块问题常见根因包括：alpha-tested caster 写入了错误 shadowcolor、depth/color target clear 或 blend 状态不一致、shadow sampler filter 与 color target 不一致、cutout 在 GBuffer 与 shadow pass 中使用了不同 UV/mip/tint/atlas 逻辑。

## 9. 修订后的目标架构：Mecraft Renderer Contract

建议不要继续把所有逻辑塞进 `Renderer`。但拆分目标不再是“与 Iris 完整对齐”，而是建立 Mecraft 自有渲染契约，让内置 DerivativeMain-like shader 可靠消费这套契约。

### 9.1 `MecraftRenderContract`

建立项目自有 contract 层：

- `MecraftShadowContract`
  - `shadowDistance`
  - `shadowMapResolution`
  - shadow projection mode：`Linear` / `CSM` / `DebugDerivativeWarp`
  - cascade/split 参数（CSM 后续）
  - PCF/PCSS/contact shadow 参数
  - terrain/cutout/translucent/entity caster toggles
  - shadow-only bounded fine mesh 参数：`maxShadowQuadSize = 1/2/4/8`
  - shadow sampler/filter/mipmap
- `MecraftBufferContract`
  - colortex/shadowcolor/depthtex 格式
  - flip/history
  - scale
  - clear color/depth
  - mipmap/filter/wrap
- `MecraftProgramContract`
  - blend
  - depth test/write
  - cull
  - program toggle
  - render scale

注意：这里不是为了做通用 shaderpack loader，而是为了避免 C++ pass、buffer、uniform、sampler 继续散落硬编码。DerivativeMain-like shader 应适配这套 Mecraft contract。

### 9.2 `ShadowRenderer`

从 `Renderer` 中拆出 shadow 子系统：

- 输入：
  - `World`
  - `Camera`
  - `RenderFrameData`
  - `MecraftShadowContract`
  - chunk/entity render registries
- 输出：
  - shadow matrices
  - shadow light direction
  - shadow render targets
  - debug info：caster count、chunk count、distance/culling mode、matrix、snap offset
- 内部：
  - `ShadowRenderContext`
  - `ShadowMatrices` / `CascadeMatrices`
  - `ShadowCasterCuller`
  - optional shadow-only bounded fine caster mesh
  - terrain caster list
  - cutout/translucent/entity caster list

这样可以防止 shadow pass 继续隐式复用主 pass 的 frustum、shader、chunk 收集副作用。

### 9.3 `ShadowCasterCuller` / `MecraftShadow`

先实现三层：

1. **P0：稳定线性阴影**
   - 默认关闭 Derivative/Radial 非线性 shadow warp。
   - 保留 texel snapping、稳定 near/far、BoxCuller 距离域、caster count debug。
   - 使用当前 greedy mesh + MDI，避免 shadow pass 顶点数爆炸。

2. **P1：CSM 或分段 shadow distance**
   - 用多级线性 shadow map 替代 DerivativeMain quartic warp 的中心分辨率优势。
   - 继续兼容 greedy mesh，因为每个 cascade 内投影仍是线性。
   - 用 contact shadow/SSAO 补近景细节。

3. **P2：shadow-only bounded fine caster mesh**
   - 只在确有质量需求时启用。
   - 主 GBuffer/forward mesh 保留 greedy。
   - shadow mesh 允许 `maxShadowQuadSize = 1/2/4/8`，用于研究或高质量 preset。
   - `Debug Disable Greedy Meshing` 只保留为诊断开关，不作为正式默认路径。

Iris 的 `BoxCuller`、shadow frustum、terrain setup 仍可作为 culling 参考，但不要求完整复现 Iris `invokeCullTerrain`。

### 9.4 `MecraftTextureContract`

建立一张硬编码但集中管理的表：

| DerivativeMain 参考名 | Mecraft target | 格式 | filter | mipmap | clear | 写入 pass | 读取 pass |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `shadowtex0` | `ShadowDepth` | DEPTH32F | 待确认 | 待确认 | depth=1 | shadow | deferred/fog/debug |
| `shadowcolor0` | `ShadowColor` | RGBA8/待确认 | 待确认 | 待确认 | black/transparent | shadow | deferred/fog |
| `shadowcolor1` | `ShadowNormal` | RGBA16F | 待确认 | 待确认 | neutral | shadow | deferred/fog |
| `depthtex0` | `GDepth` | DEPTH32F | nearest | no | depth=1 | gbuffers | deferred/composite |
| `colortex*` | Mecraft GBuffer/Scene/History | varies | varies | varies | varies | varies | varies |

所有 shader include 只通过这个 contract 获取 sampler 语义，不允许每个 shader 自己猜。表中的 DerivativeMain/Iris 名称只是迁移参考，Mecraft target 才是宿主真相。

### 9.5 `MecraftRenderPhase` / `ProgramState`

建立类似：

```cpp
enum class MecraftRenderPhase {
    GbuffersTerrain,
    GbuffersWater,
    GbuffersEntities,
    ShadowLinear,
    ShadowDebugWarp,
    Deferred,
    Composite,
    Final
};

struct ProgramState {
    MecraftRenderPhase phase;
    FramebufferContract framebuffer;
    TextureBindings textures;
    UniformSet uniforms;
    GLState state;
};
```

这不是形式主义。它能让每次移植 DerivativeMain-like 效果时都先检查“这个 Mecraft phase 能读写什么，GL 状态是什么”，避免只看 GLSL 函数本身。

## 10. 立即改造优先级

### P0：阴影 ghosting 收口

1. 建立 `ShadowRenderContext`，把 shadow matrices、distance、renderDistanceMul、intervalSize、near/far、light direction、debug info 集中起来。
2. 默认阴影路径切到 `ShadowLinear` / No Warp，确保 greedy mesh 稳定。
3. 保留 `Derivative/Radial Warp` 作为 debug 模式，不作为默认视觉目标。
4. caster list 必须输出 debug：提交 chunk 数、剔除 chunk 数、最大距离、是否来自主 frustum、是否使用 shadow culler。
5. 增加 shadow-only bounded fine mesh 实验参数：`maxShadowQuadSize = 1/2/4/8`，用于评估质量/性能，而不是全局关闭 greedy。

### P0：防止继续被局部 shader 对号误导

1. 建立 `MecraftTextureContract`。
2. 建立 `MecraftRenderContract`，先覆盖 shadow、GBuffer、history、material id。
3. 每个 shader include 顶部标注依赖的 Mecraft contract，同时可标注 DerivativeMain 参考来源。

### P1：cutout/alpha/材质语义

1. 对齐 terrain atlas alpha discard、tint、lightmap、mip alpha。
2. shadow pass 与 GBuffer pass 使用同一份 cutout alpha/tint/material helper。
3. 确认 shadowcolor0/1 对树叶、草、水、半透明方块的写入语义。
4. 分离 opaque/cutout/translucent shadow toggles。

### P1：sampler/filter/mipmap

1. shadow depth/color target 的 filter、wrap、compare mode 集中配置。
2. 按 `MecraftTextureContract` 明确哪些 target 需要 mipmap/filter/wrap，而不是在 shader 中临时假设。
3. debug 输出当前 shadow target 参数。

### P2：完整 Mecraft pipeline 拆分

1. 把 `Renderer` 中 pass 逐步拆为子 renderer。
2. 实现 `ProgramState`/`MecraftRenderPhase`。
3. 补齐 entities、block entities、hand、weather、particles 的 GBuffer/shadow contract。
4. 补齐 previous matrices、history、frame/tick time uniform contract。

## 11. 当前鬼影的已验证结论与保留排查方法

2026-05-13 已验证：开启 `Debug Disable Greedy Meshing` 后 ghosting 消失。当前结论是 **非线性 shadow warp 与 greedy 大面片插值不兼容**。以下排查顺序保留为未来 shadow bug 的通用方法，但不再作为当前 ghosting 的下一步路线：

1. 在 shadow pass 中给每个提交 chunk/caster 输出 debug 计数和最大 camera distance。
2. 增加一个 shadow caster bounds debug overlay，显示哪些 chunk 被送进 shadow pass。
3. 在 `No Warp`、`Radial Debug`、`Derivative` 三种模式下保持完全相同 caster list，只切换 vertex warp。
4. 固定太阳角度和 vegetation animation time，排除时间耦合。
5. 将 shadowDistance 改小/改大时记录 caster list 是否按 `shadowDistanceRenderMul` 成比例变化。
6. 临时强制 shadow caster list 为 camera 周围固定 chunk 方块区域，观察 ghost 是否消失。
7. 如果固定 caster 区域后 ghost 消失，根因优先锁定 C++ caster/culling。
8. 如果固定 caster 区域后 ghost 仍存在，再回到 shadow vertex 写入、depth range、shadow sampler/filter、projection inverse 查 shader/target。

这个顺序能最快区分 C++ 与 shader：

- **caster list 改变会改变/消除 ghost**：C++ shadow submission/culling 为主因。
- **caster list 固定仍 ghost，且只在 warp 模式出现**：shadow vertex warp 或 receiver inverse/unwarp 为主因。
- **debug visibility 与 final 不一致**：lighting/composite 为主因。
- **debug visibility 与 final 一致**：shadow map 或 visibility 计算之前已经错。

当前 ghosting 已不再归因于 caster domain 单独错误。最高优先级改为：默认阴影路径采用适合 Mecraft greedy mesh 的线性/CSM 投影；warp 模式仅作为 debug/研究路径保留。

## 12. 不要再误判的几个点

1. **矩阵数值接近 Iris，不代表 shadow pass 行为完整。**  
   矩阵只是 contract 的一部分，caster list、frustum、distance render mul、sampler、target、uniform time 都必须在 Mecraft contract 中一起定义清楚。

2. **DerivativeMain `shadow.culling=false` 不代表无边界提交。**  
   Iris 仍有 render distance、terrain setup/culling、entity distance、voxelization fallback。

3. **debug shadow visibility 一致，说明问题在更早阶段。**  
   不应优先怀疑 cloud shadow、tone mapping、scene composite。

4. **PCSS 开关无影响，不代表 shadow 系统没问题。**  
   如果基础 shadow map 或 caster domain 已错，PCSS 只是放大/模糊错误。

5. **缩短 shadowDistance 加重 ghost，不再单独视为 caster domain 证据。**  
   在本次问题中，它更符合“非线性 warp 有效域变化后，大面片插值误差区域随投影缩放改变”的表现。

6. **C++ 与 shader 必须作为一个协议调试。**  
   DerivativeMain 的 GLSL 默认运行在 Iris/OptiFine 宿主协议中；Mecraft 做内置光影时不能只复刻函数，也不能盲目复刻 Iris 协议。正确做法是先定义 Mecraft Renderer Contract，再把 DerivativeMain-like shader 改写为消费该 contract。

## 13. 建议新增文档/代码入口

建议后续新增或重命名为 Mecraft 自有 contract：

- `src/renderer/contract/MecraftRenderContract.h/.cpp`
- `src/renderer/contract/MecraftTextureContract.h/.cpp`
- `src/renderer/shadow/ShadowRenderer.h/.cpp`
- `src/renderer/shadow/ShadowRenderContext.h`
- `src/renderer/shadow/ShadowMatrices.h/.cpp`
- `src/renderer/shadow/ShadowCasterCuller.h/.cpp`
- `docs/Mecraft渲染契约映射表.md`

并逐步把 `Renderer.cpp` 中 shadow 相关状态迁移出去：

- `m_shadowModelView`
- `m_shadowProjection`
- `m_shadowProjectionInverse`
- `m_shadowViewProj`
- `m_shadowLightDirection`
- `m_shadowExtent`
- `m_shadowTexelWorldSize`
- `renderShadowMap`
- `buildShadowProjectionData`
- shadow target clear/bind
- shadow caster list 收集

最终 `Renderer` 只负责调度：

```cpp
ShadowResult shadow = m_shadowRenderer.render(world, camera, frame, directives.shadow);
bindShadowResultForDeferred(shadow);
```

## 14. 本报告与旧差异报告的关系

`docs/DerivativeMain内置渲染管线完整差异分析报告.md` 主要回答：

- DerivativeMain 有哪些 shader 文件、pass、函数、材质语义尚未移植。
- Mecraft 当前 target 大致映射 DerivativeMain 哪些 buffer。
- 哪些效果缺失或近似。

本报告回答另一个问题：

- Mecraft C++ 渲染宿主如何与 Iris/OptiFine 不一致。
- 为什么 shader 数学对齐后仍可能出现阴影/透明/历史/体积等异常。
- 应该如何改造 C++ 架构来承载 DerivativeMain，而不是继续局部补 shader。

后续开发应同时使用两份报告：

- 改 shader 算法时看 DerivativeMain 差异报告。
- 改 C++ pass、FBO、uniform、shadow、sampler、render list 时看本报告。

当前阴影 ghosting 优先归入本报告的 C++ 宿主契约问题，而不是旧报告中的单个 shader 函数移植问题。
