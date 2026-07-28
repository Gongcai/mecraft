# Vulkan RTGI 与 NRD

## 1. 技术结论

Vulkan 现代管线直接从现有 SSGI/Voxel GI 路线转向硬件 RTGI，技术上成立，也更符合
当前项目的目标。世界空间 Voxel GI 不是 RTGI 的必要阶段：TLAS 本身已经表达世界几何，
次级命中着色能够直接读取材质、灯光和天空辐射。

现有 SSGI 的主要问题不只是滤波参数。屏幕外表面、被遮挡表面、背面和超出深度范围的
能量根本不存在于输入中，任何降噪器都无法恢复这些信息。RTGI 解决采样域，NRD 解决
低样本随机噪声、时域稳定与边缘保持，二者职责不同。

Vulkan 现代预设只合成 RTGI。现有 `VoxelGiClipmap` 与 SSGI 保留在 OpenGL 基础功能集
和独立诊断模式中，不与 Vulkan RTGI 同帧叠加。

## 2. 首版范围

### 2.1 包含

- Vulkan 1.3 Compute Shader + `VK_KHR_ray_query`。
- 不透明与 Alpha Mask 几何的 BLAS/TLAS。
- 每像素或棋盘格单次 Diffuse Bounce。
- 天空 Miss、Emissive Hit、太阳和 Clustered Local Lights 的次级命中辐射。
- `RGB Diffuse Radiance + First-bounce Hit Distance` 逻辑信号，并按 NRD Method 分别打包。
- NRD 4.17.4 `RELAX_DIFFUSE` 与 `REBLUR_DIFFUSE`。
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

### 4.1 体素区块 BLAS

以实际渲染网格作为 BLAS 输入，不对每个方块创建 Instance。建议粒度是现有可独立修订
和上传的 Render Chunk/SubChunk Mesh：

- Opaque Geometry 使用 Opaque Flag。
- Cutout Geometry 不设置 Opaque Flag，Ray Query Candidate 执行 Alpha Test。
- Water/Glass/Blend Geometry 不进入首版 Diffuse RTGI Solid Mask，但表面仍接收 RTGI。
- 异形方块使用其真实三角形网格。
- Greedy Quad 保留每 Primitive 的 Face、Tile、UV Repeat、Material ID 与 Tint 数据。

区块生命周期：

1. Mesher 产生可光追的 Vertex/Index/Primitive Metadata Buffer。
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

- Sobol/Owen 或 Blue Noise 提供低差异样本。
- 每帧使用确定的 Cranley-Patterson Rotation。
- 相机 Jitter 与 GI Sample Sequence 使用不同维度。
- Quality：Render Extent 全像素 1 spp。
- Performance：Render Extent 棋盘格 1 spp，向 NRD 正确声明 Checkerboard Mode。

最大射线距离是画质设置中的世界单位参数，并可按室内/室外预设配置；它不是基于本帧
时间动态改变的隐藏变量。

### 5.3 Candidate 命中

Opaque Triangle 可直接接受 Committed Intersection。Cutout Triangle 按以下步骤处理：

1. 读取 Instance Custom Index、Geometry Index、Primitive ID 和 Barycentrics。
2. 由 Geometry Buffer 定位三角形顶点和 Primitive Metadata。
3. 重建 UV、方块 Greedy Repeat、Biome Tint 与 Texture Index。
4. 使用主视图同一 Alpha Cutoff 与纹理采样函数。
5. Alpha 通过时调用 `rayQueryConfirmIntersectionEXT`。

Ray Cone 根据射线距离、像素覆盖和三角形 UV 梯度选择 Texture LOD，避免树叶与细栅栏
在次级射线中出现过度锐利闪烁。

### 5.4 次级命中辐射

命中点返回的入射辐射包括：

- 材质 Emissive。
- 太阳/月亮直接辐射与可见性。
- 命中点所在 Camera-relative World Light Cell 的局部灯辐射与阴影。
- 天空漫反射环境项。

Miss 返回对应方向的物理天空辐射，包含昼夜、天气与云层透射。命中点材质使用统一
Diffuse BRDF 求值。路径估计器应用次级表面材质；主表面的 Diffuse Material Factor 在
NRD 前移除，降噪完成后再调制。首版只有一次间接反弹，不递归发射 GI Ray。

体素顶点天光/方块光可作为游戏风格的独立 Radiance Term，必须在设置与调试图中单独
标识。它不能和解析灯能量重复计算。

### 5.5 原始输出

Trace Pass 输出：

- `RtgiDiffuseRadianceHitDistance`：RGB Diffuse Radiance + First-bounce Hit Distance。
- `RtgiValidation`：Hit/Miss、Instance/Material 分类或 NaN 诊断。

RELAX 使用 `RELAX_FrontEnd_PackRadianceAndHitDist` 打包原始 First-bounce Hit Distance；
REBLUR 先调用 `REBLUR_FrontEnd_GetNormHitDist`，再用
`REBLUR_FrontEnd_PackRadianceAndNormHitDist` 打包。两者不能共享 Alpha 编码。所有
Radiance 在 Pack 前检查有限值，发现 NaN/Inf 时写入诊断计数并使该帧 RTGI 验证失败。

## 6. NRD 4.17.4 集成

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
NRD 4.17.4 的 `previous - current` 约定转换符号，并通过 `motionVectorScale` 完成 UV/像素
域变换。现代管线另生成 `.z = previousViewZ - currentViewZ` 的 2.5D 分量，提升动态物体
历史拒绝；FSR/DLSS 继续读取公共 RG16F 2D Velocity。转换后用相机平移、旋转和动态物体
三类测试验证重投影方向。

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
- Trace、Alpha Candidate、Secondary Shadow、NRD 各自拥有 Timestamp。
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

- 固定版本：NRD 4.17.4。
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
