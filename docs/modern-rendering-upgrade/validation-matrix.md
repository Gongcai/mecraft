# 现代渲染验收矩阵

## 1. 测试资产与环境

### 1.1 体素场景集

| ID | 场景 | 覆盖内容 |
| --- | --- | --- |
| V01 | Window Room | 单窗室内、太阳扫描、白炉材质、彩色发光方块 |
| V02 | Cave Turn | 洞穴拐角、完全离屏间接光、移动相机 |
| V03 | Forest Cutout | 大量树叶/草、风动、Cutout Ray Candidate |
| V04 | Glass Water Lab | 多层玻璃、染色玻璃、冰、水面、水下相机 |
| V05 | Streaming Flight | 高速飞行、区块加载/卸载、浮动原点 |
| V06 | Dynamic Blocks | 方块破坏/放置、活塞、移动方块、方块实体、生物 |
| V07 | Local Light Village | 火把、灯笼、发光方块、室内外 Probe 与局部阴影 |

### 1.2 模型场景集

| ID | 场景 | 覆盖内容 |
| --- | --- | --- |
| M01 | Material Grid | Metallic/Roughness/IOR/Clearcoat 扫描 |
| M02 | Damaged Helmet | 标准 Metallic-Roughness、Normal、IBL |
| M03 | Sponza Atrium | 大场景、局部灯、阴影、RTGI、Emissive |
| M04 | Transmission Lab | glTF Transmission/Volume、厚玻璃、多 Primitive |
| M05 | Instance Stress | 1000 实例、遮挡、LOD、GPU Culling |
| M06 | Animation Lab | Skin、Morph、Current/Previous Vertex、BLAS Update |
| M07 | Probe Interior | 多房间 Reflection Probe、Box Projection、镜面材质 |

测试资产、世界存档、Camera Path、随机种子、天气、时间、资产 Hash 和渲染设置均版本化。

### 1.3 已锁定的参考图

参考图清单当前锁定 18 个 OpenGL/Vulkan 捕获项。M0 体素与模型基线保持场景契约 v1；
V01、V02、V07、M01、M02、M03、M07 使用场景契约 v2，其中体素场景声明确定性 Fixture，
模型场景声明确定性 Reflection Probe Grid。

| 场景契约 | 版本 | 后端 | 捕获配置 | 当前状态 |
| --- | ---: | --- | --- | --- |
| `m0_voxel_baseline` | 1 | OpenGL/Vulkan | 1280×720，预热 300 帧，采样 3 帧 | 已锁定 |
| `m0_model_damaged_helmet` | 1 | OpenGL/Vulkan | 1280×720，预热 300 帧，采样 3 帧 | 已锁定 |
| `v01_window_room` | 2 | OpenGL/Vulkan | 1280×720，预热 300 帧，采样 3 帧 | 已锁定 |
| `v02_cave_turn` | 2 | OpenGL/Vulkan | 1280×720，预热 300 帧，采样 3 帧 | 已锁定 |
| `v07_local_light_village` | 2 | OpenGL/Vulkan | 1280×720，预热 300 帧，采样 3 帧 | 已锁定 |
| `m01_material_grid` | 2 | OpenGL/Vulkan | 1280×720，预热 300 帧，采样 3 帧 | 已锁定 |
| `m02_damaged_helmet` | 2 | OpenGL/Vulkan | 1280×720，预热 300 帧，采样 3 帧 | 已锁定 |
| `m03_sponza_atrium` | 2 | OpenGL/Vulkan | 1280×720，预热 300 帧，采样 3 帧 | 已锁定 |
| `m07_probe_interior` | 2 | OpenGL/Vulkan | 1280×720，预热 300 帧，采样 3 帧 | 已锁定 |

每张 PNG 同时记录字节数、FNV-1a 64 和 SHA-256；测试会重新解析场景契约，并以“场景契约
ID + 后端”验证唯一性。七个 v2 场景的 Vulkan 捕获均启用 Validation，未发现 Validation/VUID
错误。粒子不纳入本轮版本化参考图。

RTGI 专用 `rtgi_manifest.json` 另锁定 Vulkan `v03_forest_cutout`：它使用 v2 场景契约、
`m3_voxel_rtgi_quality`、1280×720、300 帧预热和 3 帧采样。fixture 固定 12 个树冠、草地与高草；
报告契约要求 3/3 有效 Counter Readback 以及非零 Candidate/Confirmed，防止 V03 退化为没有实际
Cutout Ray Candidate 的普通体素镜头。

