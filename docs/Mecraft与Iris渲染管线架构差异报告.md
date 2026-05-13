# Mecraft 与 Iris/OptiFine 渲染管线架构差异报告

> 目标：把当前 Mecraft C++/OpenGL 渲染宿主环境改造成更符合 Iris/OptiFine shaderpack 行为的架构，减少 DerivativeMain 内置光影在开发时因为引擎端契约不一致而产生的异常 bug。  
> 范围：当前 Mecraft `src/renderer/`、`assets/shaders/`、内置 `DerivativeMain/` 光影包、根目录 `Iris-26.1/` 源码。  
> 重点：本报告不是 DerivativeMain shader 文件逐项移植清单，而是 **Mecraft 引擎端宿主行为与 Iris 宿主行为的差异报告**。

## 目标边界

当前目标是：**复现 Iris/OptiFine 中 DerivativeMain 依赖的宿主契约，将 DerivativeMain 做成 Mecraft 内置光影，并以原版 Minecraft 材质包作为目标材质输入。**

本报告中的 `ShaderpackDirectives`、`ShaderpackTextureContract`、`RenderPhase` 等建议，是为了让内置 DerivativeMain 获得类似 Iris/OptiFine 的稳定运行环境，而不是要求 Mecraft 立即支持任意外部 shaderpack 替换。

明确非目标：

- 不做通用 shaderpack loader。
- 不承诺加载任意 OptiFine/Iris shaderpack。
- 不以 PBR/LabPBR/POM/高级材质包为当前效果目标。
- 不优先实现 Distant Horizons、Physics Oceans、外部材质扩展等 DerivativeMain 可选分支。

架构上保留 Iris-like 的边界，是为了防止 C++ pass、buffer、uniform、sampler 继续散落硬编码；这能降低 DerivativeMain 移植和调试成本。它不等价于“顺手支持不同 shaderpacks”。

## 0. 当前结论

当前 Mecraft 已经有一套可运行的 Hybrid Deferred 管线，但它还不是 Iris/OptiFine shaderpack 宿主。很多 pass、uniform、render target、sampler、阴影 culling、透明/实体提交规则是 Mecraft 手写约定，而不是从 shaderpack directives 和 Minecraft/Iris 渲染阶段中派生出来。

这会导致一种非常容易误判的问题：**shader 代码局部看起来已经和 DerivativeMain 对上了，但 shader 所依赖的宿主环境并没有和 Iris 对上。**

近期阴影问题就是典型症状：

- `No Warp` 不出现鬼影。
- `Radial Debug` 和 `Derivative` 都出现鬼影。
- debug view 中的 shadow visibility 与最终画面一致。
- 缩小阴影距离会加重鬼影。
- cloud shadow 对该问题无影响。
- 继续做接收端 OOB 保护、局部距离限制后，鬼影仍存在且行为一致。

这组现象不再支持“单点 shader 采样错误”作为首要判断。更高概率是 **C++ shadow pass 的宿主契约仍与 Iris/OptiFine 不一致**，尤其是阴影 caster 提交、阴影 frustum/culling、shadow projection/distortion 作用域、render distance multiplier、以及 depth/color target 语义之间的组合问题。

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

这比最初更接近正确方向，但仍不是 Iris 的 `createShadowFrustum + invokeCullTerrain`。简单 XZ 圆形距离无法表达 Iris 的 shadow culling 规则，也无法保证 DerivativeMain 的非线性 shadow warp 之后不会把不该写入的 caster 折入有效 shadow map 区域。

### 4.3 为什么“缩短阴影距离反而加重鬼影”很重要

如果问题只是接收端采样越界，缩短 shadow distance 通常会让投影范围变小、错误范围变明显，但不一定导致方向相关 ghost caster 一致存在。

现在的表现更像：

