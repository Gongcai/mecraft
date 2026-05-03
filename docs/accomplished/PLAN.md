# 基于 EnTT 的 Gameplay ECS 方案（含掉落物、实体音频、粒子）

## Summary
- 结论：在 `UI/Renderer` 不 ECS 化的前提下，引入 `EnTT` 做 gameplay ECS 是值得的；把 `掉落物 + 输入意图 + 实体音频 + 粒子数据` 纳入 ECS 后，系统边界会更完整，后续扩展怪物、投射物、环境声、特效会顺很多。
- 推荐落地方式：
  - `InputManager / ActionMap / InputContextManager` 保留。
  - `Renderer / UIRenderer / DropRenderer` 保留非 ECS 接口。
  - `ParticleSystem` 保留为渲染/资源 facade，但粒子实例数据 ECS 化。
  - `AudioEngine` 保留为底层 service；长期声源走 `AudioSourceComponent`，瞬时音效走事件。
- 难度评估：
  - `首阶段建议范围`：`5.5/10`
  - `如果把 Player facade、渲染提交流程、全部音频都一次性纯 ECS 化`：`7.5/10`

## Key Changes
- EnTT 接入：
  - 使用 `find_package(EnTT CONFIG REQUIRED)`。
  - 新增 `GameplayScene` 或 `GameplayRegistry` 封装 `entt::registry`、context、system 调度。
  - 新增 `GameplayServices` 注入 `World`、`AudioEngine`、`ParticleSystem`、`InputContextManager`、`ResourceMgr` 等非 ECS 服务。
- 输入 ECS 化：
  - 保留当前输入采集与 binding/context 语义。
  - 新增 `InputFrameState` singleton/context。
  - 新增 `InputSamplingSystem`，从 `InputContextManager` 读取 action/axis。
  - 新增 `PlayerIntentBuildSystem`，把本地输入写入本地玩家实体：
    - `MoveIntentComponent`
    - `LookIntentComponent`
    - `HotbarIntentComponent`
    - `BlockActionIntentComponent`
  - 规则不变：UI context 激活时，不写 gameplay intent。
- 玩家运行时 ECS 化：
  - 保留 `Player` 作为 facade/view，继续服务 UI/Renderer。
  - ECS 中新增：
    - `LocalPlayerTag`
    - `TransformComponent`
    - `PhysicsBodyComponent`
    - `InventoryComponent`
    - `BlockTargetComponent`
    - `BlockBreakComponent`
    - `FootstepStateComponent`
    - `LandingStateComponent`
  - 新增 `PlayerFacadeSyncSystem`，规定 `ECS 为真源，facade 只镜像`。
- 掉落物 ECS 化：
  - 以实体替代当前 `DropSystem` 内部数组。
  - 组件：
    - `ItemComponent`
    - `TransformComponent`
    - `VelocityComponent`
    - `BoundsComponent`
    - `LifetimeComponent`
    - `SpinVisualComponent`
    - `GroundedStateComponent`
    - `PickupItemComponent` 或直接复用 `ItemComponent`
  - 系统：
    - `ItemSpawnSystem`
    - `ItemPhysicsSystem`
    - `ItemMergeSystem`
    - `ItemPickupSystem`
    - `ItemLifetimeSystem`
    - `ItemPlacementResolveSystem`
  - `DropSystem` 保留为兼容 facade，对外 API 尽量不变，内部改为 registry-backed。
- 音频 ECS 化：
  - 保留 `AudioEngine` 为底层资源/设备层。
  - 新增 `AudioSourceComponent` 处理长期位置声源：
    - `clipName`
    - `loop`
    - `volume`
    - `pitch`
    - `spatial`
    - `referenceDistance`
    - `rolloff`
    - `desiredState`
    - `followTransform`
  - 新增 `AudioEventBuffer` 或 `PlaySoundEvent` 处理瞬时音效。
  - 新增 `AudioSyncSystem`：
    - 同步 `AudioSourceComponent` 到 `AudioEngine`
    - 派发瞬时事件
    - 负责 source 生命周期回收
