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

以与光栅网格同一修订生成的独立 RT 表面作为 BLAS 输入，不对每个方块创建 Instance。BLAS
粒度保持为可独立修订和上传的 SubChunk Mesh：

- Opaque Geometry 使用 Opaque Flag。
- Cutout Geometry 不设置 Opaque Flag，Ray Query Candidate 执行 Alpha Test。
- Water/Glass/Blend Geometry 不进入首版 Diffuse RTGI Solid Mask，但表面仍接收 RTGI。
- 完整不透明方块只为可见表面生成 `1×1` 单位 Quad；相邻单位面共享体素格边，不使用会产生
  T-Junction 的可变尺寸贪心矩形。异形不透明方块继续使用其真实三角形网格。
- SubChunk Snapshot 读取六向一体素 Halo，边界两侧分别裁掉被实体邻居遮挡的面；方块编辑、区块
  加载与卸载会同时标记受影响的边界 SubChunk。单位面不需要跨 BLAS 共享顶点或跨区块合并。
- 体素主表面的 GI 方向围绕轴对齐几何法线采样，法线贴图只参与 BRDF，避免扰动后的采样
  半球产生贴近实体表面的射线并穿过体素边缘。
- RT 单位面在面内重叠 `1/1024` 方块，BLAS 顶点副本再沿几何法线外扩 `1/2048` 方块，保守
  覆盖同平面共享边和垂直面交线。光栅贪心网格与 Cutout 几何不变。
- RTGI Ray Origin Bias 的契约下限为 `1/1024` 方块，即 Terrain BLAS 壳厚的两倍。Shader、设置
  规范化、UI 与运行时校验共享该常量，主射线和可见性射线不得从外扩后的实体壳内部发射。
- Debug View 89–92 固定 RTGI 采样相位，并在进入和退出检查模式时清空 NRD 历史。Hit Distance
  仅对有效 Hit 显示连续距离热图；Miss、Sky、Translucent 与 Non-finite 使用固定分类色，避免
  `65504` Miss 距离与零距离无效结果伪装成跨帧几何跳变。
- Debug View 89–92 同时将 FrameContext 投影抖动固定为零；进入和退出时以 `Method` 原因重置
  NRD、屏幕空间与上采样器历史，避免 FSR/TAA 子像素覆盖变化被误判为 BLAS Hit/Miss 跳变。
- 每个 RT 单位 Quad 保留自身方块的 Face、Tile、UV、Material ID、Tint、AO 与光照数据。不同方块
  类型不能直接合并为一个 Primitive；只有把这些属性改为命中后按体素坐标查询，才允许跨材质整面合并。

当前独立单位面是正确性基线。下一轮需记录 RT 顶点数、BLAS Build/Compaction 时间、Geometry/BLAS
字节和 Trace 时间；需要压缩时采用带边约束的共形矩形划分或体素 AABB/DDA，不能重新使用会产生
T-Junction 的普通贪心网格。

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

体素 GBuffer 的 World Normal 可以包含 LabPBR 法线贴图，不能直接作为 Ray Origin 的几何法线。
方块材质范围 `0..58` 的主表面按 shading normal 的最大绝对分量恢复带符号的轴对齐方块面法线，并用该法线约束起点偏移
和采样半球，防止逐帧样本穿过方块角点与 Greedy Mesh 接缝。模型主表面不做轴对齐处理，继续使用
严格深度邻域重建，并要求候选几何法线与 shading normal 保持同向且点积大于 `0.75`。

天空像素不发射 RTGI Ray，其环境贡献直接由天空光照链计算。透明表面在 Forward+
阶段读取同一份 Denoised GI。

### 5.2 方向采样

首版对 Diffuse Lobe 做 Cosine-weighted Hemisphere Sampling：

- 固定空间 Blue Noise 提供逐像素样本相位，不能随帧滚动纹理坐标。
- 每个像素对确定的 R2 Cranley-Patterson Rotation 使用稳定的奇数步幅和 D4 方向扰动；每个像素
  仍保持低差异序列，但相邻像素不会在同一屏幕相位同时切换局部高能命中。