## 2. 功能矩阵

| 能力 | 体素验收 | 模型验收 | 自动检查 | 通过标准 |
| --- | --- | --- | --- | --- |
| Voxel GI 删除 | 全部 | 全部 | 源码/构建/设置/Shader 引用扫描 | 不创建 3D 体积、不注册 Pass、运行时引用为 0 |
| 统一材质 | V01/V07 | M01/M02 | Reference Pixel、Material ID | Base/F0/Roughness/Emission 语义一致 |
| Clustered Light | V07 | M03 | Cluster Count、Light ID | 无容量错误，光源覆盖正确 |
| 局部灯阴影 | V07 | M03 | Shadow ID/Atlas Heatmap | 无错误复用，动态遮挡者更新 |
| GGX IBL | V01 | M01/M02 | Roughness Mip/DFG View | 扫描连续，高粗糙金属能量稳定 |
| Reflection Probe | V02/V07 | M07 | Probe ID/Weight | 室内外过渡连续，Box Projection 正确 |
| Bindless Material | V03/V07 | M05 | Slot/Generation Validation | 无悬空槽和材质错绑 |
| Chunk BLAS | V05/V06 | 不适用 | BLAS Revision/Overlay | 与光栅同修订、表面覆盖一致且无 T-Junction 漏光 |
| Model BLAS/TLAS | V06 实体 | M02/M05 | Instance Custom ID | 多实例共享，Transform 正确 |
| Cutout Ray Query | V03 | M03 Alpha Mask | Candidate/Confirmed Counter | 与光栅 Alpha Cutoff 一致 |
| Raw RTGI | V01/V02 | M03 | Hit/Miss/Radiance/Distance | 离屏能量稳定、无 NaN/Inf |
| NRD RELAX | V02/V06 | M03/M06 | Variance/Ghost Metric | 达到第 3 节门槛 |
| NRD REBLUR | V02/V06 | M03/M06 | Variance/GPU ms | 达到第 3 节门槛 |
| 多层透明 | V04 | M04 | Layer/Depth/Pair | 结果不依赖 Primitive 提交顺序 |
| 真实厚度 | V04 | M04 | Thickness/Optical Distance | 几何和材质变化符合 Beer-Lambert |
| 粗糙折射 | V04 | M04 | Refraction UV/Lobe | Roughness 连续改变模糊范围与能量 |
| 透明运动 | V04/V06 | M04/M06 | Velocity/Reactive | 无持续拖影或背景粘连 |
| GPU Culling | V05 | M05 | Cull Reason/Indirect Count | 可见结果完整，无 CPU Draw 线性增长 |
| LOD/Meshlet | V05 | M05 | LOD/Meshlet Overlay | 无抖动、裂缝、材质边界破坏 |
| Skin/Morph | V06 实体 | M06 | Position Delta/BLAS Age | 光栅、Velocity、RT Geometry 同步 |
| 动态分辨率 | V05/V07 | M03/M05 | Scale/GPU p95 | 收敛且无边缘/history 错位 |
| SDR 输出 | 全部 | 全部 | Reference Pixel | sRGB/亮度与参考一致 |
| HDR10/scRGB | V01/V07 | M01/M04 | Format/Metadata/Pixel | 实机亮度、色域、UI 合成正确 |

“不适用”只表示该资产不包含对应内容，不表示另一场景类别可以跳过同类核心能力。模型
BLAS 也用于体素世界中的实体和方块实体。

Cutout Ray Query 的底层 Vulkan GPU 契约已由真实 Compute Smoke 覆盖：Opaque 自动提交、Cutout
Candidate 拒绝/确认、Instance Custom Index、Geometry Index、Primitive ID 与 Barycentrics 均已回读
验证。生产 `RtgiTracePass` 另以 2×1 Terrain 场景验证 Barycentric UV、两帧动画纹理层、显式 LOD
Texture2DArray 采样和统一 Alpha Cutoff：透明像素必须产生 `Candidate/Confirmed = 1/0` 并命中后方
Opaque，实心像素必须产生 `1/1` 并确认前方 Cutout，且全程不得产生 Validation/VUID 错误。模型路径
另以 2×1 Static Mesh 场景执行同样两条射线，必须读取 Global Bindless Mask 纹理、输出 Opaque 与 Mask
各自的 Stable Material/Geometry Hash，并从 `RGBA16F` Alpha 回读对应 Hit Distance。该结果不替代
V03/M03 Alpha Mask 场景验收。