- 粒子 ECS 化：
  - 采用“`数据 ECS + 渲染 facade`”。
  - `ParticleSystem` 保留 `init/render/shutdown` 职责，不直接承担权威粒子池。
  - 新增组件：
    - `ParticleComponent`
      - `life`
      - `maxLife`
      - `size`
      - `grassTintFactor`
      - `layer`
      - `uvMin`
      - `uvMax`
    - `TransformComponent`
    - `VelocityComponent`
    - `LifetimeComponent` 可与 `ParticleComponent.life` 二选一，默认不重复建模
  - 新增系统：
    - `ParticleSpawnSystem`：消费粒子生成事件
    - `ParticleSimulationSystem`：更新 life、速度、位置
    - `ParticleCleanupSystem`：清理死亡粒子实体
  - 新增 `ParticleEventBuffer`：
    - 方块破坏时只发 `BlockBreakParticleEvent`
    - 由 `ParticleSpawnSystem` 批量生成粒子实体
  - `ParticleSystem::render` 改为从 registry 读取粒子 view，组装顶点并提交 GPU。
  - 这样 business 侧不再直接维护 `m_particles`，但渲染接口和资源初始化方式基本不变。
- `GameplayState` 收缩：
  - 保留 inventory/menu/command 状态切换与上下文切换。
  - 移除直接业务编排，不再自己调用 `m_particleSystem.emit`、`spawnBlockDrop`、`playClip`。
  - 只负责驱动固定步 systems。
- 推荐 fixed update 顺序：
  1. `InputManager.update`
  2. `InputSamplingSystem`
  3. `PlayerIntentBuildSystem`
  4. `CameraLookSystem`
  5. `CharacterPhysicsSystem`
  6. `BlockTargetSystem`
  7. `BlockInteractionSystem`
  8. `ItemSpawnSystem`
  9. `ItemPhysicsSystem`
  10. `ItemMergeSystem`
  11. `ItemPickupSystem`
  12. `ItemLifetimeSystem`
  13. `ItemPlacementResolveSystem`
  14. `ParticleSpawnSystem`
  15. `ParticleSimulationSystem`
  16. `ParticleCleanupSystem`
  17. `AudioSyncSystem`
  18. `World.update`
  19. `PlayerFacadeSyncSystem`

## Tick 设计（20 TPS，不替代 fixed update）
- 目标：引入独立的 gameplay tick（`20 TPS`），用于未来离散逻辑（如方块更新），不影响当前 `60Hz fixed update`。
- 原则：
  - `fixed update` 继续承担输入、角色移动、物理、即时交互、渲染相关前置更新。
  - `tick update` 只承担离散规则逻辑（方块计划更新、延迟事件、作物生长、液体扩散等）。
  - 两者并存，禁止把角色物理/相机/输入降到 20 TPS。
- 建议时钟结构：
  - `GameTickClock`（或 `GameplayTickClock`）
    - `tickRate = 20.0`
    - `tickInterval = 1.0 / 20.0`
    - `accumulator`
    - `tickIndex`（`uint64_t`）
- 建议调度位置：
  - **短期接入点**：可先接在 `Game::runFixedUpdate` 内（每次 fixed step 后推进 tick accumulator）。
  - **长期归属**：迁移到 `GameplayScene` / `GameplayRegistry` 的统一调度器中，由 scene 负责 fixed 与 tick 双通道调度。
  - 结论：可以接在 `Game::run` 这条链路上，但建议实际代码落在 `runFixedUpdate`（或 scene 的 fixed 驱动函数）而不是渲染循环分支。
- 建议执行顺序（单个 fixed step 内）：
  1. 执行现有 fixed systems（60Hz）
  2. `tickClock.accumulator += fixedStep`
  3. `while (accumulator >= tickInterval)`：
     - `TickSystemGroup.updateOneTick(tickIndex)`
     - `accumulator -= tickInterval`
     - `++tickIndex`
- TickSystemGroup（首阶段可为空壳）：
  - 预留 `BlockScheduledUpdateSystem`
  - 预留 `WorldRuleTickSystem`
  - 预留 `GameplayScheduledEventSystem`
- 兼容与安全约束：
  - 每帧可设置 `maxTicksPerFrame`（如 2~4）防止长卡顿后补偿风暴。
  - 所有 tick 逻辑必须幂等、可重入，避免与 fixed 系统共享可变状态时产生竞态。
  - 不允许渲染/UI 直接依赖 tick clock；展示层只消费最终状态。

## Public Interfaces
- 新增核心类型：
  - `GameplayScene` / `GameplayRegistry`
  - `GameplayServices`
  - `InputFrameState`
  - `AudioEventBuffer`
  - `ParticleEventBuffer`
- 保持对外稳定的 facade：
  - `Player`
  - `DropSystem`
  - `ParticleSystem`
