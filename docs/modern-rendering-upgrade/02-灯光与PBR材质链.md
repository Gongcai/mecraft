# 灯光与 PBR 材质链

## 1. 目标与现状判断

当前 Deferred 管线已经具备 Cook-Torrance GGX、太阳光、CSM、天空 SH、天气材质响应，
glTF 模型也已支持 Metallic-Roughness、Specular-Glossiness 及 IOR、Clearcoat、
Transmission、Volume 的主要参数。这说明基础 BRDF 和资产入口已经成立，但整个画面仍
缺少四条端到端链路：

1. 方块与模型共用的可随机访问 GPU 材质表。
2. 局部灯光、Clustered Light Culling 和局部灯阴影。
3. 粗糙度正确的 GGX 环境预过滤、DFG LUT 和局部反射探针。
4. 次级射线可复现主视图材质结果的材质求值函数。

本章建设的是统一链路，不能为模型展示和体素世界分别复制一套光照实现。

## 2. 统一材质语义

### 2.1 `GpuMaterial`

建议的逻辑字段如下。正式 GPU 布局按访问频率分为紧凑核心表与扩展表，并通过静态断言
校验布局，不要求逐字采用此 CPU 示例。

```cpp
struct GpuMaterial {
    glm::vec4 baseColorFactor;
    glm::vec4 emissiveFactorAndStrength;
    glm::vec4 metallicRoughnessNormalOcclusion;
    glm::vec4 dielectricSpecularAndIor;
    glm::vec4 clearcoatFactors;
    glm::vec4 transmissionVolumeFactors;
    glm::vec4 attenuationColorAndDistance;
    glm::vec4 gameplaySurfaceFactors;
    std::array<uint32_t, 12> textureIndices;
    std::array<uint32_t, 12> samplerIndices;
    uint32_t alphaMode;
    uint32_t flags;
};
```

语义要求：

- Base Color、Emissive 使用线性工作色域；颜色纹理由 sRGB 采样格式完成解码。
- Normal、Metallic、Roughness、Occlusion、Thickness 等数据纹理按线性格式采样。
- Roughness 存感知粗糙度，BRDF 内部使用 `alpha = roughness²`。
- 电介质 F0 由 IOR 和 Specular 扩展共同计算，保存 RGB F0，不能继续压成单个标量。
- Metallic、Clearcoat、Transmission、Thickness 和 Emissive 是独立语义，不复用同一位域。
- `gameplaySurfaceFactors` 表示 Wetness、Porosity、Subsurface 和自定义游戏表面参数。
- Alpha Mode 明确区分 Opaque、Mask、Blend、Transmission 和 Additive。
- 纹理索引指向 Global Bindless Set，缺省材质属性使用显式常量纹理槽。

### 2.2 glTF 映射

资产加载时执行以下规范化：

| glTF 语义 | 统一材质字段 |
| --- | --- |
| Base Color / Alpha | `baseColorFactor` + Base Color Texture |
| Metallic-Roughness | Metallic、Perceptual Roughness 与 ORM 纹理 |
| Normal / Occlusion / Emissive | 对应独立纹理及强度 |
| `KHR_materials_specular` | RGB Dielectric F0 |
| `KHR_materials_ior` | IOR 与 Dielectric F0 |
| `KHR_materials_clearcoat` | Clearcoat、Clearcoat Roughness、Clearcoat Normal |
| `KHR_materials_transmission` | Transmission |
| `KHR_materials_volume` | Thickness、Attenuation Color/Distance |
| `KHR_texture_transform` | 每纹理 UV Transform |
| `KHR_materials_emissive_strength` | Emissive Strength |

当前明确拒绝的 Sheen、Anisotropy 和 Diffuse Transmission 要保留结构化导入错误；对应
BRDF 与 GBuffer 完成后，再把它们纳入支持列表。Importer 不应接受但忽略扩展。

### 2.3 体素材质映射

体素资源继续兼容现有方块纹理数组和 LabPBR 数据，但在加载阶段转换为同一语义：