Terrain Primitive Metadata 基础层另由 GPU Buffer 回读与双代际生命周期 Smoke 约束：16 字节记录必须
与 CPU 生成值逐字节一致，Opaque/Cutout Geometry Index 必须映射到连续 Vertex/Primitive Base；新
Terrain BLAS 激活后，旧 Active TLAS 的 Custom Index 仍须保留旧 Vertex/Metadata Device Address，直到
对应 TLAS 代际完成退役。每个 TLAS 代际的 64 字节 Terrain 命中记录也必须逐字节匹配 CPU 编码；
Static Mesh Custom Index 对应 Terrain 记录必须保持全零，表字节数必须严格等于
`Instance Count × 64`。TLAS 代际 Material/Geometry/Instance 三张表还必须逐字节匹配 CPU 规范化结果，
共享模型资产的两个实例不得重复展开 Material/Geometry。

## 3. RTGI 与 NRD 画质门槛

### 3.1 Reference

每个固定镜头生成 64 spp Diffuse Reference，关闭 NRD，使用相同一次反弹材质与灯光公式。
随机序列固定，Reference 以 Linear EXR 保存。1 spp Raw 与 Denoised 都在相同 Pre-exposure
下比较。

### 3.2 静态画面

在 V01、V02、M03 的静止相机区域：

- Denoised Luminance Variance 相对 Raw 降低至少 70%。
- 32 帧积累后，非遮挡边缘区域 SSIM 不低于 0.95。
- 32 帧积累后，线性 HDR 相对误差的 95th Percentile 不高于 10%。
- Normal/Depth Discontinuity 两侧不得出现超过 2 Pixels 的稳定泄漏带。
- NaN/Inf、负 Radiance 与 AS Pending 像素计数为 0。

阈值以固定 ROI 计算，天空、曝光饱和区和刻意随机动画从静态指标中分离。

静态输入由 `assets/validation/rtgi_quality_profiles.json` v2 版本化。V01、V02、M03 当前均锁定 1280×720、
Camera Path 2.0 秒；V01 使用室内地面 `(256,544,256,128)`，V02/M03 使用 `(384,216,512,288)` ROI。运行时必须使用
32 帧 `raw_and_denoised` 捕获，报告记录
Profile ID、版本、ROI、质量 Render Settings 及 64 spp Reference 目标。ROI 内容在正式 Reference 采集前仍需
结合线性 HDR 与 Depth/Normal 边界检查确认；不得仅因矩形契约已固定而宣称画质门槛通过。
`--validation-rtgi-reference` 将同一 Profile 切换为严格 64 帧 Raw-only 运行：NRD 必须关闭，RTGI 的
低差异相位仍逐帧推进，报告记录 `capture_mode=reference` 与 `nrd_enabled=false`。这 64 个线性样本随后必须
平均为单张 Reference EXR；不能直接选取其中一帧作为参考。`rtgi_quality_report_tool` 已实现严格序列检查、
IEEE Half 解码、64 帧线性平均、32 帧 Raw/Denoised 方差与均值、SSIM、HDR Error p95 和非法辐射门禁。
Leakage Band 必须读取质量序列同次捕获的 ViewZ/Normal EXR；缺少或非法 Guide 会拒绝生成报告，不会生成
无证据的通过结论。AS Pending 必须由同次 Validation Capture Report 的保守整帧 Mask 提供，身份、样本数
或计数不一致也会直接拒绝生成质量报告。
Reference 帧首重置现明确排除 `referenceSamplingEnabled`：只有既没有 NRD、也没有 Reference Sampling 的路径
才将显式样本索引清零，成功的 Reference 帧在图执行后递增。每像素独立旋转、XOR 扰动的周期 64 点
Hammersley 集合保证任意连续 64 个实际帧覆盖同一完整集合，并以偶/奇交错让两个半序列覆盖完整二维域。
报告 schema v2 对前后两个 32 帧均值额外计算 SSIM 与
HDR Error p95；它们用于判断 Reference 收敛可信度，不是验证矩阵新增的最终门槛。

