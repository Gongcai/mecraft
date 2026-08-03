# Vulkan RTGI 与 NRD

## 1. 技术结论

Vulkan 现代管线从现有 SSGI 路线直接转向硬件 RTGI，技术上成立，也更符合当前项目的
目标。世界空间 Voxel GI 不是 RTGI 的必要阶段：TLAS 本身已经表达世界几何，次级命中
着色能够直接读取材质、灯光和天空辐射。

现有 SSGI 的主要问题不只是滤波参数。屏幕外表面、被遮挡表面、背面和超出深度范围的
能量根本不存在于输入中，任何降噪器都无法恢复这些信息。RTGI 解决采样域，NRD 解决
低样本随机噪声、时域稳定与边缘保持，二者职责不同。

`VoxelGiClipmap` 已确认没有可见画质收益并造成性能下降，因此不保留在 OpenGL、Vulkan、
独立诊断或模型场景中。删除范围包含实现文件、CMake 源文件项、Render Graph Pass、3D
纹理与上传资源、Scene Composite Variant、设置序列化、默认配置、UI 控件和统计接口。
删除工作不要求 Reference Capture，也不要求输出兼容。Vulkan 现代预设只合成 RTGI；
OpenGL 基础功能集继续使用其明确列出的 SSGI/SSR 能力。

## 2. 首版范围

### 2.1 包含

- Vulkan 1.3 Compute Shader + `VK_KHR_ray_query`。
- 不透明与 Alpha Mask 几何的 BLAS/TLAS。
- 每像素或棋盘格单次 Diffuse Bounce。
- 天空 Miss、Emissive Hit、太阳和 Clustered Local Lights 的次级命中辐射。
- `RGB Diffuse Radiance + First-bounce Hit Distance` 逻辑信号，并按 NRD Method 分别打包。
- NRD 4.17.3 `RELAX_DIFFUSE` 与 `REBLUR_DIFFUSE`。
- 区块、静态模型、动态刚体实例的 AS 生命周期。
- 完整历史失效、诊断视图与 GPU 时间统计。

### 2.2 后续范围

- 蒙皮/Morph 变形 BLAS Update。
- 独立 Specular RT Reflection + NRD Specular Method。
- 多反弹路径、ReSTIR GI、辐射缓存与可见光斑。
- 透射路径中的多界面间接光、色散与焦散。

这些能力拥有独立设置、资源和验收项，不改变首版 Diffuse RTGI 的结果定义。

## 3. 总体数据流

```text
Chunk Mesh / Model Mesh / Dynamic Entity
            │
            ├── Geometry + Primitive Metadata + Material ID
            └── Current/Previous Transform + Stable Object ID
            │
            ▼
BLAS Build/Compact/Cache ──► TLAS Build
                              │
GBuffer + Depth + Blue Noise ─┴─► RTGI Trace
                                   ├── Reconstruct primary visible point
                                   ├── Sample diffuse direction
                                   ├── Ray Query + alpha candidate test
                                   ├── Hit material/direct radiance/emissive
                                   └── Miss sky radiance
                                         │
                                         ▼
                         Diffuse Radiance + First-bounce Hit Distance
                                         │
Normal/Roughness + View-Z + Motion ──────┴─► NRD RELAX/REBLUR
                                                │
                                                ▼
                                      Denoised Diffuse Indirect
                                                │
                                                ▼
                           Albedo/BRDF Modulation + Opaque HDR Composite
```

RTGI 在 Tonemap 和 Temporal Upscale 之前运行，所有辐射都处于线性、Pre-exposed HDR
域。NRD 输出不包含后处理 Bloom、Tone Map 或显示色域变换。

## 4. 加速结构架构

当前已完成 Vulkan AS 基础层：公共 RHI 提供 BLAS/TLAS、Triangles/AABBs/Instances、
Build/Update、Clone/Compact、Build Size、Device Address、Compacted Size Query 与 AS Barrier；
Vulkan 初始化强制启用 Acceleration Structure、Ray Query、Deferred Host Operations 和 Buffer
Device Address，并完成函数指针、强类型注册表、Submission Sequence 生命周期及延迟销毁。
Global Bindless Binding 4 已能发布 TLAS，Shader Reflection 也已识别 Acceleration Structure
Descriptor。真实三角形 BLAS、Update、Compaction、Clone、TLAS 和 Descriptor Smoke 已通过，
未发现 Validation/VUID 错误。

