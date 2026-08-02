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
   双后端 RHI 测试与 Vulkan Validation 均已接入；V07 与 M03 已完成场景契约 v2、
   OpenGL/Vulkan 正式参考图与 Vulkan Validation 验收。M03 锁定三个 Point Light、一个
   Spot Light、缓存/动态阴影策略和 Emissive 灯具。）
7. 生成 Sky Cubemap、GGX Prefilter Mips、DFG LUT。（实现完成：动态天气天空生成 128×128
   HDR Cubemap，构建 8 级 GGX 预过滤链与 256×256 Split-sum DFG LUT；Reflection Pass 已按
   Roughness/NoV 消费并提供 Mip/DFG Debug View；天空修订使用双代资源，Radiance 整体快照后
   每帧更新一个 Prefilter Face/Mip，完整 48 项成功提交后原子切换。V01 使用确定性单窗房间
   Fixture，M01 使用 4×5 Metallic/Roughness/IOR/Clearcoat 材质球阵列，M02 锁定 Damaged Helmet；
   三个场景均完成场景契约 v2、1280×720 双后端正式参考图与内容 Hash 验收。）
8. 建设 Reflection Probe 数据与 Box Projection。（基础契约完成：固定 96 字节 CPU/GPU
   数据布局、结构化输入校验、稳定 Top-4 权重选择、Box Projection CPU/GLSL 参考实现已落地；
   运行时已接入固定 16 米单元格 Grid、稳定 ID 排序的紧凑候选表、事务式 GPU Buffer 上传、
   Prefiltered Cube Array 资源契约、Reflection Pass Box Projection/Top-4 消费，以及 Probe ID/
   Weight 调试视图。Capture 运行时已建立 128×128 HDR Radiance/Prefilter Cube Array、每个
   Probe 双槽代际，以及按 6 个 Radiance Face + 48 个 Prefilter Face/Mip 排列的确定性队列；
   只有完整 54 项提交成功后才切换活动槽。模型场景已接入版本化场景文件中的手工 Probe 与
   规则网格数据源及 RGBA16F 六面绘制器，复用 glTF Material Sampling、主环境直接光和完整
   模型局部灯快照；编辑器支持添加、删除、空间参数编辑和按场景包围盒生成固定顺序网格，
   运行时为每个文档 ID 分配独立稳定捕获 ID 与修订。体素场景已建立按加载区域 AABB 生成固定
   z/y/x 顺序源列表的独立契约，并严格校验容量、稳定 ID、修订和 GPU Probe 输入。运行时已接入
   单个 16 米相机单元流式 Probe：相机跨单元、活动区块集合或方块内容修订变化会创建新的捕获
   代际，影响盒保持当前单元，Box Projection 与捕获远平面覆盖已加载区块 AABB。地形 Capture
   Renderer 使用独立六面 View/ViewProjection 重新收集区块。每个 Radiance Face 已拆为不透明
   绘制、颜色/深度复制和透明合成三个 Render Graph 节点：opaque/cutout 与场景实体先写入
   128×128 RGBA16F + Depth32 目标，再复制出可采样的不透明快照，最后保持玻璃与水面网格按
   探针距离从远到近统一混合。Capture 使用独立地形 PBR 管线，复用 LabPBR 法线/高度/Specular、
   Biome Tint、Wetness 和统一 GGX/Lambert 直接光，并将体素 `GpuLight` 快照按探针相对坐标上传；
   水体已接入 DerivativeMain 波高视差、波面/雨滴法线、IOR Fresnel、屏幕空间折射，以及基于
   不透明深度重建光学距离的 Beer-Lambert 吸收与散射。OpenGL/Vulkan `m0_voxel_baseline`
   120 帧预热加 60 帧采样已通过，Vulkan Validation 无报错。方块实体、掉落物、下落方块、
   活塞移动方块和生物/远端角色使用各自 RGBA16F Forward 管线进入六面捕获；下落方块与活塞
   移动方块复用同一实例收集及方块纹理、Biome Tint、动画帧、体素光照路径，本地第一人称玩家
   不进入环境探针。体素静态 glTF 已复用模型场景 Probe Capture 管线，不透明、Alpha Test、
   Alpha Blend 与 Transmission Primitive 均保留 glTF PBR 材质、主环境直接光、Emission 和完整
   `GpuLight` 快照。V01、V02、V07、M01、M02、M03、M07 已完成场景契约 v2、1280×720
   双后端正式参考图和内容 Hash 锁定，统一清单共包含 18 个捕获项；Vulkan 捕获未发现
   Validation/VUID 错误。粒子不纳入本轮版本化质量验收。Dashboard 与模型场景 Reflections 面板已接入队列
   深度、当前工作项、代际和槽位展示。）

