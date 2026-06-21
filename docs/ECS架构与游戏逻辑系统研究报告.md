# Mecraft ECS 架构与游戏逻辑系统研究报告

> 基于源码 `src/ecs`、`src/game`、`src/app`、`src/world`、`src/client`、`src/server` 等模块的深度分析。

---

## 目录

1. [总体架构概览](#1-总体架构概览)
2. [ECS 核心架构](#2-ecs-核心架构)
3. [组件系统](#3-组件系统)
4. [系统管线 (Pipeline)](#4-系统管线-pipeline)
5. [实体工厂与定义注册表](#5-实体工厂与定义注册表)
6. [事件总线与帧级通信](#6-事件总线与帧级通信)
7. [游戏逻辑层](#7-游戏逻辑层)
8. [游戏内状态机](#8-游戏内状态机)
9. [游戏模式规则](#9-游戏模式规则)
10. [世界系统](#10-世界系统)
11. [物理系统](#11-物理系统)
12. [客户端 / 服务器架构](#12-客户端--服务器架构)
13. [网络同步机制](#13-网络同步机制)
14. [光照系统](#14-光照系统)
15. [世界生成系统](#15-世界生成系统)
16. [架构特性总结](#16-架构特性总结)

---

## 1. 总体架构概览

Mecraft 采用经典分层架构，从应用入口到 ECS 系统管线形成清晰的职责链：

```
main.cpp / dedicated_server.cpp
    │
    ▼
GameManager (应用层: 窗口、输入、资源、音频、UI、线程池)
    │
    ├── AppStateMachine (应用状态机: MainMenu → Loading → Gameplay)
    │
    ▼
Game (游戏会话管理器)
    │
    ├── GameSession (会话聚合: World + ECS + Physics + Camera + ...)
    │     ├── GameServer       (权威 World)
    │     ├── GameClient       (ClientWorld 镜像)
    │     ├── GameplayScene    (ECS 场景: Registry + Services + Pipeline)
    │     ├── PhysicsSystem
    │     ├── DropSystem / ParticleSystem / CraftingSystem
    │     ├── CameraController
    │     └── GameStateMachine (游戏内状态机)
    │
    ├── GameplayRenderRuntime  (渲染运行时)
    ├── GameplayHudPresenter   (HUD)
    └── GameFrameOrchestrator  (帧编排器)
```

**主循环**（`GameManager::run()`）使用累加器模式实现固定时间步：

```cpp
while (!window.shouldClose()) {
    window.pollEvents();
    Time::update();
    frameTime = clamp(Time::getRawDeltaTime(), 0.25);  // 防止螺旋死亡
    accumulator += frameTime;
    appStateMachine.update(frameTime, accumulator);
    appStateMachine.render(frameTime);
}
```

**双频率更新**：
- **FixedUpdate (60 Hz)**：物理、输入、战斗、交互、物品、粒子、动画
- **Tick (20 TPS)**：流体、方块支撑、下落方块（Minecraft 语义的 tick 级逻辑）

---

## 2. ECS 核心架构

ECS 核心位于 `src/ecs/`，基于 `entt::registry` 构建薄封装层，提供类型安全的组件访问、服务注入和系统调度。

### 2.1 核心接口

#### ISystem (`ecs/ISystem.h`)

所有系统的抽象基类：

```cpp
class ISystem {
public:
    using Dependencies = NoSystemDependency;
    virtual ~ISystem() = default;
    virtual void update(SystemContext& ctx) = 0;
};
```

- 通过 `SystemDependency<RequiredTuple, WrittenTuple>` 模板声明系统依赖（用于调试验证和文档化）
- `NoSystemDependency` 是空依赖别名

#### SystemContext (`ecs/SystemContext.h`)

每帧传递给每个系统的统一上下文：

```cpp
struct SystemContext {
    GameplayRegistry& registry;
    GameplayServices& services;
    float dt = 0.0f;
    uint64_t tickIndex = 0;
};
```

### 2.2 GameplayRegistry (`ecs/GameplayRegistry.h`)

对 `entt::registry` 的薄封装，提供实体与组件的 CRUD 操作：

```cpp
class GameplayRegistry {
    entt::registry m_registry;
public:
    entt::entity create();
    void destroy(entt::entity e);
    template<typename T, typename... Args> T& emplace(entt::entity e, Args&&... args);
    template<typename T> T& get(entt::entity e);
    template<typename T> bool has(entt::entity e);
    template<typename T> T* try_get(entt::entity e);
    template<typename... Components> auto view();
    
    // Context/Singleton 机制: 存储全局帧状态
    template<typename T> T& ctxSet(T&& value);
    template<typename T> T& ctxGet();
    template<typename T> bool ctxHas();
};
```

**Context 机制**用于存储帧级共享状态，如 `InputFrameState`、各种 `EventBus`。

### 2.3 GameplayServices (`ecs/GameplayServices.h`)

外部服务的可选注入容器，通过 `OptionalService<T>` RAII 包装：

```cpp
struct GameplayServices {
    OptionalService<World>                   world;
    OptionalService<const IWorldView>        worldView;
    OptionalService<client::GameClient>      gameClient;
    OptionalService<AudioEngine>             audioEngine;
    OptionalService<InputContextManager>     inputContextManager;
    OptionalService<ResourceMgr>             resourceMgr;
    OptionalService<DropSystem>              dropSystem;
    OptionalService<ParticleSystem>          particleSystem;
    OptionalService<UIRenderer>              uiRenderer;
    OptionalService<physics::PhysicsSystem>  physicsSystem;
    OptionalService<CameraController>        cameraController;
};
```

`OptionalService<T>` 提供：
- `get()` — 返回裸指针（可能为 null）
- `require()` — debug 断言非空
- `operator->` / `operator*` — 便捷访问

所有服务在 `GameSession::initECS()` 中绑定。

### 2.4 GameplayScene (`ecs/GameplayScene.h/.cpp`)

ECS 场景容器，聚合 Registry、Services、Pipeline 和 TickClock：

```cpp
class GameplayScene {
    GameplayRegistry m_registry;
    GameplayServices m_services;
    GameTickClock    m_tickClock;
    GameplayPipeline m_pipeline;      // 构造为 Client profile
    entt::entity     m_localPlayer;
};
```

**`initLocalPlayer(spawnPos)`** 创建本地玩家实体，挂载以下组件：

| 组件类别 | 组件列表 |
|----------|----------|
| 标识 | `LocalPlayerTag` |
| 输入意图 | `MoveIntentComponent`, `LookIntentComponent`, `HotbarIntentComponent`, `BlockActionIntentComponent` |
| 变换 | `TransformComponent` (position, eyeHeight=1.62) |
| 物理 | `PhysicsBodyComponent` (halfExtents=0.3,0.9,0.3), `CharacterControllerComponent` |
| 相机 | `CameraStateComponent` (yaw=-90, pitch=0, fov=75), `SprintFovComponent` |
| 物品栏 | `InventoryComponent`, `InventoryDataComponent` |
| 交互 | `BlockTargetComponent`, `BlockBreakComponent`, `BlockInteractionRuntimeComponent` |
| 战斗 | `MeleeAttackComponent`, `ProjectileThrowerComponent` |
| 状态 | `FlightStateComponent`, `FootstepStateComponent`, `LandingStateComponent`, `FallRollComponent` |
| 生命 | `HealthComponent` (20/20), `ArmorComponent` (0/20), `FoodComponent` (20/20, sat=5) |
| 视觉 | `ViewBobComponent`, `HurtEffectComponent` |

同时在 registry context 中注册 `InputFrameState`。

### 2.5 GameTickClock (`ecs/util/GameTickClock.h`)

20 TPS 的 tick 时钟，使用累加器机制：

```cpp
class GameTickClock {
    static constexpr double DEFAULT_TICK_RATE = 20.0;
    void advance(double deltaTime);
    bool shouldTick() const;      // accumulator >= tickInterval
    void consumeTick();           // accumulator -= interval, ++tickIndex
    uint64_t tickIndex() const;
    uint32_t maxTicksPerFrame = 4;  // 防止卡顿时的 catch-up 雪崩
};
```

---

## 3. 组件系统

组件按功能域分组，位于 `src/ecs/components/`，全部为 POD 结构体。

### 3.1 Tag 组件 (`TagComponents.h`)

| 组件 | 说明 |
|------|------|
| `LocalPlayerTag` | 本地玩家标记 |
| `DropItemTag` | 掉落物标记 |
| `ProjectileTag` | 投射物标记 |
| `FallingBlockTag` | 下落方块标记 |
| `ParticleTag` | 粒子标记 |
| `SteveTag` | Steve 模型根标记 |
| `MobTag` | Mob 标记 |

### 3.2 变换组件 (`TransformComponents.h`)

| 组件 | 字段 |
|------|------|
| `TransformComponent` | `glm::vec3 position`, `float eyeHeight=1.62` |
| `ParentComponent` | `entt::entity parent` |
| `ChildrenComponent` | `std::vector<entt::entity> children` |
| `LocalTransformComponent` | `vec3 localPosition`, `vec3 localRotation(Euler deg)`, `vec3 localScale`; `toMatrix()` |
| `WorldTransformComponent` | `glm::mat4 worldMatrix` |

### 3.3 输入意图组件 (`InputComponents.h`)

| 组件 | 字段 |
|------|------|
| `MoveIntentComponent` | `vec2 move`, `wantsJump`, `wantsSprint`, `wantsCrouch`, `toggleFlightMode` |
| `LookIntentComponent` | `float deltaX`, `float deltaY` |
| `HotbarIntentComponent` | `bool slotSelected[9]`, `scrollUp`, `scrollDown` |
| `BlockActionIntentComponent` | `wantsBreak`, `wantsPlace` |

**设计要点**：输入意图与原始输入解耦，`PlayerIntentBuildSystem` 将世界空间意图（wishDir = front*vertical + right*horizontal）写入 `MoveIntentComponent`，实现相机相对移动。

### 3.4 物理组件 (`PhysicsComponents.h`)

| 组件 | 字段 |
|------|------|
| `PhysicsBodyComponent` | `PhysicsBody body` (position, velocity, halfExtents, colliderOffset, eyeOffsetY, isGrounded, landingImpactSpeed, isInWater, hitWall, isFullySubmerged, isEyesInWater) |
| `CharacterControllerComponent` | `PhysicsTuning tuning`, `standEyeHeight=1.62`, `crouchEyeHeight=1.0`, `eyeHeightLerpSpeed=15`, `crouchChangesEyeHeight` |
| `FlightStateComponent` | `bool isFlying` |
| `VelocityComponent` | `vec3 velocity` |
| `BoundsComponent` | `vec3 halfExtents` |
| `GroundedStateComponent` | `bool grounded` |

### 3.5 相机组件 (`CameraComponents.h`)

| 组件 | 字段 |
|------|------|
| `CameraStateComponent` | `yaw=-90`, `pitch=0`, `fov=75`, `sensitivity=0.1`, `front`, `right`, `up` |
| `SprintFovComponent` | `walkFov=75`, `sprintFov=90`, `lerpSpeed=10` |
| `ViewBobComponent` | `amplitude=0.25`, `horizontalAmplitude=0.02`, `frequency=6`, `phaseOffset`, `blend`, `fadeInSpeed=10`, `fadeOutSpeed=8`, `verticalOffset`, `horizontalOffset` |

### 3.6 交互组件 (`InteractionComponents.h`)

| 组件 | 字段 |
|------|------|
| `InventoryComponent` | `int selectedHotbarSlot` |
| `InventoryDataComponent` | `Inventory inventory` |
| `BlockTargetComponent` | `hasTarget`, `targetState`, `targetBlock(ivec3)`, `placeBlock(ivec3)`, `hitNormal(ivec3)` |
| `BlockBreakComponent` | `active`, `blockPos`, `progress01` |
| `BlockInteractionRuntimeComponent` | `placeCooldownRemaining`, `creativeBreakCooldownRemaining`, `breakActive`, `breakBlockPos`, `breakElapsedMs`, `breakRequiredMs`, `recentlyPlacedBlock`, `postPlaceInteractionSuppressSeconds`, `heldItemSwingSequence` |

### 3.7 玩家状态组件 (`PlayerStateComponents.h`)

| 组件 | 字段 |
|------|------|
| `FootstepStateComponent` | `timer`, `clipIndex` |
| `LandingStateComponent` | `justLanded`, `impactSpeed` |
| `FallRollComponent` | `active`, `elapsed`, `currentRadians`; `kMaxRadians=0.06`, `kDurationSeconds=0.24`, `kPeakRatio=0.35` |
| `HealthComponent` | `current=20`, `max=20` |
| `PlayerModeComponent` | `bool creative` |
| `ArmorComponent` | `current=0`, `max=20` |
| `FoodComponent` | `current=20`, `max=20`, `saturation=5`, `lastHungerTick` |
| `HurtEffectComponent` | `classicHurtEffectPending`, `flashSecondsRemaining`, `flashDurationSeconds=0.18`, `soundId`, `soundVolume`; `triggerClassicHurt()` |

### 3.8 战斗组件 (`CombatComponents.h`)

| 组件 | 字段 |
|------|------|
| `DropTableEntry` | `itemId`, `minCount`, `maxCount` |
| `DropTableComponent` | `itemId`, `minCount`, `maxCount`, `vector<DropTableEntry> entries` |
| `MeleeAttackComponent` | `cooldownRemaining`, `cooldownSeconds=0.45`, `reach=3.25`, `damage=4` |
| `DamageEvent` | `target`, `source`, `amount` |
| `DeathEffectComponent` | `particleBlock`, `particleCount=24`, `soundId`, `volume` |

### 3.9 掉落物组件 (`DropComponents.h`)

| 组件 | 字段 |
|------|------|
| `DropEntityIdComponent` | `size_t dropId` |
| `ItemComponent` | `ItemID itemId`, `uint32_t stackCount` |
| `LifetimeComponent` | `ageSeconds`, `lifeTimeSeconds` |
| `SpinVisualComponent` | `yawRadians`, `spinSpeedRadians` |
| `FallingBlockComponent` | `BlockID blockId`, `ivec3 gridPosition`, `ivec3 prevGridPosition`, `float tickAccumulator` |

### 3.10 投射物组件 (`ProjectileComponents.h`)

| 组件 | 字段 |
|------|------|
| `ProjectileDefinition` | `itemId`, `damage=4`, `hitRadius=0.45`, `gravity=8`, `throwSpeed=15`, `upwardBias=1.2`, `spawnForwardOffset=0.75`, `boundsHalfExtent=0.22`, `lifetimeSeconds=4`, `spinSpeedRadians=10`, `entityImpactParticleBlock`, `entityImpactParticleCount=14`, `throwSoundId`, `impactSoundId` |
| `ProjectileComponent` | `owner`, `damage`, `hitRadius`, `gravity`, `entityImpactParticleBlock`, `entityImpactParticleCount`, `impactSoundId` |
| `ProjectileThrowerComponent` | `cooldownRemaining`, `cooldownSeconds=0.55` |
| `EntityImpactComponent` | `position`, `particleBlock`, `particleCount` |

### 3.11 粒子组件 (`ParticleComponents.h`)

| 组件 | 字段 |
|------|------|
| `ParticleComponent` | `life`, `maxLife`, `size=0.1`, `biomeTintFactor`, `layer`, `uvMin(vec2)`, `uvMax(vec2)` |

### 3.12 音频组件 (`AudioComponents.h`)

| 组件 | 字段 |
|------|------|
| `AudioSourceComponent` | `clipName`, `loop`, `volume=1`, `pitch=1`, `spatial=true`, `referenceDistance=8`, `rolloff=1`, `desiredPlaying`, `followTransform` |

### 3.13 Steve / Mob 组件 (`SteveComponents.h`)

| 组件 | 字段 |
|------|------|
| `MobAIComponent` | `State{Idle,Wander,Pursue,Attack}`, `target`, `wanderTimer`, `wanderInterval=3`, `wanderDir`, `wanderSpeed=0.45`, `pursueSpeed=0.85`, `acquisitionRange=14`, `loseTargetRange=20`, `attackRange=1.35`, `attackCooldownSeconds=1.1`, `attackCooldownRemaining`, `attackDamage=3`, `yaw` |
| `MobVisualComponent` | `model="humanoid"`, `textureKey="zombie"`, `skinLayout`, `scale=1.0` |
| `StevePartType` | enum: Torso, Head, RightArm, LeftArm, RightLeg, LeftLeg |
| `StevePartComponent` | `StevePartType partType` |
| `SteveAnimationStateComponent` | `walkCyclePhase`, `walkCycleSpeed=8`, `isWalking`, `isOnGround`, `lastPosition` |

### 3.14 网络组件 (`NetworkComponents.h`)

| 组件 | 字段 |
|------|------|
| `EntityNetIdComponent` | `EntityNetId netId` (uint32_t) |
| `NetworkSyncTag` | 标记需要网络同步的实体 |
| `EntityTypeComponent` | `std::string entityId` |
| `PendingNetworkDespawnTag` | 标记待 despawn |
| `NetworkSnapshotSample` | `serverTick`, `position`, `velocity`, `yaw`, `pitch` |
| `NetworkInterpolationComponent` | `targetPosition/Velocity/Yaw/Pitch`, `positionLerpSpeed=12`, `rotationLerpSpeed=16`, `snapDistance=8`, `serverTickRate=20`, `interpolationDelayTicks=2`, `renderServerTick`, `latestServerTick`, `snapshots[8]`, `snapshotCount`, `hasRenderServerTick`, `hasTarget` |

---

## 4. 系统管线 (Pipeline)

### 4.1 GameplayPipeline (`ecs/GameplayPipeline.h/.cpp`)

系统管线的核心调度器，支持两种 Profile：

```cpp
enum class GameplayPipelineProfile {
    Client,   // 完整客户端系统集
    Server    // 仅权威逻辑系统
};
```

**双频率管线**：
- `m_fixedUpdateSystems` — 60 Hz FixedUpdate
- `m_tickSystems` — 20 TPS Tick

### 4.2 Client FixedUpdate 系统注册顺序

| # | 系统 | 分类 | 职责 |
|---|------|------|------|
| 1 | `InputSamplingSystem` | State | 采样输入到 `InputFrameState` |
| 2 | `PlayerIntentBuildSystem` | State | 构建 MoveIntent/LookIntent/HotbarIntent/BlockActionIntent |
| 3 | `MobAISystem` | State | Mob AI 状态机 (Idle/Wander/Pursue/Attack) |
| 4 | `CharacterPhysicsSystem` | State | 角色物理移动（调用 PhysicsSystem） |
| 5 | `PlayerRuntimeUpdateSystem` | State | 相机向量、FOV lerp、hotbar 选择 |
| 6 | `FallDamageSystem` | State | 摔落伤害（impactSpeed > 10） |
| 7 | `ViewBobSystem` | State | 视角晃动（blend + sin/cos 波形） |
| 8 | `BlockTargetSystem` | State | 射线检测目标方块 |
| 9 | `PlayerMeleeSystem` | State | 近战攻击（Ray-AABB 相交） |
| 10 | `ProjectileSystem` | State | 投射物生成 + 物理 + 碰撞 |
| 11 | `DamageSystem` | State | 消费 DamageEventBus，应用伤害 |
| 12 | `HurtEffectDecaySystem` | State | 受伤闪烁衰减 |
| 13 | `DeathSystem` | State | Mob 死亡处理（掉落、粒子、音效、网络 despawn） |
| 14 | `BlockBreakSystem` | State | 方块破坏（创造即时/生存计时） |
| 15 | `BlockPlaceSystem` | State | 方块放置（冷却、状态解析、库存消耗） |
| 16 | `ItemSpawnSystem` | Drop | 掉落物生成（从 DropSpawnEventBus） |
| 17 | `ItemPhysicsSystem` | Drop | 掉落物物理（重力、碰撞、摩擦） |
| 18 | `ItemMergeSystem` | Drop | 掉落物合并（空间哈希网格） |
| 19 | `ItemPickupSystem` | Drop | 掉落物拾取 |
| 20 | `ItemLifetimeSystem` | Drop | 掉落物过期销毁 |
| 21 | `ParticleSpawnSystem` | Particle | 粒子生成（从 ParticleEventBus） |
| 22 | `ParticleSimulationSystem` | Particle | 粒子物理（重力、位置更新） |
| 23 | `ParticleCleanupSystem` | Particle | 过期粒子清理 |
| 24 | `HungerDepletionSystem` | State | 饥饿消耗（每100秒-1） |
| 25 | `PlayerFootstepAudioSystem` | State | 脚步声和着陆音效 |
| 26 | `FallRollEffectSystem` | State | 摔落翻滚动画 |
| 27 | `AudioSyncSystem` | State | 音频事件回放 + AudioSource 组件同步 |
| 28 | `NetworkInterpolationSystem` | State | 网络实体插值平滑 |
| 29 | `SteveSyncSystem` | State | 玩家位置/相机同步到 Steve 模型 |
| 30 | `SteveAnimationSystem` | State | Steve 行走动画 |
| 31 | `MobAnimationSystem` | State | Mob 行走动画 |
| 32 | `TransformHierarchySystem` | State | 父子层级世界矩阵计算 (BFS) |
| 33 | `FallingBlockInterpolateSystem` | State | 下落方块渲染插值 |

### 4.3 Server FixedUpdate 系统（11个）

`MobAISystem`, `CharacterPhysicsSystem`, `PlayerMeleeSystem`, `ProjectileSystem`, `DamageSystem`, `HurtEffectDecaySystem`, `DeathSystem`, `ItemPhysicsSystem`, `ItemMergeSystem`, `ItemPickupSystem`, `ItemLifetimeSystem`

Server 仅运行权威逻辑系统，跳过所有视觉表现系统（视角晃动、动画、音频、粒子渲染等）。

### 4.4 Client Tick 系统（4个）

| # | 系统 | 职责 |
|---|------|------|
| 1 | `FluidTickSystem` | 流体调度 tick |
| 2 | `BlockSupportSystem` | 方块支撑验证（torch/flower/重力方块） |
| 3 | `FallingBlockSpawnSystem` | 从事件生成下落方块实体 |
| 4 | `FallingBlockTickSystem` | 每 tick 下落一格 |

### 4.5 关键系统详解

#### InputSamplingSystem
从 `InputContextManager` 采样所有轴和动作到 registry context 中的 `InputFrameState`。

#### PlayerIntentBuildSystem
读取 `InputFrameState`，将世界空间意图写入本地玩家的意图组件：
- `MoveIntentComponent.wishDir = front * vertical + right * horizontal`
- `LookIntentComponent`（含手柄灵敏度检测）
- `HotbarIntentComponent`、`BlockActionIntentComponent`
- 死亡时清零意图

#### CharacterPhysicsSystem
查询 `MoveIntentComponent + TransformComponent + PhysicsBodyComponent` 的实体，调用 `physicsSystem.updateBody()`：
- 处理飞行模式切换（创造模式双击跳跃）
- eyeHeight lerp（蹲下/站立）
- 着陆检测（`LandingStateComponent`）
- 地面状态（`GroundedStateComponent`）
- 速度同步（`VelocityComponent`）

#### PlayerMeleeSystem
玩家左键攻击时，从眼睛位置沿 `camera.front` 射线，与所有 Mob 的 AABB 做相交检测，最近命中者推入 `DamageEvent` 到 `DamageEventBus`，设置冷却，清除 `wantsBreak`。

#### ProjectileSystem（两阶段）
1. **投掷者阶段**：检查 `wantsPlace` + 冷却，尝试消耗库存中的投射物物品，生成投射物实体（`EntityFactory::createProjectile`）
2. **投射物阶段**：步进式碰撞检测（0.25m/step），检测 solid block 和 Mob AABB，命中时产生伤害事件、粒子、音效并销毁

#### DamageSystem
消费 `DamageEventBus` 中所有事件，对 target 的 `HealthComponent` 减血，触发 `HurtEffectComponent.triggerClassicHurt()`，排队受伤音效。创造模式本地玩家免疫。

#### DeathSystem
查询 `MobTag + HealthComponent` 且 `current <= 0` 的实体：
- 掉落：遍历 `DropTableComponent`，随机 roll count，调用 `ItemSpawnSystem::spawnAtPosition()`
- 死亡效果：粒子 + 音效（如果有 `DeathEffectComponent`）
- 网络实体：标记 `PendingNetworkDespawnTag`；本地实体：递归销毁整个子树

#### BlockBreakSystem
根据 `GameplayModeRules`：
- **创造模式**：即时破坏（`breakDurationMs` 作为冷却）
- **生存模式**：计时破坏（`breakElapsedMs / breakRequiredMs` 进度）
- 支持客户端/服务端模式（`mutableWorld == nullptr` 时发送网络 action）
- 破坏时产生音频、粒子、掉落事件，处理箱子库存

#### BlockPlaceSystem
处理方块放置：
- 冷却管理、距离验证（6.5m）
- 通过 `GameplayModeRules.decideBlockAction()` 决策
- `resolvePlacementState()` 解析放置状态（方向、sneaking）
- 服务端模式发送网络 action；本地模式直接 `world.setBlock()`
- 创造模式不消耗物品，生存模式 `consumeSelectedOne()`

#### ItemPhysicsSystem
掉落物物理：
- 重力（20 m/s²，终端速度 25）
- 三轴步进碰撞（`moveAndCollideAxis`）
- 地面摩擦 0.86，空中阻尼 0.92
- 旋转动画

#### ItemMergeSystem
每 0.2 秒执行一次：
- 构建空间哈希网格（cell=1.75m）
- 3x3x3 邻域搜索同物品合并
- 按 stackCount 加权平均位置/速度

#### ItemPickupSystem
玩家 1.35m 半径内拾取掉落物，0.35s 最小年龄延迟，`inventory.addItem()` 合并堆叠。

#### MobAISystem（四状态 AI）
- **Idle**: 等待 wanderTimer，30% 概率保持 idle，70% 随机方向 wander
- **Wander**: 以 wanderSpeed 随机方向移动
- **Pursue**: 以 pursueSpeed 追击最近玩家
- **Attack**: 在 attackRange 内按 attackCooldown 攻击，推入 `DamageEvent`

#### TransformHierarchySystem（BFS 层级计算）
1. Root 实体（有 `WorldTransformComponent` 无 `ParentComponent`）：`worldMatrix = translate(position)` 或 `local.toMatrix()`
2. BFS 遍历子节点：`child.worldMatrix = parent.worldMatrix * child.local.toMatrix()`

#### BlockSupportSystem（Tick 系统）
验证方块支撑规则：
- `"ground"`: 下方需 solid
- `"attached_face"`: 火把朝向对应面需 solid
- `affectedByGravity`: 下方需 solid，否则生成下落方块实体
- 每帧最多处理 1024 个位置

#### FallingBlockTickSystem（Tick 系统，20 TPS）
每个下落方块每 tick 下移一格，遇到 solid 方块时着地（`tryLandBlock`），产生粒子+音效并销毁实体。

#### FallingBlockInterpolateSystem（Fixed 系统，60 Hz）
在两个 tick 网格位置间 lerp 渲染位置（`alpha = tickAccumulator / tickInterval`）。

#### NetworkInterpolationSystem
客户端实体平滑：
- 优先使用快照缓冲区插值（`sampleSnapshotBuffer`）：根据 `renderServerTick` 在两个快照间 lerp
- 回退到目标 lerp（`applyFallbackTarget`）：指数平滑到 `targetPosition/Yaw/Pitch`
- 超过 `snapDistance` 直接 snap

#### AudioSyncSystem（两部分）
1. 消费 `AudioEventBus`：播放 spatial/2D 音效
2. 同步 `AudioSourceComponent`：管理 tracked AudioSource 生命周期（创建/重建/停止），更新 volume/pitch/position

### 4.6 Hook 与 Profiling

- **Hook 机制**：`GameplayPipelineHooks` 支持在 `DamageSystem` 之后插入回调
- **调试 Profile**：`runFixedUpdateProfiled()` 按分类（State/Drop/Particle）收集各系统耗时

---

## 5. 实体工厂与定义注册表

### 5.1 EntityFactory (`ecs/entity/EntityFactory.h/.cpp`)

静态工厂方法：

| 方法 | 说明 |
|------|------|
| `createServerPlayerProxy()` / `ensureServerPlayerProxy()` | 服务端玩家代理 |
| `createMob(entityId, position)` | 从 `EntityDefinitionRegistry` 查找定义，通过 `MobModelFactory` 创建 |
| `createZombie()` | 快捷创建僵尸 |
| `createItemDrop(params)` | 掉落物实体（DropItemTag + DropEntityId + Transform + Velocity + Item + Bounds + Lifetime + Spin + Grounded + NetworkSyncTag） |
| `createFallingBlock(params)` | 下落方块实体（FallingBlockTag + FallingBlockComponent + Transform + Bounds + Grounded + DropEntityId + NetworkSyncTag） |
| `createProjectile(owner, position, velocity, definition)` | 投射物实体 |

### 5.2 EntityDefinitionRegistry (`ecs/entity/EntityDefinitionRegistry.h/.cpp`)

单例模式，从 JSON 文件加载实体定义：

```cpp
struct MobEntityDefinition {
    std::string id;
    std::string model;
    std::string textureKey;
    EntitySkinLayout skinLayout;
    float visualScale;
    // Physics
    struct { vec3 halfExtents; vec3 colliderOffset; float eyeOffsetY; } physics;
    // AI
    struct { float wanderSpeed, pursueSpeed, acquisitionRange, loseTargetRange, attackRange, attackCooldownSeconds, attackDamage; } ai;
    // Combat
    float health;
    std::vector<DropTableEntry> drops;
    // Effects
    struct { std::string particleBlock; int particleCount; std::string soundId; float volume; } deathEffect, hurtEffect;
};
```

JSON 解析支持：health (int 或 {current, max} 对象)、physics、ai、drops (引用 item name)、deathEffect、hurtEffect。

### 5.3 MobModelFactory / SteveModelFactory

创建人形模型实体层级：

```
Root (Transform + AnimationState + Children)
  └── Torso (StevePart + LocalTransform + WorldTransform + Parent + Children)
        ├── Head
        ├── RightArm
        ├── LeftArm
        ├── RightLeg
        └── LeftLeg
```

Mob 版本额外添加 `MobTag`, `MobVisualComponent`, `MobAIComponent`, `PhysicsBodyComponent`, `HealthComponent`, `DropTableComponent`, `NetworkSyncTag`。

### 5.4 EntitySkinLayout

三种皮肤布局：`Steve64x64`, `Classic64x64`, `Classic64x32`。Classic 布局使用镜像左肢。

---

## 6. 事件总线与帧级通信

### 6.1 EventBus (`ecs/util/EventBus.h`)

帧级事件总线，存于 registry context，实现系统间松耦合：

```cpp
template <typename T>
struct EventBus {
    std::vector<T> events;
    void push(const T& event);
    std::vector<T> drain();   // 取出并清空
    void clear();
};
```

### 6.2 具体事件总线实例

| 事件总线 | 事件类型 | 生产者 | 消费者 |
|----------|----------|--------|--------|
| `DamageEventBus` | `DamageEvent` | PlayerMelee / MobAI / Projectile | DamageSystem |
| `AudioEventBus` | `PlaySoundEvent` | 各系统 | AudioSyncSystem |
| `ParticleEventBus` | `BlockBreakParticleEvent` | BlockBreak / Death / Projectile | ParticleSpawnSystem |
| `DropSpawnEventBus` | `DropSpawnRequestEvent` | BlockBreak / BlockSupport | ItemSpawnSystem |
| `FallingBlockSpawnEventBus` | `FallingBlockSpawnEvent` | BlockSupport | FallingBlockSpawnSystem |

### 6.3 其他上下文状态

- **`InputFrameState`**：单例上下文，存储从 `InputContextManager` 采样的原始轴值和动作状态
- **`GameplayRuntimeContext`**：存储当前游戏模式规则（`modeRules` + `gameplayMode`），供系统查询创造/生存模式
- **`PlayerQuery`**：只读查询工具，通过 `LocalPlayerTag` 查找玩家实体，提供 30+ 方法访问位置、物理、相机、移动状态、方块交互、统计、视角晃动、受伤效果、库存
- **`DropPhysicsHelpers`**：掉落物物理辅助（`isSolidBlock()`, `overlapsSolid()`, `moveAndCollideAxis()`）

---

## 7. 游戏逻辑层

### 7.1 Game (`game/Game.h/.cpp`)

顶层游戏会话管理器，拥有 `GameSession`、渲染运行时、HUD、音频同步和帧编排器。

**加载阶段状态机**：

```
NotStarted → Session → RenderRuntime → Ecs → InitialChunks → Complete
```

| 阶段 | 职责 |
|------|------|
| Session | `session.init()`, `initWorld(seed)` |
| RenderRuntime | `renderRuntime->init()`, `setRenderScene()` |
| Ecs | `initECS()`, `loadLocalPlayer()`, `initStateMachine()`, 创建 AudioSyncSystem 和 HudPresenter |
| InitialChunks | 多帧 pump chunk 加载直到完成（5x5 区块） |
| Complete | 稳定玩家位置 |

**帧方法**：
- `fixedUpdate(fixedStep, accumulator)` — 委托 `GameFrameOrchestrator`
- `updateFrame(deltaTime)` — 音频监听同步
- `renderFrame(frameTime)` — 委托 orchestrator 渲染

### 7.2 GameFrameOrchestrator (`game/orchestrator/GameFrameOrchestrator.h/.cpp`)

无状态帧编排器。

**`runFixedUpdate()` 流程**：

```
1. input.update()                          — 采样输入
2. session.receiveWorldMessages()          — 接收网络消息
3. 死亡重生检测（R 键）
4. stateMachine.update()                   — 游戏内状态机
5. 检查 pausesSimulation()                 — 暂停时跳过模拟
6. Time::advanceGameTime()                 — 推进游戏时间
7. Server tick（20 TPS，最多4次/帧）        — session.updateWorldAroundLocalPlayer()
8. ECS FixedUpdate（60 Hz）                — gameplayScene.runFixedUpdate()
9. world.flushInteractiveLighting()        — 刷新交互光照
10. sendClientInput()                      — 发送客户端输入到服务端
11. ECS Tick（20 TPS）                     — tickClock.advance() → runOneTick() 循环
```

**`renderFrame()` 流程**：

```
1. 构建 GameplayPresentationSnapshot（从 ECS 读取渲染数据）
2. 设置 RenderScene 状态（本地模型、光照、水面）
3. 渲染 3D 场景
4. Pre-UI 回调（截图）
5. HUD 渲染
6. Debug Dashboard（MECRAFT_DEBUG）
7. SwapBuffers
```

### 7.3 GameSession (`game/session/GameSession.h`)

聚合所有会话对象：

```cpp
class GameSession {
    GameServer   m_server;      // 权威 World
    GameClient   m_client;      // ClientWorld 镜像
    PhysicsSystem m_physics;
    GameplayScene m_ecs;        // ECS 场景
    DropSystem   m_dropSystem;
    CraftingSystem m_crafting;
    ParticleSystem m_particles;
    RainRenderer m_rain;
    CameraController m_camera;
    GameplayPresentationBuilder m_presentation;
    GameStateMachine m_stateMachine;
};
```

关键方法：`init()`, `initWorld()`, `initECS()`, `initStateMachine()`, `updateWorldAroundLocalPlayer()`, `saveLocalPlayer()`, `loadLocalPlayer()`, `syncLocalPlayerMode()`。

---

## 8. 游戏内状态机

### 8.1 IGameState 接口 (`game/states/IGameState.h`)

```cpp
class IGameState {
    virtual void onEnter() {}
    virtual void onExit() {}
    virtual void update(float dt, const InputSnapshot& snapshot) = 0;
    virtual bool pausesSimulation() const { return false; }
    virtual void render() {}
};
```

### 8.2 GameStateMachine (`game/states/GameStateMachine.h/.cpp`)

栈式状态机，支持 push/pop/change。使用延迟操作（`PendingOp`）机制避免在 dispatch 中修改栈。

### 8.3 GameplayState（主游戏状态）

- **onEnter**: 切换到 Gameplay 输入上下文，捕获鼠标，设置 `GameplayRuntimeContext`（modeRules + gameplayMode）
- **update**: 依次处理箱子交互 → 物品栏切换 → 命令输入 → 菜单切换 → legacy bridge
- 支持 `GameplayMode::Survival` 和 `GameplayMode::Creative` 两种模式

### 8.4 CreativeModeState

继承 `GameplayState`，使用 `CreativeModeRules` 和 `GameplayMode::Creative`。

### 8.5 CommandState

命令输入状态：
- 打开 UI 输入上下文，显示 `/` 前缀输入框
- 支持命令历史导航（上下箭头，最多 50 条）
- 命令解析：`/gamemode creative|survival`, `/time set <value>`, `/spawn <mobType>`
- 非斜杠开头作为聊天消息发送

### 8.6 UIState（暂停菜单 + 设置）

- `pausesSimulation() = true`
- 管理两个子屏幕：PauseMenuScreen 和 SettingsScreen
- ESC/Cancel 导航：设置 → 暂停 → 恢复游戏

---

## 9. 游戏模式规则

### 9.1 IGameplayModeRules (`game/modes/GameplayModeRules.h`)

```cpp
class IGameplayModeRules {
    virtual GameplayBlockAction decideBlockAction(request) const = 0;
    virtual float placeCooldownSeconds() const = 0;
    virtual float breakDurationMs(BlockID) const = 0;
    virtual bool shouldReportBreakProgress() const = 0;
};
```

### 9.2 两种模式实现

| 规则 | SurvivalModeRules | CreativeModeRules |
|------|-------------------|-------------------|
| placeCooldown | 0.18s | 0.18s |
| breakDuration | BlockDef.timeToBreak | 180ms (固定) |
| shouldReportBreakProgress | true | false |

---

## 10. 世界系统

### 10.1 World (`world/World.h`)

继承 `IWorldView`，权威世界实现。

**核心组成**：
- `TerrainGenerator` — 地形生成
- `LightService` — 光照计算
- `DayNightSystem` — 昼夜循环
- `FluidSystem` — 流体模拟
- `WeatherSystem` — 天气系统
- `BlockNeighborUpdateQueue` — 邻居更新队列
- `ChunkTicketManager` — 区块票据管理
- `SaveManager*` — 存档系统

**关键接口**：
- `getBlock(x,y,z)` / `setBlock(x,y,z,id)` / `getBlockState()` / `setBlockState()`
- `raycast(PhysicsInfo, maxDist)` — 方块射线检测
- `update(playerPos, dt)` — 区块流式加载
- `setBlockChangeCallback()` — 方块变更回调（服务端收集脏区块）
- `flushInteractiveLighting()` — 刷新交互式光照

### 10.2 IWorldView (`world/IWorldView.h`)

只读世界视图接口，`World` 和 `ClientWorld` 都实现此接口，解耦渲染器与具体 World。

### 10.3 Chunk (`world/chunk/Chunk.h`)

Chunk 是一个区块列（ChunkColumn），由 16 个垂直 SubChunk 组成：

```cpp
static constexpr int SIZE_X = 16;
static constexpr int SIZE_Y = 256;
static constexpr int SIZE_Z = 16;
static constexpr int NUM_SUB_CHUNKS = 16;  // 256/16
static constexpr std::size_t BLOCK_COUNT = 65536;
```

**成员**：
- `std::array<std::unique_ptr<SubChunk>, 16> m_subChunks`
- `Chunk* neighbors[4]` — 4方向邻居链接
- `std::array<int, 256> m_heightMap`
- `uint32_t m_dirtySubChunkMask`
- `uint64_t m_lightRevision`

### 10.4 SubChunk (`world/chunk/SubChunk.h`)

```cpp
static constexpr int SIZE = 16;
static constexpr std::size_t BLOCK_COUNT = 4096;  // 16^3
```

**语义类型**（用于渲染剔除）：
- `Air` — 全空气，无存储、无网格、不渲染
- `Solid` — 全单一方块，最小存储
- `Normal` — 混合内容，完整存储、网格、渲染

**存储**：Palette + BitPackedArray 压缩，支持方块数据和流体数据（waterlogged）。

**顶点格式**：
- 标准格式：32 字节（位置、UV、法线、天光、方块光、AO、层、动画、染色）
- 紧凑格式：16 字节（GPU 友好）

**渲染层**：opaque、cutout、cutoutDistance、transparent、water

---

## 11. 物理系统

### 11.1 PhysicsSystem (`physics/PhysicsSystem.h/.cpp`)

```cpp
class PhysicsSystem {
    PhysicsSystem(const IWorldView* worldView);
    void updateBody(PhysicsBody& body, const MoveIntent& intent, float dt);
    void updateBody(PhysicsBody& body, const MoveIntent& intent, float dt, const PhysicsTuning& tuningOverride);
    PhysicsTuning tuning;
};
```

### 11.2 核心数据结构

```cpp
struct PhysicsBody {
    vec3 position, velocity, halfExtents{0.3,0.9,0.3}, colliderOffset;
    float eyeOffsetY = 1.62;
    bool isGrounded, isInWater, hitWall, isFullySubmerged, isEyesInWater;
    float landingImpactSpeed;
};

struct PhysicsTuning {
    float gravity=20, jumpSpeed=8.5, moveSpeed=4.5, sprintMultiplier=1.3;
    float airControl=0.35, groundFriction=10, airDrag=1, terminalVelocity=30;
    float waterGravityScale=0.25, waterDrag=6, swimSpeed=3.2, swimUpAccel=10, waterFlowPush=18;
};
```

物理系统支持：陆地移动、跳跃、冲刺、潜行、飞行模式、水中游泳、水流推动、终端速度限制。

---

## 12. 客户端 / 服务器架构

### 12.1 架构模式

Mecraft 采用**权威服务器模型**：

- **单机模式**：GameServer 和 GameClient 在同一进程内运行，通过 `InProcessTransport` 零拷贝通信（`shared_ptr<Chunk>` 直接传递）
- **专用服务器模式**：独立进程运行 GameServer，客户端通过 ENet 网络连接

### 12.2 GameServer (`server/GameServer.h/.cpp`)

权威游戏服务器，拥有世界、ECS、物理系统。

**ConnectedClient 结构**：
```cpp
struct ConnectedClient {
    net::ClientId id;
    std::unique_ptr<net::ITransportEndpoint> transport;
    glm::vec3 lastPosition, lastVelocity;
    float lastYaw, lastPitch;
    uint8_t selectedHotbarSlot;
    uint32_t lastAckedInput, pendingInputActions;
    int viewDistance = 8;
    bool receivedHello, receivedViewConfig, isAdmin, awaitingRespawn, deathDropsSpawned;
    int respawnSnapshotTicksRemaining;
    net::NetworkGameplayMode gameplayMode;
    net::EntityNetId playerNetId;          // 0x80000000 | clientId
    entt::entity ecsPlayerEntity;
    std::unordered_set<net::EntityNetId> spawnedPlayerNetIds, spawnedEntityNetIds;
    std::unordered_set<int64_t> sentChunks;
};
```

**`tick()` 方法**（每 1/20 秒调用）：

```
1. processClientMessages()          — 处理客户端消息
2. cleanupDisconnectedClients()     — 清理断开连接
3. tickWorldSystems()               — 流体tick + 方块支撑
4. world.update(loadCenter, dt)     — 世界更新（区块加载/卸载/光照）
5. tickServerEcs(dt)                — 服务器ECS tick（专用模式）
6. updatePlayerLifecycle(dt)        — 玩家生命周期（死亡/重生）
7. sendNewChunksToClients()         — 发送新区块
8. sendSnapshotsToClients()         — 发送权威快照
9. sendInventorySnapshotsToClients()— 发送物品栏
10. syncEntitiesToClients()         — 实体同步
11. syncPlayersToClients()          — 多人玩家同步
12. sendBlockUpdatesToClients()     — 发送方块更新
13. checkSpawnChunksReady()         — 检查出生区块
```

**自动保存**：每 300 秒（5 分钟）自动保存一次。

### 12.3 GameClient (`client/GameClient.h/.cpp`)

客户端游戏逻辑核心，管理与服务器的连接、发送输入、接收世界状态更新。

**发送方法**（按通道分类）：

| 方法 | 通道 | 频率 |
|------|------|------|
| `sendHello()` | ReliableControl | 连接时 |
| `sendViewConfig(renderDistance)` | ReliableControl | 配置时 |
| `sendInput(dt, moveInput, lookDelta, ...)` | UnreliableState | 每固定更新 |
| `sendBlockAction(action)` | ReliableWorld | 玩家操作时 |
| `sendChatMessage(message)` / `sendCommandRequest(command)` | ReliableChat | 用户输入时 |
| `sendRespawnRequest()` | ReliableControl | 重生时 |

**消息接收**（按 MessageType 分发）：

| 消息类型 | 处理 |
|----------|------|
| ServerHello | 设置 clientId 和出生点 |
| ChunkData | 添加区块到 ClientWorld，计数到 25 后标记 spawnChunksReady |
| ChunkUnload | 移除区块 |
| BlockUpdateBatch | 应用方块更新和光照补丁 |
| ServerSnapshot | 更新权威位置、血量、受击效果、重生 |
| EntitySpawn/Despawn/Impact/Snapshot | 委托给 ClientEntityStore |
| InventorySnapshot | 同步物品栏（36槽位） |
| WorldStateSnapshot | 同步时间和天气 |
| PlayerModeUpdate | 更改游戏模式 |

### 12.4 ClientWorld (`client/ClientWorld.h/.cpp`)

客户端世界镜像，实现 `IWorldView` 接口，提供线程安全的区块存储和查询。

**线程安全**：使用 `std::mutex` 保护 `ChunkMap`，通过修订号（`m_activeChunkRevision`）机制提供快照引用。

**光照补丁三种格式**：
- 完整区块光照（`Chunk::BLOCK_COUNT` 字节）
- 单 SubChunk 光照（`SubChunk::BLOCK_COUNT` 字节）
- 奇数立方体补丁（推断边长，居中于 xyz）

### 12.5 ClientEntityStore (`client/ClientEntityStore.h/.cpp`)

管理远程实体，将 `EntityNetId` 映射到本地 `entt::entity`。

**实体创建**（根据 `EntityKind`）：
- `createDropEntity` — 掉落物
- `createProjectileEntity` — 投射物
- `createPlayerEntity` — 玩家（通过 `SteveModelFactory::createSteve`）
- `createMobEntity` — 怪物（通过 `MobModelFactory::createHumanoidMobReplica`）

**快照处理**：
- 跳过本地权威实体
- 使用 `NetworkInterpolationComponent` 进行插值，维护快照缓冲区（排序、去重、上限）
- 首次快照直接 snap 到位置
- 更新速度、血量，触发受伤效果

### 12.6 ChunkTicketManager (`server/ChunkTicketManager.h/.cpp`)

管理区块加载票据，支持多级半径和卸载迟滞。

**票据类型**：
- `Spawn` — 世界出生点，始终加载
- `PlayerSimulation` — 玩家位置，tick 半径
- `PlayerView` — 玩家视距，加载并发送给客户端
- `Forced` — 命令/调试，始终加载

**半径体系**（圆形判定，`dx*dx + dz*dz <= r*r`）：
- `simulationRadius`（默认8）— 实体/流体/随机 tick
- `viewRadius`（默认16）— 视距
- `loadRadius = viewRadius + 1` — 加载半径
- `unloadRadius = viewRadius + 3` — 卸载半径（迟滞带宽度2）

---

## 13. 网络同步机制

### 13.1 网络协议 (`net/Protocol.h`)

**通道系统**（映射到 ENet 通道）：

| 通道 | 用途 |
|------|------|
| ReliableControl (0) | 登录、握手、断开、配置 |
| ReliableWorld (1) | 区块数据、方块更新、物品栏 |
| UnreliableState (2) | 高频实体快照、玩家输入 |
| ReliableChat (3) | 聊天、命令、系统消息 |

**Packet 结构**：
```cpp
struct Packet {
    PacketChannel channel;
    MessageType type;
    std::vector<uint8_t> payload;       // 网络传输用二进制数据
    std::any inProcessPayload;          // 进程内零拷贝
};
```

### 13.2 传输层 (`net/Transport.h`)

抽象传输接口：
- `InProcessTransport` — 进程内，零拷贝
- `ENetTransport` — 网络传输（UDP + ENet 可靠性层）

### 13.3 实体同步流程

1. 服务器检测新的 `NetworkSyncTag` 实体，分配 `EntityNetId`
2. 处理 `PendingNetworkDespawnTag` 实体（发送冲击+销毁消息）
3. 为每个客户端发送尚未同步实体的 spawn 消息
4. 构建批量快照（位置/速度/朝向/血量/受伤标志），通过 UnreliableState 通道发送
5. 客户端 `ClientEntityStore` 接收并创建本地实体
6. `NetworkInterpolationSystem` 在客户端平滑插值

### 13.4 玩家网络 ID

服务器分配的玩家网络 ID：`0x80000000 | clientId`，最高位标识玩家实体。

---

## 14. 光照系统

### 14.1 LightService (`world/light/LightService.h/.cpp`)

异步光照服务，管理光照计算作业的提交、执行和结果合并。

**每区块光照状态**（`LightChunkState`）：
- `dirty`, `queued`, `inFlight`
- `pendingBlockChanges` — 待处理的方块变化
- `boundaryCache[4]` — 4方向边界缓存
- `pendingHaloMeshDirtyMask` — 待处理的光环网格脏掩码

**关键方法**：
- `onChunkLoaded(chunk)` — 标记区块为脏，建立基础光照缓存，强制新区块和邻居进行边界同步
- `onBlockChanged(wx, wy, wz, oldId, newId)` — 重新计算高度图，递增光照修订号，更新基础光照缓存
- `submitJobs(cameraPos, submitBudget)` — 按优先级（距离 + 原因偏置）选择脏区块，提交到线程池
  - 背压机制：已完成队列 > 64 时跳过提交
  - 原因偏置：BlockChanged=0, ChunkLoaded=500, NeighborBoundary=1000
- `drainCompleted(world, mergeBudget, timeBudgetMs)` — 从完成队列取出结果，应用光照到区块
- `processInteractiveJobsInline(cameraPos, jobBudget, ...)` — 主线程内联处理交互式光照

### 14.2 LightSolver (`world/light/LightSolver.cpp`)

基于 BFS 的光照传播求解器，支持增量更新和全量重建。

**传播规则**：
```cpp
template <LightKind Kind>
uint8_t propagateLevelFromOpacity(uint8_t level, int direction, uint8_t opacity) {
    if (level == 0 || opacity >= 15) return 0;
    uint8_t attenuation = (opacity == 0) ? 1 : opacity;
    if constexpr (Kind == LightKind::Sky) {
        if (DY[direction] == -1) attenuation = opacity; // 天空光向下不减额外1
    }
    return level > attenuation ? level - attenuation : 0;
}
```

**优化**：
- `isInteriorCell` 快速路径：内部单元格使用 `INDEX_DELTAS` 直接计算邻居索引
- `thread_local SolverBuffers`：每线程复用工作队列缓冲区，避免每次 3.5MB 堆分配
- 边界 dirtyMask 计算：只比较变化面的 16x256 切片

### 14.3 LightCache (`world/light/LightCache.h/.cpp`)

基础光照缓存构建和维护：
- `buildBaseLightFromChunk(chunk)` — 全量构建（天光自顶向下 + 方块光源收集）
- `recomputeSkyColumn(chunk, x, z, packed)` — 重算单列天空光
- `rebuildBlockLightFromSources(sources, packed)` — 从稀疏光源列表重建方块光

---

## 15. 世界生成系统

### 15.1 TerrainGenerator (`world/gen/TerrainGenerator.h/.cpp`)

**生物群系**：
- `Temperate` — 温带（草地/泥土）
- `Arid` — 干旱（沙子）
- `Mountain` — 山地
- `HighMountain` — 高山（石头/泥土）

**噪声系统**（Value Noise + FBM）：

| 噪声层 | 单元大小 | 倍频 | 用途 |
|--------|----------|------|------|
| continental | 320.0 | 4 | 大陆形状 |
| detail | 64.0 | 4 | 细节 |
| rough | 28.0 | 3 | 粗糙度 |
| ridgeBase | 96.0 | 4 | 山脊 |
| mountainNoise | 220.0 | 3 | 高山掩码 |
| moisture | 420.0 | 3 | 湿度（决定生物群系） |

洞穴噪声：3D FBM（44.0 单元，3 倍频），深度越深阈值越高。

**SIMD 优化**：
- SSE2 路径：`fbm2D2` — 同时处理 2 列
- AVX2 路径：`fbm2D4` — 同时处理 4 列
- 自动检测并在 `sampleSurfaceYBatch` 中批量处理

**地形生成逻辑**（`sampleBlock`）：
- y=0：基岩
- y <= surfaceY：石头，表层为 topBlock（grass/sand），向下 coverDepth 格为 fillBlock
- 洞穴雕刻（y > surfaceY-5 或 y < 10 时不雕刻）
- 矿物分布：钻石(y<=16)、金(y<=32)、铁(y<=64)、煤(y<=128)
- 海平面以下：水
- 地表以上：树木和植被

**树木生成**：
- 密度依赖湿度（温带 0.012+0.018*moisture，山地 0.004+0.008*moisture）
- 桦树概率 42%
- 树高 4-6 格，叶子半径 2
- 在区块生成时扫描 5x5 范围的锚点（允许跨区块叶子）

---

## 16. 架构特性总结

### 16.1 分层架构

```
App Layer (GameManager, AppStateMachine)
  ├── MainMenuAppState (主菜单)
  ├── LoadingAppState (加载)
  └── GameplayAppState (游戏)
        └── Game
              ├── GameSession (会话聚合)
              │     ├── GameServer (权威World)
              │     ├── GameClient (ClientWorld)
              │     ├── GameplayScene (ECS)
              │     │     ├── GameplayRegistry (entt::registry 封装)
              │     │     ├── GameplayServices (外部服务指针)
              │     │     ├── GameplayPipeline (系统管线)
              │     │     │     ├── FixedUpdate (60Hz, 33个系统)
              │     │     │     └── Tick (20Hz, 4个系统)
              │     │     └── GameTickClock
              │     ├── PhysicsSystem
              │     ├── DropSystem, ParticleSystem, CraftingSystem
              │     ├── CameraController
              │     └── GameStateMachine (游戏内状态机)
              ├── GameplayRenderRuntime (渲染)
              ├── GameplayHudPresenter (HUD)
              └── GameFrameOrchestrator (帧编排)
```

### 16.2 双频率管线设计

- **60 Hz FixedUpdate**：物理、输入、战斗、交互、物品、粒子、动画 — 需要高频率更新的逻辑
- **20 Hz Tick**：流体、方块支撑、下落方块 — Minecraft 语义的 tick 级逻辑
- Tick 系统通过 `GameTickClock` 的 accumulator 机制在 FixedUpdate 之间调度

### 16.3 事件驱动通信

系统间通过 registry context 中的 `EventBus<T>` 实现松耦合，生产者-消费者模式：

```
PlayerMelee/MobAI/Projectile  ──push──▶  DamageEventBus  ──drain──▶  DamageSystem
各系统                          ──push──▶  AudioEventBus   ──drain──▶  AudioSyncSystem
BlockBreak/Death/Projectile    ──push──▶  ParticleEventBus──drain──▶  ParticleSpawnSystem
BlockBreak/BlockSupport        ──push──▶  DropSpawnEventBus──drain──▶ ItemSpawnSystem
BlockSupport                   ──push──▶  FallingBlockSpawnEventBus ─▶ FallingBlockSpawnSystem
```

### 16.4 客户端/服务器解耦

- `IWorldView` 接口解耦渲染器与具体 World 实现
- `BlockBreakSystem` / `BlockPlaceSystem` 在客户端模式（`mutableWorld == nullptr`）发送网络 action
- `NetworkInterpolationSystem` 在客户端平滑服务端实体位置
- `EntityNetIdComponent` + `NetworkSyncTag` + `PendingNetworkDespawnTag` 管理网络实体生命周期

### 16.5 数据驱动设计

- 实体定义从 JSON 加载（`EntityDefinitionRegistry`）
- 方块定义从配置加载（`BlockRegistry`）
- 物品定义从配置加载（`ItemRegistry`）
- 合成配方从配置加载（`CraftingSystem`）
- 游戏模式规则通过接口多态（`IGameplayModeRules`）

### 16.6 性能优化亮点

| 优化点 | 技术 |
|--------|------|
| 地形生成 | SSE2/AVX2 SIMD 批量噪声采样 |
| 光照系统 | 异步多线程计算，增量更新（BFS remove/add），基础光照缓存，thread_local 缓冲区复用 |
| 区块存储 | Palette + BitPackedArray 压缩，SubChunkType 语义剔除 |
| 区块加载 | ChunkTicketManager 多级半径 + 迟滞卸载，按距离优先级加载 |
| 网络 | 进程内零拷贝，光照补丁三种格式（全区块/SubChunk/奇数立方体） |
| 渲染 | MDI（Multi-Draw Indirect）全局缓冲池，16 字节紧凑顶点格式 |

### 16.7 架构优势

1. **清晰的职责分离**：ECS 系统单一职责，通过事件总线松耦合
2. **可扩展性**：新增系统只需实现 `ISystem` 并注册到 Pipeline
3. **数据驱动**：实体、方块、物品、配方均从配置加载
4. **C/S 架构**：天然支持单机和专用服务器两种部署模式
5. **双频率更新**：高频率逻辑（60Hz）与 Minecraft 语义 tick（20Hz）分离
6. **类型安全**：基于 entt 的类型安全组件访问，编译期检查

---

*本报告基于源码分析生成，涵盖了 Mecraft 项目的 ECS 核心架构、组件系统、系统管线、实体工厂、事件总线、游戏逻辑层、状态机、世界系统、物理系统、客户端/服务器架构、网络同步、光照系统和世界生成系统。*