体素 Render Chunk/SubChunk BLAS 生产层现已接入：`TerrainBlasCache` 保留 SubChunk 局部
`BlockVertex`，将 Opaque 与 Cutout 分成不同 Geometry，执行预算化 Build、Compacted Size Query、
Compact Copy 和 Revision 原子换代；Submission Token 负责 Build/Compact 完成判定、查询槽隔离与
资源延迟销毁。新 Revision 就绪前旧 Active BLAS 保持有效，空网格及区块卸载会明确退役资源，
Graph 失败会保留 CPU Geometry 供同一任务重新录制。Dashboard 已发布 Active/Pending、Primitive、
Geometry/Primitive Metadata/BLAS/Scratch 字节统计，Vulkan 生产缓存 Smoke 与 Validation 已通过。
每三个连续非索引顶点现在生成一条固定 16 字节 `TerrainPrimitiveMetadata`，记录纹理层、动画帧数、
FPS、动画标志、Face、Derivative Material 与 Tint；Opaque Primitive 排在前，Cutout Primitive 排在后，
并由固定 Geometry Index 映射到各自的 Vertex Base 与 Primitive Base。Vertex 与 Primitive Metadata
Buffer 均具备 Storage、Device Address 与独立生命周期，压缩 BLAS 通过 `SceneBlasResource` 同时持有。

glTF 与运行时 TLAS 生产层现已接入。`StaticMeshBlasCache` 将每个静态资产的 Opaque 与 Alpha Mask
Primitive 构建为多 Geometry 压缩 BLAS，Opaque Geometry 设置 Opaque Flag，Alpha Mask Geometry
保留 Candidate 路径，Blend 与 Transmission 不进入首版 Solid BLAS。Vertex/Index Buffer 固定具备
Storage、Device Address 与 AS Build Input 用途；同一资产的多个 ECS 实例共享 BLAS。

`SceneTlasCache` 收集 Terrain 与 Static Mesh 实例，按稳定 Key 排序并生成唯一 24-bit Custom Index，
固定 GI Opaque/Cutout、Shadow、Reflection 与 First Person Mask。每个 TLAS 代际持有唯一 BLAS 集合，
Desired/Pending/Active/Retired 状态机覆盖 Transform 连续变化、空场景、部分 Graph 提交失败及资产卸载。
Terrain Custom Index 还保存所引用 BLAS 代际的 Vertex/Primitive Metadata Device Address、Stride、Revision
及 Geometry Index 范围快照；每个 TLAS 代际同时持有按 Custom Index 排列的 64 字节 Terrain 命中表，
Terrain 写入完整地址与范围，Static Mesh 写入明确零记录。因此旧 Active TLAS 不会读取新换代
Terrain BLAS 的地址。另有 TLAS 代际专属的 `GpuMaterial`、`GpuSceneGeometry`、`GpuSceneInstance`
三张表：Terrain 的 Instance 记录逐字节为零，Static Mesh 记录保存资产表范围、Transform、Bounds、
Stable Object ID 与 Custom Index；同一模型资产只展开一次 Material/Geometry。
Dashboard 已发布 Instance、唯一 BLAS、TLAS/BLAS 字节、Revision 与 Build 统计。Vulkan Smoke 覆盖
Opaque/Cutout 多 Geometry BLAS、两个 Instance 共享同一 BLAS、Transform 换代与空场景退役；Damaged
Helmet、Sponza 的 Vulkan 场景验收及 Damaged Helmet 的 OpenGL 基础渲染均已通过。

Cutout Candidate/Confirm 底层 Smoke 已完成：真实 Vulkan Compute Ray Query 使用四条确定性射线验证
Opaque 自动提交、Cutout Candidate 拒绝、Cutout 显式确认和平移实例命中，并回读 Instance Custom
Index、Geometry Index、Primitive ID 与 Barycentrics；多个 TLAS Instance 继续共享同一 BLAS，Validation
未发现错误。Gameplay `RenderScene` 与 Model Scene Deferred 现已在 Vulkan 分别持有 Global Bindless
Set，于帧开始把最新完成的 Active TLAS 发布到固定 Binding 4；重复代际不产生 Descriptor 写入，Dashboard
显示 Active Revision、Descriptor 更新次数和数组占用。OpenGL 两条路径均不创建该集合。主视图、
GBuffer、Probe Capture 与 Shadow 的非 Leaves Cutout 已共用体素材质采样契约；Leaves 按现有设计
保持实心投影。Alpha Cutoff 固定为包含边界的
`0.1`，NaN/Inf Alpha 明确拒绝，动画层使用同一确定性帧选择函数。C++ 契约同时固化
1024 层纹理数组及 6-bit 帧数/FPS 上限，非法元数据以 `std::optional` 报告。Terrain Primitive
Metadata、Geometry Index 到 Primitive Base 映射、BLAS Device Address 与 TLAS 代际快照链已完成，
并通过 GPU Buffer 回读与两代 Terrain BLAS/TLAS 生命周期 Smoke。生产 Shader 现已从 Candidate 的
Geometry Index、Primitive ID 与 Barycentrics 定位固定 32 字节 `BlockVertex` 和 Primitive Metadata，
重建 UV，按像素世界覆盖、射线距离与三角形 UV 梯度计算 Ray Cone LOD，选择动画 Texture2DArray
层并执行统一 Alpha Test。
生产 `RtgiTracePass` 已从 GBuffer 重建主表面，使用固定空间 Blue Noise、R2 低差异
Cranley-Patterson 帧旋转与
Cosine-weighted Hemisphere Sampling，并通过固定 Binding 4 对 `GI_OPAQUE | GI_CUTOUT` 执行 Compute
Ray Query。Candidate Alpha 通过时显式调用 `rayQueryConfirmIntersectionEXT`，Validation Word 同时记录
Classification、Candidate Count 与 Confirmed Count。模型路径已从 Binding 8/9/10 读取 TLAS 代际
Material/Geometry/Instance 表，通过固定顶点布局、Uint32 Index 与三角形 Metadata 重建属性，使用
Global Bindless 纹理和 Sampler 执行 glTF Alpha Mask，并在 Validation Y 写入 Stable Material/Geometry
Hash；Hit Distance 保存在 `RGBA16F` 输出 Alpha。Committed Hit 已进一步读取 Terrain 与模型完整材质，
通过 Camera-relative World Light Grid、太阳/月亮、局部阴影和 Sky Capture 计算次级辐射；以下各节继续
约束原始信号打包、NRD 与正式 Deferred 消费链。

