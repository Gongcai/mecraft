# 实施路线图

## 1. 执行原则

- 每个阶段同时覆盖体素世界与模型场景。
- 每个公共数据结构先写契约测试，再接入 Pass。
- 每个阶段提交 Debug View、Timestamp、错误码和自动化验收。
- Vulkan 现代功能与 OpenGL 基础功能使用明确能力表。
- 错误通过返回值、错误码或 `std::optional` 传播，不使用异常。
- Linux 编译测试参考 `build.sh`，Windows 编译测试参考 `build.ps1`。

## 2. 阶段依赖

```text
M0 契约与测量基线
 ├──► M1 统一材质 / Clustered Lighting / PBR IBL
 └──► M2 Bindless GPU Scene / AS RHI
          └──► M4 GPU Culling / LOD / Animation

M1 + M2 ──► M3 RTGI / NRD
M1 + M2 + M3 ──► M5 多层透明 / Reflection Probe / RT Reflection
M3 + M4 + M5 ──► M6 动态分辨率 / HDR / 全管线优化
M0..M6 ──► M7 双场景发布验收
```

M1 与 M2 可并行开发，但公共 `GpuMaterial`、`GpuSceneGeometry` 与 Stable ID 必须先冻结。

## 3. M0：契约与测量基线

### 交付物

- 固定 Vulkan Modern 与 OpenGL Base 能力表及结构化错误。
- 删除 `VoxelGiClipmap.cpp/.h` 及 CMake 源文件项。
- 删除 Deferred Pipeline/Scene Composite 的 Voxel GI Pass、3D 纹理、Bind Group、Shader
  Variant、参数和统计接口。
- 删除 `VoxelGiSettings`、JSON 序列化、默认配置、Dashboard 与 Settings Screen 控件。
- 扩充 Temporal Extent、Reset Reason、Stable Object/Material ID。
- 建立版本化体素与模型测试场景、Camera Path 和 Reference Capture。
- Render Graph 大阶段 Timestamp、p50/p95/p99 报告和显存分类。
- 现有 SSGI、SSR、透明、模型 CPU Draw 的基线 Capture；不采集 Voxel GI 基线。

### 完成条件

- 两类场景可以一条命令运行确定性截图与性能采集。
- Dashboard 能显示后端能力、历史失效原因、各 Pass GPU 时间和资源占用。
- 不支持功能的 UI 状态、日志错误码和自动测试一致。
- 产品源码、构建项、设置、UI、Shader 和 Render Graph 中的 Voxel GI 运行时引用为 0。

## 4. M1：统一灯光与 PBR IBL

### 任务

1. 固化 `GpuMaterial` 与 glTF/LabPBR 规范化。
2. 抽取主光栅/次级命中共享材质与 BRDF Include。
3. 升级 GBuffer 的 RGB F0、Stable ID 和 Material ID。
4. 建设 `GpuLight`、Cluster Build、Deferred/Forward+ 读取。
5. 接入体素发光方块代理和 glTF `KHR_lights_punctual`。
6. 建设局部灯 Shadow Atlas/Cube Array。（实现完成：稳定 Light ID 分配、Spot Atlas、
   Point Cube Array、缓存修订、Deferred/Forward+ 共享采样、Debug View、契约测试、
   双后端 RHI 测试与 Vulkan Validation 均已接入；V07/M03 场景验收待对应版本化资产落地。）
7. 生成 Sky Cubemap、GGX Prefilter Mips、DFG LUT。（实现完成：动态天气天空生成 128×128
   HDR Cubemap，构建 8 级 GGX 预过滤链与 256×256 Split-sum DFG LUT；Reflection Pass 已按
   Roughness/NoV 消费并提供 Mip/DFG Debug View；天空修订使用双代资源，Radiance 整体快照后
   每帧更新一个 Prefilter Face/Mip，完整 48 项成功提交后原子切换。V01/M01/M02 参考图验收
   待对应版本化资产落地。）
