# Mecraft Vulkan 后端与 DLSS 3 / FSR 3 集成实施方案

> 本文档面向当前已经完成 OpenGL RHI 迁移的 Mecraft 代码库，描述 Vulkan 后端、DLSS 3、FSR 3.1 与 Vulkan KHR 硬件光追的实施边界、接口改造、开发阶段和验收标准。
>
> 目标开发 GPU 为 NVIDIA GeForce RTX 4060。Vulkan 主体可以在 Linux 和 Windows 开发；DLSS 与 FSR Frame Generation 的正式接入和验证在 Windows 平台完成。

---

## 1. 固定范围

### 1.1 本方案包含

- Vulkan 1.3 RHI 后端。
- OpenGL 与 Vulkan 后端的运行时显式选择。
- Vulkan 光栅化、计算、资源、描述符、管线、命令提交、交换链和调试能力。
- NVIDIA DLSS Super Resolution。
- NVIDIA DLSS 3 Frame Generation，每个真实渲染帧生成一个插值帧，即 2x 模式。
- NVIDIA Reflex Low Latency 与 PCL 标记。
- AMD FidelityFX Super Resolution 3.1 超分。
- AMD FidelityFX Super Resolution 3.1 Frame Generation。
- Khronos Vulkan KHR Ray Tracing，包括 Ray Query 和 Ray Tracing Pipeline。
- RTX 4060 上的硬件光追验证。

### 1.2 本方案明确不包含

- DLSS Multi Frame Generation。
- DLSS Dynamic Multi Frame Generation。
- DLSS Ray Reconstruction。
- FSR 4、FSR Upscaling 4、FSR Frame Generation 4 和 Redstone 技术。
- 旧的 `VK_NV_ray_tracing` 光追接口。
- 在 OpenGL 后端中接入 DLSS、FSR 3.1 Frame Generation 或 Vulkan 光追。
- 同一交换链同时启用 DLSS Frame Generation 与 FSR Frame Generation。
- 不支持的技术自动切换到另一种超分或帧生成技术。

### 1.3 能力选择规则

- 图形后端由启动配置明确选择。
- 超分技术由设置明确选择。
- 帧生成技术由设置明确选择。
- 设备、驱动、操作系统或 SDK 条件不满足时，功能保持关闭并报告具体原因。
- 初始化失败、运行时状态异常和交换链错误通过错误码或 `std::optional` 处理。
- 禁止异常捕获。

---

## 2. 当前代码基线

### 2.1 已完成能力

当前公共 RHI 已具备以下基础：

- generation handle。
- buffer、texture、texture view、sampler、shader、pipeline 和 bind group 生命周期。
- graphics、compute、transfer command list 类型。
- 显式 rendering attachment。
- 显式 texture/buffer barrier。
- draw、draw indexed、draw indirect 和 dispatch。
- buffer/texture copy、blit 和 mipmap 生成。
- timestamp query 与 debug marker。
- submission token、GPU fence 和延迟资源销毁。
- Vulkan GLSL 到 SPIR-V 的统一编译与 reflection。

关键代码位置：

- `src/renderer/rhi/RhiDevice.h`
- `src/renderer/rhi/RhiCommandList.h`
- `src/renderer/rhi/RhiResources.h`
- `src/renderer/rhi/RhiPipeline.h`
- `src/renderer/rhi/RhiDescriptor.h`
- `src/renderer/rhi/RhiShaderCompiler.cpp`
- `src/renderer/rhi/gl/GlRhiDevice.cpp`

业务渲染层当前已经不直接调用 OpenGL API，Vulkan 后端可以在不复制高层 renderer 的前提下接入。

### 2.2 当前 Vulkan 占位状态

- `RhiBackend::Vulkan` 已存在。
- `RhiDeviceFactory.cpp` 的 Vulkan 分支当前返回空对象。
- CMake 已存在 `MECRAFT_RHI_BACKEND_VULKAN`。
- CMake 只查找和链接 Vulkan，没有 Vulkan 后端源文件。
- `RhiRenderGraph.h` 只有基础类型，没有 pass 编译、资源生命周期和 barrier planning。

### 2.3 当前接口缺口

#### 交换链

`RhiDevice` 当前只有：

- `currentSwapchainColorView()`
- `currentSwapchainColorTexture()`
- `resizeSwapchain()`
- `present()`

缺少 Vulkan 必需的显式 acquire、image index、帧状态和 present 结果。

#### 提交同步

`RhiSubmitInfo` 当前只包含 command list 数组，缺少：

- queue 类型。
- wait dependency。
- signal dependency。
- timeline value。
- acquire semaphore 关联。
- present semaphore 关联。

#### 资源状态

当前 barrier 缺少：

- image aspect。
- buffer offset 和 size。
- queue family ownership。
- acceleration structure build/read 状态。
- shader binding table 状态。

#### 格式与显示

当前格式集合缺少：

- `Bgra8Unorm`
- `Bgra8Srgb`
- `Rgb10A2Unorm`
- HDR color space 表达。
- present mode 表达。

#### 时域渲染输入

当前已经存在：

- HDR scene color。
- depth。
- `Rg16Float` velocity。
- jitter 和 previous jitter。
- 当前和上一帧 camera matrices。
- history invalidation。

仍需增加：