| 体素输入 | 统一材质字段 |
| --- | --- |
| 方块 Albedo/Tint | Base Color；Biome Tint 在采样后相乘 |
| Normal Texture Array | Tangent-space Normal 与 Normal Scale |
| Specular/LabPBR 通道 | Smoothness 转 Roughness；F0/Metal ID 转 RGB F0 与 Metallic |
| Emissive 通道/方块定义 | Emissive Color 与 Strength |
| Porosity / SSS | Gameplay Surface Factors |
| 方块透明类型 | Alpha Mode、IOR、Transmission、Thickness、Attenuation |
| 顶点太阳光/方块光/AO | 几何插值输入，不写进材质常量 |

透明方块配置应能直接表达玻璃、染色玻璃、冰与水：IOR、Roughness、Transmission、
Attenuation Color、Attenuation Distance、Thickness Mode。这样体素玻璃和 glTF 玻璃
进入同一透明解析核心。

### 2.4 主视图与次级命中共享函数

材质采样与 BRDF 分为无渲染目标依赖的 GLSL include：

```text
material_decode.glsl
  ├── sampleMaterial(materialId, geometry, uv, derivatives)
  ├── evaluateOpacity(...)
  └── evaluateEmission(...)
pbr_brdf.glsl
  ├── evaluateDirectBrdf(...)
  ├── evaluateDiffuseBrdf(...)
  ├── evaluateSpecularBrdf(...)
  └── evaluateIbl(...)
```

光栅路径提供显式纹理梯度；Ray Query 路径依据三角形位置与 UV 计算 Ray Cone LOD。
Cutout 判定、法线解码、Emissive 和 Metallic-Roughness 不能在两条路径中出现不同公式。

## 3. GBuffer 契约升级

现有五 MRT 可服务基础材质，但 RGB F0、Clearcoat、Material ID 精度和时域表面身份需要
更清楚的布局。建议使用：

| 附件 | 建议格式 | 内容 |
| --- | --- | --- |
| `GBaseColor` | `Rgba8Srgb` 或线性写入 `Rgba8Unorm` | Base Color RGB、Material Class A |
| `GNormalRoughness` | `Rgba16Float` | Oct World Normal XY、Perceptual Roughness、AO |
| `GF0Metallic` | `Rgba8Unorm` | RGB F0、Metallic |
| `GEmissionSurface` | `Rgba16Float` | Pre-exposed Emissive RGB、Surface Flags |
| `GGameplay` | `Rgba8Unorm` | Wetness、Porosity、Subsurface、Light Mask |
| `GObjectMaterialId` | `Rg32Uint` | Stable Object ID、Material ID |
| `GVelocity` | `Rg16Float` | Current UV - Previous UV |
| Depth | `D32Float` | 主深度 |

显存预算证明 `Rg32Uint` 成本过高时，可用独立半分辨率 Surface ID 历史实验，但不能
在缺少数据的情况下宣称完成 NRD 表面身份验证。Clearcoat、Transmission 与 Volume
属于 Forward+/透明层求值，不把所有扩展参数塞进 GBuffer。

NRD Preparation 将 GBuffer 的 Perceptual Material Roughness 转换为固定 NRD 版本要求的
Linear Roughness，并使用 NRD 构建时选定的 Normal/Roughness Encoding 打包专用输入。

## 4. BRDF 与能量守恒

不透明与透明路径统一使用：

- GGX/Trowbridge-Reitz NDF。
- Smith Correlated Visibility。
- Schlick Fresnel，电介质 RGB F0 由 IOR/Specular 确定。
- Lambert 或 Disney Diffuse，具体选择由离线参考图误差决定并固定为一种。
- Metallic 工作流的 Diffuse/Specular 能量分配。
- 多次散射能量补偿，避免高 Roughness 金属明显变暗。
- Clearcoat 独立 GGX Lobe，并对基底层做 Fresnel 能量衰减。
- Emissive 采用绝对场景强度并参与曝光、Bloom 和 RTGI 次级命中。