首轮 1 帧预热 V01 v1 数据覆盖中心窗口 Sky，不能用作门槛。旧 v2 室内地面数据的
Raw/Reference/Denoised ROI 平均亮度 `0.021384/0.021434/0.002634` 也不能使用：图外捕获读取了已别名的
Render Graph 瞬态信号。Raw 与 Denoised 已改为在最后有效图内访问时拷贝到持久 RGBA16F 输出。修复后的 V01
第一次正式运行仍因 readiness render 与正式 render 复用逻辑样本编号而让 NRD 每帧触发
`FrameDiscontinuity`。独立实际渲染时钟修复后，V01 的 Temporal Reset 证据为 Restart `0`、Continue `32`。
同一正式配置（300 帧预热、32+32/64）重采得到 Raw/Reference/Denoised 平均亮度
`0.002624319/0.002611804/0.002304740`；方差降低 `99.988285%`、SSIM `0.996256` 通过，HDR 相对误差 p95
`0.454668` 仍不通过。Reference 前后半诊断为 SSIM `0.985370`、HDR p95 `2.078409`，说明 64 spp 暗部估计
仍未稳定。捕获器按生产消费契约解析 RELAX 有限负振铃并解码 REBLUR YCoCg，NaN/Inf、Raw 负值和 FP16
溢出仍明确失败。这组结果把 Denoised 约 `11.8%` 能量损失与 Reference 高方差拆成两个独立定位轴。
同配置关闭 RELAX Anti-firefly 后，Denoised 均值为 `0.002572764`，相对 Reference 偏差由约 `-11.8%`
缩小到 `-1.5%`，SSIM 为 `0.997113`，方差降低为 `99.981906%`；HDR p95 为 `0.528282`，仍未通过。
该 A/B 确认 Anti-firefly 是稳定能量损失主因，RELAX 固定为关闭；Reference 半序列 p95 仍为 `2.078409`，
因此下一步先闭环 Reference 收敛性，再判断剩余逐像素误差。
质量报告 schema v3 额外发布相对误差 p50、绝对误差 p95、相对 p95 锚点和分母下限像素占比，但不改变既有
Gate。固定 64 spp A/B 已排除 8×8 分层序列，并采用偶/奇交错 scrambled Hammersley：SSIM `0.997353`、
HDR p95 `0.473326`。V01 的分母下限占比为 `0%`；Reference 前后半相对误差 p50 `0.269300`、p95
`1.103705`、绝对误差 p95 `0.003461`，说明失败来自可见性积分的广泛高方差，而不是近零亮度像素。
同一 V01 的 RELAX 主历史 30/64 帧 A/B 得到 HDR p95 `0.473326/0.473316`，排除主历史上限为静态失败主因，
正式设置保持按 0.5 秒计算。质量报告 schema v4 必须绑定同次 Validation Capture Report；AS Pending 采用
保守整帧 Mask，Scene TLAS 或 Terrain BLAS 任一 Pending 就把该采样帧全部像素计为无效。最新 300+32 正式
V01 的 Pending Frame、Invalid Pixel、Terrain Build/Compaction 峰值均为 `0`，因此 AS Pending Gate 已通过。
质量报告 schema v5 已强制绑定生产捕获的 RGBA16F 世界法线和正线性 ViewZ，Leakage 不再缺证据。
Leakage 指标以 ViewZ 相对差 `> 0.02` 或单位世界法线点积 `< 0.95` 建立几何边界；仅当 Reference 在边界
两侧的亮度差超过 `max(1e-4, 0.10 * max(sideA, sideB))` 时建立方向性 Seed。暗侧只追踪超过边界对比 `10%`
的正能量增益，亮侧只追踪同阈值的负能量损失；4 邻域扩张不得跨越另一条 Depth/Normal 边界，最大带宽门槛
保持 `<= 2 Pixels`。合成契约测试覆盖 2/3 Pixels 和非法 Guide。V01 300+32 正式报告的
`missing_evidence=[]`，但 `boundary_pixel_count=310`、`leakage_pixel_count=601`、最大带宽 `3 Pixels`，因此
Leakage Gate 与完整静态门槛仍失败。最大路径的边界 Seed 为捕获坐标 `(269,548)/(268,548)`，ViewZ 相对差
`0.479685`、Normal dot `0.008317`，末端 `(272,548)` 为暗侧能量增益；后续修复不得修改门槛、ROI 或报告缩放。

### 3.3 动态画面

在 V03、V06、M06 的 Camera Path：

- 物体离开像素后 8 帧内，历史残留亮度低于原贡献的 2%。
- Camera Translation/Rotation 的重投影方向错误像素为 0。
- 2.5D Motion 的 Z 分量满足 `previousViewZ - currentViewZ`，动态物体深度变化可被拒绝。
- Cutout 风动边缘不形成持续 3 帧以上的实色拖尾。
- Camera Cut 后首帧 History Length 为 0，后续单调进入稳定范围。
- Dynamic Resolution Rect 变化时无未清理边缘和周期闪烁。