- reactive mask。
- transparency/composition mask。
- 统一 exposure 输入。
- camera cut/reset 契约。
- render extent 与 output extent 的统一描述。
- motion vector 方向、坐标空间和缩放约定。

---

## 3. 总体架构

```text
Game / RenderScene / UI
        │
        ├── Render Pipeline
        │       ├── Raster / Compute Passes
        │       ├── Temporal Upscaler
        │       └── Ray Tracing Passes
        │
        ├── Presentation Controller
        │       ├── Native Present
        │       ├── DLSS 3 Frame Generation
        │       └── FSR 3.1 Frame Generation
        │
        └── RHI
                ├── OpenGL Backend
                └── Vulkan Backend
                        ├── Core Device / Queue / Memory
                        ├── Resources / Descriptor / Pipeline
                        ├── Swapchain / Frame Lifecycle
                        ├── Private Vulkan Interop
                        └── KHR Ray Tracing
```

核心边界：

- Temporal Upscaler 负责低分辨率到显示分辨率的时域重建。
- Frame Generation 负责 present 链、插值帧、UI 合成和 frame pacing。
- Frame Generation 不是普通 render pass。
- Vulkan 原生对象只允许出现在 Vulkan 后端和 Windows SDK bridge 中。
- 业务 renderer 只使用 RHI handle 和统一帧输入结构。

---

## 4. 公共 RHI 接口改造

### 4.1 帧获取与交换链状态

新增状态类型：

```cpp
enum class RhiFrameStatus {
    Success,
    Suboptimal,
    OutOfDate,
    Minimized,
    SurfaceLost,
    DeviceLost,
    Error
};

struct RhiFrameAcquireResult {
    RhiFrameStatus status = RhiFrameStatus::Error;
    uint64_t frameIndex = 0;
    uint32_t imageIndex = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    RhiTextureHandle colorTexture;
    RhiTextureViewHandle colorView;
    RhiTextureViewHandle depthStencilView;
};
```

`RhiDevice` 增加：

```cpp
virtual RhiFrameAcquireResult acquireFrame() = 0;
virtual RhiFrameStatus presentFrame(const RhiPresentInfo& info) = 0;
```

约束：

- swapchain color handle 只在成功 acquire 后有效。
- 最小化窗口不创建零尺寸 swapchain。
- resize、surface lost 和 out-of-date 在帧边界处理。
- swapchain 重建前等待相关 frame context 完成。
- 旧 swapchain image view 与 depth image 按 GPU 完成状态释放。
- `RhiSwapchain.h` 与 `RhiDevice` 中重复的交换链入口合并成一套契约。

### 4.2 队列与提交依赖

新增 queue 类型和 dependency：

```cpp
enum class RhiQueueType {
    Graphics,
    Compute,
    Transfer,
    Present
};

struct RhiQueueDependency {
    RhiSubmissionToken token;
    uint64_t value = 0;
};

struct RhiSubmitInfo {
    const char* debugName = nullptr;
    RhiQueueType queue = RhiQueueType::Graphics;
    RhiCommandList* const* commandLists = nullptr;
    uint32_t commandListCount = 0;
    const RhiQueueDependency* waits = nullptr;
    uint32_t waitCount = 0;
};
```

Vulkan 实现要求：

- GPU 内部提交使用 timeline semaphore 表达长期依赖。
- swapchain acquire 和 present 使用 binary semaphore。
- 每个 frame context 拥有独立 command pool、binary semaphore 和 CPU fence。
- 首版 renderer 提交集中在 graphics queue。
- FSR 3.1 Frame Generation 和 DLSS 3 Frame Generation 所需额外队列在 logical device 创建时一次性配置。

### 4.3 Barrier 与资源状态

增加：

- `RhiTextureAspectFlags`。
- buffer barrier offset/size。
- queue ownership source/destination。
- `AccelerationStructureBuildRead`。
- `AccelerationStructureBuildWrite`。
- `AccelerationStructureShaderRead`。
- `ShaderBindingTableRead`。

状态映射必须只有一个实现入口：

```text
RhiResourceState
        └── VkPipelineStageFlags2
        └── VkAccessFlags2
        └── VkImageLayout
```

Vulkan 后端不得根据调用位置推测状态。高层提交的状态和后端记录的真实状态必须一致。

完整 Render Graph 不作为 Vulkan 首帧的前置条件。Vulkan 功能对等完成后，将分散在 `DeferredRenderTargets` 等模块中的状态表迁入统一资源状态管理器。

### 4.4 格式、交换链和 HDR

增加：

- `Bgra8Unorm`
- `Bgra8Srgb`
- `Rgb10A2Unorm`
- `RhiColorSpace`
- `RhiPresentMode`
- swapchain image count。

首个可运行版本使用 SDR sRGB swapchain。HDR10 在 Vulkan 功能对等完成后单独验收。DLSS 3 Frame Generation 的首个交付目标同样使用 SDR，减少颜色空间和 UI alpha 验证变量。

### 4.5 Capability

`RhiCapabilities` 增加：

- Vulkan API version。
- Dynamic Rendering。
- Synchronization2。
- timeline semaphore。
- buffer device address。
- descriptor indexing limits。
- queue family capability。
- acceleration structure。
- ray query。
- ray tracing pipeline。
- shader binding table alignment。
- temporal upscaler provider 状态。
- frame generation provider 状态。

Capability 只能来自实际 feature、extension 和 SDK 查询结果，不能通过 GPU 名称推断。

