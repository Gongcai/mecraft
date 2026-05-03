# 项目文件状态概览

此文档为工作区中主要源文件与类/模块的精要总结（单行概述），用于快速了解代码库结构与职责分布。

- `CMakeLists.txt`: 项目构建配置（CMake），定义目标、依赖与编译选项。
- `main.cpp`: 程序入口，初始化子系统并启动游戏主循环或测试入口。

核心子系统

- `src/core/Game.cpp`: 游戏主循环与全局子系统初始化/管理。
- `src/core/Game.h`: `Game` 类接口与生命周期管理声明。
- `src/core/GameStateMachine.cpp`: 游戏状态机实现，管理不同游戏状态切换。
- `src/core/GameStateMachine.h`: 状态机类型与接口定义。
- `src/core/Camera.cpp`: 第三人称/第一人称相机实现与控制逻辑。
- `src/core/Camera.h`: 相机接口与数据结构。
- `src/core/InputManager.cpp`: 输入设备与高层输入事件转发。
- `src/core/InputManager.h`: 输入管理器接口与配置。
- `src/core/NamespacedId.cpp`: 命名空间 ID 工具实现（资源/物品标识）。
- `src/core/NamespacedId.h`: 命名空间 ID 类型定义与辅助函数。
- `src/core/IdRegistry.cpp`: 注册并管理 ID 映射（方块/物品/实体等）。
- `src/core/IdRegistry.h`: ID 注册表接口。
- `src/core/Time.cpp`: 全局时间/计时工具实现。
- `src/core/Time.h`: 时间相关类型与函数声明。
- `src/core/Window.cpp`: 平台窗口与事件集成。
- `src/core/Window.h`: 窗口抽象接口。

世界与区块

- `src/world/World.cpp`: 世界管理、区块加载/保存与高层逻辑。
- `src/world/World.h`: 世界类声明与接口。
- `src/world/Chunk.cpp`: 区块数据管理、子区块组织与高度图处理。
- `src/world/Chunk.h`: 区块数据结构与工具函数。
- `src/world/SubChunk.cpp`: 子区块（区块切片）存储、压缩与索引实现。
- `src/world/SubChunk.h`: 子区块接口与常量。
- `src/world/TerrainGenerator.cpp`: 地形生成器实现（噪声、植被、矿石、洞穴、SSE/AVX 加速路径）。
- `src/world/TerrainGenerator.h`: 地形生成器类声明与配置接口。
- `src/world/Palette.cpp`: 材质/方块调色板与 ID<->资源映射实现。
- `src/world/Palette.h`: 面向渲染/存储的调色板接口。
- `src/world/Block.cpp`: 方块基础类型与静态注册/元数据处理。
- `src/world/Block.h`: `Block` 类型定义、方块 ID、属性等。
- `src/world/BitPackedArray.cpp`: 位打包数组实现（紧凑存储常用数据）。
- `src/world/BitPackedArray.h`: 位打包数组接口与操作。
- `src/world/LightEngine.cpp`: 光照传播引擎实现（阳光与人造光源）。
- `src/world/LightEngine.h`: 光照引擎接口与调度。
- `src/world/LightService.cpp`: 光照相关服务/调度实现。
- `src/world/LightService.h`: 光照服务接口。
- `src/world/LightSolver.cpp`: 光照求解器实现（光传播规则、队列处理）。
- `src/world/LightSolver.h`: 求解器接口与规则集合。
- `src/world/LightTypes.h`: 光照类型、常量定义。
- `src/world/DropSystem.cpp`: 世界中掉落物生成/管理系统。
- `src/world/DropSystem.h`: 掉落系统接口。
- `src/world/DropSystem.cpp`:（已列出）同上。

渲染与网格化

- `src/renderer/Renderer.cpp`: 渲染器高层实现、渲染循环与资源绑定。
- `src/renderer/Renderer.h`: 渲染器接口与初始化/提交函数。
- `src/renderer/ChunkMesher.cpp`: 区块网格化（从方块数据生成渲染网格）实现。
- `src/renderer/ChunkMesher.h`: 网格化器接口与网格生成策略。
- `src/renderer/ChunkMeshingService.cpp`: 网格化服务（后台 meshing 任务调度）。
- `src/renderer/ChunkMeshingService.h`: 服务接口与任务队列定义。
- `src/renderer/ChunkMeshingService.cpp`:（已列出）同上。
- `src/renderer/Shader.cpp`: 着色器加载、编译与统一变量管理。
- `src/renderer/Shader.h`: 着色器接口与实用函数。
- `src/renderer/PostProcessRenderer.cpp`: 后处理效果实现（屏幕空间特效）。
- `src/renderer/PostProcessRenderer.h`: 后处理接口。
- `src/renderer/DropRenderer.cpp`: 掉落物渲染实现。
- `src/renderer/DropRenderer.h`: 掉落物渲染接口。
- `src/renderer/ItemModelMesh.cpp`: 物品模型网格/缓冲封装。
- `src/renderer/ItemModelMesh.h`: 物品网格接口。
- `src/renderer/stb_image.cpp`: 图片加载第三方实现（stb_image）。
- `src/renderer/TestCube.cpp`: 测试方块/网格调试工具。
- `src/renderer/TestCube.h`: 测试网格接口。