### 完成条件

- 方块材质和 glTF 材质在同一灯光阵列下能量响应一致。
- 体素火把/灯笼与模型局部灯均通过 Clustered Lighting 和阴影验收。
- Roughness/Metallic 扫描在天空与室内 Probe 中连续。
- 主视图与共享 Material Sampling 的 Reference Pixel 一致。

## 5. M2：Bindless GPU Scene 与 AS 基础

### 任务

1. Vulkan Shader Target 升级到 Vulkan 1.3 / SPIR-V 1.6。（实现完成：glslang 客户端与
   输出目标已固定为 Vulkan 1.3 / SPIR-V 1.6，双后端宏编译路径均锁定 SPIR-V Header；
   OpenGL Fragment 的 `discard` 使用可交叉编译的终止指令，Vulkan 保留 SPIR-V 1.6 的
   Demote 语义；Vulkan 设备选择、创建与能力表已强制启用
   `shaderDemoteToHelperInvocation`。Shader Compiler、OpenGL RHI Core、Vulkan RHI Smoke
   与双后端基础启动验证均通过，Vulkan 未发现 Validation/VUID 错误。）
2. RHI Descriptor Array、Binding Flags、批量更新与生命周期。（实现完成：公共 RHI 已支持
   Descriptor Array、Binding Flags 与多 Bind Group 连续区间批量更新；OpenGL/Vulkan 均执行
   整批原子校验，并锁定录制、Submission Pending、完成后的更新与资源生命周期规则。Vulkan
   更新资源进入延迟释放，描述符句柄复用保持代际安全。）
3. Global Bindless Set、Material/Geometry/Instance Buffers。（底层实现完成，Binding 4 运行时发布完成：
   已冻结强类型 Bindless Handle、Submission Sequence 感知的槽位代际分配器，以及 192 字节
   Instance、128 字节 Geometry 的 CPU/GLSL 固定布局与严格规范化规则。Vulkan Global Set
   已实现固定容量的 2D Texture、Cube Texture、Sampler、Storage Buffer 数组，并将 Binding 4
   正式接入固定 TLAS Descriptor；Scene Buffer 已实现 GPU-only 固定容量表、连续 Dirty Span 上传、
   Submission Token 确认、Revision 保护与整批原子退役。真实 Shader 编译和 Vulkan Compute Smoke
   已覆盖非一致索引读取、回写与代际复用。Gameplay `RenderScene` 与 Model Scene Deferred 现已在
   Vulkan 创建同一契约的 Global Set，并于帧开始发布最新完成的 Active TLAS；Dashboard 展示发布
   Revision、Descriptor 更新次数和各数组占用，真实 Cutout Ray Query Smoke 也通过该 Global Set
   读取 Binding 4，Validation 未发现错误。`SceneTlasCache` 现已按 TLAS 代际展平唯一模型资产的
   Material/Geometry 表，并生成与 Custom Index 一一对应的 Instance 表；共享资产的多个实例只增加
   Instance 记录。生产 `RtgiTracePass` 已在 Binding 8/9/10 消费这三张表和 Global Bindless 模型纹理。
   面向 GPU Culling/Indirect Draw 的常驻 Scene 表与资产注册表仍属于 M4。）
4. RHI AS Handle、Build Size、Build/Copy/Barrier 与 Device Address。（实现完成：公共 RHI 已提供
   强类型 AS Handle、BLAS/TLAS、Triangles/AABBs/Instances、Build/Update、Clone/Compact、精确
   Build Size、Buffer/AS Device Address、AS Barrier、Compacted Size Query，以及固定 64 字节
   TLAS Instance 布局。几何范围、地址与偏移对齐、批量 Build 的 Source/Destination 交叉和 Scratch
   重叠均执行整批校验；AS Descriptor 只接受 TLAS。）
5. Vulkan AS Feature/Extension 加载、函数指针与延迟销毁。（实现完成：设备初始化强制验证并启用
   Acceleration Structure、Ray Query、Deferred Host Operations 与 Buffer Device Address；已加载
   Create/Destroy、Build Size、Device Address、Build、Copy、Property Query 函数指针。AS 注册表、
   Submission Sequence 引用盖章、Backing Buffer 依赖传播、AS 先于 Buffer 的延迟释放顺序，以及
   OpenGL 对 AS Buffer/Descriptor/资源/命令/Shader 资源的明确拒绝均已落地。）
