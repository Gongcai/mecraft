# 🎮 Mecraft

<div align="center">

![C++17](https://img.shields.io/badge/C++-17-blue.svg)
![OpenGL](https://img.shields.io/badge/OpenGL-3.3%20Core-brightgreen.svg)
![CMake](https://img.shields.io/badge/CMake-3.20+-orange.svg)
![License](https://img.shields.io/badge/License-MIT-green.svg)

**一个基于 C++17 和 OpenGL 3.3 构建的 Minecraft 风格体素沙盒游戏引擎**


</div>

---

## 🌟 项目简介

Mecraft 是一个从零构建的桌面端体素沙盒游戏项目，旨在还原 Minecraft 的核心体验并提供一个高性能的体素引擎架构。项目采用了现代 C++17 标准，结合 Entity Component System (ECS) 架构，实现了程序化地形生成、动态区块加载、物理碰撞、光照传播以及 3D 空间音频等核心特性。

无论你是体素游戏爱好者，还是希望学习 C++ 图形学、游戏引擎架构开发的开发者，Mecraft 都可以作为一个优秀的参考项目。

## ✨ 核心特性

* 🌍 **无限程序化世界**
  * 基于 `FastNoiseLite` 的多层噪声地形生成（大陆、群系、洞穴、细节起伏）。
  * 包含多种地形与群系（森林、平原、沙漠），支持矿石分布与地表装饰。
  * 动态区块加载与卸载队列，支持自定义渲染距离。
* 🧱 **高性能渲染管线**
  * 异步多线程网格构建（`ChunkMeshingService`），避免主线程卡顿。
  * 智能面剔除（Face Culling）与环境光遮蔽（AO）计算。
  * 基于视锥体剔除（Frustum Culling）的三级分层优化（Region -> Column -> Chunk）。
  * 半透明方块（水、玻璃）深度排序渲染与独立后处理（如水下色调滤镜）。
* 💡 **动态光照系统**
  * 阳光垂直投射与 BFS 算法的体积光传播。
  * 动态光源（火把）与光照级别的实时更新（放置/破坏方块触发）。
* 🏃 **物理与交互**
  * 完善的 AABB 碰撞检测与分轴碰撞解析（Sweep and Resolve）。
  * 支持玩家移动、冲刺、跳跃、蹲伏及流体（水域）物理阻尼。
  * 基于 3D DDA 算法的高精度视线射线检测（Raycasting），用于方块破坏与放置。
* 🎵 **沉浸式音频**
  * 基于 OpenAL 的 3D 空间音效（位置音频、衰减模型）。
  * 动态触发脚步声、坠落伤害声及背景音乐无缝切换。
* 🛠️ **现代引擎架构**
  * **ECS 架构**: 深度集成 `EnTT` 库管理玩家实体、掉落物与粒子效果。
  * **数据驱动**: 方块属性、UI、键位绑定及合成配方通过 JSON/TXT 外部配置。
  * **开发者工具**: 集成 ImGui，提供实时的性能面板、渲染状态监控。

## 🧰 技术栈

| 模块 | 技术 / 库 |
| --- | --- |
| **编程语言** | C++17 |
| **构建系统** | CMake (3.20+) |
| **图形 API** | OpenGL 3.3 Core Profile (GLAD) |
| **窗口与输入** | GLFW 3 |
| **数学库** | GLM |
| **ECS 框架** | EnTT |
| **音频引擎** | OpenAL Soft |
| **字体与渲染** | FreeType |
| **序列化** | nlohmann/json |
| **实用工具** | stb_image (图像), FastNoiseLite (噪声), ImGui (调试UI) |

## 🚀 快速开始

### 1. 环境准备

在编译之前，请确保您的系统中已安装以下依赖：

* C++17 兼容的编译器 (MSVC, GCC, Clang)
* CMake 3.20 或更高版本
* **外部依赖库**: `glfw3`, `glm`, `nlohmann_json`, `OpenAL`, `Freetype`, `EnTT`

### 2. 编译项目

克隆仓库并使用 CMake 进行构建：

```bash
# 1. 克隆仓库
git clone https://github.com/yourusername/mecraft.git
cd mecraft

# 2. 生成构建文件
cmake -S . -B build

# 3. 编译项目 (Release 模式以获得最佳性能)
cmake --build build --config Release
```

### 3. 运行游戏

为了确保游戏能够正确加载 `assets/` 目录下的资源（着色器、纹理、音效、配置等），请务必**在项目根目录或包含正确资源相对路径的位置运行可执行文件**。

```bash
# Windows
.\build\Release\mecraft.exe

# Linux / macOS
./build/mecraft
```

### 4. 运行测试

Mecraft 包含全面的单元测试和性能基准测试（覆盖物理、网格划分、UI、音频等系统）。

```bash
cd build
ctest -C Release --output-on-failure
```

## 🎮 默认操作指南

| 按键/操作 | 功能说明 |
| --- | --- |
| `W`, `A`, `S`, `D` | 移动 |
| `Space` | 跳跃 / (创造模式下) 上升 |
| `Left Shift` | 潜行 / (创造模式下) 下降 |
| `Left Ctrl` | 冲刺 |
| `鼠标左键` | 破坏方块 |
| `鼠标右键` | 放置方块 |
| `滚轮` / `1-9` | 切换快捷栏物品 |
| `E` | 打开/关闭物品栏 |
| `F3` | 开启/关闭 ImGui 开发者调试面板 |
| `Esc` | 暂停游戏 / 呼出菜单 |

*(支持在 `assets/config/keybindings.txt` 中自定义键位)*

## 📂 项目结构

```text
mecraft/
├── assets/                  # 游戏资源 (着色器, 纹理, 音效, 配置文件)
├── docs/                    # 项目设计文档与开发日志
├── src/                     # 源代码目录
│   ├── core/                # 引擎核心：主循环, 状态机, 窗口与输入管理
│   ├── ecs/                 # 基于 EnTT 的实体组件系统与各类 System
│   ├── world/               # 核心世界数据：区块(Chunk), 地形生成, 光照引擎
│   ├── renderer/            # 渲染管线：ChunkMesher, 后处理, Shader管理
│   ├── physics/             # 物理系统：AABB碰撞, 射线检测
│   ├── player/              # 玩家控制器与背包逻辑
│   ├── audio/               # 基于 OpenAL 的音频子系统
│   ├── ui/                  # 游戏内 HUD, 准心, 调试面板及输入框
│   └── item/                # 物品与合成系统
├── tests/                   # CTest 测试用例与性能基准测试代码
├── CMakeLists.txt           # CMake 构建配置
└── main.cpp                 # 游戏入口点
```

## 🤝 参与贡献

欢迎任何形式的贡献！如果你想参与到 Mecraft 的开发中，你可以：

1. Fork 本仓库
2. 创建你的特性分支 (`git checkout -b feature/AmazingFeature`)
3. 提交你的更改 (`git commit -m 'Add some AmazingFeature'`)
4. 推送到分支 (`git push origin feature/AmazingFeature`)
5. 开启一个 Pull Request

在提交代码前，请确保通过了所有的 `ctest` 单元测试，并在有必要时补充相应的测试用例。

## 📄 许可证

本项目采用 MIT 许可证开源 - 详情请查看 [LICENSE](LICENSE) 文件。

*声明：本项目灵感来源于 Mojang Studios 开发的 Minecraft，仅供学习、研究与技术交流使用。*