- facade 的职责调整：
  - `DropSystem`：兼容旧调用，内部转调 registry。
  - `ParticleSystem`：渲染与资源管理 facade，不再是权威粒子容器。
  - `Player`：UI/Renderer 只读 facade，不再负责原始输入到移动意图的解释。

## Test Plan
- 继续通过现有测试：
  - [physics_mvp_test.cpp](/D:/project/mecraft/tests/physics_mvp_test.cpp)
  - [drop_system_test.cpp](/D:/project/mecraft/tests/drop_system_test.cpp)
  - [audio_test.cpp](/D:/project/mecraft/tests/audio_test.cpp)
- 新增输入/ECS 测试：
  - Gameplay/UI context 下 action/axis 到 intent 的映射一致。
  - 本地玩家 `WASD`、跳跃、蹲下、冲刺、LookX/LookY 的 ECS 意图与旧逻辑一致。
- 新增掉落物 ECS 测试：
  - 同类掉落物合并、寿命到期、落地、拾取、放置方块顶起行为与现有一致。
- 新增音频 ECS 测试：
  - 瞬时事件正确派发。
  - `AudioSourceComponent` 跟随实体移动、停止、销毁时不会泄漏底层 source。
- 新增粒子 ECS 测试：
  - 方块破坏事件能生成正确数量的粒子实体。
  - 粒子 life、重力、位置更新与现有 `ParticleSystem::update` 行为一致。
  - 死亡粒子会被清理。
  - `ParticleSystem::render` 能从 registry 视图正确提交顶点，即使零粒子或大量粒子时也行为稳定。
- 架构验收：
  - `Renderer`、`UIRenderer`、`DropRenderer` 不直接依赖 `entt::registry`。
  - `GameplayState` 不再直接串接掉落/粒子/音频业务调用。
  - `ParticleSystem` 不再持有 gameplay 权威粒子池。

## 落地原则
- 采用“`先骨架、再迁移、最后收口`”的渐进式改造，不做一次性纯 ECS 重写。
- 每个阶段结束后都要求：`主程序可运行`、`现有核心测试可通过`、`对外 facade API 不破坏`。
- 首阶段只让 gameplay 逻辑进入 ECS；`Renderer`、`UIRenderer`、`DropRenderer`、`AudioEngine`、`ParticleSystem` 继续保留现有职责边界。
- 新系统优先挂在 fixed update 中运行；渲染线程/渲染接口不直接暴露 `entt::registry`。
- 所有迁移都以“`ECS 为真源，旧 facade 只做桥接或只读镜像`”为统一规则。

## 当前代码映射（便于落地）
- 构建与依赖：
  - `CMakeLists.txt`
- fixed update / 游戏组装：
  - `src/core/Game.cpp`
- gameplay 编排入口：
  - `src/core/states/GameplayState.h`
- 玩家 facade：
  - `src/player/Player.cpp`
  - `src/player/Player.h`
- 掉落物：
  - `src/world/DropSystem.cpp`
  - `src/world/DropSystem.h`
  - `src/renderer/DropRenderer.cpp`
- 粒子：
  - `src/particle/ParticleSystem.cpp`
  - `src/particle/ParticleSystem.h`
- 音频：
  - `src/audio/AudioEngine.cpp`
  - `src/audio/AudioEngine.h`
- 回归测试入口：
  - `tests/physics_mvp_test.cpp`
  - `tests/drop_system_test.cpp`
  - `tests/audio_test.cpp`

## 落地里程碑
### Milestone 0：接入骨架与系统调度
- 目标：先把 ECS 容器、服务注入、fixed-step 调度骨架接入，并同时引入 `20 TPS` tick 时钟（先不挂复杂业务）。
- 主要改动：
  - `CMakeLists.txt` 增加 `find_package(EnTT CONFIG REQUIRED)` 并链接目标。
  - 新增 `GameplayScene` / `GameplayRegistry`，封装：
    - `entt::registry`
    - context / singleton
    - fixed update systems 调度入口
    - tick systems 调度入口（`20 TPS`）
  - 新增 `GameplayServices`，注入 `World`、`AudioEngine`、`ParticleSystem`、`InputContextManager`、`ResourceMgr` 等 service。
  - 新增 `GameTickClock`（`tickIndex + accumulator + tickInterval`）。
  - `Game` 持有 gameplay scene，并把 fixed-step 调度切到 scene 层；过渡期可在 `runFixedUpdate` 内推进 tick。
