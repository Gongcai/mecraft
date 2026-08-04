# Mecraft

<div align="center">

![C++17](https://img.shields.io/badge/C++-17-blue.svg)
![RHI](https://img.shields.io/badge/RHI-Vulkan%201.3%20%7C%20OpenGL%204.5-brightgreen.svg)
![CMake](https://img.shields.io/badge/CMake-3.20+-orange.svg)
![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux-lightgrey.svg)
![Lines](https://img.shields.io/badge/Code-%E7%BA%A619%20%E4%B8%87%E8%A1%8C-9cf.svg)

**一个基于 C++17、面向 Vulkan 1.3 与现代光追的统一 RHI 体素游戏引擎**

体素沙盒世界 + glTF 模型双场景 · 硬件 RTGI 实时全局光照 · NRD 降噪 · Bindless GPU Scene · 聚类灯光 · 客户端/服务器联机架构

</div>

---

## 项目简介

Mecraft 是一个从零构建的桌面端体素沙盒游戏引擎，目标是还原 Minecraft 的核心体验，并在此之上提供一套**电影级画质的内置渲染管线**与**真正的客户端/服务器联机架构**。在保留 Minecraft 风格体素世界的同时，项目加入了独立的 **glTF 模型展示场景**，体素与模型共享同一套现代渲染契约（灯光、材质、RTGI、时域输出）。

项目使用现代 C++17 编写，当前约 **19 万行代码（797 个源文件）**，采用 EnTT 驱动的 Entity Component System (ECS) 架构。渲染层已从单一 OpenGL 状态机演进为**统一的 RHI (Render Hardware Interface) 抽象**，同时提供 **Vulkan 1.3 现代后端**与 **OpenGL 4.5 基础后端**，两套后端共用同一个 RenderScene / RenderPipeline / RenderGraph 高层框架。

单机与联机共用同一套 C/S 代码——单机模式下客户端连接的是一个进程内嵌的权威服务器，因此单人游戏与多人联机在逻辑上完全一致。

## 核心特性

### 体素世界

- **自研噪声地形** — 基于哈希的 value noise + fBm 分形叠加（**非第三方噪声库**），含手写 SSE2 / AVX2 SIMD 优化。生成阶段涵盖大陆度、山脊、湿度群系、3D 洞穴雕刻、分层矿石、植被与树木。
- **群系系统** — 温带 / 干旱 / 山地 / 高山四种群系，由大陆度、山脊与湿度阈值共同判定地表方块与覆盖层。
- **区块结构** — 16×256×16 区块沿 Y 轴切分为 16 个 16³ 子区块（SubChunk），方块数据采用**调色板 + 位压缩数组**存储，空气子区块零分配。
- **多线程流式加载** — 区块生成、网格构建（`ChunkMeshingService`）、光照求解均在线程池中异步执行，主线程按帧预算合并结果。
- **BFS 体积光照** — 经典泛洪式光照传播，阳光与方块光各 0–15 级 packed 存储。光照求解完全多线程化，支持玩家挖/放方块的交互式即时光照。
- **昼夜与天气** — 一天 20 分钟昼夜循环（日月旋转 + 8 相月相）；天气系统含晴 / 雨 / 雷暴 / 雪，派生湿度、降水强度、闪电等参数（指数半衰期平滑）。
- **流体系统** — 基于计划方块刻 + 优先队列的水流扩散，物理系统读取水位做浮力与流向。

### 渲染系统与 RHI 架构

Mecraft 引入了一套显式的 **RHI (Render Hardware Interface)** 抽象层，公共接口完全按 Vulkan 的显式资源 / 描述符 / 命令 / 同步模型设计，OpenGL 仅作为该模型的一个后端实现。高层渲染由 `RenderScene` 编排，通过统一的 `RenderPipeline` 接口在前向与延迟双管线间切换，并共享后处理与时域链。

```
Game / UI / Resource / Renderer
        ▼
RenderScene / RenderPipeline / RenderGraph
        ▼
RHI 公共接口
   ├── Vulkan 1.3 后端（现代）
   └── OpenGL 4.5 后端（基础）
```

**Vulkan 现代管线**核心能力：

| 能力 | 说明 |
| --- | --- |
| **硬件 RTGI** | 基于 `VK_KHR_ray_query` 的 Compute 射线查询，单次反弹 Diffuse 全局光照，世界空间命中（体素区块 + 模型 BLAS/TLAS），次级命中材质 / Emissive / 天空辐射 |
| **NRD 降噪** | 集成 NVIDIA NRD 4.17.3，Quality 用 `RELAX_DIFFUSE`、Performance 用 `REBLUR_DIFFUSE`，含 2.5D 运动向量、Pre-exposure 转换与按所有者的 History Reset |
| **Bindless GPU Scene** | 全局 Bindless Set（2D/Cube 纹理 + Sampler + Storage Buffer 数组）、GPU-only 场景表、连续 Dirty Span 上传与代际保护的槽位分配 |
| **加速结构 (AS)** | BLAS/TLAS 生命周期（Build/Compact/Clone/Update），体素 SubChunk 按修订驱动 `TerrainBlasCache`，glTF 静态网格共享 `SceneBlasResource` |
| **聚类灯光 (Clustered Lighting)** | `GpuLight` 缓冲 + 集群构建，Deferred / Forward+ 共享，支持 Point / Spot 局部灯与 Shadow Atlas / Cube Array |
| **IBL 环境光** | 动态天空 128×128 HDR Cubemap + 8 级 GGX 预滤波链 + 256×256 Split-sum DFG LUT |
| **反射探针** | Reflection Probe Grid + Box Projection，六面 Radiance 捕获与 54 项确定性 Prefilter 队列，体素 / 模型双场景接入 |
| **现代超分** | AMD FidelityFX FSR 3.1（随 Vulkan 后端编译）；NVIDIA DLSS 3 Frame Gen（Streamline 2.12，Windows，可选开关） |
| **Pre-exposure HDR** | 自适应曝光回读 + 按 2 的幂整档量化的 Pre-exposure 域，RTGI / Lighting / Composite 全程预曝光 |

**通用渲染特性**：

```
SkyCapture → GBuffer(地形/实体/掉落/坠落方块) → Velocity → Shadow(CSM/PCSS)
→ SSAO → DeferredLighting → SSR → Cloud → SceneComposite → WaterComposite
→ 透明地形 → 粒子 → Volumetric Fog → TAA → Motion Blur → DoF → Post(Bloom/Tonemap/Grade)
```

| 特性 | 状态 | 说明 |
| --- | --- | --- |
| **延迟渲染管线** | ✅ | 5-MRT GBuffer，前向 / 延迟双路径自动切换 |
| **实体 GBuffer** | ✅ | 人形实体、掉落物、坠落方块均写入延迟 GBuffer，接收完整延迟光照 |
| **级联阴影 (CSM)** | ✅ | 4 级联，PCSS 软阴影 + PCF + 接触阴影 + cascade 过渡混合 |
| **PBR BRDF** | ✅ | Cook-Torrance（GGX + Smith G2）+ Hammon 漫反射 |
| **球谐天光 (FromSH)** | ✅ | L1 球谐环境照明 |
| **大气散射 LUT** | ✅ | Bruneton 预计算大气散射，太阳临边昏暗、月亮渲染 |
| **体积云 / 体积雾** | ✅ | 光线步进 + 多阶 3D 噪声 + Beer-Powder 散射 + 时域重投影 |
| **屏幕空间反射 (SSR)** | ✅ | 反射探针 + 粗糙度自适应 + 时域重投影 |
| **环境光遮蔽 (SSAO)** | ✅ | 半分辨率 + 双边滤波 + 时域累积 |
| **时域抗锯齿 (TAA)** | ✅ | YCoCgR AABB 裁剪，Reinhard 域混合 |
| **水体渲染** | ✅ | 波浪法线 + 菲涅耳折射 + 水雾 + 水下效果 + SSR |
| **HDR 后处理** | ✅ | 自动曝光、Bloom、色彩分级、CAS、运动模糊、景深，前向 / 延迟共用 |
| **SSGI（OpenGL 路径）** | ✅ | 屏幕空间间接光照，Vulkan Modern 预设不参与 GI 合成 |
| **RTGI / NRD（Vulkan 路径）** | ✅ | 硬件光追实时全局光照 + NRD 时空降噪 |

### glTF 模型展示场景

项目内置独立的 **glTF 模型场景**（`ModelSceneAppState`），与体素世界共用同一套 Deferred / RTGI / NRD / Reflection Probe / 时域输出链：

- 支持 glTF **PBR metallic-roughness** 材质，及 `KHR_materials_ior`、`KHR_materials_clearcoat`、`KHR_materials_transmission`、`KHR_materials_volume` 扩展。
- 支持 `KHR_lights_punctual` 点光 / 聚光，接入 Clustered Lighting。
- 编辑器支持场景层次、手工 / 规则网格 Reflection Probe、Box Projection 与命令历史。

### 客户端 / 服务器联机架构

- **统一 C/S 架构** — 单机模式用进程内零拷贝传输（`InProcessTransport`，直接传 `shared_ptr<Chunk>`）；联机模式用 **ENet (UDP)** 传输（`ENetTransport`）。逻辑完全一致。
- **权威服务器** — `GameServer` 拥有 `World` 并以 20 TPS 跑 tick 循环，权威处理方块破坏 / 放置、实体同步、玩家移动和解、聊天命令、自动存档。
- **独立专用服务器** — `mecraft_server` 可执行目标（`dedicated_server.cpp`）。
- **网络协议** — 自定义二进制小端协议，多条信道，区块数据走 RLE 压缩。
- **已同步内容** — 玩家移动、方块交互（权威 + 批量广播 + 光照增量）、掉落物、生物、抛射物、物品栏、世界时间 / 天气、游戏模式、死亡 / 重生。

### 存档系统

- **区块** — 自定义二进制 **MCHK** 格式（magic + 版本 + CRC32 + 调色板编码位压缩 payload），区域文件缓存，原子写。
- **世界元数据** — `level.json`（种子、出生点、时间、天数、天气、模式、时间戳、缩略图）。
- **玩家状态** — JSON 保存位置 / 朝向 / 血量 / 护甲 / 饥饿 / 物品栏。
- **持久实体与方块实体** — 怪物、掉落物、箱子物品栏均 JSON 序列化。
- **自动保存** — 每 5 分钟 flush，区块异步写盘，关闭时阻塞 flush。

### ECS 与游戏逻辑

- **EnTT 驱动** — 区分 `fixedUpdate`（60Hz 物理）与 `tick`（20 TPS 模拟）两条流水线，并区分客户端 / 服务器 profile。
- **系统覆盖** — 玩家输入与物理、方块交互、战斗与伤害、物品生成 / 拾取 / 合并、怪物 AI、粒子、流体刻、坠落方块、人形动画、脚步音频、网络插值。
- **物理** — AABB 分轴 sweep 碰撞（Y→X→Z），DDA 体素射线检测 + slab 精确求交，支持行走 / 冲刺 / 跳跃 / 潜行 / 游泳 / 创造飞行。

### 物品、粒子与音频

- **物品与合成** — ItemID 独立于 BlockID，命名空间化注册表，Block↔Item 双向映射，掉落表；合成配方从 JSON 加载。
- **粒子** — billboard 粒子可渲入 SceneComposite 并接受统一体积雾；独立的纹理化降水渲染器（雨 / 雪），输出天气遮罩。
- **音频** — OpenAL Soft 3D 空间音频，WAV + OGG Vorbis 解码，音源池复用，BGM 系统与音效随机变体。

## 现代渲染升级路线（M0–M7）

项目将渲染升级拆分为阶段化路线（详见 `docs/modern-rendering-upgrade/roadmap.md`），体素世界与模型场景同步推进：

| 阶段 | 主题 | 状态 |
| --- | --- | --- |
| **M0** | 契约与测量基线、删除 Voxel GI | ✅ 完成 |
| **M1** | 统一灯光 / Clustered Lighting / PBR IBL / 反射探针 | ✅ 完成 |
| **M2** | Bindless GPU Scene / 加速结构 (BLAS/TLAS) / Vulkan 1.3 SPIR-V 1.6 | ✅ 完成 |
| **M3** | RTGI / NRD 实时全局光照 | ✅ 完成（数值画质门槛待接入自动验收） |
| **M4** | GPU Culling / LOD / 动画 | ⏳ 规划中 |
| **M5** | 多层透明 (PPLL) / 现代反射 | ⏳ 规划中 |
| **M6** | 动态分辨率 / HDR10 / 全管线优化 | 🔄 部分（FSR3.1、DLSS、Pre-exposure 已就绪） |
| **M7** | 双场景发布验收 | 🔄 进行中（7 个场景契约 v2 + 18 项参考图已锁定） |

当前已锁定 V01、V02、V07、M01、M02、M03、M07 共七个版本化验收场景（含体素与模型），以 300 帧预热、3 帧采样生成 OpenGL / Vulkan 1280×720 正式参考图。近期工作集中在火把点光性能验证、点光阴影优化与 RTGI 时域稳定性。

## 技术栈

| 模块 | 技术 / 库 |
| --- | --- |
| **编程语言** | C++17 |
| **构建系统** | CMake 3.20+（默认使用 vcpkg / Ninja） |
| **图形 API / RHI** | Vulkan 1.3 + OpenGL 4.5（统一 RHI 抽象，含 GLAD） |
| **实时全局光照** | NVIDIA NRD 4.17.3（RELAX / REBLUR，Vulkan） |
| **超分辨率** | AMD FidelityFX FSR 3.1（Vulkan）；NVIDIA Streamline 2.12 / DLSS 3（Windows，可选） |
| **模型场景** | cgltf（glTF 解析）+ mikktspace（切线） |
| **窗口与输入** | GLFW 3 |
| **数学库** | GLM |
| **ECS 框架** | EnTT |
| **音频引擎** | OpenAL Soft + libvorbis |
| **字体渲染** | FreeType |
| **序列化** | nlohmann/json |
| **网络传输** | ENet (UDP)，随源码内置编译 |
| **调试 UI** | Dear ImGui + ImGuizmo，随源码内置编译 |
| **图像加载** | stb_image / stb_image_write，随源码内置编译 |

依赖分两类：`glfw3`、`glm`、`nlohmann_json`、`OpenAL`、`Vorbis`、`Freetype`、`EnTT` 通过 `find_package` 解析（推荐用 vcpkg 提供）；`glad`、`enet`、`imgui`、`imguizmo`、`stb`、`cgltf`、`mikktspace`、`nrd`、`fidelityfx_fsr31` 已置于 `third_party/` 随项目一同编译。

## 快速开始

### 环境准备

- C++17 兼容编译器（MSVC 2022/2025、GCC、Clang）
- CMake 3.20 或更高版本
- 推荐使用 vcpkg 安装外部依赖：

```bash
vcpkg install glfw3 glm nlohmann-json openal-soft libvorbis freetype entt
```

> 构建 NRD / FSR3.1 需要 Vulkan SDK（Vulkan 1.3）。若不需要现代 Vulkan 特性，可关闭对应 CMake 开关。

### 编译项目（Linux）

```bash
git clone git@github.com:Gongcai/mecraft.git
cd mecraft
./build.sh                    # 默认构建 Release 的 mecraft 目标
./build.sh -t mecraft_server  # 构建专用服务器
```

`build.sh` 支持 `-b <build-dir>`、`-c <config>`、`-j <jobs>`、`--vcpkg-root` 等参数，详见 `./build.sh --help`。关键 CMake 开关：

| 开关 | 默认 | 说明 |
| --- | --- | --- |
| `MECRAFT_RHI_BACKEND_VULKAN` / `OPENGL` | ON | 是否编译 Vulkan / OpenGL RHI 后端 |
| `MECRAFT_DEFAULT_RHI_BACKEND` | OpenGL | 默认 RHI 后端（`OpenGL` 或 `Vulkan`） |
| `MECRAFT_ENABLE_NRD` | ON | NVIDIA NRD 降噪（要求 Vulkan 后端） |
| `MECRAFT_ENABLE_FSR31` | ON | AMD FSR 3.1 超分（要求 Vulkan 后端） |
| `MECRAFT_ENABLE_STREAMLINE` | OFF | NVIDIA Streamline / DLSS 3（仅 Windows + Vulkan） |
| `MECRAFT_BUILD_TESTS` | ON | 构建单元测试与性能基准 |

Windows 平台参考 `build.ps1`。

构建产物：

- `mecraft` — 主游戏客户端（含内嵌服务器，支持单机）
- `mecraft_server` — 独立专用服务器

### 运行

```bash
# 客户端（Linux）
./cmake-build-linux-manifest/mecraft

# 专用服务器：端口 / 种子 / 渲染距离
./cmake-build-linux-manifest/mecraft_server 25565 1234 8
```

### 运行测试

项目含 **139 个测试源文件**（单元测试 + 契约测试 + 性能基准），覆盖区块序列化、光照求解、RHI / RenderGraph、RTGI 信号契约、NRD / FSR3.1 SDK、加速结构、反射探针、网络传输、ECS 系统、红石、UI 控件、合成、物理等：

```bash
# Linux（开启测试构建）
./build.sh -- -DMECRAFT_BUILD_TESTS=ON
cd cmake-build-linux-manifest && ctest --output-on-failure
```

也可用 `ctest -R <测试名>` 运行指定测试（如 `rhi_core_test`、`rtgi_nrd_signal_contract_test`、`terrain_blas_cache_contract_test`）。

## 默认操作

| 按键 / 操作 | 功能 |
| --- | --- |
| `W` `A` `S` `D` | 移动 |
| `Space` | 跳跃 /（创造）上升，双击切换飞行 |
| `Left Shift` | 潜行 /（创造）下降 |
| `Left Ctrl` | 冲刺 |
| `鼠标左键` | 破坏方块 |
| `鼠标右键` | 放置方块 |
| `滚轮` / `1`–`9` | 切换快捷栏 |
| `E` | 打开 / 关闭物品栏 |
| `F3` | 开关 ImGui 调试面板（含 RTGI / NRD / AS 诊断视图） |
| `Esc` | 暂停 / 菜单 |

支持手柄，并可在 `assets/config/keybindings.txt` 中自定义键位。

## 项目结构

```text
mecraft/
├── assets/                  # 着色器(162)、纹理、音效、BGM、字体、配置、glTF 模型、验证场景
├── docs/                    # 技术分析报告、RHI 迁移进度、现代渲染升级路线
├── third_party/             # glad / enet / imgui / imguizmo / stb / cgltf / mikktspace / nrd / fsr31 等
├── src/
│   ├── app/                 # GameManager + 应用状态机、RHI 后端选择、Validation Run 控制器
│   ├── game/                # Game 外壳、GameSession、帧编排、游戏内状态机、红石、相机
│   ├── ecs/                 # EnTT ECS：components / systems / entity
│   ├── world/               # 区块、地形生成、光照、昼夜、天气、流体、红石、掉落
│   ├── renderer/
│   │   ├── rhi/             # RHI 公共接口 + gl/ 与 vulkan/ 双后端
│   │   ├── core/            # RenderScene / RenderPipeline / RenderGraph / Bindless Set / GPU Scene
│   │   ├── passes/          # 30+ 个渲染 Pass（含 RtgiTrace / RtgiSignalPack / NrdGuidePrep / ClusteredLighting / LocalShadow / Reflection*）
│   │   ├── gi/              # 全局光照（RTGI Trace / Signal Pack / Sampling Contract）
│   │   ├── nrd/             # NVIDIA NRD RenderGraph 桥接
│   │   ├── upscaling/       # FSR 3.1 / DLSS 3 / Streamline / 时域升采样
│   │   ├── lighting/        # 体素方块光注册表
│   │   ├── mesh/            # 地形网格：TerrainRenderCache / Mesher / MeshingService
│   │   ├── targets/         # 帧缓冲资源：GBuffer / Shadow / Common targets
│   │   ├── renderers/       # 子渲染器：天空 / 人形 / 掉落 / 手持物 / glTF 静态网格
│   │   ├── shadow/          # CSM 阴影渲染与剔除
│   │   ├── contracts/       # 渲染契约：GBuffer / 材质 / 灯光 / RTGI / 时域
│   │   ├── capture/         # 场景捕获与纹理回读
│   │   ├── debug/           # RenderDebugService（GPU 计时、诊断视图）
│   │   └── shaderpack/      # 光影包指令解析
│   ├── net/                 # 传输层抽象：InProcess / ENet、协议、编解码
│   ├── server/              # GameServer 权威服务端、区块票据
│   ├── client/              # GameClient、ClientWorld、ClientEntityStore
│   ├── scene/               # 场景运行时（glTF 模型展示、场景层次）
│   ├── save/                # SaveManager、MCHK 序列化、玩家 / 实体存档
│   ├── physics/             # AABB 碰撞、射线检测
│   ├── player/              # 背包、输入映射
│   ├── item/ crafting/      # 物品注册表、合成系统
│   ├── particle/            # 粒子系统、雨雪渲染
│   ├── audio/               # OpenAL 3D 音频、解码、BGM
│   ├── ui/                  # UI 框架、HUD、物品栏、调试面板、Dashboard
│   ├── resource/ thread/ locale/  # 资源管理、线程池、本地化
│   └── engine/              # 平台窗口、引擎基础设施
├── tests/                   # 单元测试与性能基准（139 个源文件）
├── main.cpp                 # 客户端入口
├── dedicated_server.cpp     # 专用服务器入口
└── CMakeLists.txt
```

## 许可与声明

本项目采用 [MIT 许可证](LICENSE) 开源，详见仓库根目录的 `LICENSE` 文件。

> 本项目灵感来源于 Mojang Studios 开发的 Minecraft，与 Mojang 及 Microsoft 无任何隶属关系。渲染管线的部分算法与公式参考自 DerivativeMain 光影包，相关版权归原作者所有。项目集成 NVIDIA NRD（NVIDIA RTX SDK 许可，非 OSI 开源许可）与 AMD FidelityFX FSR 3.1、NVIDIA Streamline/DLSS（分别受其相应许可约束），分发时需遵循对应第三方许可证条款。本项目仅供学习、研究与技术交流使用。
