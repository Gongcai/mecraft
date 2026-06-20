# Mecraft

<div align="center">

![C++17](https://img.shields.io/badge/C++-17-blue.svg)
![OpenGL](https://img.shields.io/badge/OpenGL-4.5%20Core-brightgreen.svg)
![CMake](https://img.shields.io/badge/CMake-3.20+-orange.svg)
![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux-lightgrey.svg)

**一个基于 C++17 与 OpenGL 4.5 从零构建的 Minecraft 风格体素沙盒引擎**

内置移植自 DerivativeMain 光影包的延迟渲染管线 · 客户端/服务器联机架构 · 完整存档系统

</div>

---

## 项目简介

Mecraft 是一个从零构建的桌面端体素沙盒游戏引擎，目标是还原 Minecraft 的核心体验，并在此之上提供一套**电影级画质的内置渲染管线**与**真正的客户端/服务器联机架构**。

项目使用现代 C++17 编写，约 9 万行代码，采用 EnTT 驱动的 Entity Component System (ECS) 架构。它实现了自研噪声地形生成、多线程异步区块流式加载、BFS 体积光照传播、AABB 物理碰撞、3D 空间音频，以及一套移植自 **DerivativeMain** 光影包算法的混合延迟渲染管线。

单机与联机共用同一套 C/S 代码——单机模式下客户端连接的是一个进程内嵌的权威服务器，因此单人游戏与多人联机在逻辑上完全一致。

## 核心特性

### 体素世界

- **自研噪声地形** — 基于哈希的 value noise + fBm 分形叠加（**非第三方噪声库**），含手写 SSE2 / AVX2 SIMD 优化。生成阶段涵盖大陆度、山脊、湿度群系、3D 洞穴雕刻、分层矿石、植被与树木。
- **群系系统** — 温带 / 干旱 / 山地 / 高山四种群系，由大陆度、山脊与湿度阈值共同判定地表方块与覆盖层。
- **区块结构** — 16×256×16 区块沿 Y 轴切分为 16 个 16³ 子区块（SubChunk），方块数据采用**调色板 + 位压缩数组**存储，空气子区块零分配。
- **多线程流式加载** — 区块生成、网格构建（`ChunkMeshingService`）、光照求解均在线程池中异步执行，主线程按帧预算合并结果。
- **BFS 体积光照** — 经典泛洪式光照传播，阳光与方块光各 0–15 级 packed 存储。光照求解完全多线程化，支持玩家挖/放方块的交互式即时光照。
- **昼夜与天气** — 一天 20 分钟昼夜循环（日月旋转 + 8 相月相）；天气系统含晴 / 雨 / 雷暴 / 雪，派生湿度、降水强度、闪电等参数（指数半衰期平滑，移植自 DerivativeMain）。
- **流体系统** — 基于计划方块刻 + 优先队列的水流扩散，物理系统读取水位做浮力与流向。

### 内置渲染管线（移植自 DerivativeMain 光影包）

Mecraft 实现了一套完整的**混合延迟渲染管线 (Hybrid Deferred)**，算法与数据流逐字移植自 DerivativeMain shader pack。前向与延迟两条管线通过统一的 `RenderPipeline` 接口**完全解耦**，由 `RenderScene` 编排切换，共享后处理。

延迟管线的 Pass 执行顺序：

```
SkyCapture → GBuffer(地形/实体/掉落/坠落方块) → Velocity → Shadow(CSM/PCSS)
→ SSAO → DeferredLighting → SSR → Cloud → SceneComposite → WaterComposite
→ 透明地形 → 粒子 → Volumetric Fog → TAA → Motion Blur → DoF → Post(Bloom/Tonemap/Grade)
```

| 特性 | 状态 | 说明 |
| --- | --- | --- |
| **延迟渲染管线** | ✅ | 5-MRT GBuffer（albedo / 法线+AO / 体素光 / PBR 参数 / 材质 ID），前向 / 延迟双路径自动切换 |
| **实体 GBuffer** | ✅ | 人形实体、掉落物、坠落方块均写入延迟 GBuffer（专属 `entity_gbuffer` 着色器 + `MATERIAL_SKIN` 材质），接收完整延迟光照 |
| **级联阴影 (CSM)** | ✅ | 4 级联，PCSS 软阴影 + PCF + 接触阴影 + cascade 过渡混合，独立 `ShadowPass` |
| **PBR BRDF** | ✅ | Cook-Torrance 高光（GGX + Smith G2）+ Hammon 漫反射 |
| **球谐天光 (FromSH)** | ✅ | L1 球谐，25 方向采样累积，接入延迟光照 |
| **大气散射 LUT** | ✅ | Bruneton 预计算大气散射（透射 / 散射 / 辐照 3D LUT），太阳临边昏暗、月亮渲染 |
| **天空捕获系统** | ✅ | 等距柱状投影天空辐射度 + GPU LUT 元数据，作为 IBL 与光照单一来源 |
| **体积云** | ✅ | 光线步进 + 多阶 3D 噪声 + Beer-Powder 散射 + 时域重投影 |
| **屏幕空间反射 (SSR)** | ✅ | 反射探针 + 粗糙度自适应 + 双边滤波 + 时域重投影，天空捕获回退 |
| **环境光遮蔽 (SSAO)** | ✅ | 半分辨率原始 → 双边滤波 → 深度感知上采样 → 时域累积 |
| **次表面散射 (SSS)** | ✅ | 按材质 ID 区分强度（草 / 叶雪 / 旗帜 / 皮肤） |
| **时域抗锯齿 (TAA)** | ✅ | YCoCgR AABB 裁剪，Reinhard 域混合 |
| **水体渲染** | ✅ | 波浪法线 + 视差 + 菲涅耳折射 + 水雾 + 水下效果 + SSR |
| **体积雾** | ✅ | 太阳方向光学深度 + 多瓣相函数 + 云影 + 时域重投影 |
| **HDR 后处理** | ✅ | 自动曝光、7 级 Bloom、色彩分级、CAS 锐化、运动模糊、景深，前向 / 延迟共用 `PostProcessPass` |
| **FSR1 超分** | ✅ | AMD FSR1（EASU + RCAS）上采样 |
| **全局光照 (RSM/GI)** | ❌ | 无真实多次反弹 GI，当前以 `CalculateFakeBouncedLight` 假反弹 + AO 近似 |

### 客户端 / 服务器联机架构

- **统一 C/S 架构** — 单机模式用进程内零拷贝传输（`InProcessTransport`，直接传 `shared_ptr<Chunk>`）连接内嵌权威服务器；联机模式用 **ENet (UDP)** 传输（`ENetTransport`）。逻辑完全一致。
- **权威服务器** — `GameServer` 拥有 `World` 并以 20 TPS 跑 tick 循环，权威处理方块破坏 / 放置、实体同步、玩家移动和解、聊天命令、自动存档。
- **独立专用服务器** — `mecraft_server` 可执行目标（`dedicated_server.cpp`），参数 `[port=25565] [seed=1234] [render_distance=8]`，支持 Ctrl+C 优雅退出。
- **网络协议** — 自定义二进制小端协议，4 条信道（可靠控制 / 可靠世界 / 不可靠状态 / 可靠聊天），约 30 种消息类型。区块数据走 RLE 压缩。
- **已同步内容** — 玩家移动（输入 → 权威快照和解）、方块交互（权威 + 批量广播 + 光照增量）、掉落物、生物、抛射物、物品栏、世界时间 / 天气、游戏模式、死亡 / 重生。

### 存档系统

- **区块** — 自定义二进制 **MCHK** 格式（magic + 版本 + CRC32 + 调色板编码位压缩 payload），区域文件缓存，原子写（`.tmp` → 重命名 + `.bak` 备份）。
- **世界元数据** — `level.json`（种子、出生点、时间、天数、天气、模式、时间戳、缩略图）。
- **玩家状态** — JSON 保存位置 / 朝向 / 血量 / 护甲 / 饥饿 / 物品栏。
- **持久实体与方块实体** — 怪物、掉落物、箱子物品栏均 JSON 序列化。
- **自动保存** — 每 5 分钟 flush 区块 / 实体 / 元数据，区块异步写盘，关闭时阻塞 flush。

### ECS 与游戏逻辑

- **EnTT 驱动** — 区分 `fixedUpdate`（60Hz 物理）与 `tick`（20 TPS 模拟）两条流水线，并区分客户端 / 服务器 profile。
- **系统覆盖** — 玩家输入与物理、方块交互、战斗与伤害、物品生成 / 拾取 / 合并、怪物 AI、粒子、流体刻、坠落方块、人形动画、脚步音频、网络插值。
- **物理** — AABB 分轴 sweep 碰撞（Y→X→Z），DDA 体素射线检测 + slab 精确求交。支持行走 / 冲刺 / 跳跃 / 潜行（防掉落）/ 游泳（浮力 + 水流）/ 创造飞行。

### 物品、粒子与音频

- **物品与合成** — ItemID 独立于 BlockID，命名空间化注册表，Block↔Item 双向映射，掉落表。合成配方从 JSON 加载，支持任意尺寸合成格的位置无关匹配（自动裁剪空白行列）。
- **粒子** — billboard 粒子可渲入 SceneComposite 并接受统一体积雾；独立的纹理化降水渲染器（雨 4000 / 雪 2500 滴），输出天气遮罩供后处理使用。
- **音频** — OpenAL Soft 3D 空间音频，WAV + OGG Vorbis 解码，音源池复用，设备热插拔，BGM 系统与音效随机变体。

## 技术栈

| 模块 | 技术 / 库 |
| --- | --- |
| **编程语言** | C++17 |
| **构建系统** | CMake 3.20+ |
| **图形 API** | OpenGL 4.5 Core Profile（GLAD） |
| **窗口与输入** | GLFW 3 |
| **数学库** | GLM |
| **ECS 框架** | EnTT |
| **音频引擎** | OpenAL Soft + libvorbis |
| **字体渲染** | FreeType |
| **序列化** | nlohmann/json |
| **网络传输** | ENet (UDP)，随源码内置编译 |
| **调试 UI** | Dear ImGui，随源码内置编译 |
| **图像加载** | stb_image / stb_image_write，随源码内置编译 |

依赖分两类：`glfw3`、`glm`、`nlohmann_json`、`OpenAL`、`Vorbis`、`Freetype`、`EnTT` 通过 `find_package` 解析（推荐用 vcpkg 提供）；`glad`、`enet`、`imgui`、`stb` 已置于 `third_party/` 随项目一同编译。

## 快速开始

### 环境准备

- C++17 兼容编译器（MSVC 2022/2025、GCC、Clang）
- CMake 3.20 或更高版本
- 推荐使用 vcpkg 安装外部依赖：

```bash
vcpkg install glfw3 glm nlohmann-json openal-soft libvorbis freetype entt
```


### 编译项目

```bash
git clone https://github.com/Gongcai/mecraft
cd mecraft
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

构建产物：

- `mecraft` — 主游戏客户端（含内嵌服务器，支持单机）
- `mecraft_server` — 独立专用服务器

### 运行

请在项目根目录运行，以确保正确加载 `assets/` 下的着色器、纹理、音效与配置。

```bash
# 客户端（Windows）
.\build\Release\mecraft.exe

# 专用服务器：端口 / 种子 / 渲染距离
.\build\Release\mecraft_server.exe 25565 1234 8
```

### 运行测试

项目含约 70 个单元测试与性能基准，覆盖区块序列化、光照求解、网络传输、ECS 系统、UI 控件、合成、物理等：

```bash
cd build
ctest -C Release --output-on-failure
```

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
| `F3` | 开关 ImGui 调试面板 |
| `Esc` | 暂停 / 菜单 |

支持手柄，并可在 `assets/config/keybindings.txt` 中自定义键位。

## 项目结构

```text
mecraft/
├── assets/                  # 着色器(107)、纹理、音效、BGM、字体、配置、主题
├── DerivativeMain/          # DerivativeMain 光影包解包文件（移植参考）
├── docs/                    # 技术分析报告、设计文档、开发路线图
├── third_party/             # glad / enet / imgui / stb（随项目编译）
├── src/
│   ├── app/                 # GameManager + 应用状态机（主菜单 → 加载 → 游戏中）
│   ├── game/                # Game 外壳、GameSession、帧编排、游戏内状态机
│   ├── ecs/                 # EnTT ECS：components / systems / entity
│   ├── world/               # 区块、地形生成、光照、昼夜、天气、流体、掉落
│   ├── renderer/
│   │   ├── core/            # RenderScene / RenderPipeline / Forward / Deferred
│   │   ├── passes/          # 19 个独立渲染 Pass
│   │   ├── mesh/            # 地形网格：TerrainRenderCache / Mesher / MeshingService
│   │   ├── targets/         # 帧缓冲资源：GBuffer / Shadow / Common targets
│   │   ├── renderers/       # 子渲染器：天空 / 人形 / 掉落 / 手持物 / 后处理
│   │   ├── shadow/          # CSM 阴影渲染与剔除
│   │   ├── contracts/       # 渲染契约：混合状态、纹理契约
│   │   └── shaderpack/      # 光影包指令解析
│   ├── net/                 # 传输层抽象：InProcess / ENet、协议、编解码
│   ├── server/              # GameServer 权威服务端、区块票据
│   ├── client/              # GameClient、ClientWorld、ClientEntityStore
│   ├── save/                # SaveManager、MCHK 序列化、玩家 / 实体存档
│   ├── physics/             # AABB 碰撞、射线检测
│   ├── player/              # 背包、输入映射
│   ├── item/ crafting/      # 物品注册表、合成系统
│   ├── particle/            # 粒子系统、雨雪渲染
│   ├── audio/               # OpenAL 3D 音频、解码、BGM
│   ├── ui/                  # UI 框架、HUD、物品栏、调试面板
│   ├── resource/ thread/ locale/  # 资源管理、线程池、本地化
├── tests/                   # 单元测试与性能基准（约 70 个）
├── main.cpp                 # 客户端入口
├── dedicated_server.cpp     # 专用服务器入口
└── CMakeLists.txt
```

## 文档

`docs/` 下含大量技术分析与设计文档，主要包括：

| 文档 | 说明 |
| --- | --- |
| `docs/accomplished/DerivativeMain内置渲染管线完整差异分析报告.md` | Mecraft 与 DerivativeMain 完整差异分析 |
| `docs/accomplished/延迟渲染管线架构技术分析.md` | 延迟渲染架构技术分析 |
| `docs/accomplished/阴影映射系统技术分析.md` | CSM 阴影系统技术分析 |
| `docs/accomplished/大气散射系统技术分析.md` | 大气散射 LUT 技术分析 |
| `docs/accomplished/体积云渲染系统技术分析.md` | 体积云技术分析 |
| `docs/accomplished/水体渲染系统技术分析.md` | 水体渲染技术分析 |
| `docs/accomplished/后处理管线技术分析.md` | HDR 后处理技术分析 |
| `docs/multiplayer-cs-architecture-design.md` | 客户端 / 服务器联机架构设计 |
| `docs/save-system-design.md` | 存档系统设计 |
| `docs/渲染管线清理重构计划.md` | 渲染管线重构计划与架构 |

## 许可与声明

本项目采用 [MIT 许可证](LICENSE) 开源，详见仓库根目录的 `LICENSE` 文件。

> 本项目灵感来源于 Mojang Studios 开发的 Minecraft，与 Mojang 及 Microsoft 无任何隶属关系。渲染管线的算法与公式参考自 DerivativeMain 光影包，相关版权归原作者所有。本项目仅供学习、研究与技术交流使用。