- 相机 Jitter 与 GI Sample Sequence 使用不同维度。
- Quality：Render Extent 全像素 1 spp。
- Performance：Render Extent 棋盘格 1 spp，向 NRD 正确声明 Checkerboard Mode。

最大射线距离是画质设置中的世界单位参数，并可按室内/室外预设配置；它不是基于本帧
时间动态改变的隐藏变量。

当前 C++/GLSL 共享确定性整数 Hash、R2 帧旋转、余弦半球映射、128 字节 Push Constant 与 112 字节
次级光照 UBO 契约。单元测试固化 Hash 结果、R2 环面步进、固定噪声寻址、采样有效域、期望余弦
分布、体素主轴几何法线、UBO 布局及 Terrain Normal/Specular Map Flag；真实 Vulkan Smoke 使用 Blue Noise、Scene TLAS
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
该补偿将在独立 Specular RT 接入后由真实镜面输运替代。路径估计器应用次级表面材质；主表面的
Diffuse Albedo 在 Deferred Lighting 的统一合成点乘入 NRD 输出。首版只有一次间接反弹，不递归发射 GI Ray。

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
当前仍服从主视图共享阴影契约并显式返回 `RayQueryUnavailable`。命中结果最后累加天空环境项。Sky
Capture 含有主天空合成阶段会先抑制、再由解析太阳盘重建的高亮太阳瓣；RTGI Miss 在读取 Sky Capture
后应用同一组角度遮罩和亮度限制，避免 1 spp 射线偶发命中太阳盘形成逐帧出现、消失的间接光点，也避免
与解析太阳直射项重复计能。普通天空、天气与云层方向辐射保持不变。

Vulkan Smoke 对 Terrain 使用非白 Grass Colormap 与天空环境项，确认 Opaque 和 Grass Tint 后的 RGB
Radiance 不同；模型用红色 Emissive、绿色太阳和蓝色 Point Light 将三条能量来源隔离到不同通道，
同时保留 Mask 拒绝/确认与稳定身份检查；独立 Miss 用例逐通道回读固定 `(0.25, 0.5, 0.75)` 天空值。

体素顶点天光/方块光仍保留在 GBuffer，供游戏逻辑、天空可见性先验和 OpenGL 基础管线使用；
Vulkan 现代 Deferred、Forward+ 与 RTGI 的直接/次级局部灯统一来自 `GpuLight`，不再把无阴影的
传播方块光作为 Radiance Term 叠加。这样点光源的遮挡只由对应 Raster Shadow 或 Ray Query 决定，
不会被第二份无阴影能量重新照亮。

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
RELAX 的 A-Trous 迭代次数作为显式质量参数开放，合法范围为 `2..8`，默认值为 `5`。
性能对比可固定使用 `3` 次迭代，但不得由运行时根据帧耗时自动改写。

### 6.2 输入契约

NRD Bridge 每帧提供：

- World-space Normal + Linear Roughness，使用 `NRD_FrontEnd_PackNormalAndRoughness`，
  编码与 `LibraryDesc` 和 NRD CMake 配置完全一致。
- Linear View-Z，符号和投影约定与固定 NRD 版本一致。
- Method 对应的 `Diffuse Radiance + Hit Distance` 打包结果。
- Non-jittered 2D 或 2.5D Screen-space Motion Vector。
- 当前/上一帧 Non-jittered View-to-Clip、World-to-View 等矩阵。
- 当前/上一帧 Jitter、Resource Size、Rect Size、Frame Index 与 Frame Time。
- Disocclusion Threshold 与 Reset/Continue Accumulation Mode。

