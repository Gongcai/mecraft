# GPU Scene 与资产管线

## 1. 目标

当前体素地形已经使用池化 Buffer 与 Multi-Draw Indirect，模型路径则由 CPU 遍历 ECS
实例、逐 Primitive 调用 Renderer。现代管线需要把两者统一为 GPU Scene：CPU 负责提交
场景修订，GPU 负责实例/网格剔除、LOD 选择、可见列表和间接命令。统一后的数据同时
服务光栅、阴影、RTGI、反射探针和透明链。

## 2. 数据分层

```text
Asset Source
  ├── glTF Mesh / Material / Texture / Skin / Animation
  └── Block Definition / Texture Array / Chunk Mesh
          │
          ▼
Canonical Asset Data
  ├── Mesh Geometry + Primitive Metadata
  ├── GpuMaterial + Bindless Texture Handles
  ├── LOD Group + Meshlets
  └── Skeleton / Morph / Animation Clips
          │
          ▼
Resident GPU Resources
  ├── Vertex/Index/Meshlet Buffers
  ├── Texture/Sampler Descriptors
  ├── BLAS
  └── Generation Handles
          │
          ▼
Frame GPU Scene
  ├── Instance / Previous Transform
  ├── Geometry / Material / Light Tables
  ├── Visible Instance + Meshlet Lists
  └── Indirect Draw + TLAS Instance Buffers
```

资产数据与帧实例数据分离。相同模型的千个实例共享 Mesh、Material、Texture 和 BLAS，
只新增 Instance 记录。

## 3. Bindless 资源

### 3.1 Descriptor Array

Vulkan Global Bindless Set 保存 Sampled Image、Sampler 和 Storage Buffer 数组。材质表只
存 32-bit Index，不为每个 Primitive 创建 Bind Group。必要特性包括：

- Runtime Descriptor Array。
- Partially Bound。
- Variable Descriptor Count 能力；Global Set 的 Binding 0–3 不设置该 Binding Flag。
- Update-after-bind 与 Update-unused-while-pending。
- Sampled Image/Storage Buffer Non-uniform Indexing。

Vulkan 每个 Descriptor Set 只允许最高编号的一个 Binding 使用 Variable Descriptor Count。
Global Set 的 Binding 4 已冻结给 TLAS，因此 2D Texture、Cube Texture、Sampler 和 Storage
Buffer 四个数组均使用初始化时确定的固定描述符数量，Shader 保持运行时非一致索引。
容量创建前必须校验 Descriptor Set 与 Per-stage Update-after-bind 上限。

公共 RHI 已支持 Descriptor Array、首数组元素、Binding Flags 和多 Bind Group 连续区间的
整批原子更新。Vulkan `GlobalBindlessSet` 已完成四类资源的强类型发布和生命周期管理；
OpenGL 明确不创建该全局集合。

### 3.2 句柄生命周期

资源索引使用 `index + generation`：

1. Asset Registry 分配槽并创建 GPU 资源。
2. 材质发布后才能进入可见 GPU Scene。
3. 销毁先发布不再引用该槽的新一代 Material/Scene Buffer。
4. 等待最后 Submission Token 完成。
5. 增加 Generation 后复用槽。

纹理流送不允许把未 Resident 的索引放入可见材质。Residency 状态是资产状态机的一部分，
不是 Shader 内的条件替换。

### 3.3 体素纹理数组

方块 Albedo/Normal/Specular Texture Array 继续作为高效资源产品，每个数组只占一个
Bindless Image Slot，Material/Primitive Metadata 保存 Layer Index。Biome Tint、动画帧和
Greedy UV Repeat 仍由体素材质采样器处理，最终输出统一 PBR 参数。

## 4. GPU Scene Buffer

帧资源至少包括：

| Buffer | 内容 | 更新方式 |
| --- | --- | --- |
| Instance | 当前/上一帧 Transform、Bounds、Geometry Range、Stable ID | Dirty Range Upload |
| Geometry | Device Address、Index Range、Material、Meshlet Range | 资产修订上传 |
| Material | PBR 参数与 Bindless Index | Dirty Range Upload |
| Light | 统一局部灯 | Dirty Range Upload |
| Visible Instance | 剔除结果 | GPU 写 |
| Visible Meshlet | Meshlet 剔除结果 | GPU 写 |
| Indirect Command | GBuffer/Shadow/Transparent 等 Draw Command | GPU 写 |
| Draw Count | 每类可见命令数量 | GPU 写 |
| TLAS Instance | Ray Tracing Instance | GPU 或 CPU 写，AS Build 读 |

