# Renderer 后续瘦身优化方案

> 前置条件：渲染管线重构 Phase 1-11 已完成，`Game` 不再直接驱动 `Renderer::renderOpaqueAndCutout()` / `renderTransparentAndOverlays()`，`RenderScene` 是 gameplay 的唯一渲染入口。
> 目标：把 `Renderer` 从 legacy 渲染编排类降级为可删除的过渡层，最终只保留明确归属的渲染资源、terrain streaming、overlay、debug/profiling 等小模块。

---

## 当前判断

当前 `Renderer` 已经不再是单纯的“所有渲染都在里面”的上帝类，而是三种职责混在一起：

| 类别 | 当前内容 | 处理方向 |
|------|----------|----------|
| Legacy 管线编排 | `renderWorldDeferred()`、`renderWorldForward()`、`renderTransparentCompositePass()`、`renderGBufferTerrain()` | 迁入 `DeferredPipeline` / `ForwardPipeline` / terrain stage 后删除 |
| 共享基础设施 | terrain cache、meshing service、world render buffer、thread pool、render targets、sky renderer、shadow renderer | 拆成 `RenderResourceHub` 或由 `RenderScene` 明确持有 |
| 兼容桥接 | `RenderPipelineSettings`、legacy getter/setter、`syncFrameOutputFromLegacyRenderer()` 依赖 | 迁到 `RenderSettings` / `FrameOutput` 后删除 |
| 小型独立渲染器 | 方块 outline、break overlay、debug overlay | 提取为 `BlockInteractionOverlayRenderer` / `DebugOverlayRenderer` |
| Debug/profiling | GPU timer、chunk/meshing frame stats | 提取为 debug-only profiler/facade |

关键问题不是“文件太长”本身，而是 `Renderer` 仍然同时扮演：

- legacy render pipeline
- render resource owner
- terrain streaming coordinator
- render settings compatibility layer
- overlay renderer
- debug/profiler provider

这会继续阻碍后续可编辑 pipeline 和 shaderpack pipeline，因为新 pipeline graph 不应该依赖一个 legacy `Renderer` 私有状态池。

---

## 最终目标形态

建议最终删除或重命名 `Renderer`。可选目标：

### 推荐目标：删除 Renderer

```text
RenderScene
  ├─ RenderPipelineService
  ├─ RenderResourceHub
  ├─ TerrainStreamingService
  ├─ ForwardPipeline
  ├─ DeferredPipeline
  ├─ BlockInteractionOverlayRenderer
  └─ RenderDebugService
```

### 兼容目标：Renderer 改名为 RenderResourceHub

如果一次删除成本过高，可以先把 `Renderer` 降级为资源容器，之后重命名：

```cpp
class RenderResourceHub {
public:
    TerrainRenderCache& terrainCache();
    TerrainRenderer& terrainRenderer();
    WorldRenderBuffer& worldRenderBuffer();
    ChunkMeshingService& meshingService();
    DeferredRenderTargets& deferredTargets();
    CommonFrameTargets& commonTargets();
    GameplaySkyRenderer& skyRenderer();
    shadow::ShadowRenderer& shadowRenderer();
};
```

`RenderResourceHub` 不允许出现 `renderWorldDeferred()`、`renderTransparentAndOverlays()`、`RenderPipelineSettings` 这类编排或 legacy 状态。

---

## 优化原则

1. **先迁调用者，再删实现**：外部不再调用 legacy API 后，才能删除 `Renderer` 对应方法。
2. **先统一设置，再迁编排**：Dashboard/Game 必须先改为 `RenderSettings`，否则 `RenderPipelineSettings` 会继续把 legacy 状态钉在 `Renderer` 里。
3. **先迁 terrain，再迁 transparent**：terrain batch/transparent batch 是 `Renderer` 剩余编排的核心共享状态，必须先独立。
4. **FrameContext 是唯一帧输入**：新代码不得从 `Renderer::RenderFrameData` 读取帧数据。
5. **FrameOutput 是唯一帧输出**：Game/UI/held item/postprocess 不再查询 `isDeferredFrameActive()`、`gbufDepthTexture()`、`weatherMaskTexture()`。