- 本阶段不做：
  - 不迁移 `Player`、`DropSystem`、`ParticleSystem` 的权威数据。
  - 不在 tick 中实现完整方块更新链路（仅建立接口与空系统组）。
  - 不修改 `Renderer` / `UIRenderer` 的读数据方式。
- 退出标准：
  - 主程序可启动、可进入 gameplay。
  - 现有 fixed update 顺序仍可工作。
  - tick 以 `20 TPS` 稳定推进（即使当前 tick 组为空）。
  - 未引入额外渲染/UI 依赖到 registry。

### Milestone 1：输入 ECS 化 + 本地玩家意图层
- 目标：先迁移“输入解释”而不是一次性迁移全部玩家逻辑。
- 主要改动：
  - 新增 `InputFrameState` context/singleton。
  - 新增 `InputSamplingSystem`，从 `InputContextManager` 采样 action/axis。
  - 新增本地玩家实体与组件：
    - `LocalPlayerTag`
    - `MoveIntentComponent`
    - `LookIntentComponent`
    - `HotbarIntentComponent`
    - `BlockActionIntentComponent`
  - 新增 `PlayerIntentBuildSystem`，把输入写入本地玩家实体。
  - `GameplayState` 仅保留状态切换、UI 切换、菜单/命令入口，不再直接解释 `WASD / Look / Hotbar`。
- 推荐保留：
  - `Player` 继续存在，并暂时仍负责部分移动/相机外观逻辑。
- 退出标准：
  - Gameplay / UI context 下的输入屏蔽规则与当前行为一致。
  - 玩家移动、朝向、快捷栏切换行为与现状一致。
  - 新增输入映射测试通过。

### Milestone 2：玩家运行时收口 + facade 同步
- 目标：把本地玩家运行时状态迁入 ECS，确立“ECS 真源”。
- 主要改动：
  - 新增组件：
    - `TransformComponent`
    - `PhysicsBodyComponent`
    - `InventoryComponent`
    - `BlockTargetComponent`
    - `BlockBreakComponent`
    - `FootstepStateComponent`
    - `LandingStateComponent`
  - 新增系统：
    - `CameraLookSystem`
    - `CharacterPhysicsSystem`
    - `BlockTargetSystem`
    - `PlayerFacadeSyncSystem`
  - `Player` 改为 facade / view，给 UI 和渲染层提供只读接口。
- 推荐做法：
  - 先把 `PlayerFacadeSyncSystem` 做出来，再逐步抽空 `Player::update`，避免一次性切断老逻辑。
- 退出标准：
  - `Player` 不再是输入解释真源。
  - `Renderer` / `UIRenderer` 继续只依赖 `Player` facade，不直接读 registry。
  - 角色基础移动、视角、选中方块、破坏进度显示保持稳定。

### Milestone 3：掉落物 ECS 化
- 目标：把当前 `DropSystem` 的内部数组迁到 registry，同时保持旧 API 尽量稳定。
- 主要改动：
  - 新增组件：
    - `ItemComponent`
    - `TransformComponent`
    - `VelocityComponent`
    - `BoundsComponent`
    - `LifetimeComponent`
    - `SpinVisualComponent`
    - `GroundedStateComponent`
  - 新增系统：
    - `ItemSpawnSystem`
    - `ItemPhysicsSystem`
    - `ItemMergeSystem`
    - `ItemPickupSystem`
    - `ItemLifetimeSystem`
    - `ItemPlacementResolveSystem`
  - `DropSystem` 对外保留：
    - `spawnItemDrop`
    - `spawnBlockDrop`
    - `onBlockPlaced`
    - `collectNearbyDrops`
    - `getDrops`
  - `DropRenderer` 继续读 facade，不直接依赖 registry。
- 推荐做法：
  - 先让 `DropSystem` 变为 registry-backed facade，再考虑是否删除旧 `DropEntity` 结构。
- 退出标准：
  - 现有掉落物测试全部通过。
  - 合并、寿命、落地、拾取、方块顶起行为与当前实现一致。
  - `DropRenderer` 无需感知 ECS 细节。

### Milestone 4：粒子 ECS 化 + 事件化生成
- 目标：保留 `ParticleSystem` 的 GPU facade 职责，迁出粒子实例数据与生成逻辑。
- 主要改动：
  - 新增 `ParticleEventBuffer` 与 `BlockBreakParticleEvent`。
  - 新增组件：
    - `ParticleComponent`
    - `TransformComponent`
    - `VelocityComponent`
  - 新增系统：
    - `ParticleSpawnSystem`
    - `ParticleSimulationSystem`
    - `ParticleCleanupSystem`
  - `ParticleSystem::render` 改为从 registry 视图读取粒子并组装顶点。
  - `GameplayState` 不再直接调用 `m_particleSystem.emit(...)`。