Material、Geometry 和 Instance 底层表采用固定容量与增量 Dirty Range。CPU 不每帧重写
静态 Material/Geometry。帧生成类 Buffer 的环形资源由运行时剔除链接入时建立。所有表都
具有容量、使用量、峰值和结构化容量错误。

### 4.1 当前底层实现边界

`GpuSceneBufferSet` 已创建 Material、Geometry、Instance 三个 GPU-only Storage Buffer，
并把它们发布到 Vulkan Global Bindless Storage Buffer Array。每张 CPU 表将写入合并为一个
连续 Dirty Span；上传录制后必须由有效 Submission Token 确认，累计上传字节与 Dirty 状态
才会提交。录制完成后发生的同索引写入通过 Revision 保留到下一批上传，命令录制或提交
失败时可显式丢弃录制批次而不清除 Dirty 数据。三个 Storage Buffer 的退役先执行整批
预校验，任一重复、失效或陈旧句柄都会拒绝整批状态变更。

当前实现尚未连接体素区块、模型实例和资产注册表到 Material/Geometry/Instance 三张 GPU Scene
表，也尚未生成 Visible、Indirect 与 Draw Count Buffer；这些运行时链路属于 M4。

独立的 CPU TLAS 生产链已经完成：`SceneTlasCache` 将 Terrain 与 Static Mesh 快照规范化为固定
64 字节 Instance Buffer，按稳定 Key 分配 24-bit Custom Index，并通过 Desired/Pending/Active/Retired
代际管理提交、换代和卸载。Active 代际只保留唯一 BLAS 引用，Dashboard 可读取 Instance 数、唯一
BLAS 数、TLAS/BLAS 字节、Revision 与 Build 计数。Gameplay 与模型场景的 Vulkan 运行时所有者现已
创建 Global Bindless Set，并在帧开始把最新完成的 Active TLAS 发布到固定 Binding 4；Dashboard 同时
报告发布 Revision、Descriptor 更新次数和四类数组占用。OpenGL 明确不创建该集合。生产
`RtgiTracePass` 已绑定该 Global Set 并从 Binding 4 读取 Active TLAS；Scene Buffer 与次级材质
消费继续由 M3 接入。

## 5. GPU Culling 与间接绘制

### 5.1 Pass 链

```text
InstanceUpload
      │
      ▼
FrustumAndDistanceCull
      │
      ├── LOD Selection
      ├── Hi-Z Occlusion Test
      └── Visibility History Update
      │
      ▼
MeshletCull / PrimitiveGroupCull
      │
      ├── GBuffer Indirect Commands
      ├── Shadow Cascade Indirect Commands
      ├── Transparent Gather Commands
      └── TLAS Instance Buffer
```

RHI 增加 `drawIndexedIndirectCount`，Vulkan 映射到 Core 1.2 的 Indirect Count 能力。每类
Draw 以 Pipeline/Material Class 分桶，Bindless Material ID 解除逐材质 Bind Group 切换。

### 5.2 遮挡历史

Hi-Z 使用上一帧深度进行 Instance Occlusion，当前帧对新出现、快速移动、相机切换和
历史失效实例保持可见标记。Occlusion History 由 Stable Object ID 索引。剔除统计包含
Frustum、Distance、Occlusion、LOD 和最终 Draw Count。

### 5.3 体素区块

现有 `WorldRenderBuffer` 的 Buffer Pool 与 MDI 是良好基础，改造重点是：

- 区块 Bounds、Mesh Class、Buffer Range 和 Revision 写入 GPU Scene。
- Compute 生成各 Mesh Class 的 Indirect Command，而不是 CPU 整理全部可见命令。
- GBuffer、CSM、Reflection Probe Capture 和 TLAS 共用同一可见区块产品。
- Buffer Pool 搬迁导致 Device Address 改变时增加 Geometry Revision 并重建相关 BLAS。
- 区块上传与 AS Build 由 Submission Token 和 Render Graph Dependency 串联。