历史深度不是 NRD 自动生成或托管的资源。NRD 只维护自己的 Permanent Pool 和 Transient Pool；
`IN_VIEWZ`、`IN_MV` 以及它们所依赖的上一帧深度必须由应用侧准备。Mecraft 的
`DeferredRenderTargets` 为可见表面深度保留两张 ping-pong 纹理：`Deferred.HistoryCopy` 在帧末把当前
Depth 写入当前槽，`commitDeferredHistoryState()` 在提交成功后翻转槽位，下一帧通过
`historyDepthTexturePrevHandle()` 读取上一帧内容。NRD Guide Prep 在 `historyValid` 为真时用这张纹理
和上一帧非抖动逆投影重建上一帧正 View-Z；全局 Temporal Reset 时不读取它，并让 NRD 进入 Restart。

这与 NVIDIA 官方样例一致：
[`Shared.hlsli::GetMotion`](https://github.com/NVIDIA-RTX/NRD-Sample/blob/9deb12a5408c4e2e07a6ff261f0a1051dd22f5d6/Shaders/Include/Shared.hlsli)
在应用侧计算 `MV.xy = uvPrev - uv`、`MV.z = viewZPrev - viewZ`；
[`NRDSample.cpp`](https://github.com/NVIDIA-RTX/NRD-Sample/blob/9deb12a5408c4e2e07a6ff261f0a1051dd22f5d6/Source/NRDSample.cpp)
再将应用准备的 `IN_MV`、`IN_VIEWZ`、Radiance/Hit Distance 和输出资源提交给 NRD。官方样例的
`OUT_VALIDATION` 也是可选的应用纹理，不属于 NRD 历史管理。

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
并验证 `Pre-exposure = 4` 的 Raw/NRD 输入转换。Scene HDR 的 Lighting、SceneComposite、Reflection、
Water、Transparent、Cloud、Volumetric 和 Particle producer 已统一写入 Pre-exposed 域，Tonemap 前除回；
按历史所有者细分的 Reset 已完成。`TemporalFrameInput` 将当前/上一帧 Pre-exposure 传给时域上采样器；
FSR 3.1 另用 `R32F` 1x1 纹理把后处理的 Scene-referred Exposure 换算为 `Exposure / PreExposure`，使
FSR 的亮度分析、历史颜色与最终 Tonemap 处于同一可见能量域。`PreExposure` 会重启 Scene HDR 域历史，
NRD 输入仍保持去曝光稳定。

NRD 的 `frameIndex` 每个真实渲染帧严格增加 1，并与 Checkerboard Phase 同步。当前生产 Bridge
绑定 `IN_MV`、`IN_NORMAL_ROUGHNESS`、`IN_VIEWZ`、方法对应的 Radiance/Hit Distance，以及
`OUT_DIFF_RADIANCE_HITDIST` 与 `OUT_VALIDATION`。只有 Debug View 100 请求 NRD 生成 Validation；其他
Deferred Debug View 通过显式资源初始化保证静态描述符始终处于合法的 Shader Read 状态。
`IN_DIFF_CONFIDENCE` 和 `IN_DISOCCLUSION_THRESHOLD_MIX` 不接入生产管线。NRD 的历史置信度要求由独立的
上一帧光照梯度追踪生成，不能使用当前像素是否落在上一帧视口内，也不能直接使用 RT Hit/Miss 或 32-bit
Object ID 比较结果。

RELAX/REBLUR 的主历史按 SDK 推荐的 `0.5` 秒积累周期和真实渲染帧间隔换算为帧数，并受对应 Method
的历史上限约束；Fast History 使用 `0.1` 秒周期且始终长于 History Fix。这样在关闭垂直同步的高帧率
运行中，历史不会因固定 30 帧而缩短成很小的时间窗口。RELAX 保留 SDK 默认 Anti-lag 加速与重置响应，
使当前 Raw 辐射能在反遮挡和运动轮廓上替换误投影的暗历史；太阳盘稀疏亮点在 RTGI Trace 源头消除，
不通过关闭历史响应来掩盖。几何、材质和视口变化仍由正常的反遮挡与 Temporal Reset 路径处理。

Debug View 89、98、99 统一显示 Scene-referred Linear RGB：89 在预览前移除 Raw RTGI 的当前
Pre-exposure，98 对 REBLUR 输出先执行 YCoCg 解码，99 再在相同辐射域计算 Raw/Output Delta。因此
99 的变化只表达降噪差异，不再混入曝光倍率或编码差异。

Debug View 101 显示 `NRD Reprojection Coverage`：白色表示当前表面重投影后位于上一帧视口内，黑色表示
新显露区域、屏幕边界外或无有效深度。该画面只用于观察运动时的历史覆盖边界，不参与 NRD 权重计算。
平滑 FOV 变化由当前/上一帧投影矩阵直接重投影，不再因逐帧矩阵数值变化而连续重启历史；资源尺寸变化、
相机切换和资产修订仍按对应 Temporal Reset 原因重启相关历史。

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

Signal Pack 按当前 NRD Method 创建专用 Compute Pipeline。RELAX 变体只执行线性 RGB 与真实
Hit Distance 打包，REBLUR 变体只执行 YCoCg 与归一化 Hit Distance 打包，不再在每个像素上计算并
写出另一种 Method 的无用结果。Method 切换与 NRD 实例重建、历史重启保持同一生命周期。

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
和 NRD 的阈值设置判定，不清空整帧历史；应用侧 Confidence/Disocclusion Mask 仍未接入当前生产路径。
`DeferredPipeline::invalidateHistory()` 同时标记 NRD Permanent Pool 为 Clear，确保开关切换、世界加载
与相机历史失效不会继续读取旧 GI。

### 6.5 Demodulation 与合成

NRD 要求材质与待降噪信号解耦。当前路径在 `RtgiSignalPackPass` 中将 Raw Radiance 按方法打包
为线性 RGB 或 YCoCg，NRD 输出回到 `Deferred Lighting` 后，主表面 `albedo` 在同一处统一乘入
直接光、Clustered 光和 RTGI 漫反射累积结果。主表面材质没有在 Trace Pass 中通过名为
`NRD_MaterialFactors` 的对象单独处理。次级表面 Albedo 已经参与路径输运，合成时不能再次相乘。

Albedo 白炉、纯 Emissive、黑色材质和高饱和光源测试用于检查重复调制与能量爆炸。

### 6.6 后续待办：DLSS Ray Reconstruction

在 RTGI 几何命中、2.5D Motion、RELAX/REBLUR 稳定性和性能验收完成后，单独评估 DLSS Ray
Reconstruction。该路径需要 NVIDIA NGX/Vulkan 能力门槛、独立的 Ray Reconstruction 输入与输出、
历史重置/资源生命周期、验证视图和与 NRD 的质量对比；当前不实现，也不改变现有 NRD Diffuse 路径。

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
BLAS；普通完整方块在每个 SubChunk BLAS 内使用可见单位面，仍然不创建每方块 TLAS Instance，
因此不会按方块数量膨胀 TLAS。

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
- RTGI Trace/Signal Pack 与 NRD Guide/SDK Dispatch 已分别拥有独立 Timestamp；Dashboard 与
  Benchmark JSON 使用 `RTGI.Trace`、`RTGI.SignalPack`、`NRD.GuidePrep`、`NRD.Dispatch` 发布细分
  p50/p95/p99，同时保留不重复计入总时间的 RTGI/NRD 聚合项。RTGI Validation Image 的
  Candidate/Confirmed 已通过 GPU 归约和异步读回发布总量、确认率与每像素峰值；Secondary Shadow
  的细分计时仍通过后续诊断增量补充。
- `SceneTLAS`、Terrain BLAS Build/Compaction、动态资源准备和 RTGI bootstrap 已接入独立的 CPU/GPU
  计时、工作量与驻留峰值；Static BLAS 在资产加载时记录 Build/Compaction 时间及压缩前后字节。
  bootstrap 包含内部 SceneTLAS，其时间只用于归因，不重复计入已追踪 GPU 总时间。AS 报告还发布
  TLAS Generation Allocation/Reuse/Wait、动态 BLAS Action/Reject Counter、Terrain Opaque/Cutout Primitive
  分布和 Static BLAS Opaque/Cutout Primitive 分布。
- RT Distance、Max Local Lights Per Hit 与 Signal Resolution 是公开画质参数。
- 动态分辨率只改变 Render Extent，不改变 Method、Ray Distance 或材质复杂度。

具体预算见《06-时域输出与性能》。任何优化都以 Reference Capture 的图像误差、p95 GPU
时间和历史稳定性共同评估。

### 9.1 Caustica 对照后的策略边界

动态实体 BLAS 使用独立的 `DynamicBlasPolicyContract` 固定决策条件。Rigid Reuse 要求保留引用与当前快照的
Build Flags、Geometry 顺序、格式、数量和 Index Topology 一致，姿态必须是位相等或 Yaw+Translation 刚体
关系，Shading Hash 也必须一致。UPDATE 要求选中的槽以 `AllowUpdate` 构建，全部 Geometry 兼容字段一致；
连续 UPDATE 上限为 120，达到上限的操作是完整 BUILD。当前动态实体尚未接入 TLAS，因此生产 Action Counter
保持为零，契约测试覆盖了 Shading、Bucket Primitive、Topology、Build Flag 和周期重建拒绝原因。

Caustica 的四槽 TLAS 解决每帧动态实例导致的 VMA 创建与销毁尖峰；Mecraft 当前只在场景快照变化时创建
generation。`scene_tlas_generations` 现已直接测量 Allocation、Reuse、Reuse Wait 和 Retired Generation。
最新 3 帧报告中体素为 0 次 Allocation，Sponza 为 1 次 Allocation，Reuse/Wait 都为 0；固定 Ring 不进入
当前生产实现。

Terrain BLAS 的 Shader ABI 仍是 Opaque/Cutout 两类。体素参考场景当前为 202,312 个 Opaque Primitive 和
876 个 Cutout Primitive，Cutout 占约 0.43%；Sponza 与场景内 Static BLAS 的 Cutout Primitive 为 0。
Ray Query Candidate Loop 与 Any-Hit/SBT 的执行模型不同，OMM 还要求新增 RHI Extension、Capability、
Micromap 资产和生命周期契约。Candidate/Confirmed 已接入 8x8 GPU 归约、低高 32-bit 原子累加和三槽
Submission Token 异步读回；CPU 严格校验 Counter ABI 版本、Extent、像素数及 Confirmed 不超过 Candidate，
Benchmark 固定窗口按唯一 readback sequence 聚合。

2026-08-09 重新采集的 Sponza 与体素洞穴报告均得到 2,764,800 个有效像素样本，Candidate、Confirmed 与
每像素峰值都为 0：前者没有 Cutout，后者的射线没有实际遇到驻留的 Cutout Primitive。V03 Forest Cutout
补足了该压力缺口：固定 `mecraft.forest_cutout` fixture 的 12 个树冠、草地方块和高草使 Terrain 当前
Cutout Primitive 达到 6,926。相同 300 帧预热加 3 帧 1280×720 报告累计 2,142,695 Candidate、1,636,266
Confirmed，确认率为 76.36%，Candidate 峰值为每像素 8，`RTGI.Trace` p95 为 3.874 ms，完整 GPU Span
p95 为 13.990 ms。生产 `RtgiTracePass` Vulkan Smoke 也继续覆盖已知 Cutout 像素的非零总量和峰值读回。

这些数据证明 Candidate Loop 在生产 V03 路径真实承受 Cutout 压力；OMM 的采用结论由后续正式 A/B 给出。
`VK_EXT_opacity_micromap` 已完成可选 RHI Capability 协商：只有扩展与 `micromap` feature 同时存在才会在
逻辑设备启用并报告为可用，缺少任一条件时明确拒绝 OMM 模式。Micromap 资产与生命周期契约已经落地，资产校验
Alpha/Profile 身份、每个 BLAS primitive 的 subdivision/state，并严格
执行 Empty→CpuReady→GpuBuildPending→Resident→Retired 状态迁移。RHI/Vulkan 已接入 Micromap Storage/Build Input Buffer、独立同步状态、
强类型 Handle、Build Size、GPU Build、提交引用和延迟销毁。RHI BLAS Triangle Geometry 已接入 OMM Usage Group、
可选 Primitive Index Buffer 和 Micromap 引用；RTX 4060 Laptop Vulkan Smoke 已真实完成 Micromap Build、同步和
关联 BLAS Build，Validation 无新增错误。

Terrain Cutout 生产链现已接入：`BlockTextureLibrary` 保留规范化 RGBA8 Texture Array 与 Alpha Hash，CPU 分类
固定使用 `0.1` Cutoff、动画全部有效帧联合覆盖、Repeat UV 和 Caustica 对齐的 Vulkan microtriangle index 顺序，
subdivision 4 的 Four-state 状态打包为 Transparent、Opaque、Unknown Opaque。生产缓存先上传 Opacity 与 Triangle
Record，再在同一命令列表执行 Micromap Build、`MicromapBuildWrite→MicromapRead`、Cutout BLAS Build；Opaque
Geometry 不关联 OMM。压缩后的 `SceneBlasResource` 同时持有 Micromap Handle 和 backing buffer，TLAS 旧代仍引用
旧 BLAS 时不会提前销毁。设置显式选择 Candidate Loop 或 OMM，已有 BLAS generation 时拒绝切换；Candidate 模式
不创建 Micromap 资源。

Benchmark 的 `terrain_opacity_micromaps` 已发布 mode、Four-state subdivision、Alpha/Profile Hash、活动 Micromap
数量/字节、构建 Input/Storage/Scratch 字节和 Opaque/Transparent/Unknown microtriangle 计数。2026-08-09 在同一
V03 Camera Path、Alpha 纹理、质量 Profile 和 RTX 4060 Laptop 上完成 Candidate Loop/OMM 各 300 帧预热加
1000 帧采样的正式 A/B：

| 指标 | Candidate Loop | Opacity Micromap | 变化 |
| --- | ---: | ---: | ---: |
| `RTGI.Trace` p95 | 3.759872 ms | 2.804736 ms | -25.40% |
| Complete GPU Span p95 | 17.859584 ms | 16.940544 ms | -5.15% |
| Candidate 总量 | 716,114,823 | 128,248,955 | -82.09% |
| Confirmed 总量 | 538,191,865 | 92,007,395 | -82.90% |
| RHI 总内存 | 1,012,668,336 B | 1,013,282,224 B | +613,888 B |

OMM 常驻 52 个 Micromap、598,144 B，1,773,056 个 microtriangle 精确分为 1,145,448 Opaque、
510,754 Transparent 和 116,854 Unknown Opaque，等于 6,926 个 Cutout Primitive 各 256 个。最终 RGBA8
显示输出的 RGB MAE 为 0.000991734、RGB RMSE 为 0.002267379、全局亮度 SSIM 为 0.999885319，满足独立
A/B 门禁；该显示输出比较不替代 64 spp Linear EXR、固定 ROI 和动态 Ghost/Disocclusion 的 M3 画质门槛。
`rtgi_cutout_traversal_ab_test` 从锁定 PNG/报告重算身份、样本、Micromap 分区、性能收益和图像指标。M3 V03
Vulkan 质量运行据此采用 `opacity_micromap`，Candidate Loop 保留为显式诊断实现轴。
捕获层同时提供严格 RGBA16F Linear EXR 写入，Reference runner 已接入该原始 HDR 输出。
`RtgiQualityValidationContract` 已统一线性 HDR 评价口径：在固定 ROI 内对 Raw/Denoised 逐像素帧序列使用
无偏亮度方差，对 32 帧结果与 Reference 计算亮度 SSIM 和 95th 相对亮度误差，并拒绝 NaN/Inf、负辐射、
尺寸不一致与不足两帧的序列。独立质量工具严格检查 32 帧 Raw/Denoised 与 64 帧 Reference 的连续编号和
尺寸，使用 IEEE Half 解码、流式线性平均并写出最终 64 spp EXR，然后输出指标、阈值、证据可用性与通过状态。
`FrameOutput` 已发布 Deferred Graph 解析的 `rtgiRawDiffuse` 与 `nrdDiffuse`，两者均为当前 pre-exposed
线性 HDR 域。验证控制器已为每个采样帧生成递增 EXR 序号，启动参数
`--validation-rtgi-hdr-capture-dir` 与 `--validation-rtgi-hdr-capture raw|denoised|raw_and_denoised` 必须成对
出现。Gameplay 与 Model 场景均已接入：分别从 `RenderScene::FrameOutput` 和模型 Deferred 输出写入
`rtgi_raw_####.exr`/`rtgi_denoised_####.exr`；请求信号无效、非 RGBA16F 或任何 EXR 写入失败均明确终止运行。
RTX 4060 Laptop 的 Vulkan 小窗口实跑已分别覆盖两条路径（320×180、1 帧预热、2 帧采样），每组 Raw/Denoised
均完整生成两帧无压缩 RGB Half scanline EXR，`exrheader` 可解析且 Vulkan Validation 无错误。
捕获器的严格读回同时校验 Header、offset table 与 RGB Half scanline，防止格式表面有效而像素数据不可读取。
版本化静态质量 Profile 已锁定 V01/V02/M03 的场景契约、M3 RTGI Render Settings、1280×720、Camera Path
2.0 秒和固定 ROI；选择 Profile 会强制 32 帧 Raw/Denoised 且保持相机静止。V01 的真实 32+32 EXR 小预热
运行已验证调度、写入和报告字段。
Reference 运行轴现会强制 64 帧 Raw-only、关闭 NRD，并让 R2 低差异相位在无降噪器时逐帧推进。V01 实跑的
帧首重置条件此前错误地把所有 `!nrdEnabled` 帧清零，现已排除显式 Reference Sampling，并由源码契约测试
锁定 Reference 不重置、成功帧递增。修复后的 300 帧预热正式重采中，首帧、次帧和第 64 帧 EXR 哈希均不同，
报告中 `NRD.GuidePrep`/`NRD.Dispatch` 为 0。质量工具已生成单张
64 spp EXR。首轮 v1 ROI 覆盖中心窗口 Sky，不能用于静态门槛；Profile v2 将 V01 移到室内地面
`(256,544,256,128)`。`FrameOutput` 现明确 Raw 为 pre-exposed、NRD 输出为 scene-referred，捕获器在写入
Denoised EXR 时使用同帧 Pre-exposure 统一评价域。图外读取 Render Graph 瞬态目标会命中其后的别名资源，
因此 Raw 和 Denoised 在各自最后有效 Pass 后拷贝到持久 RGBA16F 输出，再交给验证 runner。当前 V01 的
Pre-exposure 为 1；300 帧预热正式运行的 Raw/Reference/Denoised 平均亮度为
`0.002882143/0.002882066/0.002634361`，方差降低 `99.834749%`，但 SSIM `0.775638`、HDR 相对误差 p95
`0.321060` 仍失败。该差异需要定位 RELAX 的真实空间/时域根因，禁止用阈值、ROI 扩张或报告缩放处理；
修复后的 Reference 序列、平均 EXR 和报告与当前正式归档逐字节一致，因此这些指标不变；
Leakage Band 与 AS Pending 当前在报告中明确为缺少证据，
`complete_static_gate_passed=false`。

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
