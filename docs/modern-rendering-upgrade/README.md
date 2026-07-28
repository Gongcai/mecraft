# Mecraft 现代渲染升级总纲

> 编制日期：2026-07-28
>
> 目标后端：Vulkan 1.3 现代管线、OpenGL 基础管线
>
> 目标场景：Minecraft 风格体素游戏世界、glTF 模型展示场景

## 1. 结论

当前 `VoxelGiClipmap` 已被画质测试否决：画面没有可见提升，只增加 GPU/CPU 时间和资源
占用。本方案将其按废弃代码直接删除，不保留后端、调试、兼容、基线或资产接入要求。

Mecraft 的 Vulkan 高级画面路线直接建设硬件 RTGI。RTGI 以 `VK_KHR_ray_query`、统一
GPU Scene、体素区块与模型 BLAS/TLAS、次级命中材质求值、NVIDIA NRD 时空降噪为核心。
首个质量模式使用 `RELAX_DIFFUSE`，性能模式使用 `REBLUR_DIFFUSE`。

这不是模型预览器专属方案。方块地形、异形方块、水体、方块实体、生物、掉落物、
粒子和 glTF 模型必须遵循同一套场景、材质、灯光、运动矢量、曝光与时域历史契约。
任何阶段同时提交体素世界和模型场景的验收证据，单一场景通过不代表该阶段完成。

OpenGL 保持独立的基础功能集。Vulkan 现代功能未满足设备、驱动或构建条件时，设置项
保持不可选并显示准确原因；渲染器不会暗中切换为名字相似的另一种效果。

## 2. 当前基线与关键缺口

当前已经具备 Deferred/Forward、Render Graph、Vulkan/OpenGL RHI、CSM/PCSS、
SSAO、SSGI、SSR、体积雾、体积云、物理大气、TAA、FSR 1、FSR 3.1、
DLSS、HDR 中间颜色和 Hi-Z。glTF 路径已经支持 metallic-roughness，并处理
`KHR_materials_ior`、`KHR_materials_clearcoat`、`KHR_materials_transmission` 与
`KHR_materials_volume` 的主要参数。

代码库目前仍包含 `VoxelGiClipmap`、3D 纹理更新、Scene Composite Shader Variant、设置
序列化和 UI 控件，这些属于待删除实现，不计入当前能力，也不进入任何后续阶段的依赖图。

当前距离完整现代画面的关键链路是：

1. RHI 只有光追能力字段，缺少加速结构资源、构建命令、资源状态和 Ray Query 绑定。
2. Vulkan 着色器编译目标仍为 Vulkan 1.1 / SPIR-V 1.3。
3. Descriptor Indexing 已查询，但 Bind Group 限制 `arrayCount == 1`，尚未形成
   Bindless 材质表。
4. 场景缺少统一局部光源缓冲、Clustered Light Culling 和局部灯阴影链。
5. 环境光只有低阶 SH 与方向天空采样，缺少 GGX 预过滤、DFG LUT 和局部反射探针。
6. SSGI 的屏幕空间信息缺失无法由现有降噪器恢复；Vulkan RTGI 需要真实世界空间命中
   与专用降噪器。
7. 透明物按 Primitive 中心距离排序，已有 IOR、粗糙折射和体积吸收，但缺少逐像素
   多层排序、真实前后表面厚度和透明运动矢量。
8. 模型实例仍由 CPU 逐实例、逐 Primitive 提交，缺少统一 GPU Scene、实例剔除、LOD
   与间接绘制。
9. Vulkan 交换链固定为 SDR，动态分辨率也未形成基于 GPU 时间的控制闭环。

## 3. 固定架构决策

| 主题 | 决策 |
| --- | --- |
| Voxel GI | 直接删除实现、资源、设置、UI、Shader Variant 和构建项 |
| Vulkan GI | 单次反弹 Diffuse RTGI + NRD，从统一场景与材质契约独立建设 |
| 首版光追执行模型 | Compute Shader + `VK_KHR_ray_query`，暂不引入 RayGen/Miss/Hit/SBT |
| 质量降噪 | `RELAX_DIFFUSE` |
| 性能降噪 | `REBLUR_DIFFUSE` |
| OpenGL | 固定基础光栅功能集，不承诺 RTGI、NRD、Bindless GPU Scene 与 HDR 交换链 |
| PBR | 统一材质语义，体素纹理数组与 glTF 纹理均映射到同一 BRDF 输入 |
| 局部灯 | Clustered Deferred + Forward+ 透明共享同一 `GpuLight`/Cluster 列表 |
| 环境反射 | GGX 预过滤天空 + DFG LUT + Reflection Probe Grid；Vulkan 可增加混合光追反射 |
| 多层透明 | Vulkan PPLL 收集与排序解析，显式报告存储耗尽，不静默丢弃层 |
| 场景提交 | GPU Scene、Bindless 材质、GPU Culling、Indirect Draw 为 Vulkan 主路径 |
| 时域输出 | 完整运动矢量与历史失效契约、GPU 时间驱动动态分辨率、显式 SDR/HDR 显示模式 |

## 4. 目标帧图