6. 体素 Render Chunk/SubChunk BLAS Build/Compaction/Revision。（实现完成：新增独立
   `TerrainBlasCache`，以 SubChunk 局部 `BlockVertex` 三角形生成 Opaque 与非 Opaque Cutout
   Geometry，Water/Transparent 不进入首版 Solid Mask；RT Geometry Buffer 固定具备 Storage、
   Device Address、AS Build Input 与 Transfer Dst 用途。调度器按请求序号和 SubChunk Stable Key
   确定性执行每帧 Build 数、Geometry 字节、Primitive 数及 Compaction 数预算，超预算首任务可独占
   一帧。Build、Compacted Size Query、Compact Copy、Submission Token、查询槽隔离与延迟销毁已形成
   完整状态机；新 Revision 压缩完成前保留旧 Active BLAS，完成后原子换代，空网格和区块卸载会
   明确退役对应资源，Graph 失败保留 CPU Geometry 并按同一算法重新录制。Dashboard 已显示 Active、
   Pending、Primitive、Geometry/Primitive Metadata/BLAS/Scratch 字节；固定 16 字节 Primitive Metadata、
   Geometry Index 范围与 Device Address 已纳入压缩 BLAS 生命周期。纯契约测试与 Vulkan 生产缓存 Smoke
   已覆盖输入校验、确定性顺序、Metadata 回读、Build/Compact、Revision 换代和卸载，Validation
   未发现错误。）
7. glTF Static Mesh BLAS 共享和 TLAS Instance。（实现完成：新增共享 `SceneBlasResource`，由
   TLAS 代际共同持有 BLAS、Backing Storage、Geometry 与 Primitive Metadata Buffer 生命周期；
   `StaticMeshBlasCache`
   将同一 glTF 资产的 Opaque 与 Alpha Mask Primitive 构建为多 Geometry 压缩 BLAS，Blend 与
   Transmission 不进入 Solid BLAS，多个 ECS 实例共享同一资产 BLAS。`SceneTlasCache` 按稳定
   Instance Key 排序，分配唯一 24-bit Custom Index，并固定 GI Opaque/Cutout、Shadow、Reflection
   与 First Person Mask；Transform、CCW、Double-sided、空场景退役、Graph 提交失败和连续换代均
   使用 Desired/Pending/Active/Retired 状态机。Gameplay Deferred/Forward 与 Model Scene Deferred
   已接入运行时 TLAS；Dashboard 展示 Instance、唯一 BLAS、TLAS/BLAS 字节、Revision 与构建计数。
   OpenGL 保持明确 Unsupported。契约测试、Vulkan Smoke、Damaged Helmet 与 Sponza 场景验收均已
   通过，Validation 未发现错误。）
8. AS Smoke Test、Cutout Candidate Test 与显存统计。（实现完成：AS、体素/glTF 生产缓存、运行时
   TLAS 与显存统计均已覆盖。Vulkan 已验证真实三角形 BLAS Build/Update、Compacted Size、
   Clone/Compact、多 Geometry Static BLAS、共享 BLAS 的多 TLAS Instance、Transform 换代、空场景
   退役、Binding 4、Shader Reflection、体素 Revision 原子换代、卸载和延迟销毁。真实 Compute Ray
   Query Smoke 进一步覆盖 Opaque 自动提交、Cutout Candidate 拒绝/显式确认，以及 Instance Custom
   Index、Geometry Index、Primitive ID、Barycentrics 回读，Validation 未发现错误。统一 Alpha
   Cutoff、动画层选择、正式 Terrain Primitive Metadata 与每 Custom Index 64 字节 TLAS 代际命中表
   已在 M3 接入。生产 `RtgiTracePass` 的 2×1 Vulkan Smoke 已真实覆盖 Barycentric UV、动画纹理层、
   Texture2DArray Alpha 采样、Ray Cone LOD 计算，以及 Cutout 拒绝后命中后方 Opaque 与显式确认
   Cutout 两条路径。Static Mesh Smoke 进一步覆盖 48 字节顶点、Uint32 Index、16 字节三角形 Metadata、
   两张 `GpuMaterial`、Global Bindless Texture/Sampler、共享资产表去重、三张 GPU Scene 表逐字节回读，
   以及模型 Alpha Mask 拒绝后命中 Opaque 与显式确认两条路径。）

### 完成条件

- Vulkan Validation 无 AS/Descriptor/同步错误。
- 方块编辑、区块流送、模型实例增删不会产生悬空 Device Address。
- TLAS Debug View 与光栅几何逐对象重合。
- OpenGL 编译和基础渲染测试继续通过，公共 RHI 不暴露 Vulkan 类型。

## 6. M3：RTGI 与 NRD

### 任务