### 4.6 Vulkan 私有互操作

新增内部接口，例如：

```text
src/renderer/rhi/vulkan/VkRhiInterop.h
```

它可以向 Vulkan SDK bridge 提供：

- `VkInstance`
- `VkPhysicalDevice`
- `VkDevice`
- `VkQueue` 与 family index
- `VkCommandBuffer`
- `VkImage`
- `VkImageView`
- `VkDeviceMemory`
- `VkFormat`
- `VkImageLayout`
- `VkSemaphore`
- Vulkan function pointer

该接口不得被通用 pass、资源系统、UI 或游戏逻辑 include。

---

## 5. Vulkan 后端模块设计

### 5.1 目录结构

```text
src/renderer/rhi/vulkan/
    VkRhiDevice.h/.cpp
    VkRhiSwapchain.h/.cpp
    VkRhiCommandList.h/.cpp
    VkRhiResources.h/.cpp
    VkRhiMemory.h/.cpp
    VkRhiDescriptor.h/.cpp
    VkRhiPipeline.h/.cpp
    VkRhiConversions.h
    VkRhiDebug.h/.cpp
    VkRhiInterop.h/.cpp
    VkRhiRayTracing.h/.cpp
```

Windows SDK bridge：

```text
src/renderer/upscale/
    TemporalUpscaler.h
    FrameGenerationProvider.h

src/renderer/upscale/vulkan/
    VulkanTemporalResources.h/.cpp

src/renderer/upscale/nvidia/
    StreamlineRuntime.h/.cpp
    DlssTemporalUpscaler.h/.cpp
    DlssFrameGeneration.h/.cpp
    ReflexController.h/.cpp

src/renderer/upscale/amd/
    Fsr3TemporalUpscaler.h/.cpp
    Fsr3FrameGeneration.h/.cpp
```

### 5.2 Instance 与设备创建

Vulkan 基线：

- Vulkan 1.3。
- `VK_KHR_swapchain`。
- Dynamic Rendering core feature。
- Synchronization2 core feature。
- timeline semaphore core feature。
- buffer device address core feature。
- `VK_EXT_debug_utils` 用于 Debug 和 RelWithDebInfo。

创建顺序：

1. 初始化平台层。
2. 设置 `GLFW_CLIENT_API = GLFW_NO_API`。
3. 在 Windows 且启用 Streamline 编译项时初始化 Streamline runtime。
4. 收集 GLFW、Streamline 和调试层需要的 instance extensions。
5. 创建 `VkInstance`。
6. 创建 GLFW surface。
7. 枚举 physical device。
8. 查询 swapchain、queue、RHI 和 SDK feature requirements。
9. 选择满足当前启动配置的 physical device。
10. 创建 logical device 和 queues。
11. 创建 allocator、descriptor pool、pipeline cache 和 swapchain。

RTX 4060 是开发机目标，但 physical device 选择仍按 feature 查询执行。

### 5.3 内存管理

使用 Vulkan Memory Allocator：

- `GpuOnly` 映射到 device local allocation。
- `CpuToGpu` 映射到持久映射 upload allocation。
- `GpuToCpu` 映射到 readback allocation。

新增 staging ring：

- 每 frame context 独立分配范围。
- buffer 与 texture 上传通过 copy command 完成。
- `updateBuffer` 不直接等价为 `vkCmdUpdateBuffer`。
- 大于命令限制的数据统一进入 staging ring。

### 5.4 Command List

Vulkan command list 直接记录到 `VkCommandBuffer`，不复制 OpenGL 后端的字节流 replay 设计。

映射关系：

- `beginRendering` → `vkCmdBeginRendering`
- `endRendering` → `vkCmdEndRendering`
- barrier → `vkCmdPipelineBarrier2`
- timestamp → `vkCmdWriteTimestamp2`
- debug label → `VK_EXT_debug_utils`

每个 command pool 绑定创建线程。线程销毁约束保持与现有 `RhiCommandListPool` 契约一致。

### 5.5 Descriptor 与 Pipeline

- `RhiBindGroupLayout` 映射为 `VkDescriptorSetLayout`。
- `RhiBindGroup` 映射为 `VkDescriptorSet`。
- descriptor pool 按 frame 与长期资源分开管理。
- pipeline layout 直接消费 reflection 后的 set/binding 和 push constant 信息。
- graphics pipeline 使用 Dynamic Rendering attachment formats。
- pipeline cache 写入构建目录或用户缓存目录。
- shader module 直接使用现有 SPIR-V 结果。

公共 descriptor array 契约需要类型化，禁止保留 layout 声明数组但 bind group 无法表达数组元素的状态。

### 5.6 Swapchain 与 Frame Context

每个 frame context 包含：

- graphics command pool。
- transfer command pool。
- image-available semaphore。
- render-finished semaphore。
- frame fence 或 timeline value。
- staging ring range。
- deferred deletion queue。
- timestamp query range。

交换链重建触发条件：

- framebuffer extent 变化。
- `VK_ERROR_OUT_OF_DATE_KHR`。
- surface lost。
- present mode 变化。
- color space 变化。
- Frame Generation provider 发生变更。

`VK_SUBOPTIMAL_KHR` 必须作为明确状态上传到应用层，由帧生命周期控制器决定重建时点。

### 5.7 Debug 与诊断