### 4.1 体素区块 BLAS

以实际渲染网格作为 BLAS 输入，不对每个方块创建 Instance。建议粒度是现有可独立修订
和上传的 Render Chunk/SubChunk Mesh：

- Opaque Geometry 使用 Opaque Flag。
- Cutout Geometry 不设置 Opaque Flag，Ray Query Candidate 执行 Alpha Test。
- Water/Glass/Blend Geometry 不进入首版 Diffuse RTGI Solid Mask，但表面仍接收 RTGI。
- 异形方块使用其真实三角形网格。
- Greedy Quad 保留每 Primitive 的 Face、Tile、UV Repeat、Material ID 与 Tint 数据。

区块生命周期：

1. Mesher 产生可光追的非索引 Vertex 与 Primitive Metadata Buffer。
2. 上传完成后，以 `geometryRevision` 请求 BLAS Build。
3. 静态区块使用 Fast Trace + Compaction；Build 完成后查询压缩尺寸并复制。
4. 压缩 BLAS 就绪后进入下一代 TLAS，旧代资源持有到 Submission Token 完成。
5. 方块编辑改变网格时构建新一代 BLAS；只改变世界偏移时仅更新 TLAS Transform。
6. 区块卸载先从新 TLAS 移除，再延迟销毁 BLAS 与几何 Buffer。

BLAS 构建队列设置每帧字节数、Primitive 数和 GPU 时间预算。未建成的区块在现代 RT
预设中标记为 `AsPending`，Dashboard 明确显示；最终验收镜头必须等待场景 AS Ready。

### 4.2 模型与动态对象 BLAS

- 静态 glTF Mesh：资产级 BLAS，可被多个 TLAS Instance 共享。
- 刚体动画：BLAS 不变，更新 TLAS Transform。
- 蒙皮/Morph：Compute Skinning 写当前顶点 Buffer，随后执行 BLAS Update；Topology 变化
  时执行 Build。
- 生物、掉落物、方块实体：按共享 Mesh BLAS + 多 Instance 表达。
- 第一人称物体：使用独立 Instance Mask，是否参与 GI 由明确设置决定。

所有 AS Vertex Buffer 同时拥有 Storage Buffer 与 Device Address 用途，次级命中着色
直接读取顶点属性。Index Format、Stride 与 Address 由 `GpuSceneGeometry` 描述。

### 4.3 TLAS

TLAS 每帧由可见距离内的 RT Instance 列表构建或更新。Instance Custom Index 索引
`GpuSceneInstance`，Instance Mask 区分：

- `GI_OPAQUE`
- `GI_CUTOUT`
- `SHADOW_CASTER`
- `REFLECTION_VISIBLE`
- `FIRST_PERSON`

浮动世界原点发生变化时，批量更新 Instance Transform，并以 `worldOriginRevision` 触发
RTGI/NRD 历史失效。CPU 与 GPU 坐标统一使用相机相对世界空间，避免远距离浮点精度
破坏 Ray Origin 和 AABB。

## 5. Ray Query 着色

### 5.1 主表面重建

每个有效像素从 Depth 与逆 View-Projection 重建相机相对世界坐标，读取 World Normal、
Roughness、Material ID 和 Stable Object ID。Ray Origin 沿几何法线偏移，偏移量由世界
单位、入射角和浮点误差计算，不能使用对所有场景固定的巨大 Bias。