- shadow projection 半径缩小后，shadow warp 的有效域变得更紧。
- 某些 caster 提交仍没有按 Iris 预期剔除。
- 这些 caster 经过 DerivativeMain/Radial warp 后被压入 shadow map 可采样区域。
- 因为 caster 集合与相机朝向、chunk collect、LOD/visible set 有关，所以不同视角 ghost 不同。
- `No Warp` 消失，说明普通线性 shadow projection 下这些 caster 没有被折入或折入不明显。
- debug view 和 final 一致，说明错误已经存在于 shadow visibility/深度关系中，不是后续 lighting、cloud shadow 或 tone mapping 引入。

因此下一步不应继续优先改 lighting shader，而应优先让 shadow pass 的 caster 域、frustum、distance multiplier、render list 与 Iris 对齐。

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

所以 Mecraft 不能把 `shadow.culling=false` 简化成“shadow pass 不做任何剔除”。正确目标是：**复刻 Iris 在该 directive 组合下最终提交给 shadow pass 的 terrain/entity 集合**。

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

建议建立一个固定表：`ShaderpackTextureContract`，将 DerivativeMain 的每个 sampler 映射到 Mecraft target、格式、filter、wrap、mipmap、写入 pass、读取 pass。没有这张表，后续每移植一个 shader 都可能出现“采样到了看似合理但语义错误的 buffer”。

## 6. Pass/phase 架构差异

Iris 的 shaderpack pass 有明确语义：

- `gbuffers_*`：按 Minecraft render type 写入基础材质/法线/光照/深度。
- `shadow`：从 light 视角渲染 shadow terrain/entities/translucent 等。
- `deferred*`：延迟光照、AO/GI、反射等。
- `composite*`：云、体积、bloom、TAA、后处理链。
- `final`：最终输出。

Mecraft 当前 pass graph 不直接使用这些名字，而是按自身功能拆分。这可以保留，但必须补一个 `ShaderpackPhase` 或 `RenderPassContract` 层，使每个 Mecraft pass 明确声明：

- 对应哪个 shaderpack phase。
- 当前绑定哪些 color/depth attachments。
- 当前允许读取哪些 texture。
- 当前写入哪些 texture。
- viewport/scale 是全分辨率、半分辨率还是自定义。
- GL blend/depth/cull/color mask 状态。
- 必须上传哪些 OptiFine/Iris uniform。
- 是否需要 previous/current matrix、camera、time。

没有这个 contract 层，Renderer 会越来越像“能跑但难以证明等价”的状态机。阴影 bug 只是第一个放大器；后续 TAA、SSR、体积云、透明、水、手持物也会遇到同类问题。

## 7. Uniform 与时间/坐标契约差异

DerivativeMain 依赖大量 OptiFine/Iris uniform 语义。Mecraft 当前只上传项目 shader 已经用到的 uniform，例如 shadow matrices、camera、time、noise、sky 等。

需要补齐一个统一的 `IrisUniformState`：

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

需要确认每个路径在 shadow pass 中与 DerivativeMain 一致：

- cutout alpha discard 阈值。
- mip alpha 处理。
- 树叶/草 tint 是否参与 shadowcolor。
- shadow depth 是否写入 alpha-tested geometry。
- shadow normal/color 是否对 cutout 写入一致。
- backface culling 是否符合 `SHADOW_BACKFACE_CULLING`。
- translucent shadow 是否按 DerivativeMain 预期启用。

白/黑块问题常见根因包括：alpha-tested caster 写入了错误 shadowcolor、depth/color target clear 或 blend 状态不一致、shadow sampler filter 与 color target 不一致、cutout 在 GBuffer 与 shadow pass 中使用了不同 UV/mip/tint/atlas 逻辑。

## 9. 与 Iris 对齐的目标架构

建议不要继续把所有逻辑塞进 `Renderer`。目标架构应拆成以下层：

### 9.1 `ShaderpackDirectives`

即使当前只支持内置 DerivativeMain，也要建立 directives 层：

- `ShadowDirectives`
  - `shadowDistance`
  - `shadowDistanceRenderMul`
  - `shadowIntervalSize`
  - `sunPathRotation`
  - `shadowMapResolution`
  - near/far/fov
  - terrain/translucent/entities/blockEntities/player toggles
  - shadow culling state
  - shadow sampler/filter/mipmap