- Debug 构建启用 validation layer。
- Debug 和 RelWithDebInfo 创建 debug messenger。
- 所有 Vulkan 对象写入稳定 debug name。
- validation error 计入测试失败。
- Device Lost 输出最近 submission、frame index、queue 和 pass label。
- RenderDoc 与 Nsight Graphics 捕获路径必须可用。

---

## 6. 构建系统

### 6.1 CMake 选项

```cmake
option(MECRAFT_RHI_BACKEND_OPENGL "Build OpenGL RHI backend" ON)
option(MECRAFT_RHI_BACKEND_VULKAN "Build Vulkan RHI backend" OFF)
option(MECRAFT_ENABLE_STREAMLINE "Enable NVIDIA Streamline integration" OFF)
option(MECRAFT_ENABLE_FSR3 "Enable AMD FSR 3.1 integration" OFF)
option(MECRAFT_ENABLE_VULKAN_RAY_TRACING "Enable Vulkan KHR ray tracing" OFF)
```

约束：

- Streamline 和 FSR 3.1 编译项只在 Windows Vulkan 构建生效。
- Linux 构建不包含 Windows SDK header、library 或 DLL 部署逻辑。
- Vulkan 后端可以独立构建，不要求 Streamline 或 FSR 3.1。
- OpenGL-only、Vulkan-only 和双后端构建都必须受 CI 或本地脚本验证。

### 6.2 依赖

Vulkan 主体：

- Vulkan SDK / Loader / Headers。
- Vulkan Memory Allocator。
- GLFW。
- glslang。
- SPIRV-Cross。

Windows NVIDIA：

- Streamline 2.12.0 固定发布包。
- DLSS plugin。
- DLSS-G plugin。
- Reflex plugin。
- PCL plugin。
- NVIDIA 签名运行时 DLL。

Windows AMD：

- FidelityFX SDK v1.1.4。
- FSR 3.1.4 Vulkan backend。
- Vulkan FrameInterpolationSwapchain。

不得将 FSR SDK v2.x 的 DX12-only FSR 4/Frame Generation 4 模块加入 Vulkan 构建。

---

## 7. Vulkan 开发阶段

### 阶段 V0：冻结公共帧协议

任务：

- 实现 acquire/present 状态码。
- 合并交换链接口。
- 扩展 submit dependency。
- 扩展格式、barrier 和 capability。
- 更新 OpenGL 后端以满足新契约。
- 编写 backend-neutral RHI contract test。

完成标准：

- OpenGL 画面和性能无意外变化。
- 现有 RHI 测试通过。
- 新的帧协议没有 Vulkan 类型泄漏。

### 阶段 V1：Vulkan 启动与交换链

任务：

- instance、surface、physical device、logical device。
- graphics/present queue。
- swapchain 与 depth image。
- frame contexts。
- acquire、clear、present。
- validation 和 debug name。

完成标准：

- Vulkan-only 构建成功。
- 空场景可以稳定清屏和 present。
- resize、最小化、恢复和退出无 validation error。

### 阶段 V2：资源、描述符和管线

任务：

- VMA。
- buffer、texture、view、sampler。
- staging upload 和 readback。
- descriptor set。
- graphics/compute pipeline。
- dynamic rendering。
- query、debug marker、copy、blit、mipmap。

完成标准：

- backend-neutral RHI contract test 同时覆盖 OpenGL 和 Vulkan。
- shader reflection、descriptor binding 和 push constants 验证通过。
- 资源销毁无 validation leak。

### 阶段 V3：实际渲染功能对等

垂直切片顺序：

1. 主菜单与 UI。
2. Forward pipeline。
3. Deferred GBuffer。
4. Shadow。
5. Compute 与 storage image pass。
6. Temporal effects。
7. Post process。
8. 截图与 readback。
9. Debug dashboard 与 GPU timestamp。

完成标准：

- 主菜单、加载界面和游戏世界可运行。
- Forward 与 Deferred 均可运行。
- 固定相机截图与 OpenGL 基线进行差异比较。
- Vulkan validation error 为零。

### 阶段 V4：稳定性与性能

任务：

- pipeline cache。
- descriptor pool 压力测试。
- swapchain 重建压力测试。
- 资源创建销毁循环。
- GPU/CPU frame time 基线。
- 显存占用与 fragmentation 检查。
- RenderDoc 和 Nsight Graphics 验证。

完成标准：

- 连续运行、Alt-Tab、resize、全屏切换和退出稳定。
- 无持续增长的 Vulkan 对象、descriptor 或 allocation。
- RTX 4060 上记录 Vulkan 与 OpenGL 的固定场景性能报告。

---

## 8. 时域超分统一接口

### 8.1 统一输入

```cpp
struct TemporalUpscaleInput {
    RhiTextureHandle color;
    RhiTextureHandle depth;
    RhiTextureHandle motionVectors;
    RhiTextureHandle exposure;
    RhiTextureHandle reactiveMask;
    RhiTextureHandle transparencyMask;
    RhiTextureHandle output;
    uint32_t renderWidth = 0;
    uint32_t renderHeight = 0;
    uint32_t outputWidth = 0;
    uint32_t outputHeight = 0;
    glm::vec2 jitter = glm::vec2(0.0f);
    glm::vec2 motionVectorScale = glm::vec2(1.0f);
    float frameTimeMilliseconds = 0.0f;
    float preExposure = 1.0f;
    float cameraNear = 0.1f;
    float cameraFar = 500.0f;
    float verticalFovRadians = 1.0f;
    bool reset = false;
};
```