8. 建设 Reflection Probe 数据与 Box Projection。

### 完成条件

- 方块材质和 glTF 材质在同一灯光阵列下能量响应一致。
- 体素火把/灯笼与模型局部灯均通过 Clustered Lighting 和阴影验收。
- Roughness/Metallic 扫描在天空与室内 Probe 中连续。
- 主视图与共享 Material Sampling 的 Reference Pixel 一致。

## 5. M2：Bindless GPU Scene 与 AS 基础

### 任务

1. Vulkan Shader Target 升级到 Vulkan 1.3 / SPIR-V 1.6。
2. RHI Descriptor Array、Binding Flags、批量更新与生命周期。
3. Global Bindless Set、Material/Geometry/Instance Buffers。
4. RHI AS Handle、Build Size、Build/Copy/Barrier 与 Device Address。
5. Vulkan AS Feature/Extension 加载、函数指针与延迟销毁。
6. 体素 Render Chunk/SubChunk BLAS Build/Compaction/Revision。
7. glTF Static Mesh BLAS 共享和 TLAS Instance。
8. AS Smoke Test、Cutout Candidate Test 与显存统计。

### 完成条件

- Vulkan Validation 无 AS/Descriptor/同步错误。
- 方块编辑、区块流送、模型实例增删不会产生悬空 Device Address。
- TLAS Debug View 与光栅几何逐对象重合。
- OpenGL 编译和基础渲染测试继续通过，公共 RHI 不暴露 Vulkan 类型。

## 6. M3：RTGI 与 NRD

### 任务

1. RTGI Compute Ray Query、Blue Noise/Cosine Sampling。
2. 体素 Greedy Primitive Metadata 与 Cutout Alpha Candidate。
3. 模型 Geometry/Material 次级命中读取。
4. 次级太阳、局部灯、Emissive、天空 Radiance。
5. Raw Diffuse Radiance + First-bounce Hit Distance，并按 RELAX/REBLUR 规范分别打包。
6. NRD 4.17.4 Build、License、RHI Pipeline 与 Render Graph Bridge。
7. RELAX_DIFFUSE Quality、REBLUR_DIFFUSE Performance。
8. Non-jittered Matrix、2.5D Motion、Pre-exposure 转换和 History Reset。
9. RTGI/NRD Debug View、Timestamp、Reference Capture。

### 完成条件

- 体素洞穴间接光不随屏幕朝向消失。
- 模型 Sponza/Helmet 的次级材质、Emissive 与法线正确。
- NRD 达到验证矩阵的方差、拖影和 GPU 时间门槛。
- Vulkan Modern 的间接漫反射只来自 RTGI，不混入 SSGI。

## 7. M4：GPU Culling、LOD 与动画

### 任务

1. Instance/Geometry/Material Dirty Range Upload。
2. Frustum、Distance、Hi-Z Occlusion 与 Visibility History。
3. LOD Selection、Meshlet Data 与 Compute Culling。
4. GBuffer/Shadow/Transparent/Probe 的 Indirect Command。
5. 体素 `WorldRenderBuffer` 纳入统一可见列表。
6. `ModelSceneRuntime` 移除逐实例 Primitive 提交。
7. glTF LOD/Meshlet 构建产品。
8. Compute Morph/Skinning、双帧顶点与 BLAS Update。

### 完成条件

- 1000 模型实例 CPU Submission 不随 Primitive Draw 数线性增长。
- 体素高速移动和区块流送无可见列表、Descriptor 或 AS 代际错误。
- LOD、Occlusion、动画速度和 RT Geometry 通过调试 Overlay。

## 8. M5：多层透明与现代反射

### 任务