### 3.4 RELAX 与 REBLUR

两种 Method 分开存档和判定：RELAX 以质量门槛为主，REBLUR 同时检查质量与成本。
Method 切换必须清空对应历史，并在 Dashboard 显示新 Method 与 Reset Reason。

## 4. 透明画质门槛

| 测试 | 操作 | 通过标准 |
| --- | --- | --- |
| 排序不变性 | 随机化 Primitive/Instance 提交顺序 100 次 | 输出 Hash/浮点容差内一致 |
| 多层观察 | 三层不同颜色玻璃叠放 | 前层可读取已处理后层，次序正确 |
| 厚度 | 0.25/0.5/1/2 m 同材质体积 | Optical Distance 线性，吸收符合公式 |
| 粗糙度 | 0..1 连续扫描 | 无 LOD 台阶，模糊半径单调 |
| IOR | 1.0/1.33/1.5/2.0 | 折射方向、F0、TIR 与参考一致 |
| 水下切换 | 相机穿越水面 | 介质、曝光与历史在同帧切换 |
| 动态玻璃 | 活塞/模型平移旋转 | Velocity 正确，无背景粘连 |
| 容量 | 构造超过 PPLL 容量的像素 | 返回准确错误、计数和热图 |

## 5. GPU Scene 与 AS 稳定性

### 5.1 压力测试

- V05 连续飞行 30 分钟，区块加载/卸载与浮动原点循环。
- V06 每帧批量编辑边界方块，验证相邻 SubChunk 修订。
- M05 反复创建/销毁 1000 实例与 100 资产。
- M06 播放 Skin/Morph 动画 30 分钟并循环 Asset Reload。
- Resize/Fullscreen/World Reload/Backend Restart 各执行 100 次。

### 5.2 通过标准

- Vulkan Validation Error 为 0。
- Bindless Generation、Device Address、Submission Token Error 为 0。
- 光栅与 TLAS Overlay 中不可解释的不一致为 0。
- 资源卸载后 AS/Descriptor 不再引用被释放对象。
- 稳态显存无单调增长，峰值不超过预算。
- AS Build Queue 在停止场景修改后于规定帧数内清空。

## 6. 性能矩阵

目标设备：RTX 4060 Laptop 8GB，固定高性能电源模式。正式性能组每组预热 300 帧，采集 1000 个
真实渲染帧；参考图仍使用 3 帧采样。正式 GPU p95 必须来自完整 Render Graph Frame Span，不能使用
只覆盖显式阶段的阶段和。

| 场景 | Output/模式 | 总 GPU p95 | CPU Render p95 | 显存 |
| --- | --- | ---: | ---: | ---: |
| V07 | 1080p、Modern Quality | ≤ 16.67 ms | ≤ 3.0 ms | ≤ 6.5 GiB |
| V05 | 1080p、Modern Quality、Streaming | ≤ 16.67 ms | ≤ 3.5 ms | ≤ 6.5 GiB |
| M03 | 1080p、Modern Quality | ≤ 16.67 ms | ≤ 3.0 ms | ≤ 6.5 GiB |
| M05 | 1080p、1000 Instances | ≤ 16.67 ms | ≤ 3.0 ms | ≤ 6.5 GiB |

同时报告每个大 Pass p50/p95/p99、Triangle、Ray、Candidate、Draw、Cluster、PPLL Node、
BLAS/TLAS 和 NRD Dispatch 数。Frame Generation 只单列展示 FPS，不用于满足 GPU p95。

### 6.1 2026-08-08 复测观察（尚非门禁结果）

以下数据来自 RTX 4060 Laptop、Vulkan、RELAX_DIFFUSE、场景契约 v2，300 帧预热加 1000 帧采样。
`已追踪 GPU p95` 是显式 Pass 阶段和，未包含完整 Render Graph 跨图跨度、AS/TLAS 和调度间隙；
`update+render p95` 是墙钟时间，不能作为纯 CPU Render p95。两种口径都不满足完整 GPU 门禁，
仅用于定位当前瓶颈。