- 推荐做法：
  - 先把 `emit` 改成写事件缓冲，再把 `update` 改成 ECS 驱动，最后替换 `render` 数据源。
- 退出标准：
  - 方块破坏粒子数量、生命周期、运动效果与当前实现基本一致。
  - 零粒子/高粒子数场景下渲染稳定。
  - `ParticleSystem` 不再持有权威粒子池。

### Milestone 5：音频 ECS 化 + GameplayState 收缩
- 目标：把 gameplay 侧的音频触发从直接调用改成组件/事件驱动，同时完成 `GameplayState` 收口。
- 主要改动：
  - 新增 `AudioEventBuffer` / `PlaySoundEvent`。
  - 新增 `AudioSourceComponent` 用于长期声源。
  - 新增 `AudioSyncSystem`，负责：
    - 同步长期声源到 `AudioEngine`
    - 派发瞬时事件
    - 回收失效 source
  - `GameplayState` 删除对：
    - `spawnBlockDrop`
    - `m_particleSystem.emit`
    - `m_audioEngine.playClip / playSound2D`
    的直接业务编排。
- 推荐做法：
  - 先迁移瞬时音效事件，再补长期位置声源，避免一次性改动 `AudioEngine` 生命周期管理。
- 退出标准：
  - 脚步、落地、挖掘/放置等音效行为与当前体验一致。
  - `GameplayState` 只负责状态切换与 fixed-step 驱动，不再直接串业务效果。
  - 不出现底层 `AudioSource` 泄漏。

### Milestone 6：收尾、回归与删除过渡逻辑
- 目标：清理临时桥接层，补齐测试与架构验收。
- 主要改动：
  - 删除已废弃的旧更新路径和重复状态。
  - 补齐输入、掉落物、粒子、音频 ECS 测试。
  - 检查 fixed update 顺序是否与本文一致。
  - 校验 debug 工具、性能统计、序列化/资源生命周期是否仍稳定。
- 退出标准：
  - 现有回归测试通过。
  - 新增 ECS 测试通过。
  - 文档中的架构验收项全部满足。

## 阶段交付物与建议顺序
- 推荐按 `0 -> 1 -> 2 -> 3 -> 4 -> 5 -> 6` 执行，不建议跳阶段并行大改。
- 最小可见收益顺序：
  1. 先完成 `Milestone 0`，建立稳定注入与调度骨架。
  2. 再完成 `Milestone 1~2`，解决输入与玩家真源问题。
  3. 然后做 `Milestone 3`，优先迁移掉落物，因为它与渲染层耦合较低。
  4. 最后做 `Milestone 4~5`，把粒子/音频改为事件驱动并收缩 `GameplayState`。
- 单阶段 PR 建议：
  - 每个 milestone 尽量拆成 `构建接入`、`数据组件`、`系统迁移`、`测试补齐` 四类提交。
  - 每次提交都保证游戏可编译、可运行、可回退。

## 主要风险与回滚策略
- 风险 1：`Player` 与 ECS 双写，导致状态不同步。
  - 规避：尽早引入 `PlayerFacadeSyncSystem`，并明确 ECS 为真源。
- 风险 2：`GameplayState` 迁移过程中行为回归。
  - 规避：先事件化副作用，再删除旧调用路径。
- 风险 3：掉落物/粒子迁移后渲染层被迫直接读 registry。
  - 规避：坚持 facade 输出只读 view/model，不把 registry 传给渲染层。
- 风险 4：音频 source 生命周期复杂，容易泄漏。
  - 规避：只让 `AudioSyncSystem` 统一创建、停止、销毁底层 source。
- 回滚策略：
  - 每个 milestone 保留一层兼容 facade；如果新系统行为异常，可回退到上一阶段实现，而无需同时回退渲染/UI 层。

## Assumptions
- 默认 `World` 仍是体素世界权威源，ECS systems 通过它做查询与写入。
- 默认 `ParticleSystem` 保留现有 GPU 资源与渲染提交职责，只迁出粒子实例数据。
- 默认瞬时音效与粒子都优先走事件缓冲，不为短命效果强行建复杂状态机。
- 默认首阶段不让 UI/Renderer 直接读 registry；如果未来要进一步纯化展示层，再单独规划第二阶段。