天空像素不发射 RTGI Ray，其环境贡献直接由天空光照链计算。透明表面在 Forward+
阶段读取同一份 Denoised GI。

### 5.2 方向采样

首版对 Diffuse Lobe 做 Cosine-weighted Hemisphere Sampling：

- 固定空间 Blue Noise 提供逐像素样本相位，不能随帧滚动纹理坐标。
- 每帧使用确定的 R2 低差异 Cranley-Patterson Rotation，避免整幅图像发生随机相位跳变。
- 相机 Jitter 与 GI Sample Sequence 使用不同维度。
- Quality：Render Extent 全像素 1 spp。
- Performance：Render Extent 棋盘格 1 spp，向 NRD 正确声明 Checkerboard Mode。

最大射线距离是画质设置中的世界单位参数，并可按室内/室外预设配置；它不是基于本帧
时间动态改变的隐藏变量。

当前 C++/GLSL 共享确定性整数 Hash、R2 帧旋转、余弦半球映射、128 字节 Push Constant 与 112 字节
次级光照 UBO 契约。单元测试固化 Hash 结果、R2 环面步进、固定噪声寻址、采样有效域、期望余弦
分布、UBO 布局及 Terrain Normal/Specular Map Flag；真实 Vulkan Smoke 使用 Blue Noise、Scene TLAS
与生产 `RtgiTracePass`。
Terrain 生产链使用 2×1 GBuffer 与两层动画纹理数组：左像素在动画层 Alpha
为零时拒绝 Cutout 并命中约 2 世界单位后的 Opaque，右像素确认 Cutout 并命中约 1 世界单位；两者
分别回读 `Candidate/Confirmed = 1/0` 与 `1/1`。模型生产链回读约 1.5/0.5 世界单位的 Opaque/Mask
命中；独立 1×1 用例验证无几何命中时返回 Sky Capture Radiance。Validation 未发现错误。

### 5.3 Candidate 命中

Opaque Triangle 可直接接受 Committed Intersection。Cutout Triangle 按以下步骤处理：

1. 读取 Instance Custom Index、Geometry Index、Primitive ID 和 Barycentrics。
2. 由 Geometry Buffer 定位三角形顶点和 Primitive Metadata。
3. 重建 UV、方块 Greedy Repeat、Biome Tint 与 Texture Index。
4. 使用主视图同一 Alpha Cutoff 与纹理采样函数。
5. Alpha 通过时调用 `rayQueryConfirmIntersectionEXT`。

Terrain 生产链已完成第 1、2、4、5 项：固定 Geometry Range 定位 Vertex/Metadata，Barycentrics
重建 Greedy UV，动画元数据选择纹理层，显式 LOD 采样复用统一 Alpha Cutoff，并在通过时确认 Candidate。
Committed Surface 已通过通用属性插值恢复 Position、UV、顶点天光、方块光和 AO，应用 Biome/
Redstone Tint，并读取 LabPBR Normal、Specular 与 Emission；这些材质数据不改变 Candidate Alpha
边界。模型生产链同样完成 Candidate
读取：Geometry Index 定位 `GpuSceneGeometry`，固定 48 字节顶点与 Uint32 Index 重建 Position、Normal、
Tangent、UV，16 字节 Metadata 校验 Material/Geometry Stable ID；Alpha Candidate 仅按 Ray Cone LOD
读取 Global Bindless Base Color，并应用 Base Color Factor 与 `materialPassesAlphaTest`。Committed
Surface 使用逆转置法线变换及镜像/非均匀缩放安全的 Tangent Frame，采样全部 12 个材质语义并输出
Base Color、Normal、Metalness、Roughness、AO、Emissive、Dielectric F0 与稳定身份。Terrain 与模型
各自的 2×1 生产 Smoke 均锁定 Cutout 拒绝后命中 Opaque、显式确认及对应次级材质辐射。

Ray Cone 根据射线距离、像素覆盖和三角形 UV 梯度选择 Texture LOD，避免树叶与细栅栏
在次级射线中出现过度锐利闪烁。

### 5.4 次级命中辐射

命中点返回的入射辐射包括：

- 材质 Emissive。
- 太阳/月亮直接辐射与可见性。
- 命中点所在 Camera-relative World Light Cell 的局部灯辐射与阴影。
- 天空漫反射环境项。

Miss 返回对应方向的物理天空辐射，包含昼夜、天气与云层透射。命中点材质只写入
Diffuse BRDF 输运，不能把次级镜面高光混入 `NRD Diffuse` 信号。由于独立 Specular RT 尚未实现，
金属材质使用上限明确的 `0.35 * Albedo` 漫反射输运补偿，使金属方块仍能产生稳定的材质色溢出；
该补偿将在独立 Specular RT 接入后由真实镜面输运替代。路径估计器应用次级表面材质；主表面的 Diffuse Material Factor 在
NRD 前移除，降噪完成后再调制。首版只有一次间接反弹，不递归发射 GI Ray。