统一接口：

```cpp
class TemporalUpscaler {
public:
    virtual ~TemporalUpscaler() = default;
    virtual bool configure(const TemporalUpscaleConfig& config) = 0;
    virtual bool execute(RhiCommandList& commandList,
                         const TemporalUpscaleInput& input) = 0;
    virtual void shutdown() = 0;
};
```

### 8.2 帧顺序

启用 DLSS SR 或 FSR 3.1 SR 时：

```text
Jittered Scene Rendering
    → Depth / Motion Vector / Reactive Masks
    → DLSS SR or FSR 3.1 SR
    → Output-resolution Motion Blur / DOF / Post Process
    → HUD-less Final Color
    → UI
    → Frame Generation / Present
```

规则：

- DLSS SR 与 FSR 3.1 SR 替代当前主 TAA resolve。
- 当前 `TemporalResolvePass` 在外部时域超分启用时不执行。
- 内置 TAA、DLSS SR、FSR 3.1 SR 三者互斥。
- FSR1 保持独立的空间超分选项，但不与时域超分同时启用。
- motion blur 与 DOF 的输入分辨率和执行位置通过画质与性能测试确定后固定。
- renderer 统一生成 reset 标志，SDK bridge 不自行判断 camera cut。

### 8.3 Motion Vector 契约

必须明确并测试：

- 当前像素到上一帧像素的方向。
- normalized UV 或 pixel-space 表达。
- jitter 是否包含在 motion vector 中。
- 动态实体 motion vector。
- 天空和远平面 motion vector。
- alpha-tested 与透明物体 motion vector。

增加专用 debug view：

- motion vector 方向颜色。
- motion vector 长度。
- disocclusion。
- reactive mask。
- transparency mask。
- camera cut/reset 状态。

---

## 9. DLSS 3 Windows Vulkan 集成

### 9.1 固定技术范围

- DLSS Super Resolution。
- DLSS 3 Frame Generation 2x。
- Reflex Low Latency。
- PCL markers。
- RTX 4060。

不加载 Multi Frame Generation 和 Ray Reconstruction plugin。

### 9.2 接入方式

采用 Streamline 2.12.0 manual hooking：

- RHI 保持 Vulkan device 和 swapchain 生命周期的显式控制。
- Streamline 初始化发生在任何 Vulkan API 调用之前。
- 通过 `slGetFeatureRequirements` 获取 instance/device extension、feature chain 和额外 queue 要求。
- logical device 创建后调用 `slSetVulkanInfo`。
- Vulkan function hook 集中在 `StreamlineRuntime`，业务层不接触 Streamline API。

加载 feature：

- `sl::kFeatureDLSS`
- `sl::kFeatureDLSS_G`
- `sl::kFeatureReflex`
- `sl::kFeaturePCL`

### 9.3 DLSS Super Resolution

输入标签：

- Scaling Input Color。
- Scaling Output Color。
- Depth。
- Motion Vectors。
- Exposure。

每帧提供：

- render/output extent。
- jitter。
- motion vector scale。
- camera matrices，矩阵不包含 jitter。
- reset。
- HDR/pre-exposure 信息。

DLSS optimal settings 决定内部 render extent。质量模式作为用户设置，初始化结果必须记录实际 render size。

### 9.4 DLSS 3 Frame Generation

输入：

- backbuffer/final color。
- depth。
- dense motion vectors。
- HUD-less color。
- UI alpha 或 UI color/alpha。
- frame token 和公共 camera constants。

固定配置：

- `numFramesToGenerate = 1`。
- 不显示多倍帧生成设置。
- 不启用 Dynamic MFG。
- Vulkan FG 模式下不开放 VSync 选项，因为 Streamline 2.12.0 的 Vulkan FG 不支持应用控制 VSync。

交换链规则：

- FG provider 启用状态变化时重建交换链。
- resize、全屏切换和窗口模式变化前关闭 interpolation。
- 菜单、加载、暂停和不产生游戏帧的状态关闭 interpolation。
- FG 输入资源保持有效直到 Streamline 完成 present 阶段处理。
- 修改或销毁跨帧输入前等待 Streamline completion fence。

### 9.5 Reflex

Reflex 与 DLSS 3 Frame Generation 在同一个 Windows 阶段完成。

帧标记至少包括：

- Simulation Start / End。
- Render Submit Start / End。
- Present Start / End。
- Trigger Flash。

`slReflexSleep` 放置在游戏主循环固定位置，并在 Reflex mode 关闭时继续按 SDK 契约调用。

验收：

- RTX 4060 上 DLSS SR 各质量模式可运行。
- Frame Generation 实际呈现帧数为每个真实帧对应两个显示帧。
- Reflex Verification 工具无集成错误。
- resize、Alt-Tab、暂停、菜单和退出无死锁。
- Vulkan FG 关闭时交换链没有持续的额外拷贝成本。

---

## 10. FSR 3.1 Windows Vulkan 集成

### 10.1 固定版本

使用：

- FidelityFX SDK `v1.1.4`。
- FSR 3.1.4 Upscaling。
- FidelityFX Frame Interpolation 1.1.3。
- FidelityFX Frame Interpolation Swapchain 1.1.3。
- Vulkan FidelityFX backend。
- Vulkan FrameInterpolationSwapchain。

