# Mecraft

<div align="center">

![C++17](https://img.shields.io/badge/C++-17-blue.svg)
![OpenGL](https://img.shields.io/badge/OpenGL-4.5%20Core-brightgreen.svg)
![CMake](https://img.shields.io/badge/CMake-3.20+-orange.svg)
![License](https://img.shields.io/badge/License-MIT-green.svg)

**一个基于 C++17 和 OpenGL 4.5 构建的 Minecraft 风格体素沙盒游戏引擎**

</div>

---

## 项目简介

Mecraft 是一个从零构建的桌面端体素沙盒游戏项目，旨在还原 Minecraft 的核心体验并提供一个高性能的体素引擎架构。项目采用现代 C++17 标准，结合 Entity Component System (ECS) 架构，实现了程序化地形生成、动态区块加载、物理碰撞、光照传播以及 3D 空间音频等核心特性。

当前项目目标为 **主世界 (world0) + 原版 Minecraft 材质包 + 内置 DerivativeMain-like 光影效果**——将 DerivativeMain shader pack 的大气、光照、色调、HDR、水体、体积雾、材质风格等视觉算法移植为引擎内置渲染管线，而非外部 shader pack 替换。Mecraft 引擎拥有自己的 Renderer Contract，DerivativeMain 作为视觉与算法参考，当其运行假设与引擎基础设施冲突时以 Mecraft contract 为准（例如保留 greedy meshing + MDI，不使用非线性 shadow warp）。

## 核心特性

### 体素引擎

- **无限程序化世界** — 基于 FastNoiseLite 的多层噪声地形生成（大陆、群系、洞穴、细节起伏），支持多种群系与矿石分布。
- **动态区块加载** — 异步多线程网格构建（`ChunkMeshingService`），支持自定义渲染距离。
- **高性能渲染** — 智能面剔除 + 环境光遮蔽 (AO)，视锥体三级分层剔除（Region -> Column -> Chunk），Multi-Draw Indirect (MDI) 批量渲染。
- **物理与交互** — AABB 碰撞检测与分轴解析，支持移动/冲刺/跳跃/蹲伏/流体阻尼，3D DDA 射线检测。
- **动态光照** — 阳光垂直投射 + BFS 体积光传播，动态光源实时更新，球谐天光 (FromSH) 重建环境辐射。
- **ECS 架构** — 深度集成 EnTT 管理玩家实体、掉落物与粒子效果。
- **沉浸式音频** — 基于 OpenAL 的 3D 空间音效与动态触发。

### 渲染管线 (DerivativeMain-like)

Mecraft 实现了完整的 **混合延迟渲染管线 (Hybrid Deferred)**，参考 DerivativeMain shader pack 的算法与数据流。光照链路已统一为 SkyCapture metadata / LightingEnvironment 单来源：

```
SkyCapture (LUT + GPU Metadata) -> GBuffer -> Velocity -> Shadow (CSM/PCSS)
-> SSAO -> Deferred Lighting (FromSH + BlockLighting) -> SSR -> Cloud
-> Scene Composite -> Volumetric Fog -> TAA -> Motion Blur -> DoF
-> Water Composite -> Transparent -> Post (Bloom / Tonemap / Grade)
```

| 特性 | 状态 | 说明 |
| --- | --- | --- |
| **延迟渲染管线** | ✅ 已完成 | GBuffer MRT (5 color + depth)，Forward/Deferred 双路径自动回退 |
| **级联阴影 (CSM)** | ✅ 已完成 | 4-cascade，PCSS 软阴影，cascade 过渡混合，接触阴影；已提取为独立 ShadowPass |
| **PBR BRDF** | ✅ 已完成 | Cook-Torrance 高光 + Hammon 漫反射，逐字移植自 DerivativeMain |
| **球谐天光 (FromSH)** | ✅ 已完成 | L1 SH 25 方向采样，per-vertex 构建，已接入 deferred lighting |
| **大气散射 LUT** | ✅ 已完成 | Bruneton 预计算大气 LUT (透射/散射/辐照)，太阳临边昏暗，月亮渲染，SkyCapture 天体盘剥离 |
| **天空捕获系统** | ✅ 已完成 | 256×514 等距矩形投影 + GPU LUT 元数据 pass，LightingEnvironment 单来源光照 |
| **体积云** | ✅ 已完成 | 32 步体积光线步进，4 阶 3D 噪声，Beer-Powder 散射，3 层云（积云/卷云/卷积云），premultiplied 合成，LightingEnvironment 光照 |
| **屏幕空间反射** | ✅ 已完成 | 28 步线性光线步进，粗糙度自适应，天空捕获回退 |
| **SSAO** | ✅ 已完成 | 金角旋转采样 + 双边滤波，NdotL 加权 |
| **次表面散射 (SSS)** | ✅ 已完成 | 算法已移植自 DerivativeMain |
| **时域抗锯齿 (TAA)** | ✅ 已完成 | YCoCgR AABB 裁剪，Reinhard-domain 混合 |
| **水体渲染** | ✅ 已完成 | 4 阶波浪法线，60 步视差，菲涅耳折射，水雾 + 水下效果，SSR |
| **体积雾** | ✅ 已完成 | 4 档位，太阳方向 OD，多瓣相函数，云影 |
| **天气系统** | ✅ 已完成 | World::WeatherSystem 架构，天气预设迁移，帧数据从 World 读取 |
| **HDR 后处理** | ✅ 已完成 | 自动曝光，7 级 Bloom，色彩分级，CAS 锐化，运动模糊，景深；已提取为独立 PostProcessPass |
| **全局光照 (RSM/GI)** | ❌ 缺失 | 仅有 CalculateFakeBouncedLight 作为 fallback |
| **实体 GBuffer** | ❌ 缺失 | 所有实体纯 forward 渲染，不接收 deferred lighting/SSAO/SSR |

### 渲染管线架构重构

项目正在进行渲染管线的清理与现代化重构（Phase 1-7 已完成，Phase 8-9 进行中），目标是实现前向/延迟管线解耦、Pass-Oriented 架构和 Game 类渲染剥离：

| Phase | 名称 | 状态 |
| --- | --- | --- |
| 1 | 最小架构骨架 (FrameContext/FrameOutput/RenderSettings/RenderPipeline/RenderScene) | ✅ 完成 |
| 2 | Render target 分层 (CommonFrameTargets/DeferredFrameTargets/ShadowTargets) | ✅ 完成 |
| 3 | RenderPass 基类 + SSAO 试点拆分 | ✅ 完成 |
| 4 | 批量 Pass 拆分 — 屏幕空间效果 | ✅ 完成 |
| 5 | 批量 Pass 拆分 — 场景渲染 + 共享后处理 | ✅ 完成 |
| 6 | Terrain mesh 生命周期提取 (TerrainRenderCache) | ✅ 完成 |
| 7 | GBuffer + Shadow + SkyCapture 拆分 + TerrainRenderer | ✅ 完成 |
| 8 | Uniform 绑定统一 | ⏳ 进行中 |
| 9 | 前向/延迟 Pipeline 实现迁移 | ⏳ 进行中 |
| 10 | RenderScene 接管编排 + Renderer 瘦身 | ⏳ 待开始 |
| 11 | Game 类渲染剥离 | ⏳ 待开始 |

### 渲染技术参考来源

渲染管线的算法与公式参考自 DerivativeMain shader pack，核心移植模块包括：

- `derivative_brdf.glsl` — PBR BRDF 完整移植 (FresnelSchlick, GGX, Smith G2, Hammon)
- `derivative_sunlight.glsl` — Henyey-Greenstein 相函数，次表面散射，伪弹射光
- `derivative_shadow.glsl` — Common.inc 辅助函数 (30+)，阴影畸变
- `mecraft_shadow.glsl` — Mecraft 自有 CSM contract (PCSS, PCF, contact shadow)
- `atmosphere_lut.glsl` — Bruneton 预计算大气散射 LUT 查询
- `sky_sh.glsl` — 球谐天光 (ToSH/FromSH/buildSkySH)，L1 SH 25 方向采样
- `lighting_environment.glsl` — LightingEnvironment 统一光照入口，从 SkyCapture metadata 读取
- `gbuffer_contract.glsl` — GBuffer 材质 contract (33 种材质 ID)
- `render_contract.glsl` — 天空捕获布局与元数据 contract

## 技术栈

| 模块 | 技术 / 库 |
| --- | --- |
| **编程语言** | C++17 |
| **构建系统** | CMake (3.20+) |
| **图形 API** | OpenGL 4.5 Core Profile (GLAD) |
| **窗口与输入** | GLFW 3 |
| **数学库** | GLM |
| **ECS 框架** | EnTT |
| **音频引擎** | OpenAL Soft |
| **字体与渲染** | FreeType |
| **序列化** | nlohmann/json |
| **实用工具** | stb_image (图像), FastNoiseLite (噪声), ImGui (调试UI) |

## 快速开始

### 环境准备

- C++17 兼容编译器 (MSVC, GCC, Clang)
- CMake 3.20 或更高版本
- 外部依赖库: `glfw3`, `glm`, `nlohmann_json`, `OpenAL`, `Freetype`, `EnTT`

### 编译项目

```bash
git clone https://github.com/Gongcai/mecraft
cd mecraft
cmake -S . -B build
cmake --build build --config Release
```

### 运行游戏

请在项目根目录运行可执行文件，以确保正确加载 `assets/` 目录下的资源（着色器、纹理、音效、配置等）。

```bash
# Windows
.\build\Release\mecraft.exe

# Linux / macOS
./build/mecraft
```

### 运行测试

```bash
cd build
ctest -C Release --output-on-failure
```

## 默认操作指南

| 按键/操作 | 功能说明 |
| --- | --- |
| `W`, `A`, `S`, `D` | 移动 |
| `Space` | 跳跃 / (创造模式) 上升 |
| `Left Shift` | 潜行 / (创造模式) 下降 |
| `Left Ctrl` | 冲刺 |
| `鼠标左键` | 破坏方块 |
| `鼠标右键` | 放置方块 |
| `滚轮` / `1-9` | 切换快捷栏物品 |
| `E` | 打开/关闭物品栏 |
| `F3` | 开启/关闭 ImGui 开发者调试面板 |
| `Esc` | 暂停游戏 / 呼出菜单 |

支持在 `assets/config/keybindings.txt` 中自定义键位。

## 项目结构

```text
mecraft/
├── assets/
│   └── shaders/             # GLSL 着色器 (延迟管线、后处理、共享库)
├── DerivativeMain/          # DerivativeMain shader pack 解包文件 (移植参考)
├── docs/                    # 技术分析报告、差异报告、开发路线图
├── src/
│   ├── game/                # Game 主循环与帧编排
│   ├── ecs/                 # 基于 EnTT 的实体组件系统
│   ├── world/               # 区块, 地形生成, 昼夜, 天气, 掉落物
│   ├── renderer/
│   │   ├── core/            # 管线核心：Renderer, RenderScene, RenderPipeline,
│   │   │                   #   FrameContext, FrameOutput, RenderSettings, DeferredPipeline
│   │   ├── passes/          # 渲染 Pass (16 个独立 Pass 类)
│   │   │                   #   SsaoPass, VelocityPass, ReflectionPass, TemporalResolvePass,
│   │   │                   #   MotionBlurPass, DepthOfFieldPass, VolumetricPass, CloudPass,
│   │   │                   #   SceneCompositePass, WaterCompositePass, DeferredLightingPass,
│   │   │                   #   SkyCapturePass, GBufferPass, ShadowPass, DebugPass, PostProcessPass
│   │   ├── mesh/            # 地形网格：TerrainRenderCache, TerrainRenderer,
│   │   │                   #   ChunkMesher, ChunkMeshingService, WorldRenderBuffer
│   │   ├── targets/         # 帧缓冲资源：CommonFrameTargets, DeferredFrameTargets,
│   │   │                   #   ShadowTargets, DeferredRenderTargets
│   │   ├── renderers/       # 子渲染器：GameplaySkyRenderer, PostProcessRenderer,
│   │   │                   #   HumanoidRenderer, DropRenderer, FirstPersonHeldItemRenderer
│   │   ├── shadow/          # 阴影系统：ShadowRenderer, ShadowCasterCuller, ShadowMatrices
│   │   ├── contracts/       # 渲染契约：GLBlendState, MecraftTextureContract
│   │   └── shaderpack/      # 光影包指令解析
│   ├── physics/             # AABB碰撞, 射线检测
│   ├── player/              # 玩家控制器与背包
│   ├── audio/               # OpenAL 3D 音频
│   ├── ui/                  # Dashboard ImGui 调试面板
│   ├── item/                # 物品与合成系统
│   ├── particle/            # 粒子系统, 雨滴渲染
│   ├── resource/            # 资源管理器
│   ├── thread/              # 线程池
│   ├── locale/              # 本地化
│   └── crafting/            # 合成系统
├── tests/                   # 单元测试与性能基准
├── CMakeLists.txt
└── main.cpp
```

## 文档

| 文档 | 说明 |
| --- | --- |
| `docs/渲染管线清理重构计划.md` | 渲染管线清理重构 11 阶段计划与架构设计 |
| `docs/渲染管线清理重构完成情况.md` | 重构进度追踪（Phase 1-7 完成，Phase 8-9 进行中） |
| `docs/accomplished/渲染管线实现状态分析与开发路线图.md` | 各系统实现状态详细分析与 Phase 0-10 开发路线图 |
| `docs/accomplished/内置光影完整渲染管线设计.md` | 渲染管线架构设计，目标管线与里程碑规划 (M0-M7) |
| `docs/accomplished/DerivativeMain内置渲染管线完整差异分析报告.md` | Mecraft 与 DerivativeMain 完整差异分析 |
| `docs/accomplished/Mecraft与Iris渲染管线架构差异报告.md` | 与 Iris/OptiFine 管线的架构对比 |
| `docs/accomplished/延迟渲染管线架构技术分析.md` | 延迟渲染架构技术分析 |
| `docs/accomplished/阴影映射系统技术分析.md` | 阴影系统技术分析 |
| `docs/accomplished/大气散射系统技术分析.md` | 大气散射技术分析 |
| `docs/accomplished/体积云渲染系统技术分析.md` | 体积云技术分析 |
| `docs/accomplished/水体渲染系统技术分析.md` | 水体渲染技术分析 |
| `docs/accomplished/光照模型与全局照明技术分析.md` | 光照模型与 GI 技术分析 |
| `docs/accomplished/后处理管线技术分析.md` | 后处理管线技术分析 |
| `docs/accomplished/表面渲染与材质系统技术分析.md` | 表面渲染与材质系统技术分析 |
| `docs/accomplished/体积光与体积雾系统技术分析.md` | 体积光与体积雾技术分析 |

## 参与贡献

欢迎任何形式的贡献。请确保通过所有 `ctest` 单元测试，并在有必要时补充测试用例。

1. Fork 本仓库
2. 创建特性分支 (`git checkout -b feature/AmazingFeature`)
3. 提交更改 (`git commit -m 'Add some AmazingFeature'`)
4. 推送到分支 (`git push origin feature/AmazingFeature`)
5. 开启 Pull Request

## 许可证

本项目采用 MIT 许可证开源 - 详情请查看 [LICENSE](LICENSE) 文件。

*声明：本项目灵感来源于 Mojang Studios 开发的 Minecraft，仅供学习、研究与技术交流使用。*
