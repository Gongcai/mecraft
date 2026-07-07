# Mecraft RHI 实际渲染迁移开发文档

> 本文档是 `RHI与Vulkan渲染后端重构方案.md` 的执行补充。当前目标不是继续扩大旧 OpenGL 渲染路径，而是把实际渲染提交迁移到 RHI。迁移顺序为：先让 OpenGL 成为完整可用的 RHI 后端，验证画面和性能，再接入 Vulkan 后端。

---

## 1. 当前目标

### 1.1 总目标

将渲染系统从“业务层直接调用 OpenGL”迁移为：

```text
RenderScene / Pipeline / Pass / UI / Resource
        │
        ▼
RHI resource + RHI command list + RHI pipeline + RHI bind group
        │
        ├── OpenGL RHI backend
        └── Vulkan RHI backend
```

OpenGL 后端必须先完整执行 RHI 命令，作为功能和画面正确性的基准。Vulkan 后端只在 OpenGL/RHI 路径稳定后接入。

### 1.2 当前已完成基础

- 公共 RHI 类型、handle、device、command list、descriptor、pipeline 头文件已经建立。
- 纹理资源公共接口已大面积改为 `RhiTextureHandle`。
- `DeferredRenderTargets` 对外不再公开 native texture getter。
- 资源、UI、pass 中的很多调用点已经通过 `RhiTextureHandle` 表达纹理身份。
- OpenGL 后端目前已有 `GlRhiDevice` 与 `GlRhiTextureRegistry`，但实际 draw、dispatch、barrier、rendering、pipeline、bind group 提交仍需要实现。

### 1.3 当前主要缺口

- `GlRhiCommandList` 目前没有执行真实 GL 命令。
- `RhiDevice` 创建资源后端对象的能力不足，当前主要分配 handle。
- shader 仍主要由 `Shader` 类管理 GL program 和字符串 uniform。
- pass 仍分散调用 `glEnable`、`glBindTexture`、`glBindFramebuffer`、`glDrawArrays`、`glDispatchCompute`。
- render target 内部仍由 GL FBO 和 GL texture 管理。
- UI、字体、粒子、terrain、debug timer 仍存在直接 GL 提交。

---

## 2. 迁移原则

### 2.1 OpenGL 后端必须是完整 RHI 后端

OpenGL 后端不是 GL id 查询工具。迁移完成时，业务层只能调用 RHI 公共接口；`renderer::rhi::gl::*` 只能被 OpenGL 后端内部和后端测试使用。

### 2.2 命令必须有明确语义

RHI command list 的每个命令要么执行真实后端行为，要么在验证层报告明确错误。禁止静默忽略有渲染语义的命令。

### 2.3 公共层不暴露后端对象

以下类型不得出现在 RHI 公共头、renderer 公共头、resource 公共头、UI 公共头：

- `GLuint`
- `GLenum`
- `GLint`
- `VkImage`
- `VkBuffer`
- `VkCommandBuffer`
- FBO id
- texture unit
- OpenGL uniform location

### 2.4 按垂直切片迁移

每次迁移选择一个可验证的垂直切片，例如“fullscreen pass RHI 化”或“UI 图片 RHI 化”。切片必须包含：

- RHI 接口补齐。
- OpenGL 后端真实实现。
- 调用点迁移。
- 旧 GL 调用删除。
- 构建、测试、扫描验证。

### 2.5 Vulkan 需求前置约束 OpenGL 实现

OpenGL 后端内部可以使用 GL 状态机，但 RHI 公共接口必须按显式模型设计：

- attachment 由 `RhiRenderingInfo` 描述。
- resource state 由 barrier 或 render graph 管理。
- shader binding 由 bind group 描述。
- pipeline 描述完整包含 depth、blend、raster、vertex input。
- draw/dispatch 不依赖调用点手写 GL 全局状态。

---

## 3. RHI 能力补齐清单

### 3.1 Device 与资源

需要补齐或落实的能力：

- `RhiDevice::createBuffer` 创建真实后端 buffer。
- `RhiDevice::createTexture` 创建真实后端 texture。
- `RhiDevice::createTextureView` 创建真实后端 texture view。
- `RhiDevice::createSampler` 创建真实后端 sampler。
- buffer 上传与局部更新接口。
- texture 子资源上传接口。
- 延迟释放队列，按 frame 管理 GPU 资源销毁。
- debug name 写入后端对象。

验收标准：

- `ResourceMgr` 不再手写 `glGenTextures`。
- `WorldRenderBuffer` 不再手写 `glGenBuffers`。
- atlas、texture array、cubemap、glyph atlas、render targets 均由 RHI device 创建。

