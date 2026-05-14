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

**2026-05-13 阴影系统已完成迁移**：原 DerivativeMain/Radial 非线性 shadow warp 与 Mecraft greedy 大面片不兼容导致的 ghosting 问题已通过迁移到 CSM 级联阴影解决。ShadowRenderer 已从 Renderer 拆分，warp 代码已清理，CSM 4 cascade + transition fade + PCSS + contact shadow 已实现。正式阴影路线已确认为 Mecraft 自有 CSM contract。

仍需推进的工作：
- Mecraft Renderer Contract 系统化（`MecraftTextureContract`、`MecraftRenderContract`、`MecraftRenderPhase`）
- GBuffer Material 合同（Material.inc 逐 ID、实体/手/掉落物进 GBuffer）
- Atmosphere/Cloud/Fog/Water 视觉收敛

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

## 4. 阴影系统差异：已通过 CSM 迁移解决

### 4.1 已完成的阴影重构（2026-05-13）

阴影系统已从 DerivativeMain/Iris shadow warp 路线迁移到 Mecraft 自有 CSM 级联阴影：

- `ShadowRenderer` 已从 `Renderer` 拆分（`src/renderer/shadow/ShadowRenderer.h/.cpp`）。
- CSM 4 cascade depth texture array，per-cascade matrix/split/texel snapping。
- `mecraft_shadow.glsl` 作为正式 CSM contract：cascade 选择、投影、bias、PCF 3x3、PCSS、cascade transition fade。
- 旧 `shadowWarpMode`/`shadowWarpCutoff`/`derivativeExactShadow` 已从 settings、renderer、shader 中删除。
- Water/stained glass 在 CSM depth-only pass 中 discard。
- Cutout/SSS 阴影白化已修复。
- Contact shadow 16 步调优。
- Debug views：CSM Cascade、CSM Depth 0-3、Cascade Info。

### 4.2 CSM 架构下的 shadow caster 提交

Mecraft 当前 shadow pass 流程：

1. `ShadowRenderer::computeLightDirection()` 从天空颜色计算光照方向。
2. `ShadowRenderer::update()` 构建 4 cascade 级联矩阵（texel snapping、frustum slice）。
3. Per-cascade 循环：
   - `DeferredRenderTargets::bindCsmShadowLayer(cascade)` 绑定 depth array layer。
   - 设置 per-cascade viewProj/shadowModelView/shadowProjection uniforms。
   - `ShadowCasterCuller::setup()` 建立 camera-centered cube 剔除域。
   - `renderOpaqueChunksAndCollectPasses()` + `renderCutoutChunks()` 提交 terrain。
4. `glCopyImageSubData` CSM layer 0 → legacy single-layer shadow depth（debug 兼容）。

与 Iris 差异：
- Iris 有独立 shadow frustum + `invokeCullTerrain`；Mecraft 使用 BoxCuller 距离域。
- Iris 可分别处理 terrain/entity/block entity shadow；Mecraft 当前只处理 terrain。
- 但 CSM 线性投影下 greedy mesh 稳定，ghosting 问题已解决。

### 4.3 鬼影根因与解决方案（已解决）

**根因**：DerivativeMain/Radial 非线性 shadow warp 在 Mecraft greedy 大面片顶点上执行，GPU 对 warp 后顶点进行线性插值，导致 shadow map 写入端的深度/颜色布局被扭曲。

**验证**：开启 `Debug Disable Greedy Meshing` 后 ghosting 消失。

**解决方案**：迁移到 CSM 级联阴影（线性正交投影），适配 greedy mesh。旧 warp 代码已从正式路径删除。

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

**树叶/草 cutout 阴影白化/黑斑已修复（2026-05-13）**：SSS depth 符号修正后，leaves/grass 在 soft shadow 下不再出现白块/黑点。

当前 Mecraft chunk GBuffer 和 shadow pass 的 chunk 分类：

- opaque chunk → depth write ON
- cutout entries → depth write ON（alpha test discard）
- transparent entries → 不进 shadow pass
- water/stained glass → `discard`（不写 hard depth）

仍需确认：
- cutout alpha discard 阈值、mip alpha 处理在 GBuffer 与 shadow pass 间的一致性。
- 树叶/草 tint 是否参与 shadowcolor（当前不参与，cutout 作为 opaque caster）。
- translucent shadow 按 Mecraft contract 重新定义。

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

### 9.2 `ShadowRenderer` ✅ 已实现

已从 `Renderer` 中拆出 shadow 子系统：

- `src/renderer/shadow/ShadowRenderer.h/.cpp` — cascade 数据、光照方向、uniform 绑定
- `src/renderer/shadow/ShadowMatrices.h/.cpp` — cascade 矩阵计算
- `src/renderer/shadow/ShadowCasterCuller.h/.cpp` — Box-culler 距离域剔除
- `src/renderer/shadow/ShadowRenderContext.h` — 数据契约结构体

`Renderer` 保留 shadow pass 编排（chunk 迭代、FBO 层绑定），数据层完全由 ShadowRenderer 管理。

### 9.3 `ShadowCasterCuller` / `MecraftShadow` ✅ P0+P1 已完成