| 场景 | 分辨率 | 已追踪 GPU p95 | RTGI.Trace p95 | NRD.Dispatch p95 | update+render p95 | 平均 FPS |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| V02 体素洞穴 | 1280×720 | 9.215 ms | 2.664 ms | 2.959 ms | 31.734 ms | 39.42 |
| M03 Sponza | 1280×720 | 10.870 ms | 4.742 ms | 3.642 ms | 14.207 ms | 88.83 |
| V02 体素洞穴 | 1920×1080 | 18.359 ms | 5.462 ms | 6.624 ms | 65.510 ms | 19.07 |
| M03 Sponza | 1920×1080 | 23.321 ms | 10.665 ms | 7.261 ms | 29.974 ms | 40.92 |

当前状态：四组均为“未判定”。1080p 体素的墙钟 p95 为 65.510 ms，1080p Sponza 的 RTGI.Trace 和
NRD.Dispatch 是明确的 GPU 热点；720p Sponza 的墙钟 p95 约 14.2 ms，不能外推到体素或 1080p。
在完整 Span 与 AS/TLAS 计时发布前，不得把任何一组写成“达到 ≤16.67 ms”或作为动态分辨率控制器
的目标样本。

## 7. 后端能力测试

### 7.1 OpenGL Base

- Deferred/Forward、基础 PBR、CSM、SSAO、现有 SSGI/SSR 按现有测试运行。
- `VoxelGiClipmap`、Voxel GI 设置、UI、Shader Variant 和 GPU 资源不存在。
- RTGI、NRD、PPLL Multi-layer、Bindless GPU Scene、HDR10/scRGB 不出现在可选设置中。
- API/日志返回稳定的 `BackendFeatureUnavailable` 原因。
- 加入公共 GPU Scene/RHI 类型后，OpenGL 编译和 Smoke Test 继续通过。

### 7.2 Vulkan Modern

- 枚举并验证 AS、Ray Query、Descriptor Indexing 各子特性和限制。
- Vulkan Shader Target、Acceleration Structure Descriptor 与运行时数组 Reflection 正确。
- Gameplay 与模型场景运行时创建 Global Bindless Set，Active TLAS 发布到固定 Binding 4；重复代际不
  产生 Descriptor 写入，Dashboard 的 Revision 与更新计数一致。
- Terrain Primitive Metadata 回读值、Geometry Index 范围及 TLAS 代际 Device Address 快照一致，
  BLAS 换代期间不出现悬空或跨代地址。
- Modern 设置只有在所有硬要求满足时可选。
- 缺少单个扩展/特性的模拟设备测试能返回对应错误码。

## 8. 色彩与显示测试

- AP1/Rec.709/Rec.2020 转换矩阵 CPU 与 GPU Reference Test。
- Macbeth ColorChecker 与灰阶的 SDR Screenshot Delta。
- 100/203/1000 Nit Patch 的 HDR10 PQ Code Value。
- HDR Metadata 与用户 Peak/Paper White 一致。
- SDR/HDR 中 UI 白色、透明度与 Premultiplied Alpha 一致。
- Exposure 变化时 RTGI/NRD/SSR/Transparent History 无亮度脉冲。

## 9. 构建与平台

| 平台 | 后端 | 构建 | 必测内容 |
| --- | --- | --- | --- |
| Linux | Vulkan | `build.sh` | RHI、AS、RTGI、NRD、Render Graph、SDR；设备支持时测 HDR |
| Linux | OpenGL | `build.sh` | Base 能力与公共代码编译 |
| Windows | Vulkan | `build.ps1` | 全部现代管线、DLSS/FSR、Frame Generation、HDR |
| Windows | OpenGL | `build.ps1` | Base 能力与设置边界 |

每次发布记录 OS、Driver、Vulkan Loader、NRD Version、Upscaler/Frame Generation SDK 与
GPU VBIOS/功耗模式。

## 10. 第三方与发布检查

- NRD 固定 4.17.3，源码 Commit 可追踪。
- NVIDIA RTX SDK License 文本与 Notice 位于发布包要求位置。
- `MECRAFT_ENABLE_NRD` 开关在启用/关闭构建均通过。
- Shader/Library 二进制产物来自固定版本，构建日志记录 Hash。
- 设置界面显示 NRD Method 与版本。

## 11. 发布门禁

发布候选必须同时满足：

1. 功能矩阵中的所有 Must 项通过。
2. 体素与模型画质指标均达标。
3. 四个性能场景达到总时间、CPU 与显存预算。
4. 长时压力测试无资源、同步、容量和历史错误。
5. OpenGL/Vulkan 能力状态准确，不改变用户所选算法。
6. Linux/Windows 规定构建与 Smoke Test 通过。
7. NRD 与其他第三方许可证检查通过。