---

## 分阶段方案

### Phase R1：切断外部 legacy API

目标：

- `Game` 不再直接访问 `Renderer`。
- Dashboard 不再直接读写 `Renderer::RenderPipelineSettings`。
- `RenderScene::syncFrameOutputFromLegacyRenderer()` 不再是正常路径，只保留短期断言或删除。

具体任务：

- `Game::renderFrame()` 只调用 `RenderScene::renderFrame()`。
- `Game::renderPrecipitation()` 从 `FrameOutput.sceneDepthTex/gbufferDepthTex` 获取深度，不再调用 `Renderer::gbufDepthTexture()`。
- `Game::renderHeldItem()` 只读 `RenderScene::getHeldItemShadowData()`。
- Dashboard 改为读写 `RenderScene::getSettings()` / `setSettings()`。
- 删除或标记 deprecated：
  - `Renderer::renderOpaqueAndCutout()`
  - `Renderer::renderTransparentAndOverlays()`
  - `Renderer::getRenderPipelineSettings()`
  - `Renderer::setRenderPipelineSettings()`
  - `Renderer::isDeferredFrameActive()`

验收：

- `rg "renderOpaqueAndCutout|renderTransparentAndOverlays|getRenderPipelineSettings|setRenderPipelineSettings|gbufDepthTexture|weatherMaskTexture" src/game src/ui` 无业务调用。
- Game 不 include `Renderer.h`。

### Phase R2：让 DeferredPipeline 真正接管编排

目标：

- 删除 `Renderer::renderWorldDeferred()` 的业务价值。
- `DeferredPipeline::renderFrame()` 成为完整 deferred 编排。

当前缺口：

- `DeferredPipeline::renderFrame()` 仍有 terrain、shadow、水体、粒子 TODO 或空实现。
- `Renderer::renderWorldDeferred()` 仍是更完整的真实路径。

具体任务：

- 将 `Renderer::renderGBufferTerrain()` 迁为 `DeferredTerrainStage` 或 `GBufferTerrainPass`。
- 将 `Renderer::renderShadowMap()` 依赖彻底迁到 `ShadowPass`。
- 将 `Renderer::renderWaterCompositePass()` 的参数搬运移入 `WaterCompositePass`/`DeferredPipeline`，输入改为 `FrameContext + RenderSettings + SharedRenderResources`。
- 将 `Renderer::renderParticlesToSceneResolved()` 迁到 `DeferredPipeline` 或 `ParticleRenderPass`。
- `DeferredPipeline` 使用 `RenderScene` 下发的真实 `RenderSettings`，不再 `m_currentSettings = RenderSettings{}`。

验收：

- deferred 模式不调用 `Renderer::renderWorldDeferred()`。
- `DeferredPipeline::renderFrame()` 输出完整 `FrameOutput`：scene depth、gbuf depth、weather mask、held item shadow、debug skip 标志。
- deferred 画面与迁移前一致，debug view 正常。

### Phase R3：让 ForwardPipeline 接管前向路径

目标：

- 删除 `Renderer::renderWorldForward()` 的业务价值。
- Forward fallback 不再依赖 `Renderer::m_chunkShader`、`bindChunkRenderState()`、`RenderFrameData`。

具体任务：

- 将 forward terrain 渲染接入 `TerrainRenderer` 的明确 forward API。
- `ForwardPipeline` 负责 sky、opaque/cutout、transparent、overlay 前的 scene output。
- 将 forward fog/sky/weather uniforms 从 `Renderer::bind*Uniforms()` 迁入 terrain render state builder。

验收：

- forward 模式不调用 `Renderer::renderWorldForward()`。
- Forward/Deferred 运行时切换正常。

### Phase R4：提取 TerrainStreamingService

目标：

- 把 chunk meshing、MDI allocation、terrain cache update 从 `Renderer` 移出。
- pipeline 只请求“本帧可绘制 terrain batches”，不直接管理 meshing 生命周期。