湿润表面通过受约束的材质变换降低 Roughness、加深 Base Color 并改变表面水膜 F0；
Porosity 控制变化幅度。该变换在 GBuffer 生成前完成，RTGI 次级命中使用同一函数。

## 5. Clustered Lighting

### 5.1 光源数据

`GpuLight` 至少包含：

- 类型、稳定 Light ID、位置、方向和范围。
- RGB 颜色与物理强度。
- Spot 内外锥角、Rect 尺寸。
- Shadow Policy、Shadow Index、Cookie/IES Index。
- Volumetric、Diffuse、Specular Contribution Flags。

单位约定：Directional 使用 Lux，Point/Spot 使用 Lumen 或 Candela 并在 CPU 规范化，
Rect 使用 Nit。编辑器显示原始物理单位，GPU Buffer 保存着色所需量。

### 5.2 Cluster 构建

视锥划分为屏幕 Tile 与对数 Z Slice。初始配置为 `16×16` 像素 Tile、24 个 Z Slice，
最终参数由 RTX 4060 Laptop 的占用率和 Cluster 列表统计确定。

Compute Pass 执行：

1. 依据 Light Bounds 计算覆盖 Cluster。
2. Prefix Sum 分配 Compact Light Index List。
3. 写入每个 Cluster 的 Offset/Count。
4. 统计最大、平均光源数和容量错误。

不透明 Deferred、透明 Forward+、体积雾和 RTGI 次级命中共享同一 Light Buffer，但使用
两种索引产品：主视图表面读取 View-space Cluster List；RTGI 命中、Probe Capture 与屏幕
外位置读取 Camera-relative World Light Grid。次级命中不能依赖屏幕 Cluster，因为命中点
可能位于视锥外。两种列表由同一次 Light Revision 构建，并具有独立容量统计。

Rect Light 在光栅主表面使用 LTC LUT 求值，在 RTGI 次级命中使用固定分布采样。Point、
Spot 和 Rect 的辐射衰减、单位与作用范围由同一个光源函数定义。

### 5.3 体素光源生成

方块发光定义包含 `emissiveRadiance` 与可选 Analytic Light Template。区块加载或方块
修订时生成紧凑光源代理；多个连续发光方块可由离线/区块算法合并为 Rect Light，合并
必须保持总光通量并产生确定结果。火把、灯笼、熔岩、红石灯与动态实体灯均进入统一
Light Registry。

Minecraft 风格的方块光等级仍可服务游戏逻辑和 OpenGL 基础管线；Vulkan 现代管线的
直接光来自 `GpuLight`，两者是明确的数据产品，不在着色器中混合解释。

## 6. 阴影链

### 6.1 太阳与月亮

保留四级 CSM 与现有彩色透明阴影，改进：

- Stable CSM Texel Snapping。
- Receiver Plane Depth Bias 与 Normal Bias 可视化。
- 级联间连续过渡。
- GPU Scene Shadow Culling 与间接绘制。
- Cutout 使用统一 Alpha Test 函数。

PCSS 是显式画质选项，采样预算和光源角直径进入设置与 GPU 时间统计。

### 6.2 局部灯阴影

Spot Light 使用 2D Shadow Atlas，Point Light 使用 Cube Array。每个光源的 Shadow Policy
明确为 None、Raster Map 或 Ray Query，不由帧时间临时更改。Shadow Atlas 分配采用稳定
Light ID，静态灯在遮挡者修订号不变时复用缓存页。

Ray Query Shadow 复用 TLAS 和 Cutout Candidate 判定。首轮仅对设置中明确标记的灯启用，
其射线数量与 GPU 时间单独统计。Area Light 的软阴影采样使用 Blue Noise 旋转并进入
专用时空滤波，不能把 Diffuse RTGI 的 NRD 输出当成阴影结果。

## 7. PBR IBL

### 7.1 天空环境

当前大气天空捕获扩充为 Cubemap 产品：