生产实现使用 `RtgiSecondaryLightingParams` 传递太阳/月亮方向与物理 Radiance、天空环境项、阴影
距离和 Terrain 材质 Flag。太阳/月亮 Radiance 已包含昼夜可见能量，Visibility 只决定是否发射
Alpha-aware Shadow Ray，不再次缩放 Radiance。Shadow Ray 与 GI Ray 使用独立 Instance Mask；
Cutout 遮挡仍执行与主追踪一致的 Candidate Alpha 循环。

次级命中不能复用主视图 Cluster，因此 `ClusteredLightingPass` 同时构建固定 16 米 Cell 的
Camera-relative 稀疏 `WorldLightGrid`。Directional Light 按源 Light Index 进入全局前缀，Point、Spot、
Rect Light 按影响球与 Cell AABB 的精确相交进入确定性排序列表；总索引固定限制为 262144，超过容量
时返回 `IndexCapacityExceeded` 且不发布部分结果。Shader 对 Cell 坐标执行二分查找。局部灯继续使用
统一 PBR 求值：`None`、`RasterDynamic`、`RasterCached` 读取现有 Metadata/Spot Atlas/Point Cube Array，
`RayQuery` 发射可见性射线。Ray Query 分支已通过生产 Shader 编译和源码契约；生产 `SceneLight` 入口
当前仍服从主视图共享阴影契约并显式返回 `RayQueryUnavailable`。命中结果最后累加天空环境项；Miss
直接调用 `sampleSkyRadiance` 读取原始 Sky Capture。

Vulkan Smoke 对 Terrain 使用非白 Grass Colormap 与天空环境项，确认 Opaque 和 Grass Tint 后的 RGB
Radiance 不同；模型用红色 Emissive、绿色太阳和蓝色 Point Light 将三条能量来源隔离到不同通道，
同时保留 Mask 拒绝/确认与稳定身份检查；独立 Miss 用例逐通道回读固定 `(0.25, 0.5, 0.75)` 天空值。

体素顶点天光/方块光可作为游戏风格的独立 Radiance Term，必须在设置与调试图中单独
标识。它不能和解析灯能量重复计算。

### 5.5 原始输出

Trace Pass 输出：

- `RtgiDiffuseRadianceHitDistance`：RGB Diffuse Radiance + First-bounce Hit Distance。
- `RtgiValidation`：Hit/Miss、Instance/Material 分类或 NaN 诊断。

当前 Opaque Trace 已创建 `RGBA16F` 辐射/命中距离和 `RG32UI` 验证输出，固定分类为
Sky、Translucent、Miss、Hit 与 NonFinite。Hit Distance、Terrain/模型次级材质、Emissive、太阳、
局部灯、天空环境项和 Miss Sky Radiance 均已由真实 Vulkan 回读验证。原始输出在写入 FP16 前限制到
`65504`；Miss 的 First-bounce Hit Distance 固定写入 `65504`，天空主表面和透明主表面仍表示为未采样
全零数据。

新增独立 `RtgiSignalPackPass`，同时输出两张互不共享 Alpha 编码的 `RGBA16F` 纹理：

- `RtgiRelaxDiffuseRadianceHitDistance`：按 `RELAX_FrontEnd_PackRadianceAndHitDist` 保留线性 RGB 与真实
  First-bounce Hit Distance。
- `RtgiReblurDiffuseRadianceHitDistance`：从主表面 Depth 和 `View * InverseViewProjection` 重建 View-Z，
  使用 NRD 默认 `A=3.0`、`B=0.1`、`C=20.0` 及 Diffuse Roughness `1.0` 执行
  `REBLUR_FrontEnd_GetNormHitDist`，再按 `REBLUR_FrontEnd_PackRadianceAndNormHitDist` 将 RGB 转为 YCoCg。