- `BufferDirectives`
  - colortex/shadowcolor/depthtex 格式
  - flip/history
  - scale
  - clear color/depth
  - mipmap/filter/wrap
- `ProgramDirectives`
  - blend
  - depth test/write
  - cull
  - program toggle
  - render scale

注意：这里不是为了做通用 shaderpack loader，而是为了让内置 DerivativeMain 也通过一套 Iris-like contract 驱动 C++，避免硬编码散落。

### 9.2 `ShadowRenderer`

从 `Renderer` 中拆出 shadow 子系统：

- 输入：
  - `World`
  - `Camera`
  - `RenderFrameData`
  - `ShadowDirectives`
  - chunk/entity render registries
- 输出：
  - shadow matrices
  - shadow light direction
  - shadow render targets
  - debug info：caster count、chunk count、distance/culling mode、matrix、snap offset
- 内部：
  - `ShadowRenderContext`
  - `ShadowMatrices` equivalent
  - `ShadowFrustum`
  - terrain caster list
  - cutout/translucent/entity caster list

这样可以防止 shadow pass 继续隐式复用主 pass 的 frustum、shader、chunk 收集副作用。

### 9.3 `ShadowFrustum` / `ShadowCasterCuller`

先实现三层：

1. **P0：Iris distance render mul 语义**
   - caster distance = `shadowDistance * shadowDistanceRenderMul`。
   - DerivativeMain 当前 `shadowDistanceRenderMul = 1.0`。
   - 不使用主相机朝向相关 visible set 作为 shadow caster 的唯一来源。
   - 输出 debug：实际 caster 半径、chunk 数、被剔除 chunk 数。

2. **P1：Iris-like safe shadow frustum**
   - 从 camera/player 视域中“会被阴影影响的区域”反推 caster 区域。
   - 不直接用 warped shadow clip space 判断 chunk，因为 DerivativeMain 是非线性 warp。
   - 保留安全边界，避免太阳角度/snap 时 popping。

3. **P2：高级 culling**
   - 参考 Iris `AdvancedShadowCullingFrustum`、`SafeZoneCullingFrustum`、`BoxCuller`。
   - entity/block entity 使用独立 distance multiplier。

当前简单 XZ 距离限制只能算 P0 的临时近似，不是最终架构。

### 9.4 `ShaderpackTextureContract`

建立一张硬编码但集中管理的表：

| Shaderpack 名 | Mecraft target | 格式 | filter | mipmap | clear | 写入 pass | 读取 pass |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `shadowtex0` | `ShadowDepth` | DEPTH32F | 待确认 | 待确认 | depth=1 | shadow | deferred/fog/debug |
| `shadowcolor0` | `ShadowColor` | RGBA8/待确认 | 待确认 | 待确认 | black/transparent | shadow | deferred/fog |
| `shadowcolor1` | `ShadowNormal` | RGBA16F | 待确认 | 待确认 | neutral | shadow | deferred/fog |
| `depthtex0` | `GDepth` | DEPTH32F | nearest | no | depth=1 | gbuffers | deferred/composite |
| `colortex*` | Mecraft GBuffer/Scene/History | varies | varies | varies | varies | varies | varies |

所有 shader include 只通过这个 contract 获取 sampler 语义，不允许每个 shader 自己猜。

### 9.5 `RenderPhase` / `ProgramState`

建立类似：

```cpp
enum class ShaderpackPhase {
    GbuffersTerrain,
    GbuffersWater,
    GbuffersEntities,
    Shadow,
    Deferred,
    Composite,
    Final
};

struct ProgramState {
    ShaderpackPhase phase;
    FramebufferContract framebuffer;
    TextureBindings textures;
    UniformSet uniforms;
    GLState state;
};
```

这不是形式主义。它能让每次移植 DerivativeMain 文件时都先检查“这个 shader 在 Iris 中属于哪个 phase，它能读写什么，GL 状态是什么”，避免只看 GLSL 函数本身。

## 10. 立即改造优先级

### P0：阴影 ghosting 相关