当前体素 BLAS 与 TLAS Instance 生产缓存已完成。光栅 `PackedBlockVertex` Pool 保持现有职责，光追侧为每个
SubChunk 独立保留完整 `BlockVertex` Geometry Buffer，因此光栅 Pool 扩容不会改变 BLAS Build
Input Address。Mesher Revision、Build/Compaction 状态、Submission Token、原子换代和卸载已经
串联；Active SubChunk BLAS 按 Chunk Key 与 Section Y 稳定排序，并以世界偏移生成 TLAS Transform。
GPU Scene Geometry 注册与可见区块产品复用仍属于 M4。

### 5.4 模型实例

`ModelSceneRuntime` 不再对每个 Entity 调用 `renderToGBuffer`/`renderToShadowMap`。它把
Transform、Previous Transform 和 Asset Handle 写入 Instance Buffer，GPU 产生 Draw。
同一资产的 Primitive 通过 Geometry Range 展开，实例数量只影响 Instance Buffer 与
可见命令。

当前资产级光追生产层已经完成。每个 `StaticMeshRenderer` 为 Opaque 与 Alpha Mask Primitive
分配稳定 Geometry ID，并构建一个多 Geometry、Fast Trace、压缩后的静态 BLAS；Blend 与
Transmission Primitive 保持在光栅/透明链，不进入 Solid BLAS。`ModelSceneRuntime` 为每个 ECS
实体提交独立 Transform、Stable Object ID 与 Scene Entity ID，多个实体共同引用资产 BLAS。
光栅提交仍保持现有逐实例路径，GPU Scene 与 Indirect Draw 改造属于 M4。

## 6. LOD 与 Meshlet

### 6.1 模型 LOD

每个 Mesh Asset 支持离线 LOD Chain，误差使用屏幕空间投影度量。LOD 选择加入滞回区间，
避免边界抖动；切换阶段可用 Dithered Transition，并正确写 Reactive Mask。

LOD 生成保留：UV Seam、Hard Normal、Skin Weight、Morph Target、Material Boundary 和
Alpha Mask 轮廓。每个 LOD 拥有独立 BLAS 或按资产策略共享可更新 BLAS，不能让光栅 LOD
与 TLAS Geometry 不一致。

### 6.2 体素 LOD

近距离区块保持当前完整 Greedy Mesh。远距离环可生成保持方块轮廓与材质分类的简化
Terrain LOD；其边界需要裙边或拓扑连续方案。体素 LOD 是独立里程碑，不影响近距离
GPU Scene 与 RTGI 上线。

### 6.3 Meshlet

离线把模型与区块 Mesh 划分为约 64 Vertices/126 Triangles 的 Meshlet，保存 Bounding
Sphere 与 Normal Cone。Compute Meshlet Culling 可在传统 Indexed Indirect Draw 中使用。

`VK_EXT_mesh_shader` 定义为独立 Vulkan Feature Mode；启用时使用 Task/Mesh Pipeline。
该模式不可用时设置项不可选，Indexed Indirect 仍是完整 GPU Scene 的正式模式，而不是
运行时算法替换。

## 7. glTF 资产处理

### 7.1 规范格式

glTF 2.0 是运行时规范格式。OBJ/FBX 等内容若继续支持，应在导入阶段转换为同一 Canonical
Mesh/Material/Skeleton 数据，运行时不保留格式分支。

导入管线执行：

1. 严格校验 Accessor、Buffer View、Index、Node Graph 与扩展。
2. 规范化坐标系、单位、Front Face 与 Transform。
3. 保留源 Tangent；缺少 Tangent 时使用 MikkTSpace 生成。
4. 生成优化后的 Vertex/Index Buffer、Vertex Cache/Fetched Order。
5. 规范化 PBR Material 与 Texture Transform。
6. 生成 LOD、Meshlet、Bounds 与 Ray Tracing Primitive Metadata。
7. 构建 Asset Manifest、内容 Hash 和版本号。

Importer 对不支持的 Required Extension 返回准确错误。Optional Extension 也不能被悄然
忽略；必须明确记录资产结果不包含该特性。

### 7.2 纹理