1. 物理大气、太阳、云和天气生成 HDR Sky Cubemap。
2. Cubemap 生成 Diffuse Irradiance SH 或低分辨率卷积图。
3. GGX Importance Sampling 生成 Specular Prefilter Mip Chain。
4. 预计算 Split-sum DFG LUT。
5. 天空修订时分帧更新各 Face/Mip，完成整代资源后原子切换。

所有材质按 Roughness 选择 Specular Mip，F0/NoV 查询 DFG LUT。现有直接方向天空采样
不再承担粗糙镜面预过滤职责。

当前实现由 `SkyIblPass` 将动态天气天空转换为 128×128 HDR Cubemap，生成 8 级 GGX
预过滤链和 256×256 DFG LUT；`ReflectionPass` 使用连续 Roughness Mip 与 NoV/Roughness
DFG 查询替换旧的随机粗糙法线天空近似，并提供独立 Mip/DFG 调试视图。运行时保留两代
Radiance/Prefilter 资源：首帧完整引导，后续每 48 帧请求一个天空修订，新代先在单帧完成
6 个 Radiance Face 快照，再逐帧生成一个 Prefilter Face/Mip；48 个工作项全部成功提交后
才原子切换消费者，构建失败不会推进修订或暴露半成品。第 7 项剩余验收仅为
V01/M01/M02 版本化参考图。

### 7.2 Reflection Probe Grid

局部探针包含：

- 世界位置、影响 AABB、Parallax Correction AABB。
- Prefiltered Cubemap Index。
- Validity、Capture Revision、Exposure Metadata。
- 与相邻探针的 Blend Weight 数据。

体素世界以流式网格管理探针，洞穴、室内、村庄和大型建筑可拥有局部捕获；模型场景由
编辑器放置 Probe 或生成规则网格。表面查询同时考虑包围盒、距离、法线方向和 Validity，
使用 Box Projection 修正室内反射。

当前已建立 Reflection Probe 基础契约：单个 GPU 记录固定为 96 字节，包含位置与曝光、
影响 AABB 与混合距离、Box Projection AABB、Validity、Prefiltered Cubemap Index、稳定 ID、
Capture Revision 和契约版本。CPU 参考实现对输入执行结构化校验，按边界距离、探针归一化
距离、表面朝向和 Validity 计算权重，以权重降序和稳定 ID 升序确定 Top-4 并归一化；CPU 与
GLSL 共用相同的 Box Projection 算法和字段顺序。

运行时 Grid 已使用固定 16 米单元格覆盖有效探针影响盒，限制单元格最多 16 个候选，并按稳定
ID 生成紧凑候选索引；Probe、Grid Metadata、Cell 和 Index Buffer 通过 Render Graph 事务式
上传。Reflection Pass 从 Prefiltered Cube Array 读取入选探针，使用 Box Projection 修正方向，
按 Top-4 权重混合局部环境与天空环境，并提供 Probe ID/Weight 调试视图。粗糙环境反射统一以
几何反射方向查询预过滤 Mip，不再对方向额外执行随机微表面扰动。

`ReflectionProbeCapturePass` 已建立 128×128 RGBA16F Radiance/Prefilter Cube Array。每个 Probe
拥有两个捕获槽位，一次修订按稳定 Probe ID 进入确定性队列：先记录 6 个 Radiance Face，再按
Mip/Face 顺序生成 48 个 GGX Prefilter 产品；仅当 54 个工作项全部成功提交后，才发布新的
Cubemap Index 与 Capture Revision，构建中的槽位不会进入 Reflection Pass。Capture Renderer
使用显式接口接收 Probe 世界位置、六面相机矩阵与颜色/深度目标 View。模型场景已接入版本化
场景文件中的手工 Probe 与规则网格数据源，并以独立 RGBA16F 前向管线复用 glTF Material
Sampling、太阳/月亮直接光、环境光、发光材质和完整 `KHR_lights_punctual` 快照；透明光学层按
Probe 距离全局排序，捕获过程不读取主视图时域结果。模型编辑器的 Reflection Probes 面板支持
添加、删除、空间参数编辑和按场景包围盒生成固定顺序规则网格，运行时为每个文档 ID 分配独立
稳定捕获 ID 与修订。Dashboard 与模型场景 Reflections 面板已展示 Source/Active/Building
数量、剩余工作项、当前 Probe/Work Item、活动/构建修订和 Cubemap 槽位。体素场景已具备独立
源构建契约：按加载区域 AABB、固定单元尺寸、外扩距离和捕获修订生成固定 z/y/x 顺序的世界
空间 Probe 源，并严格检查有限值、容量、稳定 ID 范围和 GPU Probe 契约。