1. 建立 `ShadowRenderContext`，把 shadow matrices、distance、renderDistanceMul、intervalSize、near/far、light direction、debug info 集中起来。
2. shadow pass 不再直接复用主 pass chunk visible/cutout list，改为独立生成 shadow caster list。
3. caster list 必须输出 debug：提交 chunk 数、剔除 chunk 数、最大距离、是否来自主 frustum、是否使用 shadow frustum。
4. 实现 DerivativeMain `shadowDistanceRenderMul = 1.0` 的明确语义，而不是散落在 `std::max(64.0f, shadowDistance)`。
5. 增加 shadow map caster debug view：显示写入 shadow map 的 chunk bounds 或 caster coverage，直接判断 ghost 是否来自远处/错误 chunk。

### P0：防止继续被局部 shader 对号误导

1. 建立 `ShaderpackTextureContract`。
2. 建立 `ShaderpackDirectives`，先只覆盖 DerivativeMain 当前用到的字段。
3. 每个 shader include 顶部标注依赖的 contract，而不是只标注 DerivativeMain 来源函数。

### P1：cutout/alpha/材质语义

1. 对齐 terrain atlas alpha discard、tint、lightmap、mip alpha。
2. shadow pass 与 GBuffer pass 使用同一份 cutout alpha/tint/material helper。
3. 确认 shadowcolor0/1 对树叶、草、水、半透明方块的写入语义。
4. 分离 opaque/cutout/translucent shadow toggles。

### P1：sampler/filter/mipmap

1. shadow depth/color target 的 filter、wrap、compare mode 集中配置。
2. 按 DerivativeMain/Iris 需要生成 mipmap，而不是按 Mecraft 方便与否。
3. debug 输出当前 shadow target 参数。

### P2：完整 Iris-like pipeline

1. 把 `Renderer` 中 pass 逐步拆为子 renderer。
2. 实现 `ProgramState`/`RenderPhase`。
3. 补齐 entities、block entities、hand、weather、particles 的 GBuffer/shadow contract。
4. 补齐 previous matrices、history flip、frame/tick time uniform contract。

## 11. 针对当前鬼影的下一步排查建议

不要先继续改 lighting shader。建议下一步按以下顺序做引擎端验证：

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

目前用户反馈属于最后一种，并且 `No Warp` 与 warp 模式差异明显，所以最高优先级是 `shadow pass caster domain + warp domain` 的一致性。

## 12. 不要再误判的几个点

1. **矩阵数值接近 Iris，不代表 shadow pass 等价 Iris。**  
   矩阵只是 contract 的一部分，caster list、frustum、distance render mul、sampler、target、uniform time 都必须一起对齐。

2. **DerivativeMain `shadow.culling=false` 不代表无边界提交。**  
   Iris 仍有 render distance、terrain setup/culling、entity distance、voxelization fallback。

3. **debug shadow visibility 一致，说明问题在更早阶段。**  
   不应优先怀疑 cloud shadow、tone mapping、scene composite。

4. **PCSS 开关无影响，不代表 shadow 系统没问题。**  
   如果基础 shadow map 或 caster domain 已错，PCSS 只是放大/模糊错误。

5. **缩短 shadowDistance 加重 ghost，是强烈的 domain mismatch 信号。**  
   它说明 projection/warp 有效域变小后，不该参与的 caster 更容易被折入可见区域。

6. **C++ 与 shader 必须作为一个协议调试。**  
   DerivativeMain 的 GLSL 是权威数学，但 Iris 是它默认运行的宿主协议。Mecraft 要做内置光影，就必须复刻协议，而不是只复刻函数。

## 13. 建议新增文档/代码入口

建议后续新增：

- `src/renderer/shaderpack/ShaderpackDirectives.h/.cpp`
- `src/renderer/shaderpack/ShaderpackTextureContract.h/.cpp`
- `src/renderer/shadow/ShadowRenderer.h/.cpp`
- `src/renderer/shadow/ShadowRenderContext.h`
- `src/renderer/shadow/ShadowMatrices.h/.cpp`
- `src/renderer/shadow/ShadowCasterCuller.h/.cpp`
- `docs/Iris宿主契约映射表.md`

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