### 3.2 Command List

需要真实实现：

- `beginRendering` / `endRendering`
- `textureBarrier` / `bufferBarrier`
- `setViewport` / `setScissor`
- `setGraphicsPipeline` / `setComputePipeline`
- `setBindGroup`
- `setVertexBuffer` / `setIndexBuffer`
- `pushConstants`
- `draw` / `drawIndexed` / `drawIndirect`
- `dispatch`
- `copyBuffer` / `copyBufferToTexture` / `copyTexture` / `blitTexture`
- `writeTimestamp`

验收标准：

- fullscreen triangle pass 可以只通过 RHI command list 绘制。
- compute pass 可以只通过 RHI command list dispatch。
- copy/blit 不再由 pass 直接调用 GL。

### 3.3 Pipeline 与 Shader

需要建立：

- `RhiShaderHandle` 对应 GL shader/program 或 Vulkan shader module。
- `RhiGraphicsPipelineDesc` 到 GL program + cached state 的映射。
- `RhiComputePipelineDesc` 到 GL compute program 的映射。
- pipeline cache。
- vertex input layout 到 VAO 或 backend vertex state 的映射。
- shader source loader 与 shader object 分离。
- per-pass uniform struct，使用 UBO、SSBO 或 push constants。

验收标准：

- pass 不再调用 `Shader::use()` 和 `Shader::set*()`。
- pipeline bind 后即可确定 shader、depth、blend、cull、primitive、attachment format。

### 3.4 Descriptor 与 Bind Group

需要建立：

- GL 后端 bind group 内部记录 buffer、texture view、sampler。
- texture/sampler 分离绑定。
- combined texture sampler 绑定。
- storage image 绑定。
- bind group layout 校验。
- bind group 与 pipeline layout 兼容性校验。

验收标准：

- pass 不再手写 `glActiveTexture` 和 `glBindTexture`。
- sampler slot 由 bind group layout 决定。
- image load/store 由 bind group 描述。

### 3.5 Render Target 与 Swapchain

需要建立：

- RHI texture 作为 color/depth attachment。
- OpenGL backend framebuffer cache。
- default framebuffer 或 swapchain texture 的统一表达。
- resize 时统一重建 render target。
- load/store/clear 通过 `RhiRenderingInfo` 表达。

验收标准：

- `DeferredRenderTargets::bind*()` 被 RHI rendering pass 替代。
- pass 不再调用 `glBindFramebuffer`、`glDrawBuffers`、`glClearBuffer*`。

### 3.6 Debug 与 Query

需要建立：

- debug label 映射到 GL debug group 和 Vulkan debug utils。
- timestamp/query pool 创建、写入、读取。
- debug service 不直接调用 GL query API。

验收标准：

- GPU timer 在 OpenGL/RHI 路径可用。
- debug group 由 RHI command list 录制。

---

## 4. 迁移阶段

## 阶段 0：建立迁移基线

目标：确认当前 OpenGL 画面和测试作为后续对照。

任务：

- 固定当前 `lightoff` 分支基线。
- 记录关键渲染场景：主菜单、普通世界、雨雪、昼夜、透明水体、阴影、UI、背包、调试视图。
- 保留当前构建与测试命令。
- 建立 RHI 迁移扫描命令。

完成标准：

- `mecraft` 构建通过。
- `rhi_core_test` 通过。
- 当前画面作为 OpenGL/RHI 对照目标。

## 阶段 1：OpenGL RHI 后端真实执行命令

目标：让 `GlRhiDevice` 从 handle 分配器变成实际后端。

任务：

- 增加 GL 后端资源记录：buffer、texture、texture view、sampler、shader、pipeline、bind group。
- 实现 `GlRhiCommandList` 的真实命令执行。
- 增加 GL framebuffer cache。
- 增加 GL pipeline state cache，避免每个 pass 手写状态。
- 增加 command validation 日志。

完成标准：

- 最小 RHI 测试能创建 texture、sampler、shader、pipeline、bind group，并绘制 fullscreen triangle。
- command list 的 draw/dispatch/copy/blit 在 OpenGL 后端有真实行为。
- 有渲染语义的 command 不再是空函数。

## 阶段 2：RHI Swapchain 与默认输出

目标：统一窗口输出入口。

任务：

- 建立 `GlRhiSwapchain`。
- 将 default framebuffer 作为 RHI current color target 表达。
- `RenderScene` 不再直接绑定 default framebuffer。
- FSR/postprocess 最终输出通过 RHI pass 写入 swapchain。