1. ✅ **P0：稳定线性阴影** — CSM 线性投影，greedy mesh 稳定，texel snapping、BoxCuller 距离域。
2. ✅ **P1：CSM 级联阴影** — 4 cascade depth texture array，cascade transition fade，cascade-specific PCF。
3. ✅ **PCSS 近 cascade** — blocker search + variable PCF，仅 cascade 0 启用。
4. ✅ **Contact shadow** — 16 步屏幕空间 ray march。
5. P2：shadow-only bounded fine caster mesh — 未实现（低优先级）。

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

## 10. 改造优先级

### ~~P0：阴影 ghosting 收口~~ ✅ 已完成

1. ✅ `ShadowRenderer` 已从 `Renderer` 拆分，拥有 cascade 数据、光照方向、uniform 绑定。
2. ✅ 默认阴影路径已切到 CSM 级联阴影（线性投影），greedy mesh 稳定。
3. ✅ 旧 `Derivative/Radial Warp` 已从正式路径删除，`derivative_shadow.glsl` 中保留为数学参考。
4. ✅ ShadowCasterCuller 输出 debug：提交 chunk 数、剔除 chunk 数、最大距离。
5. ✅ CSM 4 cascade + cascade transition fade + cascade-specific PCF + PCSS 近 cascade。
6. ✅ Contact shadow 16 步调优。
7. ✅ Water/stained glass 在 CSM depth-only pass 中 discard。
8. ✅ Cutout/SSS 阴影白化修复（SSS depth 符号修正）。
9. ✅ Bias/normal offset 从 pipeline settings 透传。
10. ✅ Debug views：CSM Cascade、CSM Depth 0-3、Cascade Info。
11. ✅ Warp 代码清理（shadowWarpMode/derivativeExactShadow/shadowWarpCutoff 删除）。

### P0：防止继续被局部 shader 对号误导（未完成）

1. 建立 `MecraftTextureContract`。
2. 建立 `MecraftRenderContract`，先覆盖 shadow、GBuffer、history、material id。
3. 每个 shader include 顶部标注依赖的 Mecraft contract，同时可标注 DerivativeMain 参考来源。

### P1：cutout/alpha/材质语义 ✅ 阴影部分已完成

1. ✅ shadow pass 与 GBuffer pass 使用同一份 cutout alpha/tint/material helper。
2. ✅ Water/stained glass discard，cutout leaves/grass 作为 opaque caster。
3. 仍需：对齐 terrain atlas alpha discard、tint、lightmap、mip alpha 的逐行一致性。
4. 仍需：分离 opaque/cutout/translucent shadow toggles（当前 cutout 始终写 depth）。

### P1：sampler/filter/mipmap（未完成）

1. shadow depth/color target 的 filter、wrap、compare mode 集中配置。
2. 按 `MecraftTextureContract` 明确哪些 target 需要 mipmap/filter/wrap。
3. debug 输出当前 shadow target 参数。

### P2：完整 Mecraft pipeline 拆分（未完成）

1. 把 `Renderer` 中 pass 逐步拆为子 renderer。
2. 实现 `ProgramState`/`MecraftRenderPhase`。
3. 补齐 entities、block entities、hand、weather、particles 的 GBuffer/shadow contract。
4. 补齐 previous matrices、history、frame/tick time uniform contract。

## 11. 鬼影问题已解决

2026-05-13 验证：`Debug Disable Greedy Meshing` 开启后 ghosting 消失。根因：**非线性 shadow warp 与 greedy 大面片插值不兼容**。

2026-05-13 解决：迁移到 CSM 级联阴影（线性正交投影），旧 warp 代码已删除。Ghosting 不再复现。

以下排查方法保留为未来 shadow bug 的通用方法：

1. 在 shadow pass 中给每个提交 chunk/caster 输出 debug 计数和最大 camera distance。
2. 在 `No Warp`、`Radial Debug`、`Derivative` 三种模式下保持完全相同 caster list，只切换 vertex warp。
3. 固定太阳角度和 vegetation animation time，排除时间耦合。
4. 将 shadowDistance 改小/改大时记录 caster list 是否按 `shadowDistanceRenderMul` 成比例变化。
5. 临时强制 shadow caster list 为 camera 周围固定 chunk 方块区域，观察 ghost 是否消失。

这个顺序能最快区分 C++ 与 shader 问题。

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

## 13. 文档/代码入口

### 已实现

- ✅ `src/renderer/shadow/ShadowRenderer.h/.cpp` — shadow 数据+uniform 层
- ✅ `src/renderer/shadow/ShadowRenderContext.h` — 数据契约结构体
- ✅ `src/renderer/shadow/ShadowMatrices.h/.cpp` — cascade 矩阵计算
- ✅ `src/renderer/shadow/ShadowCasterCuller.h/.cpp` — Box-culler
- ✅ `assets/shaders/mecraft_shadow.glsl` — CSM GLSL contract
- ✅ Shadow 相关状态已从 `Renderer` 迁移到 `ShadowRenderer`

### 待建立

- `src/renderer/contract/MecraftRenderContract.h/.cpp`
- `src/renderer/contract/MecraftTextureContract.h/.cpp`
- `docs/Mecraft渲染契约映射表.md`

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