1. RTGI Compute Ray Query、Blue Noise/Cosine Sampling。（进行中：Global Bindless Binding 4 的双运行时
   所有权与 Active TLAS 发布已完成；生产 `RtgiTracePass`、确定性帧旋转、Blue Noise/Cosine
   Sampling、Opaque 自动提交、Terrain Cutout Candidate Confirm、Render Graph 资源声明、逐像素
   Candidate/Confirmed 计数、模型 Stable Material/Geometry Hash、真实 Vulkan 命中距离回读及完整
   次级命中材质/辐射已完成。Deferred 运行时消费和 NRD 输入打包尚未完成。）
2. 体素 Greedy Primitive Metadata 与 Cutout Alpha Candidate。（实现完成：新增 C++/GLSL 体素材质
   采样契约，固化包含边界的 `0.1` Alpha Cutoff、NaN/Inf 拒绝、1024 层纹理编码与
   6-bit 动画帧数/FPS 上限；GBuffer、主视图、Probe Capture 与 Shadow 的非 Leaves Cutout 已共用
   同一 Alpha Test 及动画层选择，Leaves 保持实心投影。固定 16 字节 Terrain Primitive Metadata、
   Opaque/Cutout Primitive 顺序、BLAS Geometry Index 到 Vertex/Primitive Base 映射、Vertex/Metadata
   Device Address 及 TLAS Active Generation 精确快照已完成。Shader 通过 Physical Storage Buffer
   读取固定 32 字节 `BlockVertex` 和 Primitive Metadata，以 Barycentrics 重建 UV，按像素世界覆盖、
   射线距离和三角形 UV 梯度计算 Ray Cone LOD，选择动画纹理层并执行统一 Alpha Test。Vulkan 已覆盖
   Metadata/命中表回读、Terrain BLAS/TLAS 双代生命周期及生产 Candidate 拒绝/确认结果。）
3. 模型 Geometry/Material 次级命中读取。（实现完成：Static BLAS Geometry Index 已映射到
   `GpuSceneGeometry`，TLAS Custom Index 已映射到 `GpuSceneInstance`；每代 TLAS 对唯一资产展开
   `GpuMaterial` 与 Geometry，对共享实例仅追加 Instance。Shader 通过 Device Address 读取固定
   48 字节 Position/Normal/Tangent/UV 顶点、Uint32 Index 与 16 字节 Metadata，以 Barycentrics
   重建属性并校验 Stable Material/Geometry ID；Alpha Mask 通过 Ray Cone LOD 读取 Global Bindless
   Base Color，应用 Base Color Factor 与统一 Alpha Test 后显式确认 Candidate。Committed Surface
   已使用逆转置法线变换和镜像/非均匀缩放安全的 Tangent Frame 采样全部 12 个材质语义，输出
   Base Color、Normal、Metalness、Roughness、AO、Emissive、F0 与稳定身份。2×1 Vulkan Smoke 已验证
   左像素拒绝 Mask 后命中后方 Opaque、右像素确认 Mask，并回读两种稳定身份 Hash、Hit Distance 和
   次级材质辐射。）
4. 次级太阳、局部灯、Emissive、天空 Radiance。（实现完成：新增固定 16 米 Cell 的
   Camera-relative `WorldLightGrid`，Directional Light 进入全局索引前缀，Point/Spot/Rect 按精确
   Sphere/AABB 相交进入排序稀疏 Cell；Clustered Consumer Set 的 Binding 7/8/9 发布 Cell、Index 与
   Header，总索引固定限制为 262144，超限时原子失败。Terrain Committed Surface 已接入顶点天光/AO、Biome/Redstone Tint、LabPBR Normal/
   Specular/Emission，模型 Committed Surface 已接入完整 glTF 材质。太阳/月亮使用 Alpha-aware
   Shadow Ray，局部灯的 None、Raster Dynamic/Cached 生产资源路径与 Ray Query Shader 分支均已求值；
   Ray Query 分支已通过生产 Shader 编译和源码契约，`SceneLight` 生产入口仍与主视图契约一致地显式
   返回 `RayQueryUnavailable`。命中表面累加 Emissive、直接光和天空环境项，Miss 直接采样 Sky Capture。Vulkan Smoke 已用 RGB 通道分别锁定
   红色 Emissive、绿色太阳、蓝色 Point Light，并验证 Terrain Grass Tint、天空环境项及固定天空
   Radiance Miss；CPU/GLSL World Grid、UBO 和材质源码契约测试已通过。）
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

当前已锁定 V01、V02、V07、M01、M02、M03、M07 七个场景契约 v2；各场景均以 300 帧预热、
3 帧采样生成 OpenGL/Vulkan 1280×720 正式参考图，连同两个 M0 v1 基线形成 18 项清单，
并按场景契约版本、Camera Path、渲染设置、FNV-1a 64 和 SHA-256 锁定。该结果不代表完整
Validation Matrix、Windows 平台和长时性能门禁已经完成；粒子捕获不属于本轮范围。

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
