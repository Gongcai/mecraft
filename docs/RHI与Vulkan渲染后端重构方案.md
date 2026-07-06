# Mecraft RHI 与 Vulkan 渲染后端重构方案

> 本方案面向 Mecraft 当前 OpenGL 4.5 渲染系统，目标是在保持现有渲染功能可持续演进的前提下，引入 Vulkan 友好的现代游戏引擎 RHI 架构。方案覆盖 RHI 边界、接口模型、资源系统、命令提交、描述符绑定、Render Graph、shader 编译链、OpenGL 后端迁移路径与 Vulkan 后端接入路径。

---

## 目录

1. [总体结论](#1-总体结论)
2. [当前渲染系统问题定位](#2-当前渲染系统问题定位)
3. [设计目标与边界](#3-设计目标与边界)
4. [RHI 总体架构](#4-rhi-总体架构)
5. [核心接口设计](#5-核心接口设计)
6. [资源模型](#6-资源模型)
7. [命令录制与提交模型](#7-命令录制与提交模型)
8. [Descriptor 与 Bind Group 模型](#8-descriptor-与-bind-group-模型)
9. [Pipeline State 模型](#9-pipeline-state-模型)
10. [Render Graph 设计](#10-render-graph-设计)
11. [Shader 编译链设计](#11-shader-编译链设计)
12. [OpenGL 后端适配策略](#12-opengl-后端适配策略)
13. [Vulkan 后端接入策略](#13-vulkan-后端接入策略)
14. [现有模块迁移顺序](#14-现有模块迁移顺序)
15. [测试、验证与性能观测](#15-测试验证与性能观测)
16. [风险与工程约束](#16-风险与工程约束)
17. [阶段性交付标准](#17-阶段性交付标准)

---

## 1. 总体结论

Mecraft 可以采用“RHI 接口先行、OpenGL 后端作为首个实现、Vulkan 后端按同一接口接入”的路线。该路线的关键要求是：**RHI 公共接口从第一版开始就按 Vulkan 的显式资源与命令模型设计，OpenGL 后端只作为该模型的一个实现**。

当前项目已有 `RenderScene`、`RenderPipeline`、`FrameContext`、`FrameOutput`、`ForwardPipeline`、`DeferredPipeline` 等高层渲染组织结构，这些结构能继续保留。但底层图形 API 仍深度绑定 OpenGL，RHI 重构必须切断以下几类耦合：

- 公共头文件中的 `GLuint`、`GLenum`、FBO 概念。
- UI、资源、字体、terrain、postprocess、shadow、deferred pass 中直接调用 `gl*` 的行为。
- shader uniform 通过字符串动态设置的主路径。
- OpenGL 全局状态机式的 `glEnable`、`glBindTexture`、`glUseProgram`、`glBindFramebuffer` 分散调用。
- 窗口层直接创建 OpenGL 4.5 core context 的假设。

RHI 引入后的最终形态：

```text
Game / UI / Resource / Renderer
        │
        ▼
RenderScene / RenderPipeline / RenderGraph
        │
        ▼
RHI 公共接口
        │
        ├── OpenGL Backend
        └── Vulkan Backend
```

RHI 的工程收益主要来自长期架构控制：资源生命周期统一、命令提交可观测、pass 依赖显式化、shader 绑定稳定、多线程渲染可落地、Vulkan 后端接入成本可控。

---

## 2. 当前渲染系统问题定位

### 2.1 API 绑定范围

当前源码中 OpenGL 绑定范围较广：

- `src` 下大量文件包含 `glad/glad.h`。
- 多个公共结构直接暴露 `GLuint`。
- `ResourceMgr` 直接返回 OpenGL texture id。
- `FrameOutput` 暴露 scene/depth/shadow 等 OpenGL texture id。
- `TextureAtlas`、`TextureArray` 直接持有 `GLuint textureID`。
- `CommonFrameTargets`、`DeferredFrameTargets`、`ShadowTargets` 以 FBO/texture id 作为核心状态。
- `Window` 负责创建 OpenGL context 并加载 GLAD。

典型绑定点：

| 模块 | 当前绑定方式 | RHI 化目标 |
|------|--------------|------------|
| `engine/platform/Window` | OpenGL context + GLAD | 平台窗口与图形设备创建解耦 |
| `resource/ResourceMgr` | 返回 `GLuint` | 返回 `RhiTextureHandle` / `RhiShaderHandle` |
| `resource/TextureAtlas` | `GLuint textureID` | `RhiTextureHandle texture` |
| `renderer/core/FrameOutput` | `GLuint sceneColorTex` 等 | RHI texture handle |
| `renderer/targets/*` | FBO + texture id | RHI texture + rendering attachment |
| `renderer/core/Shader` | OpenGL program + uniform setter | RHI shader module + pipeline layout |
| `renderer/mesh/WorldRenderBuffer` | VAO/VBO/MDI buffer | RHI buffer + vertex input + indirect draw |
| `ui/*` | 控件内直接画 GL quad | UI batch + RHI draw |

### 2.2 OpenGL 状态机对架构的限制

当前许多 pass 自行管理 GL 状态：

- depth/cull/blend/scissor 由各 pass 或 widget 局部设置。
- texture unit 绑定依赖调用顺序。
- framebuffer 绑定与 viewport 状态依赖隐式上下文。
- uniform 更新依赖 program 当前绑定状态。
- compute/image 资源访问由手写 barrier 管理。

这些模式在 OpenGL 中可运行，但与 Vulkan 的显式模型冲突。RHI 不能成为 `gl*` 函数名的薄封装，而要表达 draw/dispatch 所需的完整状态与资源关系。

### 2.3 当前渲染特性对 RHI 的要求

Mecraft 当前渲染功能已经具备现代管线复杂度：

- 延迟渲染与 5-MRT GBuffer。
- Forward 与 Deferred 双管线。
- SSAO、SSGI、SSR、TAA、motion blur、DOF、bloom、auto exposure。
- CSM 级联阴影、透明阴影颜色通道、shadow comparison texture。
- 3D LUT、3D voxel GI texture。
- texture array、texture view、SSBO、MDI、compute shader、image load/store。
- GPU timer query、debug group、object label。

因此 RHI 第一版必须覆盖下列能力：

- color/depth attachment 与 load/store。
- texture 2D、2D array、3D。
- sampler 与 texture view。
- uniform buffer、storage buffer、indirect buffer。
- graphics pipeline、compute pipeline。
- bind group / descriptor set。
- draw、draw indexed、draw indirect、multi draw indirect。
- dispatch compute。
- buffer/texture copy。
- texture layout/resource state transition。
- GPU timestamp/debug marker。

---

## 3. 设计目标与边界

### 3.1 设计目标

1. **Vulkan 友好**
   RHI 的核心抽象对齐 Vulkan/D3D12/Metal 的显式模型：不可依赖全局隐式状态，不暴露 OpenGL id，不把 FBO 作为公共概念。

2. **保留现有高层渲染组织**
   `RenderScene`、`RenderPipeline`、`FrameContext`、`ForwardPipeline`、`DeferredPipeline` 的职责继续存在。RHI 主要替换底层资源与提交层。

3. **OpenGL 后端功能对等**
   第一阶段通过 OpenGL 后端承载当前渲染功能，确保重构过程中画面、调试数据、性能指标可验证。

4. **Render Graph 可演进**
   RHI 需要为 deferred pipeline 的 pass 依赖、资源生命周期、barrier、临时纹理复用提供基础。

5. **资源绑定稳定化**
   从字符串 uniform 逐步转向显式 layout：bind group、UBO、SSBO、push constants、static sampler。

6. **支持多线程渲染准备**
   允许未来将 terrain draw list、shadow pass、postprocess pass 的命令录制分摊到 worker 线程。

### 3.2 非目标

第一轮 RHI 重构不追求：

- 重写渲染算法。
- 改变视觉风格。
- 改变 ECS、World、物理、网络架构。
- 一次性把所有 shader 改为完全 Vulkan GLSL。
- 一次性实现 GPU driven rendering。

### 3.3 架构边界

RHI 层负责：

- 图形设备、队列、swapchain。
- buffer、texture、sampler、shader、pipeline。
- render target、attachment、resource state。
- command list、submit、present。
- descriptor/bind group。
- GPU query、debug marker。

RHI 层不负责：

- 世界数据组织。
- chunk mesh 生成算法。
- ECS 渲染数据采集。
- 高层 pass 调度策略。
- 材质语义与 shaderpack 语义。
- UI 布局与控件状态。

---

## 4. RHI 总体架构

### 4.1 目录规划

建议新增目录：

```text
src/renderer/rhi/
    RhiTypes.h
    RhiHandles.h
    RhiDevice.h
    RhiSwapchain.h
    RhiCommandList.h
    RhiResources.h
    RhiPipeline.h
    RhiDescriptor.h
    RhiRenderGraph.h
    RhiDebug.h

src/renderer/rhi/gl/
    GlRhiDevice.h/.cpp
    GlRhiCommandList.h/.cpp
    GlRhiResources.h/.cpp
    GlRhiPipeline.h/.cpp
    GlRhiSwapchain.h/.cpp

src/renderer/rhi/vulkan/
    VkRhiDevice.h/.cpp
    VkRhiCommandList.h/.cpp
    VkRhiResources.h/.cpp
    VkRhiPipeline.h/.cpp
    VkRhiSwapchain.h/.cpp
```

后续可以把 `src/renderer/gl/GlStateGuard.h` 中仍有价值的状态缓存逻辑移动到 `rhi/gl/` 内部，不再被业务层 include。

### 4.2 层级关系

```text
RenderScene
    ├── RenderGraph
    │     ├── Pass Builder
    │     ├── Resource Registry
    │     └── Barrier Planner
    │
    ├── ForwardPipeline
    ├── DeferredPipeline
    └── PostProcessPass

RHI
    ├── RhiDevice
    ├── RhiCommandList
    ├── RhiSwapchain
    ├── RhiResourceRegistry
    ├── RhiPipelineCache
    └── Backend
          ├── OpenGL
          └── Vulkan
```

### 4.3 设计原则

RHI 采用“对象 + 描述结构 + 命令列表”的模型：

- 资源通过 desc 创建。
- pipeline 通过完整 state desc 创建。
- draw/dispatch 通过 command list 录制。
- shader 资源通过 bind group 绑定。
- pass 输出通过 rendering info 或 render graph attachment 描述。
- resource state transition 明确进入 RHI 模型。

禁止在 RHI 公共接口出现：

- `GLuint`
- `GLenum`
- `VkImage`
- `VkBuffer`
- `VkCommandBuffer`
- FBO id
- texture unit
- OpenGL uniform location
- API 后端专用 include

---

## 5. 核心接口设计

### 5.1 Handle 设计

公共层使用强类型 handle：

```cpp
struct RhiTextureHandle {
    uint32_t index = 0;
    uint32_t generation = 0;
};

struct RhiBufferHandle {
    uint32_t index = 0;
    uint32_t generation = 0;
};

struct RhiSamplerHandle {
    uint32_t index = 0;
    uint32_t generation = 0;
};

struct RhiShaderHandle {
    uint32_t index = 0;
    uint32_t generation = 0;
};

struct RhiPipelineHandle {
    uint32_t index = 0;
    uint32_t generation = 0;
};

struct RhiBindGroupHandle {
    uint32_t index = 0;
    uint32_t generation = 0;
};
```

Handle 只表达身份，不包含后端对象。后端内部维护 handle 到 API 原生对象的映射：

| RHI handle | OpenGL 后端对象 | Vulkan 后端对象 |
|------------|-----------------|-----------------|
| `RhiTextureHandle` | texture id + metadata | `VkImage` + `VkImageView` + allocation |
| `RhiBufferHandle` | buffer id + target hints | `VkBuffer` + allocation |
| `RhiSamplerHandle` | sampler id | `VkSampler` |
| `RhiShaderHandle` | shader source/program data | `VkShaderModule` |
| `RhiPipelineHandle` | program + cached GL state | `VkPipeline` + `VkPipelineLayout` |
| `RhiBindGroupHandle` | binding table snapshot | `VkDescriptorSet` |

### 5.2 RhiDevice

`RhiDevice` 是资源创建、队列提交、帧生命周期的入口。

```cpp
class RhiDevice {
public:
    virtual ~RhiDevice() = default;

    virtual bool init(const RhiDeviceDesc& desc) = 0;
    virtual void shutdown() = 0;

    virtual RhiBackend backend() const = 0;
    virtual const RhiCapabilities& capabilities() const = 0;

    virtual RhiBufferHandle createBuffer(const RhiBufferDesc& desc,
                                         const void* initialData,
                                         size_t initialDataSize) = 0;
    virtual RhiTextureHandle createTexture(const RhiTextureDesc& desc,
                                           const RhiTextureInitialData* initialData) = 0;
    virtual RhiSamplerHandle createSampler(const RhiSamplerDesc& desc) = 0;
    virtual RhiShaderHandle createShader(const RhiShaderDesc& desc) = 0;
    virtual RhiPipelineHandle createGraphicsPipeline(const RhiGraphicsPipelineDesc& desc) = 0;
    virtual RhiPipelineHandle createComputePipeline(const RhiComputePipelineDesc& desc) = 0;
    virtual RhiBindGroupHandle createBindGroup(const RhiBindGroupDesc& desc) = 0;

    virtual void destroyBuffer(RhiBufferHandle handle) = 0;
    virtual void destroyTexture(RhiTextureHandle handle) = 0;
    virtual void destroySampler(RhiSamplerHandle handle) = 0;
    virtual void destroyShader(RhiShaderHandle handle) = 0;
    virtual void destroyPipeline(RhiPipelineHandle handle) = 0;
    virtual void destroyBindGroup(RhiBindGroupHandle handle) = 0;

    virtual RhiCommandList& beginFrame() = 0;
    virtual void submitFrame(RhiCommandList& commandList) = 0;
    virtual void present() = 0;
};
```

### 5.3 RhiCommandList

Command list 是所有 draw/dispatch/copy/barrier 的载体。

```cpp
class RhiCommandList {
public:
    virtual ~RhiCommandList() = default;

    virtual void beginDebugLabel(const char* name, const glm::vec4& color) = 0;
    virtual void endDebugLabel() = 0;

    virtual void textureBarrier(const RhiTextureBarrier& barrier) = 0;
    virtual void bufferBarrier(const RhiBufferBarrier& barrier) = 0;

    virtual void beginRendering(const RhiRenderingInfo& info) = 0;
    virtual void endRendering() = 0;

    virtual void setViewport(const RhiViewport& viewport) = 0;
    virtual void setScissor(const RhiRect2D& rect) = 0;
    virtual void setGraphicsPipeline(RhiPipelineHandle pipeline) = 0;
    virtual void setComputePipeline(RhiPipelineHandle pipeline) = 0;
    virtual void setBindGroup(uint32_t setIndex, RhiBindGroupHandle bindGroup) = 0;
    virtual void setVertexBuffer(uint32_t slot, RhiBufferHandle buffer, uint64_t offset) = 0;
    virtual void setIndexBuffer(RhiBufferHandle buffer, RhiIndexFormat format, uint64_t offset) = 0;
    virtual void pushConstants(const void* data, size_t size, RhiShaderStage stages) = 0;

    virtual void draw(uint32_t vertexCount, uint32_t instanceCount,
                      uint32_t firstVertex, uint32_t firstInstance) = 0;
    virtual void drawIndexed(uint32_t indexCount, uint32_t instanceCount,
                             uint32_t firstIndex, int32_t vertexOffset,
                             uint32_t firstInstance) = 0;
    virtual void drawIndirect(RhiBufferHandle indirectBuffer, uint64_t offset,
                              uint32_t drawCount, uint32_t stride) = 0;
    virtual void dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) = 0;

    virtual void copyBuffer(const RhiBufferCopy& copy) = 0;
    virtual void copyBufferToTexture(const RhiBufferTextureCopy& copy) = 0;
    virtual void copyTexture(const RhiTextureCopy& copy) = 0;
    virtual void blitTexture(const RhiTextureBlit& blit) = 0;

    virtual void writeTimestamp(RhiQueryPoolHandle pool, uint32_t queryIndex) = 0;
};
```

### 5.4 RhiSwapchain

Swapchain 与窗口系统分离。

```cpp
class RhiSwapchain {
public:
    virtual ~RhiSwapchain() = default;

    virtual bool create(const RhiSwapchainDesc& desc) = 0;
    virtual void destroy() = 0;
    virtual bool resize(int width, int height) = 0;
    virtual RhiTextureHandle currentColorTexture() const = 0;
    virtual RhiTextureFormat colorFormat() const = 0;
    virtual int width() const = 0;
    virtual int height() const = 0;
};
```

OpenGL 后端中 swapchain color 可以映射到 backbuffer 语义。Vulkan 后端中对应 `VkSwapchainKHR` 与当前 acquire image。

---

## 6. 资源模型

### 6.1 Texture

Texture 描述必须覆盖当前项目需要的 2D、2D array、3D、depth、mip、view、sample count。

```cpp
enum class RhiTextureDimension {
    Texture2D,
    Texture2DArray,
    Texture3D,
    Cube
};

enum class RhiTextureUsage : uint32_t {
    Sampled = 1 << 0,
    Storage = 1 << 1,
    ColorAttachment = 1 << 2,
    DepthStencilAttachment = 1 << 3,
    TransferSrc = 1 << 4,
    TransferDst = 1 << 5,
    Present = 1 << 6
};

struct RhiTextureDesc {
    const char* debugName = nullptr;
    RhiTextureDimension dimension = RhiTextureDimension::Texture2D;
    RhiTextureFormat format = RhiTextureFormat::Rgba8Unorm;
    uint32_t width = 1;
    uint32_t height = 1;
    uint32_t depthOrLayers = 1;
    uint32_t mipLevels = 1;
    uint32_t sampleCount = 1;
    RhiTextureUsageFlags usage = {};
};
```

当前映射：

| 当前资源 | RHI texture desc |
|----------|------------------|
| block texture array | `Texture2DArray + Sampled + TransferDst` |
| GBuffer albedo/normal/material | `Texture2D + ColorAttachment + Sampled + TransferSrc` |
| depth | `Texture2D + DepthStencilAttachment + Sampled + TransferSrc` |
| shadow cascades | `Texture2DArray + DepthStencilAttachment + Sampled` |
| atmosphere LUT | `Texture3D + Sampled + TransferDst` |
| voxel GI clipmap | `Texture3D + Sampled + Storage + TransferDst + TransferSrc` |
| postprocess bloom chain | `Texture2D + ColorAttachment + Sampled + TransferSrc + TransferDst` |

### 6.2 Texture View

Vulkan 中 `VkImageView` 是 descriptor 与 attachment 的实际绑定对象。OpenGL 当前使用 `glTextureView` 处理 shadow compare/raw depth。RHI 需要显式 texture view：

```cpp
struct RhiTextureViewDesc {
    RhiTextureHandle texture;
    RhiTextureViewType viewType = RhiTextureViewType::Texture2D;
    RhiTextureFormat format = RhiTextureFormat::Undefined;
    uint32_t baseMip = 0;
    uint32_t mipCount = 1;
    uint32_t baseLayer = 0;
    uint32_t layerCount = 1;
    bool depthCompare = false;
};
```

`depthCompare` 用于表达 shadow comparison sampling。OpenGL 后端内部创建 compare sampler 或 texture view，Vulkan 后端使用 sampler compare enable 与 depth image view。

### 6.3 Buffer

Buffer usage 要覆盖顶点、索引、uniform、storage、indirect、staging。

```cpp
enum class RhiBufferUsage : uint32_t {
    Vertex = 1 << 0,
    Index = 1 << 1,
    Uniform = 1 << 2,
    Storage = 1 << 3,
    Indirect = 1 << 4,
    TransferSrc = 1 << 5,
    TransferDst = 1 << 6,
    MapRead = 1 << 7,
    MapWrite = 1 << 8
};

struct RhiBufferDesc {
    const char* debugName = nullptr;
    uint64_t size = 0;
    RhiBufferUsageFlags usage = {};
    RhiMemoryUsage memoryUsage = RhiMemoryUsage::GpuOnly;
};
```

当前映射：

| 当前对象 | RHI buffer |
|----------|------------|
| `WorldRenderBuffer` vertex pool | `Vertex + TransferDst` |
| MDI command buffer | `Indirect + TransferDst` |
| subchunk metadata | `Storage + TransferDst` |
| postprocess params | `Uniform + TransferDst` |
| entity instance data | `Vertex + TransferDst` 或 `Storage + TransferDst` |
| readback/timing staging | `MapRead + TransferDst` |

### 6.4 Sampler

Sampler 必须独立于 texture：

```cpp
struct RhiSamplerDesc {
    RhiFilter minFilter = RhiFilter::Linear;
    RhiFilter magFilter = RhiFilter::Linear;
    RhiMipmapMode mipmapMode = RhiMipmapMode::Linear;
    RhiAddressMode addressU = RhiAddressMode::ClampToEdge;
    RhiAddressMode addressV = RhiAddressMode::ClampToEdge;
    RhiAddressMode addressW = RhiAddressMode::ClampToEdge;
    float maxAnisotropy = 1.0f;
    bool compareEnabled = false;
    RhiCompareOp compareOp = RhiCompareOp::LessOrEqual;
};
```

这会替代当前分散在纹理创建点中的 `glTexParameteri`。

---

## 7. 命令录制与提交模型

### 7.1 帧生命周期

建议帧级流程：

```text
RhiDevice::beginFrame()
    ├── RenderScene::buildFrameContext()
    ├── RenderGraph::compile()
    ├── RenderGraph::execute(commandList)
    ├── UI submit
    └── postprocess / present pass
RhiDevice::submitFrame()
RhiDevice::present()
```

OpenGL 后端中 command list 可以是即时执行对象，也可以记录轻量命令数组。第一阶段为了降低迁移量，可以使用即时执行；接口仍保持 command list 形式。

### 7.2 Command List 粒度

建议支持三种粒度：

| 粒度 | 用途 | 第一阶段状态 |
|------|------|--------------|
| frame command list | 主线程完整帧 | 必须实现 |
| pass command list | 每个 pass 局部录制 | 可直接映射到 frame command list |
| secondary command list | 多线程录制 | Vulkan 后端接入后启用 |

### 7.3 同步与资源状态

RHI 中定义资源状态：

```cpp
enum class RhiResourceState {
    Undefined,
    Present,
    RenderTarget,
    DepthWrite,
    DepthRead,
    ShaderRead,
    ShaderWrite,
    TransferSrc,
    TransferDst,
    VertexBuffer,
    IndexBuffer,
    IndirectArgument,
    UniformBuffer,
    StorageBuffer
};
```

OpenGL 后端可在 `textureBarrier` / `bufferBarrier` 中执行对应 memory barrier 或记录状态。Vulkan 后端将其翻译为 image layout、access mask、pipeline stage。

---

## 8. Descriptor 与 Bind Group 模型

### 8.1 设计目标

Vulkan 后端最重要的工程约束是 descriptor layout 固定。RHI 要求 shader 资源绑定从“调用时指定 texture unit + uniform name”转为“pipeline layout + bind group”。

### 8.2 Bind Group Layout

```cpp
enum class RhiBindingType {
    UniformBuffer,
    StorageBuffer,
    SampledTexture,
    StorageTexture,
    Sampler,
    CombinedTextureSampler
};

struct RhiBindGroupLayoutEntry {
    uint32_t binding = 0;
    RhiBindingType type = RhiBindingType::UniformBuffer;
    RhiShaderStageFlags stages = {};
    uint32_t arrayCount = 1;
};

struct RhiBindGroupLayoutDesc {
    const char* debugName = nullptr;
    std::vector<RhiBindGroupLayoutEntry> entries;
};
```

### 8.3 Bind Group

```cpp
struct RhiBindGroupEntry {
    uint32_t binding = 0;
    RhiBindingResource resource;
};

struct RhiBindGroupDesc {
    RhiBindGroupLayoutHandle layout;
    std::vector<RhiBindGroupEntry> entries;
};
```

### 8.4 推荐绑定分组

建议统一约定：

| Set | 名称 | 内容 |
|-----|------|------|
| set 0 | Frame | 相机、时间、天气、雾、大气、调试开关 |
| set 1 | Scene | GBuffer、shadow、atmosphere LUT、voxel GI、lightmap |
| set 2 | Material | block texture array、normal/specular array、entity texture、sampler |
| set 3 | Object | model matrix、instance buffer、subchunk metadata、skin params |
| set 4 | Pass | pass 局部参数、history texture、temporary target |

这套分组适合 Vulkan descriptor set，也可以映射到 OpenGL texture unit、UBO binding、SSBO binding。

### 8.5 Uniform 迁移策略

现有 `Shader::setFloat/setVec3/setMat4` 适合 OpenGL 快速开发，但不适合作为 RHI 主路径。迁移目标：

- 高频帧参数进入 `FrameUniforms` UBO。
- pass 参数进入 pass UBO 或 push constants。
- terrain metadata 保持 SSBO。
- texture/sampler 通过 bind group 设置。
- 字符串 uniform setter 仅用于迁移期内部适配，不进入新业务代码。

---

## 9. Pipeline State 模型

### 9.1 Graphics Pipeline

RHI graphics pipeline desc 要完整描述固定状态：

```cpp
struct RhiGraphicsPipelineDesc {
    const char* debugName = nullptr;
    RhiShaderHandle vertexShader;
    RhiShaderHandle fragmentShader;
    RhiPipelineLayoutHandle layout;
    RhiVertexInputLayout vertexInput;
    RhiPrimitiveTopology topology = RhiPrimitiveTopology::TriangleList;
    RhiRasterState raster;
    RhiDepthStencilState depthStencil;
    RhiBlendState blend;
    std::vector<RhiTextureFormat> colorFormats;
    RhiTextureFormat depthFormat = RhiTextureFormat::Undefined;
};
```

当前散落在 pass 中的 GL 状态需要进入这些结构：

- `glEnable(GL_DEPTH_TEST)` → `RhiDepthStencilState`
- `glDepthMask` → `depthWriteEnabled`
- `glDepthFunc` → `depthCompare`
- `glEnable(GL_BLEND)` / `glBlendFunc` → `RhiBlendState`
- `glEnable(GL_CULL_FACE)` / `glCullFace` → `RhiRasterState`
- `glVertexAttribPointer` → `RhiVertexInputLayout`

### 9.2 Compute Pipeline

```cpp
struct RhiComputePipelineDesc {
    const char* debugName = nullptr;
    RhiShaderHandle computeShader;
    RhiPipelineLayoutHandle layout;
};
```

当前 compute/image 用法需要迁移到：

- compute shader module。
- storage texture binding。
- explicit resource barrier。
- dispatch command。

### 9.3 Pipeline Cache

RHI 层应提供 pipeline cache：

- OpenGL 后端缓存 program + fixed state hash。
- Vulkan 后端缓存 `VkPipeline` + `VkPipelineLayout`。
- pipeline key 由 shader、render target format、vertex layout、blend/depth/raster state 组成。

---

## 10. Render Graph 设计

### 10.1 引入动机

当前 deferred pipeline pass 较多，资源读写关系由代码顺序隐式表达。Vulkan 下必须明确 attachment、sampled texture、storage texture、copy、barrier。Render Graph 用于集中表达：

- pass 输入输出。
- attachment load/store。
- resource state transition。
- 临时纹理生命周期。
- history texture ping-pong。
- pass debug scope。

### 10.2 基本接口

```cpp
class RenderGraph {
public:
    RgTextureHandle importTexture(const char* name, RhiTextureHandle texture);
    RgTextureHandle createTexture(const char* name, const RhiTextureDesc& desc);

    RenderGraphPass& addPass(const char* name, RgPassType type);

    void compile(RhiDevice& device);
    void execute(RhiCommandList& cmd);
};
```

Pass builder 示例：

```cpp
graph.addPass("GBuffer", RgPassType::Graphics)
    .writeColor(gAlbedo, RhiLoadOp::Clear)
    .writeColor(gNormalAo, RhiLoadOp::Clear)
    .writeDepth(gDepth, RhiLoadOp::Clear)
    .setExecute([&](RhiCommandList& cmd) {
        terrainRenderer.renderGBuffer(cmd, ctx);
    });
```

### 10.3 Deferred Pipeline 的 Graph 化目标

推荐按阶段把当前 pass 转成 Render Graph：

| Graph pass | 输入 | 输出 |
|------------|------|------|
| SkyCapture | atmosphere LUT、sky params | sky capture texture |
| GBuffer | terrain/entity resources | gbuffer textures、depth |
| Shadow | scene draw data | shadow depth/color |
| SSAO | depth、normal、history | ssao current/history |
| DeferredLighting | GBuffer、shadow、SSAO | scene lighting |
| Reflection | scene/depth/normal/history | reflection |
| Cloud | sky/weather/history | cloud |
| SceneComposite | lighting/reflection/cloud/volumetric | scene composite |
| WaterComposite | scene/depth/water vertices | transparent composite |
| TemporalResolve | scene/history/velocity/depth | scene resolved |
| PostProcess | scene resolved/depth/weather | backbuffer |

### 10.4 History Resource 处理

TAA、SSAO、SSR、volumetric 等历史资源不应作为临时纹理销毁，应由 `RenderScene` 或 `FrameHistoryResources` 持有，并以 imported texture 的方式进入 graph。

```text
FrameHistoryResources
    ├── taaHistory[2]
    ├── depthHistory[2]
    ├── ssaoHistory[2]
    ├── reflectionHistory[2]
    ├── cloudHistory[2]
    └── volumetricHistory[2]
```

---

## 11. Shader 编译链设计

### 11.1 当前状态

当前 `Shader` 类负责：

- 读取 GLSL 文件。
- 展开 `#include`。
- OpenGL 编译与链接。
- 字符串 uniform location 缓存。
- compute shader 编译。

这套机制需要拆分为：

```text
ShaderSourceLoader
    ├── 文件读取
    ├── include 展开
    └── 宏定义注入

ShaderCompiler
    ├── OpenGL GLSL 路径
    └── Vulkan SPIR-V 路径

RhiShader
    ├── shader stage
    ├── bytecode/source
    └── reflection metadata
```

### 11.2 Vulkan GLSL 约束

Vulkan shader 需要稳定 layout：

- `layout(set = X, binding = Y)`。
- UBO/SSBO 使用 `std140` 或 `std430`。
- sampler 与 texture binding 明确。
- clip/depth 坐标差异需要统一处理。
- `gl_BaseInstanceARB` 等 OpenGL extension 语义需要替换为 Vulkan 可用形式。

### 11.3 Shader 变体

建议引入 shader variant key：

```cpp
struct ShaderVariantKey {
    std::string shaderName;
    RhiBackend backend;
    std::vector<std::string> defines;
};
```

OpenGL 后端输出 GLSL source 或 program。Vulkan 后端输出 SPIR-V bytecode。编译结果写入构建目录缓存，避免运行时重复编译。

### 11.4 Shader Reflection

Vulkan 后端需要知道 descriptor layout。建议引入反射数据：

```cpp
struct RhiShaderReflection {
    std::vector<RhiShaderBindingInfo> bindings;
    std::vector<RhiPushConstantRange> pushConstants;
    std::vector<RhiVertexInputInfo> vertexInputs;
    std::vector<RhiFragmentOutputInfo> fragmentOutputs;
};
```

第一版可手写 pipeline layout，之后接入 SPIR-V reflection。

---

## 12. OpenGL 后端适配策略

### 12.1 后端职责

OpenGL 后端负责把 Vulkan 友好的 RHI 模型翻译成 OpenGL 4.5 行为：

- `RhiTextureHandle` → `GLuint texture`。
- `RhiBufferHandle` → `GLuint buffer`。
- `RhiPipelineHandle` → `GLuint program` + fixed state snapshot。
- `RhiBindGroupHandle` → texture/sampler/UBO/SSBO binding table。
- `beginRendering` → framebuffer object bind + draw buffers + clear。
- `textureBarrier` / `bufferBarrier` → `glMemoryBarrier` 或状态记录。
- `drawIndirect` → `glMultiDrawArraysIndirect`。
- `dispatch` → `glDispatchCompute`。

### 12.2 OpenGL 后端内部状态缓存

为减少 GL 调用，OpenGL 后端内部维护状态缓存：

```text
GlStateCache
    ├── currentProgram
    ├── currentFramebuffer
    ├── currentViewport
    ├── currentScissor
    ├── currentVertexArray
    ├── boundTextures[]
    ├── boundSamplers[]
    ├── boundUniformBuffers[]
    ├── boundStorageBuffers[]
    ├── blendState
    ├── depthState
    └── rasterState
```

该缓存只存在于 OpenGL 后端内部，业务代码不得访问。

### 12.3 Framebuffer 抽象

RHI 公共层不暴露 FBO。OpenGL 后端在 `beginRendering` 中根据 attachment 组合查找或创建 FBO：

```text
Attachment key:
    color texture views + depth texture view + mip/layer

GlFramebufferCache:
    key -> GLuint framebuffer
```

Vulkan 后端可使用 dynamic rendering 或 render pass/framebuffer cache。公共接口保持一致。

### 12.4 UI 迁移方式

UI 控件不应继续直接创建 VAO/VBO 与调用 `glDrawArrays`。建议新增：

```text
UiRhiRenderer
    ├── dynamic vertex buffer
    ├── dynamic index buffer
    ├── solid color pipeline
    ├── texture pipeline
    ├── nine-slice pipeline
    └── text pipeline
```

控件提交 draw item：

```text
UIWidget::render()
    └── UIRenderContext::drawList
            ├── addRect
            ├── addTexturedQuad
            ├── addNineSlice
            └── addText
```

`UiRhiRenderer` 统一把 draw list 转换为 RHI draw。这样可以一次性减少 UI 层大量 `gl*` 绑定。

---

## 13. Vulkan 后端接入策略

### 13.1 Vulkan 后端模块

Vulkan 后端建议按以下对象拆分：

```text
VkRhiDevice
    ├── instance
    ├── physical device
    ├── logical device
    ├── queues
    ├── allocator
    ├── descriptor pool/cache
    ├── pipeline cache
    ├── command pools
    └── deletion queue

VkRhiSwapchain
    ├── VkSurfaceKHR
    ├── VkSwapchainKHR
    ├── swapchain images
    ├── image views
    └── acquire/present sync

VkRhiCommandList
    ├── VkCommandBuffer
    ├── current rendering state
    └── pending barriers
```

### 13.2 依赖与构建

需要新增：

- `find_package(Vulkan REQUIRED)`。
- vcpkg manifest 中加入 Vulkan loader 相关依赖。
- shader 编译工具链：`glslangValidator` 或 shaderc。
- ImGui Vulkan backend 编译项。

构建选项建议：

```cmake
option(MECRAFT_RHI_BACKEND_OPENGL "Build OpenGL RHI backend" ON)
option(MECRAFT_RHI_BACKEND_VULKAN "Build Vulkan RHI backend" OFF)
set(MECRAFT_DEFAULT_RHI_BACKEND "OpenGL" CACHE STRING "Default RHI backend")
```

### 13.3 设备能力

Vulkan 后端初始化时需要检查：

- swapchain support。
- timestamp query。
- descriptor indexing 可选能力。
- multi draw indirect。
- sampler anisotropy。
- depth format。
- storage image format。
- timeline semaphore 可选能力。

`RhiCapabilities` 向上层暴露能力：

```cpp
struct RhiCapabilities {
    bool multiDrawIndirect = false;
    bool timestampQuery = false;
    bool textureView = false;
    bool samplerAnisotropy = false;
    bool storageImage = false;
    bool descriptorIndexing = false;
    uint32_t maxColorAttachments = 0;
    uint32_t maxSampledTexturesPerStage = 0;
};
```

### 13.4 内存管理

Vulkan 需要明确 GPU 内存分配。建议引入 `Vma` 或自研简化 allocator。考虑工程效率，推荐使用 Vulkan Memory Allocator。

内存类型：

| RHI memory usage | Vulkan 语义 |
|------------------|-------------|
| `GpuOnly` | device local |
| `CpuToGpu` | host visible upload |
| `GpuToCpu` | host visible readback |

### 13.5 同步模型

Vulkan 后端至少需要：

- per-frame fence。
- image available semaphore。
- render finished semaphore。
- command pool per frame。
- deletion queue 按 frame 延迟释放。
- resource state tracker。

Render Graph 编译阶段生成 barrier，Vulkan 后端负责翻译为 `vkCmdPipelineBarrier2` 或对应同步 API。

---

## 14. 现有模块迁移顺序

### 阶段 0：准备与边界收敛

目标：新增 RHI 目录、基础类型、构建开关，不改变渲染行为。

任务：

- 新增 `renderer/rhi` 公共类型。
- 新增 OpenGL 后端骨架。
- 增加 `RhiDevice` 生命周期到 `GameManager` 或 `GameplayRenderRuntime`。
- 建立 RHI debug name 与日志机制。
- 编写最小测试：创建 buffer/texture/sampler/pipeline handle。

完成标准：

- 项目仍以 OpenGL 渲染。
- RHI 单元测试可运行。
- 公共 RHI 头文件不 include GL/Vulkan。

### 阶段 1：去除公共数据结构中的 `GLuint`

目标：让资源 ID 不再穿透核心接口。

任务：

- `TextureAtlas::textureID` → `RhiTextureHandle texture`。
- `TextureArray::textureID` → `RhiTextureHandle texture`。
- `FrameOutput` 中 texture id 改为 RHI handle。
- `FirstPersonShadowData` 中 shadow texture id 改为 RHI handle。
- `UIRenderContext::backdropBlurTexture` 改为 RHI handle。

完成标准：

- 业务层不直接读取 texture id。
- OpenGL 后端提供内部 query 或 bridge 供未迁移代码临时使用，并限定在 `rhi/gl` 内部使用。

### 阶段 2：资源管理器 RHI 化

目标：`ResourceMgr` 通过 RHI 创建设备资源。

任务：

- 纹理加载改为 `RhiDevice::createTexture`。
- sampler 参数进入 `RhiSamplerDesc`。
- `TextureSamplerController` 改为修改 sampler 或重建 sampler。
- shader 加载拆成 source loader 与 RHI shader 创建。
- 保留当前 GLSL include 展开逻辑，移入 `ShaderSourceLoader`。

完成标准：

- 纹理、texture array、cubemap、lightmap、colormap 均由 RHI 创建。
- `ResourceMgr` 公共接口不返回 OpenGL 原生 id。

### 阶段 3：UI 与字体迁移

目标：用统一 UI batch 替换控件内 GL 绘制。

任务：

- 新增 `UiDrawList`。
- 新增 `UiRhiRenderer`。
- 迁移 `TextRenderer` / `GlyphAtlas` 到 RHI texture + dynamic buffer。
- 迁移 `UIPanel`、`UIImage`、`UIText`、`UIButton`、HUD 控件。
- 库存面板等复杂控件通过 draw list 提交 quad。

完成标准：

- UI 模块不 include `glad/glad.h`。
- UI 绘制只通过 RHI command list。
- 文本、贴图、九宫格、裁剪矩形可正常显示。

### 阶段 4：PostProcess 与 fullscreen pass 迁移

目标：完成最稳定的一类渲染 pass 的 RHI 化。

任务：

- 迁移 fullscreen triangle。
- 迁移 `PostProcessPass`。
- 迁移 bloom chain、auto exposure chain。
- 迁移 `Fsr1Pass`。
- 统一 fullscreen pipeline。

完成标准：

- 后处理 target 使用 RHI texture。
- backbuffer 输出通过 RHI swapchain/current target。
- bloom/exposure/postprocess 的 pass 依赖可通过 Render Graph 表达。

### 阶段 5：Render Targets 迁移

目标：替换 FBO 管理类。

任务：

- `CommonFrameTargets` 改为 RHI texture 集合。
- `DeferredFrameTargets` 改为 RHI texture 集合。
- `ShadowTargets` 改为 RHI texture 集合。
- FBO 创建逻辑移动到 OpenGL 后端 framebuffer cache。
- attachment load/store 由 `RhiRenderingInfo` 表达。

完成标准：

- targets 公共头文件不 include GL。
- pass 通过 `cmd.beginRendering` 绑定输出。

### 阶段 6：Terrain 与 mesh buffer 迁移

目标：迁移高性能路径。

任务：

- `WorldRenderBuffer` 使用 RHI buffer。
- MDI command buffer 使用 `RhiBufferUsage::Indirect`。
- subchunk metadata 使用 storage buffer。
- terrain VAO layout 改为 `RhiVertexInputLayout`。
- `TerrainRenderer` 使用 RHI pipeline + bind group。

完成标准：

- 不透明、cutout、transparent、water draw 均走 RHI。
- MDI 绘制行为保持一致。
- chunk mesh 上传统计继续可观测。

### 阶段 7：Deferred/Shadow/Entity 渲染迁移

目标：主渲染管线脱离 OpenGL。

任务：

- 迁移 `GBufferPass`。
- 迁移 `ShadowPass` 与 `ShadowRenderer`。
- 迁移 `DeferredLightingPass`、`SsaoPass`、`SsgiPass`、`ReflectionPass`。
- 迁移 `GameplaySkyRenderer`、`HumanoidRenderer`、`DropRenderer`、`FallingBlockRenderer`、`FirstPersonHeldItemRenderer`。
- 迁移 `ParticleSystem`、`RainRenderer`。

完成标准：

- gameplay scene 的所有 draw/dispatch 都通过 RHI。
- `src/renderer` 中 OpenGL include 仅存在于 `rhi/gl`。

### 阶段 8：Render Graph 接管主流程

目标：deferred pipeline 的 pass 依赖从手写顺序转为 graph。

任务：

- 把 `DeferredPipeline::renderFrame` 拆成 graph pass。
- history resource 以 imported texture 进入 graph。
- graph compile 生成 attachment 与 barrier plan。
- debug service 从 graph 读取 pass 名称与 GPU timing。

完成标准：

- 每个 pass 的读写资源可打印。
- 临时纹理生命周期由 graph 管理。
- OpenGL 与 Vulkan 后端共享同一 graph。

### 阶段 9：Vulkan 后端接入

目标：实现 Vulkan 后端并逐步达到 OpenGL 后端功能对等。

任务：

- Vulkan instance/device/surface/swapchain。
- command buffer、frame fence、semaphore。
- buffer/texture/sampler。
- shader SPIR-V 编译与加载。
- descriptor set 与 pipeline layout。
- graphics/compute pipeline。
- dynamic rendering 或 render pass。
- Render Graph barrier 翻译。
- ImGui Vulkan backend。

完成标准：

- Vulkan 后端可运行主菜单。
- Vulkan 后端可运行 gameplay forward path。
- Vulkan 后端可运行 deferred path。
- 截图、GPU timing、debug label 可用。

---

## 15. 测试、验证与性能观测

### 15.1 单元测试

新增测试建议：

- handle generation 与销毁复用测试。
- RHI desc hash 测试。
- pipeline key 稳定性测试。
- Render Graph resource lifetime 测试。
- bind group layout 匹配测试。
- shader include 展开测试。

### 15.2 渲染验证

每个迁移阶段至少验证：

- 主菜单渲染。
- 加载界面渲染。
- gameplay 基础地形。
- inventory UI。
- block breaking overlay。
- first person held item。
- rain/snow/weather。
- deferred effects 开关。
- debug dashboard。

### 15.3 图像对比

建议建立固定场景截图：

| 场景 | 覆盖内容 |
|------|----------|
| 白天平原 | terrain、sky、fog、postprocess |
| 夜晚火把 | voxel light、shadow、bloom |
| 雨天水面 | weather、water、volumetric |
| 大量实体 | entity renderer、shadow、GBuffer |
| 库存界面 | UI、text、item atlas |
| 红石复杂场景 | block entity、cutout、overlay |

### 15.4 性能观测

保留并扩展现有 `RenderDebugService`：

- CPU command recording time。
- RHI submit time。
- GPU pass timestamp。
- resource upload bytes。
- descriptor update count。
- pipeline bind count。
- draw call count。
- indirect draw count。
- render graph temporary texture bytes。

---

## 16. 风险与工程约束

### 16.1 主要风险

| 风险 | 影响 | 控制方式 |
|------|------|----------|
| RHI 被设计成 OpenGL 包装层 | Vulkan 接入成本失控 | RHI 接口禁止 texture unit、FBO、uniform location |
| 公共接口继续暴露 `GLuint` | 后端隔离失败 | 阶段 1 集中替换 |
| shader binding 不稳定 | descriptor layout 难以维护 | 统一 set/binding 约定 |
| pass 资源读写关系隐式 | Vulkan barrier 难以正确 | Render Graph 接管 pass 依赖 |
| UI 分散 GL 绘制 | 迁移面积扩大 | UI batch 化 |
| Vulkan 同步错误 | 随机闪烁或 GPU hang | resource state tracker + Render Graph barrier |
| shader 坐标系差异 | 画面翻转、深度错误 | 统一 viewport/depth 规则 |
| MDI/SSBO 差异 | terrain 性能路径受损 | RHI capabilities + 专项测试 |

### 16.2 编码约束

- RHI 公共头文件不包含 GL/Vulkan 头。
- RHI 公共结构不出现后端原生句柄。
- 新增 C++ 代码遵循 C++17。
- 不使用异常处理。
- 失败路径通过 `bool`、错误码、`std::optional` 表达。
- 新代码注释使用英文。
- 新业务代码不直接调用 `gl*` 或 `vk*`。
- 后端原生 API 调用只允许出现在 `renderer/rhi/gl` 与 `renderer/rhi/vulkan`。

### 16.3 命名约束

建议统一前缀：

| 类型 | 前缀 |
|------|------|
| 公共 RHI 类型 | `Rhi` |
| OpenGL 后端类型 | `GlRhi` |
| Vulkan 后端类型 | `VkRhi` |
| Render Graph 类型 | `Rg` |

---

## 17. 阶段性交付标准

### Milestone A：RHI OpenGL 基础设施

交付内容：

- RHI 公共接口。
- OpenGL 后端 device/command/resource/pipeline 基础实现。
- `ResourceMgr` 纹理创建走 RHI。
- `TextureAtlas` / `FrameOutput` 不暴露 `GLuint`。

验收：

- OpenGL 后端画面无变化。
- RHI 单元测试通过。
- 新增公共头文件不 include GL。

### Milestone B：UI 与 PostProcess RHI 化

交付内容：

- UI batch renderer。
- text/glyph atlas RHI 化。
- postprocess/bloom/exposure RHI 化。

验收：

- 主菜单、HUD、inventory、loading screen 正常。
- 后处理链正常。
- UI 模块不直接调用 GL。

### Milestone C：Render Targets 与 Terrain RHI 化

交付内容：

- frame targets RHI 化。
- terrain vertex pool、MDI、SSBO RHI 化。
- terrain gbuffer/forward draw 走 RHI。

验收：

- gameplay terrain 正常。
- MDI 统计正常。
- chunk mesh 上传统计正常。

### Milestone D：Deferred Pipeline RHI 化

交付内容：

- shadow、GBuffer、lighting、SSAO、SSGI、reflection、cloud、volumetric、water、TAA 走 RHI。
- Render Graph 覆盖主要 pass。

验收：

- deferred path 与 OpenGL 原画面对齐。
- GPU timer 与 debug label 可用。
- `src/renderer` 的后端原生 API 调用集中到 RHI 后端目录。

### Milestone E：Vulkan 后端可运行

交付内容：

- Vulkan device/swapchain/command/resource/pipeline/descriptor。
- Vulkan shader 编译链。
- Vulkan Render Graph 执行。
- Vulkan ImGui/UI 支持。

验收：

- Vulkan 后端可进入主菜单。
- Vulkan 后端可进入 gameplay。
- forward 与 deferred path 可分别运行。
- 固定截图场景通过图像对比。

---

## 附录 A：首批文件改造清单

第一批应处理的公共接口文件：

| 文件 | 改造点 |
|------|--------|
| `src/resource/TextureAtlas.h` | `GLuint textureID` 替换为 RHI texture handle |
| `src/resource/ResourceMgr.h` | texture/shader 返回类型 RHI 化 |
| `src/renderer/core/FrameOutput.h` | scene/depth/shadow texture handle RHI 化 |
| `src/renderer/core/FrameContext.h` | 移除 GL include |
| `src/renderer/targets/CommonFrameTargets.h` | 移除 FBO/texture id |
| `src/renderer/targets/DeferredFrameTargets.h` | 移除 FBO/texture id |
| `src/renderer/targets/ShadowTargets.h` | 移除 FBO/texture id |
| `src/engine/platform/Window.h` | 移除 glad include，窗口与 RHI device 创建解耦 |
| `src/ui/core/UIRenderContext.h` | backdrop texture 改为 RHI handle |

---

## 附录 B：推荐的 RHI 最小接口闭包

RHI 第一版必须包含：

- `RhiDevice`
- `RhiSwapchain`
- `RhiCommandList`
- `RhiBuffer`
- `RhiTexture`
- `RhiTextureView`
- `RhiSampler`
- `RhiShader`
- `RhiPipelineLayout`
- `RhiGraphicsPipeline`
- `RhiComputePipeline`
- `RhiBindGroupLayout`
- `RhiBindGroup`
- `RhiQueryPool`
- `RhiDebugLabel`

这组接口足够覆盖 UI、postprocess、terrain、deferred、compute、debug timing，并能直接映射 Vulkan 的主要概念。