不使用：

- FSR SDK v2.x 的 DX12 模块。
- FSR 4 / Redstone。
- ML Frame Generation 4。

### 10.2 FSR 3.1 Upscaling

输入：

- color。
- single-channel float depth。
- two-channel float motion vectors。
- exposure。
- reactive mask。
- transparency/composition mask。
- jitter、delta time、pre-exposure、near/far/FOV、reset。

输出：

- 显示分辨率 storage image。

要求：

- dispatch 前资源具有真实 shader-read 可见性。
- output 具有 storage write 状态。
- SDK dispatch 后恢复 RHI 需要的 layout 和状态记录。
- FSR 3.1 与内置 TAA、DLSS SR 互斥。

### 10.3 FSR 3.1 Frame Generation

FrameInterpolationSwapchain 需要：

- physical device。
- logical device。
- swapchain create info。
- game graphics queue。
- async compute queue。
- present queue。
- image acquire queue。
- queue family index。
- timeline semaphore。

Presentation Controller 负责将 Vulkan WSI 函数交给 FSR provider：

- create/destroy swapchain。
- get swapchain images。
- acquire next image。
- queue present。
- HDR metadata。

每个真实帧的 `frameID` 必须严格递增一。

异步模式资源：

- HUD-less color 双缓冲。
- UI texture 双缓冲。
- distortion field 双缓冲。
- depth 和 motion vectors 保持到对应 prepare/interpolation 工作完成。

FSR context 非线程安全，Present、Prepare、Configure 和 Destroy 通过明确的串行控制器调用。

验收：

- FSR 3.1 Quality/Balanced/Performance 模式可运行。
- FSR Frame Generation 可独立于 DLSS 工作。
- UI 在真实帧和插值帧中保持稳定。
- 基础渲染帧率、插值成本和 frame pacing 单独记录。
- resize、Alt-Tab、暂停、菜单和退出无死锁。

---

## 11. Frame Generation Presentation Controller

统一接口：

```cpp
enum class FrameGenerationMode {
    Disabled,
    Dlss3,
    Fsr3
};

class FrameGenerationProvider {
public:
    virtual ~FrameGenerationProvider() = default;
    virtual bool initialize(const FrameGenerationConfig& config) = 0;
    virtual bool configure(const FrameGenerationFrameInfo& frameInfo) = 0;
    virtual RhiFrameStatus present(const FrameGenerationPresentInfo& info) = 0;
    virtual void shutdown() = 0;
};
```

状态机：

```text
Uninitialized
    → Ready
    → Active
    → ResizePending
    → Ready
    → ShuttingDown
    → Uninitialized
```

规则：

- provider 变更只能发生在 GPU idle 且 swapchain 不被使用时。
- 每个进程只有一个 presentation provider 管理主交换链。
- 主菜单和加载界面使用 native present。
- Gameplay 中根据明确设置进入 DLSS 3 或 FSR 3.1 provider。
- provider 错误使当前功能关闭并显示原因，不自动启用其他 provider。

---

## 12. UI 与 HUD-less 输出改造

当前 UI 在场景渲染后直接写入 swapchain。帧生成要求拆分：

```text
Scene + Post Process
        └── HUD-less Full-resolution Color

UI Renderer
        └── Full-resolution UI Color/Alpha

Presentation Controller
        └── Real Frame / Generated Frame Composition
```

新增 render targets：

- `HudlessColor`。
- `UiColorAlpha` 或 `UiAlpha`。
- provider 需要的 final color。

UI 规则：

- UI render extent 与 backbuffer 一致。
- alpha 约定固定为 premultiplied 或 straight alpha，所有 provider 使用同一约定。
- screenshot 可以明确选择 HUD-less 或含 UI 输出。
- Dashboard 和调试 overlay 归入 UI texture。
- 光标、准星、物品栏和文字不能写入 HUD-less color。

---

## 13. Vulkan KHR 硬件光追

### 13.1 扩展和 feature

基础能力：

- `VK_KHR_acceleration_structure`
- `VK_KHR_ray_tracing_pipeline`
- `VK_KHR_ray_query`
- `VK_KHR_deferred_host_operations`
- buffer device address
- descriptor indexing

查询：

- `VkPhysicalDeviceAccelerationStructureFeaturesKHR`
- `VkPhysicalDeviceRayTracingPipelineFeaturesKHR`
- `VkPhysicalDeviceRayQueryFeaturesKHR`
- `VkPhysicalDeviceAccelerationStructurePropertiesKHR`
- `VkPhysicalDeviceRayTracingPipelinePropertiesKHR`

RTX 4060 上通过这些 capability 确认硬件支持，不使用 GPU 名称作为启用条件。

### 13.2 RHI 类型

新增：

- `RhiAccelerationStructureHandle`。
- BLAS/TLAS 类型。
- triangle、AABB、instance geometry。
- build/update/compact description。
- scratch buffer 和 device address。
- acceleration structure descriptor binding。
- ray tracing pipeline description。
- shader group description。
- shader binding table description。

Shader stage：

- Ray Generation。
- Miss。
- Closest Hit。
- Any Hit。
- Intersection。
- Callable。

Command list：

- build acceleration structures。
- copy/compact acceleration structure。
- write acceleration structure properties。
- trace rays。

### 13.3 世界数据策略