```text
World / Model Scene Update
        │
        ├── SceneUpload: instances, materials, lights, mesh revisions
        ├── AccelerationStructureBuild: chunk/model BLAS + TLAS
        └── GPU Culling: clusters, instances, indirect commands
        │
        ▼
Depth + GBuffer + Complete Motion Vectors
        │
        ├── Hi-Z / Clustered Light Lists / Shadow Maps
        ├── Direct PBR Lighting
        ├── RTGI Trace ──► NRD RELAX or REBLUR
        ├── Reflection: SSR + Probe + selected RT signal
        └── Atmosphere / Volumetrics / Clouds
        │
        ▼
Opaque HDR Composite
        │
        ├── Scene Color Pyramid
        ├── Transparent Layer Gather
        └── Sorted Refraction / Absorption / Reflection Resolve
        │
        ▼
Temporal Upscale / Exposure / Bloom / Tone Map
        │
        ├── SDR sRGB
        ├── HDR10 PQ
        └── scRGB
        │
        ▼
HUD-less Color ──► Frame Generation ──► UI Composite ──► Present
```

## 5. 双场景统一原则

### 5.1 体素世界不是简化版场景

体素世界必须获得完整的 RTGI、PBR、局部灯、反射、透明和时域能力：

- 贪心网格的每个三角形能定位方块材质、面方向、纹理层和 UV 重复规则。
- 区块网格修订号驱动 BLAS 生命周期，区块移动只改变 TLAS Instance Transform。
- Cutout 树叶、草和异形方块在 Ray Query Candidate 阶段执行 alpha 判定。
- 方块发光定义生成表面 Emissive，并可生成参与 Clustered Lighting 的解析光源代理。
- 天光、方块光、湿度、孔隙度、顶点 AO 等游戏语义进入统一材质与光照输入。
- 水、玻璃、冰、染色玻璃使用与模型 Transmission/Volume 相同的透射核心。
- 生物、掉落物、方块实体、移动方块和第一人称物体提供当前/上一帧 Transform。

### 5.2 模型展示不是独立特效岛

模型展示场景使用同一 Deferred Lighting、RTGI、NRD、Reflection Probe、透明解析和
时域输出链。glTF 扩展被规范化到统一材质表；静态、蒙皮、Morph 和实例化模型通过
GPU Scene 提交。模型预览中玻璃的折射与模糊必须来自正式透明链，不保留一套只在
预览窗口生效的着色逻辑。

## 6. 后端能力契约

| 能力 | OpenGL 基础管线 | Vulkan 现代管线 |
| --- | --- | --- |
| Deferred PBR / CSM / SSAO | 支持 | 支持 |
| 现有 SSGI / SSR | 支持 | 可作为独立调试模式，现代预设不参与 GI 合成 |
| `VoxelGiClipmap` | 不保留 | 不保留 |
| glTF 基础材质与现有扩展 | 支持 | 支持 |
| Clustered 局部灯 | 不在本轮承诺范围 | 支持 |
| GGX IBL / Reflection Probe Grid | 不在本轮承诺范围 | 支持 |
| RTGI / NRD | 不支持 | 支持 |
| 多层折射 PPLL | 不支持 | 支持 |
| Bindless GPU Scene | 不支持 | 支持 |
| GPU 时间动态分辨率 | 不在本轮承诺范围 | 支持 |
| HDR10 / scRGB 交换链 | 不在本轮承诺范围 | 支持 |

能力选择由后端、构建开关、设备特性和用户设置共同确定。不可用状态包含稳定错误码、
面向用户的中文原因和 Dashboard 诊断字段。

## 7. 文档导航

- [01-渲染架构与能力契约](01-渲染架构与能力契约.md)：RHI、Render Graph、统一场景
  数据和后端边界。
- [02-灯光与PBR材质链](02-灯光与PBR材质链.md)：Clustered Lighting、阴影、IBL、
  Reflection Probe 和双场景材质规范。
- [03-Vulkan-RTGI与NRD](03-Vulkan-RTGI与NRD.md)：AS、Ray Query、次级命中、NRD
  集成和体素区块生命周期。
- [04-透明折射与反射](04-透明折射与反射.md)：PPLL、多层玻璃、厚度、Beer-Lambert
  吸收和反射组合。
- [05-GPU-Scene与资产管线](05-GPU-Scene与资产管线.md)：Bindless、GPU Culling、LOD、
  Indirect 和动画资产。
- [06-时域输出与性能](06-时域输出与性能.md)：运动矢量、动态分辨率、HDR、预算与遥测。
- [roadmap](roadmap.md)：阶段依赖、交付物、完成定义和风险控制。
- [validation-matrix](validation-matrix.md)：体素世界、模型场景、跨后端与性能验收矩阵。

## 8. 方案完成定义

整个升级完成必须同时满足：

1. Vulkan 现代预设在体素世界与模型场景中均通过画质、稳定性和性能验收。
2. RTGI 的输入、输出和 NRD 历史可在 Dashboard 中逐项检查。
3. 透明玻璃能够正确呈现前后表面、粗糙折射、层间吸收和多层排序。
4. 方块与 glTF 材质在相同灯光下遵循一致的 BRDF 与曝光响应。
5. GPU Scene 消除模型逐 Primitive CPU 提交，区块与模型共享可观测的剔除统计。
6. 所有时域效果对相机切换、传送、尺寸变化和内容修订执行统一历史失效。
7. 不支持的功能状态可解释、可测试，且不会更改用户选择的渲染算法。
8. 第三方依赖许可证、版本、构建开关和运行时版本均可追踪。
9. 产品源码、CMake、设置、UI、Shader 与 Render Graph 中不再存在 Voxel GI 运行时链路。