Hit 和 Miss 执行两种打包；Sky 与 Translucent 的两张输出均为全零。Raw Radiance、Hit Distance、Depth
出现 NaN/Inf，或 Radiance/Hit Distance 为负时，两张输出均清零，`RtgiValidation` 保留原 Candidate 与
Confirmed 计数，将分类改为 NonFinite 并清除 Identity Hash。CPU 契约固定了 FP16 上限、NRD Epsilon、
YCoCg 变换、REBLUR Magic Curve 与 96 字节 Push Constant 布局；真实 Vulkan Smoke 已验证
`RGB=(1,2,3)` 转换为 `YCoCg=(2,-1,0)`、`viewZ=10/hitDist=2` 得到归一化距离 `0.5`、Miss 得到
RELAX Alpha `65504` 与 REBLUR Alpha `1`，并覆盖 NaN/Inf 诊断。NRD 4.17.3 固定依赖、RHI Pipeline、
Render Graph Bridge、真实 Vulkan RELAX 调度、生产 Deferred 消费和显式 RELAX/REBLUR Method 设置均已
完成。逐帧 Non-jittered Matrix 与独立 `RGBA16F` 2.5D Motion 已接入：Guide Prep 从当前/上一帧 Depth
重建正 View-Z，输出 `previousUv - currentUv` 与 `previousPositiveViewZ - currentPositiveViewZ`，并在
全局 Temporal Reset 时禁用深度历史。公共 `RG16F` Velocity 不变。固定数值的真实 Vulkan 回读已覆盖
XY 符号、Z 深度差、正 View-Z 和 Validation；当前/上一帧 Pre-exposure 也已进入共享 FrameContext
和 TemporalFrameInput。

## 6. NRD 4.17.3 集成

### 6.1 选择理由

NRD 针对实时低样本 Ray Tracing Signal，原生支持 Vulkan，并提供 Diffuse/Specular 的
RELAX 与 REBLUR。它使用法线、粗糙度、View-Z、运动矢量、Hit Distance 和历史矩阵，
比继续扩展现有 SSGI 空间滤波器更符合 RTGI 输入。

固定用途：

| 设置模式 | NRD Method | 特点 |
| --- | --- | --- |
| RTGI Quality | `RELAX_DIFFUSE` | 更强的历史稳定与细节保持，作为质量验收基准 |
| RTGI Performance | `REBLUR_DIFFUSE` | 更低 GPU 成本，可配合 Checkerboard Trace |

Method 由用户设置明确选择，运行中不依据 GPU 时间改动 Method。

### 6.2 输入契约

NRD Bridge 每帧提供：

- World-space Normal + Linear Roughness，使用 `NRD_FrontEnd_PackNormalAndRoughness`，
  编码与 `LibraryDesc` 和 NRD CMake 配置完全一致。
- Linear View-Z，符号和投影约定与固定 NRD 版本一致。
- Method 对应的 `Diffuse Radiance + Hit Distance` 打包结果。
- Non-jittered 2D 或 2.5D Screen-space Motion Vector。
- 当前/上一帧 Non-jittered View-to-Clip、World-to-View 等矩阵。
- 当前/上一帧 Jitter、Resource Size、Rect Size、Frame Index 与 Frame Time。
- Disocclusion Threshold、History Confidence 与 Reset/Continue Accumulation Mode。

Mecraft 当前速度纹理定义为 `currentUv - previousUv`，且纹理坐标 Y 向下。Bridge 必须按
NRD 4.17.3 的 `previous - current` 约定转换符号，并通过 `motionVectorScale` 完成 UV/像素
域变换。现代管线另生成 `.z = previousViewZ - currentViewZ` 的 2.5D 分量，提升动态物体
历史拒绝；FSR/DLSS 继续读取公共 RG16F 2D Velocity。转换后用相机平移、旋转和动态物体
三类测试验证重投影方向。

当前生产实现使用当前/上一帧 Non-jittered Inverse Projection 和双帧 Depth 重建正 View-Z，NRD
`IN_MV` 固定为 `RGBA16F`，`motionVectorScale.z = 1`。RTGI Raw 乘当前 Pre-exposure 写入有限精度
HDR，Signal Pack 在进入 NRD 前乘其倒数，Deferred Lighting 对 NRD 输出恢复当前 Pre-exposure；NRD
历史因此始终消费 Scene-referred Radiance。全局 Temporal Reset 或 NRD History Clear 期间不读取上一帧
深度，Z Motion 写零；真实 Vulkan Smoke 固定验证当前 View-Z `2`、上一帧 View-Z `3`、Z Motion `1`，
并验证 `Pre-exposure = 4` 的 Raw/NRD 输入转换。当前全局 producer 仍显式使用 `1`，按历史所有者
细分的 Reset 仍属于后续时域契约工作。

NRD 的 `frameIndex` 每个真实渲染帧严格增加 1，并与 Checkerboard Phase 同步。Material/
Stable Object ID 由应用生成 History Confidence 与 Disocclusion Threshold Mix；NRD 直接
消费这些可选 Mask，而不把 32-bit Object ID 当成原生 NRD 输入。

### 6.3 NRD Render Graph Bridge

初始化流程：

1. 创建 NRD Instance，查询所选 Method 的 Pipeline、Sampler 和 Resource 描述。
2. 把 NRD Permanent Pool 创建为持久 RHI Texture。
3. 把 Transient Pool 注册为 Render Graph 可别名资源。
4. 依据 NRD Descriptor Range 创建固定 Pipeline Layout。
5. 将 NRD SPIR-V 或编译产物创建为 RHI Compute Pipeline。