1. PPLL Head/Node/Counter、Gather 与容量诊断。
2. 每像素排序、Closed Volume Interface Pairing 与厚度。
3. Back-to-front Ping/Pong Layer Resolve。
4. Rough Transmission、Beer-Lambert、Total Internal Reflection。
5. 方块玻璃/冰/水与 glTF Transmission/Volume 共用核心。
6. Transparent Velocity、Reactive/Composition Mask。
7. Reflection Probe Grid 更新与统一反射合成。
8. 独立 RT Reflection + NRD Specular Method。

### 完成条件

- 多层玻璃顺序与 CPU Primitive 顺序无关。
- 体素连续玻璃、水下观察和模型厚玻璃都呈现真实厚度与层间吸收。
- 透明运动时无持续时域拖影。
- 节点/单像素容量不足产生准确错误与热图。

## 9. M6：动态分辨率、HDR 与优化

### 任务

1. Resource/Render/Signal/Output Extent 分离。
2. GPU Timestamp 驱动 Dynamic Resolution Controller。
3. 完整运动矢量与 Temporal History Registry。
4. AP1 工作色域、Pre-exposure 与历史校正。
5. SDR、HDR10、scRGB Swapchain 和 HDR Metadata。
6. Frame Generation 与 HDR Format/Color Space 契约。
7. AS/NRD/透明/GPU Scene 显存与 Transient Aliasing 优化。
8. Nsight/RenderDoc Capture 驱动的 Shader 和 Queue 优化。

### 完成条件

- RTX 4060 Laptop 达到参考性能预算。
- 动态分辨率稳定收敛且不改变选中的渲染算法。
- SDR/HDR 色彩、亮度与 UI 合成通过数值和实机验证。
- 8GB 显存场景没有预算超订与瞬时资源生命周期错误。

## 10. M7：双场景发布验收

### 任务

- 运行完整 Validation Matrix。
- Linux Vulkan/OpenGL 与 Windows Vulkan 构建测试。
- RTX 4060 Laptop 长时运行、Resize、Fullscreen、World Reload、Asset Reload。
- NRD License/Notice、第三方版本和构建开关检查。
- 更新设置 UI、Dashboard、截图与发布说明。

### 完成条件

所有 Must 条目在体素世界和模型场景均通过；无 Validation Error；无 PPLL、Bindless、AS、
Cluster 容量错误；性能与显存达到预算；OpenGL 能力边界与 Vulkan Modern 可用原因准确。

## 11. 风险与控制

| 风险 | 观测信号 | 工程控制 |
| --- | --- | --- |
| 区块编辑造成 AS 构建尖峰 | Build Queue、Scratch、p95 AS ms | SubChunk 粒度、Compaction、每帧公开预算 |
| Cutout 射线成本过高 | Candidate/Confirmed 比、Trace ms | Opaque/Cutout BLAS 分类、Ray Cone LOD、资产 Alpha Coverage |
| NRD 拖影 | History Length、Disocclusion、动态边缘差分 | 完整速度、Stable ID、Reset Reason、Method 参数测试 |
| PPLL 显存过高 | Node 峰值、Layer Heatmap | 场景统计驱动容量、紧凑节点、明确 Quality 配置 |
| Bindless 悬空引用 | Generation Error、Validation | Submission Token、发布代际、延迟槽复用 |
| HDR 颜色错误 | Reference Pixel、Gamut Heatmap | 单一工作色域、显式 Output Transform、实机验证 |
| 双场景实现分叉 | Shader/Pass 数、Validation Matrix 缺项 | 共享 Material/Light/Scene 契约、双场景阶段门禁 |
| 8GB 显存压力 | Budget、Peak、Eviction Queue | AS 压缩、Transient Aliasing、资产 Residency 状态机 |

## 12. 每轮提交要求

每个可验证增量独立提交，提交消息遵循：

```text
<type>(<scope>): <中文 subject>
```

示例：

```text
feat(rhi): 增加Vulkan加速结构资源与构建命令
feat(rtgi): 接入NRD漫反射时空降噪
test(renderer): 增加体素与模型RTGI验收场景
```