- Base Color/Emissive 按 sRGB 解码，其他 PBR Texture 按线性数据解码。
- Mip 生成对 Normal 执行向量重归一化，对 Roughness 采用能量合理的过滤。
- GPU 压缩格式由构建平台明确指定，Manifest 记录 BC/ASTC 等产品类型。
- Sampler Wrap/Filter/Anisotropy 完整映射。
- Texture Transform 在光栅、透明和 Ray Query Material Sampling 中一致。

### 7.3 材质扩展路线

已有 Metallic-Roughness、Specular-Glossiness、IOR、Clearcoat、Transmission、Volume
进入统一材质表。后续按以下依赖建设：

- Emissive Strength 与 Texture Transform：不改变 BRDF，直接纳入。
- Sheen：增加 Charlie/Visibility Lobe 与能量分配。
- Anisotropy：需要 Tangent Direction、Anisotropic GGX 与 IBL 支持。
- Diffuse Transmission：需要薄片双面漫透射和阴影语义。

每个扩展都要同时覆盖主 GBuffer/Forward、Shadow、Probe Capture 和 Ray Query Material
Sampling，不能只在模型预览 Shader 中生效。

## 8. 动画与变形

### 8.1 Animation Runtime

支持 TRS Channel、Linear/Step/Cubic Spline 插值，Skeleton Palette 写入 GPU Buffer。
Animation State 使用稳定时间线，暂停、跳转和循环边界产生明确的 Previous Pose。

### 8.2 Compute Skinning/Morph

Pass 顺序：

```text
AnimationEvaluate (CPU or Compute)
        │
        ▼
MorphTargets ─► Skinning ─► Current Deformed Vertex Buffer
        │                         │
        ├── Previous Buffer ──────┴──► Motion Vector
        └────────────────────────────► BLAS Update
```

变形 Buffer 使用双代资源，GBuffer 与 RTGI 在同一帧读取相同 Current Position。切换动画、
Skeleton 重建和 Asset Reload 时生成局部历史失效标记。

### 8.3 体素动画

方块纹理动画只改变 Texture Frame，不改变 BLAS。活塞、移动方块、方块实体和生物作为
动态 Instance 提供双 Transform；形变网格采用同一 Compute Deformation 契约。

## 9. Streaming 与预算

资产状态机：

```text
Unloaded → CPU Ready → Uploading → GPU Resident → AS Ready → Visible
```

状态转换由错误码和 Submission Token 驱动。Visible 只引用完整 Resident 的 Geometry、
Material 和 Descriptor。卸载按 Visible 移除、TLAS 移除、GPU 完成、资源释放的顺序执行。

预算项：

- Vertex/Index/Meshlet Buffer 字节数。
- Texture 各 Mip Resident 字节数。
- Bindless Image/Sampler/Buffer 槽数。
- BLAS 压缩前后字节数。
- TLAS Instance Buffer、TLAS Storage 与活动唯一 BLAS 字节数。
- Frame GPU Scene 与 Indirect Buffer 峰值。
- 上传队列字节数与每帧 Upload 时间。

## 10. 工具与调试视图

- Instance/Geometry/Material ID 可视化。
- Frustum/Distance/Occlusion/LOD 剔除原因。
- Meshlet Bounds/Normal Cone。
- Bindless Slot、Generation 与 Residency。
- TLAS Instance、Custom Index、Mask、Revision 与唯一 BLAS 共享量。
- Draw Count、Triangle Count、Pipeline Bucket Count。
- Skin/Morph Current-Previous Position 差。
- 光栅 Geometry 与 BLAS Geometry Overlay。

## 11. 验收

体素世界：高速飞行与区块流送无失效句柄；破坏/放置方块后光栅与 RT 几何同帧代一致；
现有 MDI 吞吐不下降；透明、阴影和 Probe Capture 可复用可见列表。

模型场景：1000 个 Damaged Helmet 实例由 GPU Scene 提交；Draw Count 与资产 Primitive
种类相关而非实例数线性增长；LOD/遮挡稳定；动画模型速度与 BLAS 一致；资产卸载不产生
Descriptor 或 Device Address 悬空引用。

共同标准：CPU Render Submission 时间、GPU Culling 时间、可见 Triangle 数、AS Instance
数和显存占用均可从 Dashboard 与自动化 Capture 读取。