完成标准：

- 窗口 resize 后 RHI swapchain 尺寸正确。
- 主菜单和游戏画面最终输出路径通过 RHI。

## 阶段 3：资源系统迁移到 RHI 创建

目标：资源加载不再直接创建 GL 对象。

任务：

- `Texture2DLibrary` 使用 `RhiDevice::createTexture`。
- `EnvironmentTextureLibrary` 使用 RHI texture。
- `CubemapLibrary` 使用 RHI cubemap texture。
- block atlas、item atlas、HUD atlas、texture array 使用 RHI texture。
- `GlyphAtlas` 使用 RHI texture。
- sampler 参数进入 `RhiSamplerDesc`。

完成标准：

- `src/resource` 不再 include `glad/glad.h`。
- `ResourceMgr` 公共接口保持 `RhiTextureHandle`。
- 所有资源纹理由 RHI 后端拥有。

## 阶段 4：Fullscreen 与 PostProcess 优先迁移

目标：先迁移结构简单、可快速验证画面的 pass。

任务：

- 建立 `FullscreenTriangleRenderer` 的 RHI 版本。
- 迁移 `PostProcessPass`。
- 迁移 `Fsr1Pass`。
- 迁移 bloom、exposure、tone mapping、copy/blit 类 pass。
- 建立 fullscreen pipeline cache。

完成标准：

- fullscreen pass 不再调用 `glBindVertexArray`、`glDrawArrays`、`glActiveTexture`。
- 后处理链只通过 RHI command list、pipeline、bind group 提交。
- 输出画面与基线一致。

## 阶段 5：Render Targets 与 DeferredRenderTargets 迁移

目标：把 FBO 管理移入 RHI 后端。

任务：

- 用 RHI texture 创建 GBuffer、scene、history、SSAO、SSGI、reflection、cloud、velocity、weather mask。
- 用 RHI texture 创建 shadow/csm target。
- 用 `RhiRenderingInfo` 表达每个 pass 的输出 attachment。
- 删除 `DeferredRenderTargets::bind*()` 调用点。
- 删除 `copy*()` 和 `blit*()` 中的直接 GL 实现，改为 RHI copy/blit pass。

完成标准：

- `DeferredRenderTargets` 成为 RHI target 描述与生命周期对象。
- FBO 只存在于 OpenGL 后端内部。
- 业务 pass 不再直接绑定 framebuffer。

## 阶段 6：Deferred Pipeline Pass 迁移

目标：主延迟渲染路径走 RHI。

建议顺序：

1. `DeferredLightingPass`
2. `SsaoPass`
3. `SsgiPass`
4. `ReflectionPass`
5. `CloudPass`
6. `VolumetricPass`
7. `VelocityPass`
8. `TemporalResolvePass`
9. `MotionBlurPass`
10. `DepthOfFieldPass`
11. `SceneCompositePass`
12. `WaterCompositePass`
13. `DebugPass`

每个 pass 的迁移任务：

- 建立 graphics/compute pipeline desc。
- 建立 bind group layout。
- 将 texture、sampler、UBO、SSBO、image 绑定迁到 bind group。
- 将字符串 uniform 更新迁到 push constants 或 uniform buffer。
- 将 draw/dispatch/copy 迁到 command list。
- 删除 pass 内直接 GL 状态调用。

完成标准：

- Deferred pipeline 可完整 RHI 渲染。
- TAA、SSAO、SSGI、SSR、cloud、volumetric、motion blur、DOF、debug view 行为正确。
- pass 内不 include `glad/glad.h`。

## 阶段 7：Terrain 与 Mesh Buffer 迁移

目标：迁移高性能地形路径。

任务：

- `WorldRenderBuffer` 使用 RHI buffer。
- chunk vertex/index/indirect/metadata buffer 使用 RHI usage 标记。
- mesh upload 改为 RHI buffer update 或 staging copy。
- MDI 绘制通过 `RhiCommandList::drawIndirect`。
- `TerrainRenderer` 使用 RHI pipeline、bind group、vertex input layout。
- opaque、cutout、transparent、water 分别建 pipeline。

完成标准：

- 地形不再绑定 VAO/VBO。
- MDI 提交统计继续有效。
- 地形、透明方块、水体、区块流式上传行为正确。

## 阶段 8：实体、粒子、天空、覆盖层迁移

目标：迁移非地形 3D 绘制。

任务：