每帧流程：

1. 填充 Common Settings 与 Method Settings。
2. 获取 NRD Dispatch Descriptions。
3. 为每个 Dispatch 注册一个 Compute Pass。
4. 导入 GBuffer、Raw Signal 与 Output，解析 Permanent/Transient Pool Index。
5. Push Constant/Constant Buffer 上传完成后 Dispatch。
6. Render Graph 生成全部 Texture Barrier。

当前 Bridge 固定校验 NRD 版本、Normal/Roughness Encoding、SPIR-V Binding Offset、Vulkan 扩展
Storage Image 能力、外部纹理格式与完整尺寸。Diffuse Output 在 NRD 内部既作为 UAV 写入，也会被后续
Pass 作为 SRV 读取，因此资源必须同时声明 `Sampled | Storage`；应用需要回读时再附加 `TransferSrc`。
RELAX 与 REBLUR 的 Pipeline/Pool/Constant Buffer 契约均由 SPIR-V Reflection 测试锁定，首帧
`CLEAR_AND_RESTART` 的 RELAX 21 个 Dispatch、第二帧 `CONTINUE` 的 10 个 Dispatch 已通过 16×16
Vulkan Render Graph 执行和有限值回读，并覆盖 Permanent Pool 历史与 Descriptor Cache 缩容。
`GetComputeDispatches` 成功后若构图失败，Bridge 会锁定执行状态并要求重建实例，避免 SDK Ping-Pong
状态与 GPU 历史产生偏差。

NRD Resource Pool 与 `resourceSize` 绑定，动态分辨率通过当前/上一帧 `rectSize` 表达，不
因每次 Active Rect 变化重建。非零 `rectOrigin` 需要以
`NRD_SUPPORTS_VIEWPORT_OFFSET=ON` 构建。Resource Size、Method、资源格式或编码变化时重建；
Checkerboard Mode 和 Rect Size 变化只更新设置并按契约决定 History Restart。资源重建只能
发生在相关 Submission 完成后。

### 6.4 历史失效

以下事件使用 `nrd::AccumulationMode::RESTART`：

- 首帧、相机传送、相机切换和世界加载。
- 不兼容的 Resource/Signal Extent 变化。
- RTGI Method、Checkerboard Mode、深度方向或坐标约定变化。
- 浮动世界原点变化。
- 大范围场景修订导致稳定对象映射失效。
- 暂停后 Frame Index/Previous Matrices 不连续。

新创建或复用且内容可能未初始化的 Permanent Pool 使用一次
`nrd::AccumulationMode::CLEAR_AND_RESTART`。动态对象局部变化依靠 Motion、Depth、Normal
以及应用生成的 History Confidence/Disocclusion Mask 判定，不清空整帧历史。
`DeferredPipeline::invalidateHistory()` 同时标记 NRD Permanent Pool 为 Clear，确保开关切换、世界加载
与相机历史失效不会继续读取旧 GI。

### 6.5 Demodulation 与合成

NRD 要求材质与待降噪信号解耦。Trace Pass 使用 `NRD_MaterialFactors` 计算主表面 Diffuse
Factor，将路径估计器产生的 Irradiance/已调制结果转换为符合 NRD Front-end 规范的
Diffuse Radiance；NRD 输出在 `IndirectLightingComposite` 中重新乘主表面 Diffuse Factor。
次级表面 Albedo 已属于路径输运，合成时不能再次相乘。

Albedo 白炉、纯 Emissive、黑色材质和高饱和光源测试用于检查重复调制与能量爆炸。

## 7. 体素世界专项设计

### 7.1 高频区块修订

方块破坏/放置只重建受影响 Render Chunk/SubChunk 及边界邻居。BLAS Manager 以 Mesh
Revision 去重请求，同一修订只构建一次。Build Scratch 由帧环形分配器管理，峰值达到
预算时返回队列状态，不覆盖仍在 GPU 使用的范围。

### 7.2 远距离世界

TLAS 只包含 RT Distance 内实例，范围外地形不发射几何命中。Miss 的天空辐射仍然完整。
RT Distance 是公开设置，体素雾距离与 RT Distance 的关系通过画质预设固定。浮动原点
确保 Ray Query 始终处于精确坐标范围。

### 7.3 小型与重复几何

Cross Plant、Torch、Fence、Slab、Stair 等使用实际 Mesh BLAS。大量相同方块实体共享
BLAS；普通地形继续使用区块合并网格，避免每方块 Instance 造成 TLAS 膨胀。

### 7.4 昼夜与天气

太阳方向、天空辐射和云遮蔽每帧可变，不要求重建 AS。材质湿润、积雪或生长状态变化只
更新 Material/Primitive Metadata；几何形状变化才增加 Geometry Revision。