用户界面与输入

- `src/ui/BitmapFont.h`: 位图字体布局/测量工具。
- `src/ui/TextRenderer.cpp`: 文本渲染实现。
- `src/ui/TextRenderer.h`: 文本渲染接口。
- `src/ui/ConsoleOverlay.cpp`: 控制台 UI 覆盖层实现。
- `src/ui/ConsoleOverlay.h`: 控制台控件接口。
- `src/ui/ConsoleDisplayBox.cpp`: 控制台显示框实现。
- `src/ui/ConsoleDisplayBox.h`: 控制台显示类型定义。
- `src/ui/InventoryPanelControl.cpp`: 物品栏/背包面板实现。
- `src/ui/InventoryPanelControl.h`: 面板接口与交互处理。
- `src/ui/CraftingGridControl.cpp`: 合成网格 UI 实现。
- `src/ui/CraftingGridControl.h`: 合成控件接口。
- `src/ui/CrosshairControl.cpp`: 准星与瞄准 UI。
- `src/ui/CrosshairControl.h`: 准星控件接口。
- `src/ui/HotbarControl.cpp`: 快捷栏 UI 实现。
- `src/ui/HotbarControl.h`: 快捷栏接口。
- `src/ui/CommandInputOverlay.cpp`: 命令行输入覆盖层实现。
- `src/ui/CommandInputOverlay.h`: 命令行控件接口。
- `src/ui/KeyboardInputBox.cpp`: 文本输入框实现。
- `src/ui/KeyboardInputBox.h`: 输入框接口。
- `src/ui/InventoryPanelControl.cpp`:（已列出）同上。
- `src/ui/ItemGridControl.cpp`: 物品格子 UI 实现。
- `src/ui/ItemGridControl.h`: 格子控件接口。
- `src/ui/Pickable.cpp`: 可拾取 UI 项目实现。
- `src/ui/Pickable.h`: 可拾取接口。
- `src/ui/PickableOverlay.cpp`: 拾取覆盖层实现。
- `src/ui/PickableOverlay.h`: 覆盖层接口。
- `src/ui/BitmapFont.h`:（已列出）同上。

玩家与实体

- `src/player/Player.cpp`: 玩家行为、输入响应与状态同步实现。
- `src/player/Player.h`: 玩家类接口与数据。
- `src/player/ActionMap.cpp`: 输入动作映射实现。
- `src/player/ActionMap.h`: 动作映射声明。
- `src/item/Item.cpp`: 物品逻辑、交互与实例化实现。
- `src/item/Item.h`: 物品类型与接口声明。
- `src/ecs/*`: 实体组件系统（多个文件），包含系统、组件与注册逻辑，用于游戏对象生命周期管理。

音频

- `src/audio/AudioEngine.cpp`: 音频后端管理与播放调度。
- `src/audio/AudioEngine.h`: 音频后端接口。
- `src/audio/AudioClip.cpp`: 音频剪辑加载/生命周期管理。
- `src/audio/AudioSource.cpp`: 音频源播放与空间化。
- `src/audio/BgmSystem.cpp`: 背景音乐管理系统。

物理与仿真

- `src/physics/PhysicsSystem.cpp`: 物理仿真主循环与碰撞响应。
- `src/physics/PhysicsSystem.h`: 物理系统接口。
- `src/particle/ParticleSystem.cpp`: 粒子系统更新与发射实现。

工具与其他

- `src/resource/ResourceMgr.cpp`: 资源加载与缓存实现。
- `src/resource/ResourceMgr.h`: 资源管理接口。
- `src/thread/ThreadPool.cpp`: 线程池与任务并行化实现。
- `src/thread/ThreadPool.h`: 线程池接口。
- `src/core/Paths.h`: 项目路径与资源路径常量。
- `src/renderer/Shader.h`:（已列出）同上。

测试相关

- `tests/*`: 包含单元测试和性能测试，覆盖了渲染、光照、网格化、采样等诸多模块（例如 `tests/terrain_perf_test.cpp`, `tests/light_solver_perf_test.cpp`）。

第三方与外部

- `assets/third_party/imgui/*`: Dear ImGui 源码用于调试/工具界面。
- `..\..\vcpkg\vcpkg\installed\*`: 第三方库头文件（glm、entt、stb、OpenAL 等）。

备注

- 本概览基于文件名与模块划分推断职责，已尽量保持简洁。若需要更详细的类成员/函数级别文档（例如为每个类列方法与责任、依赖图），可以指定范围（例如某个子目录或模块），我会生成更详尽的文档。