- 每个 chunk section 的可渲染三角形建立 BLAS。
- chunk mesh 变化时重建对应 BLAS。
- 长期稳定的 BLAS 完成压缩。
- 动态实体使用独立 BLAS。
- 当前可见 chunk 和实体作为 TLAS instances。
- TLAS 按可见集合和 transform 变化更新。
- alpha-tested 方块进入 Any Hit 处理。
- 水体和透明材质按效果需求决定是否进入 AS。

需要记录：

- BLAS build time。
- TLAS build/update time。
- scratch peak usage。
- compacted AS memory。
- 每 chunk section 平均 AS memory。

### 13.4 光追开发阶段

#### RT0：最小能力验证

- 创建 BLAS/TLAS。
- 创建 RayGen/Miss/ClosestHit pipeline。
- 创建 SBT。
- trace 一个测试三角形。
- validation error 为零。

#### RT1：Ray Query 混合效果

- 在 compute 或 fragment shader 中执行 ray query。
- 接入一项可独立验证的阴影或反射效果。
- 与现有 deferred buffer 和时域历史共同工作。

#### RT2：方块世界 AS

- chunk section BLAS。
- entity BLAS。
- visible TLAS。
- chunk 加载、卸载和重建生命周期。
- 显存预算与构建时间控制。

---

## 14. 测试与验收

### 14.1 Linux Vulkan 阶段

参考 `build.sh`：

```sh
./build.sh -b cmake-build-vulkan-debug -c Debug -- \
  -DMECRAFT_RHI_BACKEND_OPENGL=OFF \
  -DMECRAFT_RHI_BACKEND_VULKAN=ON \
  -DMECRAFT_DEFAULT_RHI_BACKEND=Vulkan

./build.sh -b cmake-build-vulkan-release -c Release -- \
  -DMECRAFT_RHI_BACKEND_OPENGL=OFF \
  -DMECRAFT_RHI_BACKEND_VULKAN=ON \
  -DMECRAFT_DEFAULT_RHI_BACKEND=Vulkan
```

测试项：

- RHI contract test。
- acquire/present/resize test。
- buffer/texture upload/readback。
- descriptor/pipeline test。
- Forward/Deferred screenshot。
- validation layer error count。
- resource leak count。
- KHR Ray Tracing capability 与最小 RT pipeline。

### 14.2 Windows Vulkan 阶段

参考 `build.ps1` 与 CMake options，分别验证：

- Vulkan core。
- Vulkan + Streamline。
- Vulkan + FSR 3.1。
- Vulkan + KHR Ray Tracing。
- Vulkan + Streamline + KHR Ray Tracing。
- Vulkan + FSR 3.1 + KHR Ray Tracing。

窗口生命周期：

- resize。
- minimize/restore。
- Alt-Tab。
- fullscreen/windowed。
- display scale 变化。
- monitor 切换。
- provider enable/disable。
- shutdown/reinitialize。

### 14.3 画质测试

固定内容：

- 世界 seed。
- 相机位置和朝向。
- 时间与天气。
- 渲染距离。
- 分辨率。
- shader 设置。

输出：

- Native Vulkan。
- 内置 TAA。
- DLSS SR。
- FSR 3.1 SR。
- DLSS 3 FG real frame 与 generated frame。
- FSR 3.1 FG real frame 与 generated frame。
- Ray Query 效果。
- RT Pipeline 效果。

重点观察：

- 细栅栏、草、树叶和远处方块边缘。
- 粒子、雨雪、云、体积雾。
- 水体和透明方块。
- 第一人称手持物。
- UI 文字和准星。
- 快速转向、冲刺和传送。
- chunk 加载边界。

### 14.4 性能测试

RTX 4060 固定记录：

- CPU frame time。
- GPU frame time。
- graphics queue time。
- upscaler cost。
- frame generation cost。
- present latency。
- VRAM usage。
- BLAS/TLAS build cost。
- 基础真实帧率和最终显示帧率。

测试组合：

- Vulkan Native。
- Vulkan + DLSS SR。
- Vulkan + DLSS SR + DLSS 3 FG。
- Vulkan + FSR 3.1 SR。
- Vulkan + FSR 3.1 SR + FSR 3.1 FG。
- Vulkan + Ray Query。
- Vulkan + RT Pipeline。
- Vulkan + RT + DLSS SR + DLSS 3 FG。
- Vulkan + RT + FSR 3.1 SR + FSR 3.1 FG。

---

## 15. 版本固定矩阵

| 组件 | 固定基线 |
|------|----------|
| Vulkan API | 1.3 |
| Vulkan SDK / Validation | 实施开始时记录精确版本并固定 |
| Vulkan Memory Allocator | 固定 tag 或 commit |
| GLFW | 当前 vcpkg baseline 对应版本 |
| glslang | 当前 vcpkg baseline 对应版本 |
| SPIRV-Cross | 当前 vcpkg baseline 对应版本 |
| Streamline | 2.12.0 |
| DLSS Frame Generation | Streamline 2.12.0 发布包内固定插件 |
| NVIDIA Vulkan native optical flow | Windows driver 527.64 或更高；最终以 feature requirements 为准 |
| FSR Vulkan | FidelityFX SDK v1.1.4 / FSR 3.1.4 |
| C++ | C++17 |

Windows DLSS 运行条件：