- `HumanoidRenderer`、`DropRenderer`、`FallingBlockRenderer` 迁到 RHI。
- `GameplaySkyRenderer`、`SkyboxRenderer` 迁到 RHI。
- `ParticleSystem`、`RainRenderer` 迁到 RHI dynamic buffer。
- `BlockInteractionOverlayRenderer` 迁到 RHI。
- first-person held item 迁到 RHI。

完成标准：

- 实体、掉落物、粒子、雨雪、天空、方块描边和破坏贴图全部通过 RHI 提交。
- `src/particle` 不再 include `glad/glad.h`。

## 阶段 9：UI 与字体迁移

目标：UI 完全通过 RHI draw list 提交。

任务：

- 建立 `UiDrawList`。
- 建立 `UiRhiRenderer`。
- `TextRenderer` 改为 RHI dynamic vertex buffer + glyph atlas handle。
- 控件只产生命令或 draw item，不直接提交 GL。
- inventory、creative、HUD、modal、toast、console 全部迁移。

完成标准：

- `src/ui` 不再 include `glad/glad.h`。
- UI 图片、文本、裁剪、透明混合、背包物品图标行为正确。

## 阶段 10：Debug、统计与工具迁移

目标：调试能力不绑定 GL。

任务：

- `RenderDebugService` 使用 RHI query pool。
- debug label 通过 command list。
- GPU timer 读回由 RHI backend 提供。
- object label 由 RHI backend 管理。

完成标准：

- GPU frame stats、shadow timing、pass timing 在 OpenGL/RHI 路径可用。
- debug service 不 include `glad/glad.h`。

## 阶段 11：清理业务层 OpenGL 依赖

目标：OpenGL 只存在于 RHI OpenGL 后端和窗口上下文创建的必要边界。

任务：

- 扫描并删除业务层 `gl*` 调用。
- 扫描并删除业务层 `renderer::rhi::gl::textureId` 调用。
- 将 `src/renderer/gl/GlStateGuard` 的有价值逻辑收进 OpenGL 后端内部。
- 将旧 `Shader` 类职责拆分并退出 pass 主路径。
- 删除旧 FBO 绑定 API。

完成标准：

- `src/renderer/passes`、`src/renderer/mesh`、`src/renderer/renderers`、`src/ui`、`src/particle`、`src/resource` 不 include `glad/glad.h`。
- 除 `src/renderer/rhi/gl` 外，业务层不调用 OpenGL API。
- OpenGL/RHI 路径画面与阶段 0 基线一致。

## 阶段 12：Vulkan 后端接入

目标：在已稳定的 RHI 上实现 Vulkan backend。

任务：

- 建立 Vulkan instance、surface、physical device、logical device、queue。
- 建立 Vulkan swapchain。
- 建立 VMA 或项目选定的显存分配层。
- 实现 Vulkan buffer、texture、view、sampler。
- 实现 Vulkan shader module、pipeline、pipeline layout、descriptor set。
- 实现 command pool、command buffer、fence、semaphore、present。
- 实现 resource state tracker 与 barrier 翻译。
- 实现 render target、copy、blit、timestamp。

完成标准：

- 同一套 renderer 代码可选择 OpenGL RHI 或 Vulkan RHI 后端。
- Vulkan 后端通过 RHI 单元测试。
- Vulkan 后端可进入主菜单和游戏世界。
- 核心渲染场景与 OpenGL/RHI 基线一致。

---

## 5. 模块迁移优先级

| 优先级 | 模块 | 原因 |
|--------|------|------|
| P0 | `GlRhiDevice` / `GlRhiCommandList` | 没有真实后端就无法迁移实际渲染 |
| P0 | `RhiDevice` 资源创建 | 资源生命周期必须先归 RHI |
| P0 | swapchain/default framebuffer | 所有最终输出依赖它 |
| P1 | fullscreen/postprocess | 调用形态简单，适合验证 command/pipeline/bind group |
| P1 | render targets | pass 输出必须脱离 FBO API |
| P1 | deferred fullscreen passes | 纹理绑定多，能验证 bind group 模型 |
| P2 | terrain/MDI | 性能路径复杂，需在基础稳定后迁移 |
| P2 | entity/particle/sky/overlay | 依赖 buffer、pipeline、blend/depth 状态 |
| P2 | UI/font | 依赖动态 buffer、裁剪、透明混合 |
| P3 | debug/timer | 依赖 query pool 与后端读回 |
| P3 | Vulkan backend | OpenGL/RHI 稳定后接入 |

---

## 6. 每个切片的完成定义

每个迁移切片必须满足：