建议新增：

```cpp
class TerrainStreamingService {
public:
    void beginFrame(const World& world, const CameraData& camera);
    void submitMeshingJobs(const World& world);
    void drainMeshingResults(const World& world);
    TerrainFrameBatches buildBatches(const World& world, const Frustum& frustum);
    void endFrame();
};
```

迁移内容：

- `submitMeshingJobs`
- `drainMeshingResults`
- `releaseStaleMdiAllocations`
- `recordMeshingHistory`
- `m_meshingInFlight`
- `m_deferredMeshResults`
- meshing budgets
- chunk culling debug state

验收：

- `Renderer` 不再持有 meshing 队列和预算字段。
- Terrain 相关测试和性能基线不退化。

### Phase R5：提取 Overlay 渲染

目标：

- 方块选择框、破坏 overlay 从 `Renderer` 独立。

建议新增：

```cpp
class BlockInteractionOverlayRenderer {
public:
    void init(ResourceMgr& resources);
    void shutdown();
    void render(const World& world,
                const CameraData& camera,
                const BlockTargetRenderData& target,
                const BlockBreakRenderData& blockBreak);
};
```

迁移内容：

- `initOutlineMesh()`
- `initBreakOverlayMesh()`
- `renderBlockOutline()`
- `renderBlockBreakOverlay()`
- outline/break overlay VAO/VBO/shader 字段

验收：

- `Renderer` 不再持有 outline/break overlay GL 资源。
- block selection 和 break overlay 正常。

### Phase R6：提取 RenderDebugService

目标：

- Debug overlay、GPU timer、pipeline stats 从 `Renderer` 移出。

建议新增：

- `RenderDebugService`
- `GpuFrameProfiler`
- `TerrainFrameStats`

迁移内容：

- `renderDeferredDebugOverlay()`
- GPU timer query 管理
- debug frame stats getter
- meshing history debug 数据

验收：

- release 构建中不携带 debug-only 字段。
- Dashboard 通过 debug facade 读取数据。

### Phase R7：删除 legacy Renderer 壳

目标：

- 删除委托空壳方法和 legacy 状态。
- `Renderer` 若仍存在，只能是 `RenderResourceHub`，否则删除。

删除候选：

- `render()`
- `renderOpaqueAndCutout()`
- `renderTransparentAndOverlays()`
- `renderWorldDeferred()`
- `renderWorldForward()`
- `renderGBufferTerrain()`
- `bindSkyLightingUniforms()`
- `bindFogUniforms()`
- `bindAtmosphereUniforms()`
- `bindWaterEffectUniforms()`
- `RenderPipelineSettings`
- `RenderFrameData`
- `m_chunkShader` 这类跨 pass mutable shader 指针
- delegated-to-pass 空壳方法

验收：

- `Renderer.cpp` 删除或缩减到资源 hub 级别。
- `Renderer.h` 不暴露渲染管线设置、pass 编排、FrameOutput 兼容查询。
- 可编辑 pipeline / shaderpack 方案不依赖 legacy `Renderer`。

### Phase R8：Legacy Removal / No Compatibility Layer

目标：

- 删除所有仅为迁移保留的兼容桥。
- 确认新架构可以独立存在，旧设计不再进入调用链、数据模型或公共 API。
- 将“没有 legacy 残留”作为渲染重构最终验收门槛，而不是后续可选清理。

必须删除：

- `RenderScene::setLegacyRenderer()`
- `RenderScene::syncFrameOutputFromLegacyRenderer()`
- `Renderer::RenderPipelineSettings`
- `Renderer::RenderFrameData`
- `Renderer::renderWorldDeferred()`
- `Renderer::renderWorldForward()`
- `Renderer::renderOpaqueAndCutout()`
- `Renderer::renderTransparentAndOverlays()`
- `Renderer::isDeferredFrameActive()`
- `Renderer::gbufDepthTexture()`
- `Renderer::weatherMaskTexture()`
- legacy settings 双向转换函数
- 从 legacy renderer 同步 `FrameOutput` 的所有路径