- Windows 10 20H1 build 19041 或更高。
- Hardware-accelerated GPU Scheduling 开启。
- RTX 4060 驱动满足 Streamline feature requirements。
- 运行时 DLL 使用正式签名版本。

每次 SDK、驱动基线或 shader compiler 升级后重新执行：

- capability test。
- 初始化与销毁测试。
- resize 和 present 压力测试。
- 画质基线。
- frame pacing 基线。
- 性能基线。

---

## 16. 主要风险与控制措施

### 16.1 交换链协议不足

风险：当前 `currentSwapchain* + void present` 无法表达 Vulkan 和 Frame Generation 生命周期。

控制：阶段 V0 完成 acquire/present/status/synchronization 契约后才实现 Vulkan swapchain。

### 16.2 高层状态表与 Vulkan layout 不一致

风险：`DeferredRenderTargets` 等模块维护的逻辑状态可能和 Vulkan 实际 layout 不一致。

控制：定义唯一状态映射；Debug 构建记录每个 subresource 的最后状态和最近 barrier。

### 16.3 Motion Vector 约定错误

风险：DLSS/FSR 出现重影、拖尾、闪烁和边缘不稳定。

控制：固定方向、坐标空间、jitter 关系和缩放；增加 motion/reactive debug view。

### 16.4 UI 直接写入 swapchain

风险：插值帧中的 UI 变形或重影。

控制：分离 HUD-less color 与 UI color/alpha，并统一 alpha 合成约定。

### 16.5 两个 Frame Generation provider 争用 present

风险：多个 SDK 同时 hook 或替换主交换链。

控制：Presentation Controller 保证同一时间只有一个 provider 拥有主交换链。

### 16.6 Linux 与 Windows 验证边界

风险：Linux Vulkan 稳定不能证明 Windows Streamline 和 FidelityFX FrameInterpolationSwapchain 稳定。

控制：Windows SDK 阶段建立独立生命周期、画质、性能和 frame pacing 验收。

### 16.7 SDK 分发

风险：签名 DLL、许可、插件版本和应用标识配置不完整。

控制：发布包记录 SDK 版本、DLL hash、许可文件和 required runtime list。

### 16.8 RTX 4060 显存预算

风险：Deferred targets、时域历史、FG resources 和 acceleration structures 同时占用较多显存。

控制：记录每项技术的独立显存增量；建立 1080p、1440p 和目标渲染距离预算。

---

## 17. 最终完成标准

### Vulkan 后端

- Linux 和 Windows Vulkan-only 构建成功。
- 主菜单、加载界面、Forward、Deferred、UI、阴影、计算和后处理可运行。
- resize、最小化、恢复、Alt-Tab、全屏切换和退出稳定。
- validation layer error 为零。
- 无持续资源或 descriptor 泄漏。
- Vulkan 与 OpenGL 固定截图差异在已记录阈值内。

### DLSS 3

- RTX 4060 上 DLSS Super Resolution 可运行。
- DLSS 3 Frame Generation 固定为 2x。
- Reflex 与 PCL 集成通过官方验证工具。
- HUD-less/UI 分离正确。
- Dynamic MFG 和 Multi Frame Generation 不出现在设置中。
- Vulkan FG 下不显示 VSync 开关。

### FSR 3.1

- FSR 3.1.4 Vulkan Upscaling 可运行。
- FidelityFX SDK v1.1.4 中的 Vulkan Frame Generation 可运行。
- 不编译、不加载 FSR 4 / Redstone 模块。
- UI、resize、暂停、菜单和退出生命周期稳定。

### Vulkan KHR 光追

- RTX 4060 capability 查询正确。
- BLAS/TLAS、SBT 和最小 RT pipeline 可运行。
- Ray Query 混合效果可运行。
- chunk section BLAS 和 visible TLAS 生命周期正确。
- AS build/read barrier 无 validation error。
- 显存和构建成本形成固定基线。

---

## 18. 官方参考

- NVIDIA DLSS：<https://developer.nvidia.com/rtx/dlss>
- NVIDIA Streamline：<https://github.com/NVIDIA-RTX/Streamline>
- Streamline Programming Guide：<https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuide.md>
- Streamline DLSS Guide：<https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS.md>
- Streamline DLSS-G Guide：<https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS_G.md>
- Streamline Reflex Guide：<https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideReflex.md>
- AMD FSR 3：<https://gpuopen.com/fidelityfx-super-resolution-3/>
- FidelityFX SDK v1.1.4 FSR 3.1：<https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/v1.1.4/docs/techniques/super-resolution-interpolation.md>
- AMD Vulkan FidelityFX API：<https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/v1.1.4/ffx-api/include/ffx_api/vk/ffx_api_vk.h>
- Khronos Vulkan Ray Tracing Guide：<https://docs.vulkan.org/guide/latest/extensions/ray_tracing.html>
- `VK_KHR_acceleration_structure`：<https://registry.khronos.org/vulkan/specs/latest/man/html/VK_KHR_acceleration_structure.html>
- `VK_KHR_ray_tracing_pipeline`：<https://registry.khronos.org/vulkan/specs/latest/man/html/VK_KHR_ray_tracing_pipeline.html>
- `VK_KHR_ray_query`：<https://registry.khronos.org/vulkan/specs/latest/man/html/VK_KHR_ray_query.html>
- NVIDIA Vulkan Driver：<https://developer.nvidia.com/vulkan-driver>