1. 相关业务模块不再直接调用 GL。
2. 相关资源由 RHI handle 表达。
3. 相关 draw/dispatch/copy/blit 通过 RHI command list 提交。
4. 相关 shader 绑定通过 bind group 或 push constants。
5. OpenGL 后端真实执行该切片所需 RHI 命令。
6. 构建和测试通过。
7. 静态扫描没有新增 GL 泄漏。
8. 画面或调试输出经过人工验证。

---

## 7. 验证命令

基础验证：

```bash
git diff --check
cmake --build cmake-build-rhi --target mecraft -j 4
cmake --build cmake-build-rhi --target rhi_core_test -j 4
ctest --test-dir cmake-build-rhi -R rhi_core_test --output-on-failure
```

公共头 native API 扫描：

```bash
rg -n "#include <glad|#include \"glad|GLuint|GLenum|GLint|GLsizei|GLboolean|GLchar|GL_|Vk[A-Za-z0-9_]+|vulkan" src --glob '*.h' --glob '*.hpp'
```

业务层 OpenGL 调用扫描：

```bash
rg -n "#include <glad|#include \"glad|\\bgl[A-Z][A-Za-z0-9_]*\\b|renderer::rhi::gl::textureId" src/renderer src/resource src/ui src/particle --glob '*.cpp' --glob '*.h'
```

最终阶段允许命中范围：

```text
src/renderer/rhi/gl/**
```

窗口和平台层可以保留必要的上下文创建代码，不能参与渲染提交。

---

## 8. 视觉验证清单

OpenGL/RHI 每个大阶段至少检查：

- 主菜单显示和交互。
- 新建/进入世界。
- 白天、夜晚、日出/日落。
- 普通地形、透明方块、水体。
- 动态阴影和 CSM 边界。
- SSAO、SSGI、SSR、TAA。
- 雨雪天气和水下视角。
- 背包、创造模式、HUD、文本。
- 掉落物、实体、手持物品。
- debug view 和 GPU timing。

Vulkan 接入后使用同一清单对比 OpenGL/RHI。

---

## 9. 开发纪律

- 不新增业务层 GL 调用。
- 不新增业务层后端专用 include。
- 不新增绕过 RHI 的渲染提交路径。
- 不把 `RhiTextureHandle` 转成 native id 后继续扩散。
- 不用字符串 uniform 作为新 pass 的主绑定方式。
- 不在 RHI 公共接口加入 OpenGL 或 Vulkan 专用参数。
- 不并行构建 `mecraft` 与 `rhi_core_test`，避免静态库同时重建。
- 每个阶段完成后提交独立 commit，提交消息使用 `<type>(<scope>): <subject>`。

---

## 10. 推荐近期切片

### 切片 1：让 OpenGL command list 能绘制 fullscreen triangle

范围：

- GL shader/pipeline/resource/bind group 最小实现。
- RHI fullscreen triangle pipeline。
- 一个最小 fullscreen pass 测试。

验收：

- RHI command list 能清屏并绘制 fullscreen triangle。
- `rhi_core_test` 覆盖 handle 生命周期和最小命令提交。

### 切片 2：迁移 PostProcessPass

范围：

- `PostProcessPass` pipeline + bind group。
- 输入 scene texture、depth texture、noise texture。
- 输出 swapchain 或 scene target。

验收：

- postprocess pass 无直接 GL 调用。
- 主画面 tone mapping 正常。

### 切片 3：迁移 RHI render target binding

范围：

- `DeferredRenderTargets` 通过 RHI texture 描述目标。
- OpenGL backend framebuffer cache。
- 一个 deferred fullscreen pass 使用 `cmd.beginRendering`。

验收：

- 该 pass 不再调用 `targets.bind*()`。
- FBO 管理只在 OpenGL backend 内部出现。

### 切片 4：迁移 DeferredLightingPass

范围：

- GBuffer、shadow、SSAO、sky、noise、LUT、lightmap 绑定进入 bind group。
- camera、sky、weather、shadow 参数进入 uniform buffer 或 push constants。
- draw 通过 RHI command list。

验收：

- `DeferredLightingPass` 不 include `glad/glad.h`。
- 延迟光照画面与基线一致。

---

## 11. Vulkan 接入前硬门槛

Vulkan 后端开工前必须满足：

- OpenGL/RHI 可以完整渲染游戏画面。
- 业务层渲染提交不调用 GL。
- 资源、pipeline、bind group、render target 生命周期全部由 RHI 管理。
- shader 绑定语义稳定，不依赖 GL texture unit 手写顺序。
- render target 依赖和 barrier 语义能被 Render Graph 或显式 RHI barrier 表达。
- debug、timer、截图或画面对比流程可用于跨后端验证。