允许保留：

- 历史文档中的 legacy 名称。
- 已改名且职责纯净的 `RenderResourceHub`，前提是它不包含管线编排、legacy settings、FrameOutput 兼容查询。
- 面向旧存档/旧配置文件的配置迁移器，前提是它只在加载配置时运行，不参与 frame render path。

验证命令：

```text
rg "setLegacyRenderer|syncFrameOutputFromLegacyRenderer|RenderPipelineSettings|RenderFrameData|renderWorldDeferred|renderWorldForward|renderOpaqueAndCutout|renderTransparentAndOverlays|isDeferredFrameActive|gbufDepthTexture|weatherMaskTexture" src
```

验收：

- 上述命令在 `src/` 中无命中，或只命中明确标记为 one-shot 配置迁移的代码。
- `FrameOutput` 只由 active `RenderPipeline` 或 `GraphRenderPipeline` 生成。
- `RenderScene` 不知道 legacy renderer 的存在。
- Dashboard、Game、UI、held item、postprocess、debug overlay 都不从旧 renderer 查询状态。
- shaderpack / pipeline editor 不依赖旧 `Renderer` 私有状态。

---

## 推荐优先级

### P0：必须先做

1. Dashboard/Game 全部改用 `RenderScene + RenderSettings + FrameOutput`。
2. `DeferredPipeline` 补齐 terrain/shadow/water/particle，取代 `renderWorldDeferred()`。
3. 移除 `RenderPipelineSettings` 外部读写。

### P1：收益最大

1. 提取 `TerrainStreamingService`。
2. 提取 `BlockInteractionOverlayRenderer`。
3. 删除 delegated 空壳和 legacy render methods。

### P2：为光影包和编辑器铺路

1. `Renderer` 降级为 `RenderResourceHub` 或删除。
2. 所有 pipeline 只依赖 `FrameContext`、`RenderSettings`、`SharedRenderResources`、contract registry。
3. 准备接入 `PipelineAsset` / `PipelineGraphCompiler`。

### P3：最终清场

1. 执行 Phase R8，删除所有 legacy 兼容桥。
2. 用搜索规则确认旧 API 不再出现在 `src/`。
3. 将“无 legacy renderer 依赖”作为进入 shaderpack/pipeline editor 正式开发的前置条件。

---

## 风险点

| 风险 | 控制 |
|------|------|
| DeferredPipeline 当前不是完整路径，直接删除 Renderer 会破画面 | 先迁 terrain/shadow/water/particle，逐帧对比 |
| Transparent/water 顺序微妙，容易影响 TAA/VFog | 保留现有注释中的 DerivativeMain 顺序，迁移后用 debug view 和水边缘检查 |
| Meshing 与 render buffer 交织，移动过大会影响性能 | `TerrainStreamingService` 分步提取，先移动状态，再移动调用 |
| Dashboard 仍依赖 legacy settings | 先统一 `RenderSettings`，再动 pipeline 编排 |
| FrameOutput 旧帧/默认值问题复发 | 禁止从 legacy renderer 同步作为正式路径，FrameOutput 由 active pipeline 直接生成 |
| 兼容桥长期残留，变成新架构的一部分 | Phase R8 设置硬性删除清单和 `rg` 验证规则 |

---

## 判断标准

优化后的好状态不是“Renderer 行数归零”，而是：

- `RenderScene` 是唯一帧入口。
- `RenderPipeline` 是唯一管线执行抽象。
- `RenderSettings` 是唯一运行时渲染配置。
- `FrameContext` 是唯一帧输入。
- `FrameOutput` 是唯一帧输出。
- terrain streaming、overlay、debug/profiler 都有独立所有权。
- shaderpack/pipeline editor 不需要知道 legacy `Renderer` 存在。
- `src/` 中不存在 legacy renderer 兼容 API；旧设计只允许出现在历史文档或 one-shot 配置迁移器里。