## 8. 模型场景专项设计

- 每个 glTF Primitive 必须提供位置、法线、UV、Index、Material ID 的设备地址。
- 多实例共享 BLAS，Instance Custom Index 唯一定位 GPU Scene Instance。
- Alpha Mask 材质使用与主 GBuffer 相同的 Texture Transform 和 Cutoff。
- Negative Scale/Double-sided Material 正确处理 Front Face 与法线方向。
- Emissive 模型参与次级命中，并受 Pre-exposure 契约约束。
- 蒙皮与 Morph 上线前，验收其当前/上一帧变形顶点和 BLAS Update 顺序。

## 9. 性能与显存策略

首轮针对 RTX 4060 Laptop 8GB：

- BLAS 采用静态压缩；记录压缩前后字节数。
- TLAS Instance Buffer、Scratch 和 NRD Transient Pool 使用帧环形资源。
- Quality 在 Render Extent 全像素 1 spp；Performance 使用 Checkerboard 1 spp。
- RTGI Trace/Signal Pack 与 NRD Guide/SDK Dispatch 已分别拥有独立 Timestamp；Alpha Candidate 与
  Secondary Shadow 的细分计时仍通过后续诊断增量补充。
- RT Distance、Max Local Lights Per Hit 与 Signal Resolution 是公开画质参数。
- 动态分辨率只改变 Render Extent，不改变 Method、Ray Distance 或材质复杂度。

具体预算见《06-时域输出与性能》。任何优化都以 Reference Capture 的图像误差、p95 GPU
时间和历史稳定性共同评估。

## 10. 调试视图与验收

必须实现：

- TLAS Instance/Mask、BLAS Age 与 AS Pending 热图。
- Raw Radiance、Hit Distance、Hit/Miss、Ray Direction。
- Cutout Candidate/Confirmed 比例。
- RELAX/REBLUR Output、History Length、Disocclusion、Responsive Accumulation。
- Denoised GI Only、Direct Only、Final Indirect Contribution。
- NaN/Inf 与 Radiance Clamp 计数。

强制测试场景：

1. 体素室内一扇窗、移动太阳、发光方块与开门动画。
2. 洞穴拐角，验证屏幕外间接光与相机旋转稳定性。
3. 大量 Cutout 树叶，验证 Alpha Candidate 与风动速度。
4. 动态方块编辑，验证 BLAS 代际与历史局部失效。
5. Sponza/Damaged Helmet，验证模型 BLAS、Emissive 与材质响应。
6. 相机传送、分辨率切换、Method 切换和世界重载。

完成条件包含 Raw 信号具有预期随机噪声、NRD 输出显著降低方差、运动边缘无持续拖影、
静止镜头无周期闪烁，且离屏能量不会随相机朝向消失。

## 11. NRD 依赖与许可证

- 固定版本：NRD 4.17.3，Tag `v4.17.3`，Commit
  `792eff196afdd350fd9c3f862119017ccb438a0e`。
- 官方仓库：<https://github.com/NVIDIA-RTX/NRD>
- 官方许可证：<https://github.com/NVIDIA-RTX/NRD/blob/master/LICENSE.txt>
- 许可证类型：NVIDIA RTX SDK License，不是 OSI 定义的开源许可证。
- 构建开关：`MECRAFT_ENABLE_NRD`。
- 集成固定源码 Commit、构建选项和产物 Hash；发布应用只分发许可证允许的 Object Code
  形式 SDK 组成部分。

用户已接受该许可证路线。项目合规清单需要纳入完整许可证文本，在 Credits 或最终用户
文档中按 RTX SDK Supplement 标注 NRD；分发修改/派生源码时加入许可证要求的 NVIDIA
Source Notice。发布检查脚本验证许可证、归属、Object Code 形式和固定版本，正式发布前
进行一次许可证合规复核。

作为技术比较：FidelityFX Denoiser 的 MIT 版本面向阴影与反射，不是通用 Diffuse
RTGI 主降噪器；Open Image Denoise 适合离线/高质量图像处理，没有本项目所需的 Vulkan
游戏时域集成；FSR Ray Regeneration 当前平台与 API 约束不符合本 Vulkan 路线。

## 12. 参考资料

- Vulkan Ray Query：<https://registry.khronos.org/vulkan/specs/latest/man/html/VK_KHR_ray_query.html>
- Vulkan Acceleration Structure：<https://registry.khronos.org/vulkan/specs/latest/man/html/VK_KHR_acceleration_structure.html>
- NVIDIA NRD：<https://github.com/NVIDIA-RTX/NRD>
- Ray Tracing Gems II：<https://developer.nvidia.com/ray-tracing-gems-ii>