体素运行时以相机所在的 16 米单元中心维护一个流式 Probe。相机跨单元、活动区块集合变化或
方块内容变化会递增 Capture Revision；影响 AABB 固定为当前单元，Box Projection AABB 覆盖
已加载区块。地形 Capture Renderer 使用每个 Cubemap Face 的独立 View/ViewProjection 重新执行
区块视锥裁剪和间接命令上传，将 opaque/cutout 地形写入 128×128 RGBA16F 与 Depth32 目标，
随后把当前 Face 可见的透明地形与水面网格按探针距离从远到近排序并执行 SrcAlpha 混合。捕获
材质当前复用方块纹理数组、昼夜 Lightmap、顶点 AO、动画和生物群系染色，并关闭雾与主视图
时域效果。`m0_voxel_baseline` 已完成 OpenGL/Vulkan 120 帧预热和 60 帧采样，Vulkan Validation
无报错。

模型 Probe Capture 复用主材质采样和直接光。体素 Capture 已在透明地形之前绘制方块实体、
掉落物和生物/远端角色，各类别使用自身 RGBA16F Forward 管线；本地第一人称玩家被明确排除，
避免观察者模型进入环境探针。水面网格当前使用方块 Forward 材质，现代水体折射/吸收、下落
方块、体素静态 glTF、粒子和现代 PBR 直接光仍需接入。V02/V07/M07 版本化资产用于最终检查
室内外过渡、局部光响应与 Box Projection。动态探针按确定的更新队列逐 Face/Mip 构建，
Dashboard 展示队列长度与资源代际。

## 8. 反射能量组合

现代模式将镜面能量分解为：

- 天空/局部 Probe 提供完整粗糙镜面 Lobe 的环境基线。
- SSR 解析屏幕内高置信度、低粗糙度可见反射。
- 选中的 Vulkan RT Reflection 解析屏幕外或遮挡区域的定向信号。
- BRDF 权重、SSR Confidence、Ray Hit Validity 与 Probe Visibility 参与一次统一合成。

合成必须保证同一镜面能量只计算一次。调试视图分别显示来源、权重和最终结果。RT
Reflection 使用 NRD 的 Specular Method 时拥有独立历史，不与 Diffuse RTGI 共用纹理。

## 9. 双场景验收要点

### 9.1 体素世界

- 火把移动时 Cluster 列表、局部光和阴影实时更新。
- 洞穴入口的 Reflection Probe 与天空环境过渡稳定，无明显明暗跳变。
- 金属方块粗糙度从 0 到 1 的高光宽度、能量和环境反射连续。
- 染色玻璃、冰、水能读取相同的 IBL、局部灯和体积参数。
- Wetness/Porosity 在主视图和 RTGI 次级命中中一致。

### 9.2 模型场景

- Metallic-Roughness 扫描球与 Khronos glTF Sample Viewer 参考趋势一致。
- Damaged Helmet、Flight Helmet、Sponza 等资产的法线、F0、Emissive 与 Probe 结果正确。
- Clearcoat 基底衰减、IOR F0 和 Volume Absorption 通过独立参考图验证。
- 多实例场景不随实例提交顺序改变灯光或反射结果。

## 10. 参考资料

- glTF 2.0 Specification：<https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html>
- glTF Material Extensions：<https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos>
- Physically Based Rendering, 4th Edition：<https://pbr-book.org/4ed/contents>
- LearnOpenGL PBR IBL：<https://learnopengl.com/PBR/IBL/Specular-IBL>
