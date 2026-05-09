
## ECS 系统重构完整方案

本方案分 **5 个阶段**，每个阶段独立可交付，不会破坏现有功能。按优先级从高到低排列。

---

### 阶段一：统一系统接口 + SystemContext

**目标**：消除系统接口不统一的问题，建立一致的调用约定。

**核心改动**：

#### 1.1 新增 `SystemContext`

```cpp
// src/ecs/SystemContext.h
#ifndef MECRAFT_ECS_SYSTEM_CONTEXT_H
#define MECRAFT_ECS_SYSTEM_CONTEXT_H

#include "GameplayRegistry.h"
#include "GameplayServices.h"

namespace ecs {

struct SystemContext {
    GameplayRegistry& registry;
    GameplayServices& services;
    float dt = 0.0f;
};

} // namespace ecs
#endif
```

#### 1.2 定义 `ISystem` 接口

```cpp
// src/ecs/ISystem.h
#ifndef MECRAFT_ECS_ISYSTEM_H
#define MECRAFT_ECS_ISYSTEM_H

#include "SystemContext.h"

namespace ecs {

class ISystem {
public:
    virtual ~ISystem() = default;
    virtual void update(SystemContext& ctx) = 0;
};

} // namespace ecs
#endif
```

#### 1.3 将所有系统改为实现 `ISystem`

以 `ViewBobSystem` 为例（最简系统）：

```cpp
// ViewBobSystem.h
class ViewBobSystem : public ISystem {
public:
    void update(SystemContext& ctx) override;
};

// ViewBobSystem.cpp
void ViewBobSystem::update(SystemContext& ctx) {
    auto& registry = ctx.registry;
    float dt = ctx.dt;
    // ... 原有逻辑不变 ...
}
```

对于需要外部服务的系统，从 `ctx.services` 获取：

```cpp
// CharacterPhysicsSystem.cpp
void CharacterPhysicsSystem::update(SystemContext& ctx) {
    if (!ctx.services.physicsSystem) return;
    auto& registry = ctx.registry;
    auto& physicsSystem = *ctx.services.physicsSystem;
    float dt = ctx.dt;
    // ... 原有逻辑 ...
}
```

#### 1.4 重构 `GameplayScene` 为系统列表驱动

```cpp
// GameplayScene.h
class GameplayScene {
public:
    GameplayScene();

    GameplayRegistry& registry() { return m_registry; }
    const GameplayRegistry& registry() const { return m_registry; }

    GameplayServices& services() { return m_services; }
    const GameplayServices& services() const { return m_services; }

    GameTickClock& tickClock() { return m_tickClock; }

    void initLocalPlayer(const glm::vec3& spawnPos);
    void runFixedUpdate(float dt);
    void runOneTick();

private:
    GameplayRegistry m_registry;
    GameplayServices m_services;
    GameTickClock    m_tickClock;
    entt::entity     m_localPlayer = entt::null;

    // Fixed-update pipeline (ordered)
    std::vector<std::unique_ptr<ISystem>> m_fixedUpdateSystems;
    // Tick-rate pipeline
    std::vector<std::unique_ptr<ISystem>> m_tickSystems;
};
```

```cpp
// GameplayScene.cpp
GameplayScene::GameplayScene() {
    // Fixed update pipeline — 执行顺序即声明顺序
    m_fixedUpdateSystems.push_back(std::make_unique<InputSamplingSystem>());
    m_fixedUpdateSystems.push_back(std::make_unique<PlayerIntentBuildSystem>());
    m_fixedUpdateSystems.push_back(std::make_unique<MobAISystem>());
    m_fixedUpdateSystems.push_back(std::make_unique<CharacterPhysicsSystem>());
    m_fixedUpdateSystems.push_back(std::make_unique<PlayerRuntimeUpdateSystem>());
    m_fixedUpdateSystems.push_back(std::make_unique<ViewBobSystem>());
    m_fixedUpdateSystems.push_back(std::make_unique<BlockTargetSystem>());       // 阶段二拆分
    m_fixedUpdateSystems.push_back(std::make_unique<BlockBreakSystem>());        // 阶段二拆分
    m_fixedUpdateSystems.push_back(std::make_unique<BlockPlaceSystem>());        // 阶段二拆分
    m_fixedUpdateSystems.push_back(std::make_unique<ItemSpawnSystem>());         // 阶段二提升
    m_fixedUpdateSystems.push_back(std::make_unique<ItemPhysicsSystem>());       // 阶段二提升
    m_fixedUpdateSystems.push_back(std::make_unique<ItemMergeSystem>());         // 阶段二提升
    m_fixedUpdateSystems.push_back(std::make_unique<ItemPickupSystem>());        // 阶段二提升
    m_fixedUpdateSystems.push_back(std::make_unique<ItemLifetimeSystem>());      // 阶段二提升
    m_fixedUpdateSystems.push_back(std::make_unique<ParticleSpawnSystem>());
    m_fixedUpdateSystems.push_back(std::make_unique<ParticleSimulationSystem>());
    m_fixedUpdateSystems.push_back(std::make_unique<ParticleCleanupSystem>());
    m_fixedUpdateSystems.push_back(std::make_unique<PlayerAudioBridgeSystem>());
    m_fixedUpdateSystems.push_back(std::make_unique<FallRollEffectSystem>());
    m_fixedUpdateSystems.push_back(std::make_unique<AudioSyncSystem>());
    m_fixedUpdateSystems.push_back(std::make_unique<SteveSyncSystem>());
    m_fixedUpdateSystems.push_back(std::make_unique<SteveAnimationSystem>());
    m_fixedUpdateSystems.push_back(std::make_unique<MobAnimationSystem>());
    m_fixedUpdateSystems.push_back(std::make_unique<TransformHierarchySystem>());
}

void GameplayScene::runFixedUpdate(float dt) {
    SystemContext ctx{m_registry, m_services, dt};
    for (auto& system : m_fixedUpdateSystems) {
        system->update(ctx);
    }
}

void GameplayScene::runOneTick() {
    if (m_services.world) {
        FluidTickSystem::update(*m_services.world, m_tickClock.tickIndex());
    }
}
```

**迁移策略**：逐步迁移，未迁移的系统可用 `LegacySystemAdapter` 包装：

```cpp
// 过渡适配器 — 将旧的 static update 签名包装为 ISystem
class LegacySystemAdapter : public ISystem {
public:
    using UpdateFn = std::function<void(SystemContext&)>;
    explicit LegacySystemAdapter(UpdateFn fn) : m_fn(std::move(fn)) {}
    void update(SystemContext& ctx) override { m_fn(ctx); }
private:
    UpdateFn m_fn;
};
```

---

### 阶段二：拆分 Bridge 系统 + 消除编排器

**目标**：将 3 个 Bridge 系统拆分为纯 ECS 系统，消除 `DropCollectionBridgeSystem` 编排器。

#### 2.1 拆分 `BlockInteractionBridgeSystem`（231行 → 3个系统）

**拆分方案**：

| 新系统 | 职责 | 读取组件 | 写入组件 | 写入事件 |
|--------|------|----------|----------|----------|
| `BlockTargetSystem` | 射线检测，更新目标方块 | Transform, CameraState, BlockActionIntent | BlockTargetComponent | — |
| `BlockBreakSystem` | 破坏进度+执行破坏 | BlockTarget, BlockActionIntent, BlockBreak, BlockInteractionRuntime | BlockBreakComponent, BlockInteractionRuntimeComponent | AudioEvent, ParticleEvent, DropSpawnRequestEvent |
| `BlockPlaceSystem` | 放置逻辑+冷却 | BlockTarget, BlockActionIntent, Inventory, InventoryData, MoveIntent, BlockInteractionRuntime | BlockInteractionRuntimeComponent, InventoryData | AudioEvent, DropSpawnRequestEvent |

**新增事件类型**：

```cpp
// 在 util/DropSpawnEventBuffer.h 中
struct DropSpawnRequestEvent {
    BlockID blockId = 0;
    glm::ivec3 blockPos{};
};

struct DropSpawnEventBuffer {
    std::vector<DropSpawnRequestEvent> spawnRequests;
};

inline DropSpawnEventBuffer& ensureDropSpawnEventBuffer(GameplayRegistry& registry) {
    if (!registry.ctxHas<DropSpawnEventBuffer>()) {
        registry.ctxSet<DropSpawnEventBuffer>();
    }
    return registry.ctxGet<DropSpawnEventBuffer>();
}
```

**BlockTargetSystem 实现**：

```cpp
// systems/interaction/BlockTargetSystem.h
class BlockTargetSystem : public ISystem {
public:
    void update(SystemContext& ctx) override;
};

// systems/interaction/BlockTargetSystem.cpp
void BlockTargetSystem::update(SystemContext& ctx) {
    if (!ctx.services.world) return;
    auto& world = *ctx.services.world;
    auto& registry = ctx.registry;

    auto view = registry.view<LocalPlayerTag,
                              BlockActionIntentComponent,
                              TransformComponent,
                              CameraStateComponent,
                              BlockTargetComponent>();
    for (auto e : view) {
        auto& target = view.get<BlockTargetComponent>(e);
        const auto& transform = view.get<TransformComponent>(e);
        const auto& camera = view.get<CameraStateComponent>(e);

        PhysicsInfo pickRay = {
            transform.position + glm::vec3(0.0f, transform.eyeHeight, 0.0f),
            camera.front
        };
        const RayHit hit = world.raycast(pickRay, kPickDistance);
        target.hasTarget = hit.hit;
        target.targetBlock = hit.hit ? hit.blockPos : glm::ivec3{};
        target.placeBlock = hit.hit ? hit.blockPos + hit.normal : glm::ivec3{};
        target.hitNormal = hit.hit ? hit.normal : glm::ivec3{};
    }
}
```

**BlockBreakSystem 实现**：

```cpp
// systems/interaction/BlockBreakSystem.h
class BlockBreakSystem : public ISystem {
public:
    void update(SystemContext& ctx) override;
};

// systems/interaction/BlockBreakSystem.cpp
void BlockBreakSystem::update(SystemContext& ctx) {
    if (!ctx.services.world || !ctx.services.uiRenderer) return;
    auto& world = *ctx.services.world;
    auto& uiRenderer = *ctx.services.uiRenderer;
    auto& registry = ctx.registry;
    float dt = ctx.dt;

    const IGameplayModeRules& modeRules = resolveModeRules(registry);
    auto& audioEvents = ensureAudioEventBuffer(registry);
    auto& particleEvents = ensureParticleEventBuffer(registry);
    auto& dropEvents = ensureDropSpawnEventBuffer(registry);

    auto view = registry.view<LocalPlayerTag,
                              BlockActionIntentComponent,
                              BlockTargetComponent,
                              BlockBreakComponent,
                              BlockInteractionRuntimeComponent>();
    for (auto e : view) {
        auto& runtime = view.get<BlockInteractionRuntimeComponent>(e);
        auto& blockBreak = view.get<BlockBreakComponent>(e);
        const auto& intent = view.get<BlockActionIntentComponent>(e);
        const auto& target = view.get<BlockTargetComponent>(e);

        runtime.creativeBreakCooldownRemaining =
            std::max(0.0f, runtime.creativeBreakCooldownRemaining - dt);

        if (!intent.wantsBreak || !target.hasTarget) {
            resetBreakSession(blockBreak, uiRenderer, runtime);
            continue;
        }

        const glm::ivec3 hitBlock = target.targetBlock;
        const BlockID targetBlock = world.getBlock(hitBlock.x, hitBlock.y, hitBlock.z);
        if (targetBlock == 0 || !BlockRegistry::get(targetBlock).isSelectable) {
            resetBreakSession(blockBreak, uiRenderer, runtime);
            continue;
        }

        uiRenderer.setHeldItemPreviewActionAnimationActive(true);

        if (!modeRules.shouldReportBreakProgress()) {
            // Creative instant break
            if (runtime.creativeBreakCooldownRemaining > 0.0f) continue;
            world.setBlock(hitBlock.x, hitBlock.y, hitBlock.z, 0);
            audioEvents.playSoundEvents.push_back({...});
            particleEvents.blockBreakEvents.push_back({hitBlock, targetBlock});
            dropEvents.spawnRequests.push_back({targetBlock, hitBlock});
            runtime.creativeBreakCooldownRemaining = modeRules.breakDurationMs(targetBlock) / 1000.0f;
            resetBreakSession(blockBreak, uiRenderer, runtime);
            continue;
        }

        // Survival break with progress
        const float requiredMs = modeRules.breakDurationMs(targetBlock);
        if (!runtime.breakActive || runtime.breakBlockPos != hitBlock) {
            runtime.breakActive = true;
            runtime.breakBlockPos = hitBlock;
            runtime.breakElapsedMs = 0.0f;
            runtime.breakRequiredMs = requiredMs;
        }

        runtime.breakElapsedMs += dt * 1000.0f;
        blockBreak.active = true;
        blockBreak.blockPos = hitBlock;
        blockBreak.progress01 = std::clamp(runtime.breakElapsedMs / runtime.breakRequiredMs, 0.0f, 1.0f);

        if (runtime.breakElapsedMs >= runtime.breakRequiredMs) {
            world.setBlock(hitBlock.x, hitBlock.y, hitBlock.z, 0);
            audioEvents.playSoundEvents.push_back({...});
            particleEvents.blockBreakEvents.push_back({hitBlock, targetBlock});
            dropEvents.spawnRequests.push_back({targetBlock, hitBlock});
            resetBreakSession(blockBreak, uiRenderer, runtime);
        }
    }
}
```

**BlockPlaceSystem 实现**：类似拆分，读取 `BlockActionIntentComponent.wantsPlace`，处理放置冷却和物品消耗。

#### 2.2 消除 `DropCollectionBridgeSystem` 编排器

**现状**：编排器内部按顺序调用 5 个子系统，其中 `ItemPickupSystem` 需要玩家位置和背包。

**新方案**：

1. 将 5 个子系统全部提升为 `ISystem`，由 `GameplayScene` 统一调度
2. `ItemSpawnSystem::update()` 改为消费 `DropSpawnEventBuffer` 中的请求
3. `ItemPickupSystem` 改为自行查询 LocalPlayer 实体获取位置和背包

```cpp
// 修改后的 ItemSpawnSystem
void ItemSpawnSystem::update(SystemContext& ctx) {
    auto& registry = ctx.registry;
    if (!registry.ctxHas<DropSpawnEventBuffer>()) return;
    auto& spawnBuffer = registry.ctxGet<DropSpawnEventBuffer>();

    for (const auto& req : spawnBuffer.spawnRequests) {
        // 转换 BlockID → ItemID (via loot table)
        ItemID itemId = BlockRegistry::get(req.blockId).dropItem;
        spawn(registry, itemId, req.blockPos, 1);
    }
    spawnBuffer.spawnRequests.clear();
}
```

```cpp
// 修改后的 ItemPickupSystem — 自行查询玩家
void ItemPickupSystem::update(SystemContext& ctx) {
    auto& registry = ctx.registry;
    constexpr float kDropCollectRadius = 1.35f;

    auto playerView = registry.view<LocalPlayerTag, TransformComponent, InventoryDataComponent>();
    for (auto e : playerView) {
        const auto& transform = playerView.get<TransformComponent>(e);
        auto& inventoryData = playerView.get<InventoryDataComponent>(e);
        pickup(registry, transform.position, kDropCollectRadius, inventoryData.inventory);
        break; // 只处理第一个 LocalPlayer
    }
}

// 提取核心逻辑为独立方法
uint32_t ItemPickupSystem::pickup(GameplayRegistry& registry,
                                   const glm::vec3& position,
                                   float radius,
                                   Inventory& inventory) {
    // 原有 update 的核心逻辑 ...
}
```

**删除** `DropCollectionBridgeSystem`，在 `GameplayScene` 构造函数中替换为 5 个独立系统。

#### 2.3 纯化 `PlayerAudioBridgeSystem`

`PlayerAudioBridgeSystem` 已经是纯 ECS 操作（只读写组件和事件缓冲），改名为 `PlayerFootstepAudioSystem`，实现 `ISystem` 接口即可。

```cpp
// systems/audio/PlayerFootstepAudioSystem.h
class PlayerFootstepAudioSystem : public ISystem {
public:
    void update(SystemContext& ctx) override;
};
```

---

### 阶段三：拆分 Components.h + 减少编译依赖

**目标**：将单一 `Components.h` 拆分为按域分组的头文件，消除不必要的重编译。

#### 3.1 拆分方案

```
src/ecs/components/
├── Tags.h                        # LocalPlayerTag, DropItemTag, ParticleTag, SteveTag, MobTag
├── IntentComponents.h            # MoveIntent, LookIntent, HotbarIntent, BlockActionIntent
├── TransformComponents.h         # Transform, LocalTransform, WorldTransform, Parent, Children
├── PhysicsComponents.h           # PhysicsBody, Velocity, Bounds, GroundedState, CharacterController
├── CameraComponents.h            # CameraState, FlightState, SprintFov
├── BlockInteractionComponents.h  # BlockTarget, BlockBreak, BlockInteractionRuntime
├── PlayerStateComponents.h       # FootstepState, LandingState, FallRoll, ViewBob, HurtEffect
├── PlayerStatsComponents.h       # Health, Armor, Food, Inventory, InventoryData
├── ItemComponents.h              # DropEntityId, Item, Lifetime, SpinVisual
├── ParticleComponents.h          # ParticleComponent
├── AudioComponents.h             # AudioSourceComponent
├── ModelComponents.h             # StevePart, SteveAnimationState, SkinType, MobAI
└── Components.h                  # 汇总 — 仅 #include 上述所有文件
```

#### 3.2 汇总头文件保持兼容

```cpp
// components/Components.h — 保留，向后兼容
#ifndef MECRAFT_ECS_COMPONENTS_H
#define MECRAFT_ECS_COMPONENTS_H

#include "Tags.h"
#include "IntentComponents.h"
#include "TransformComponents.h"
#include "PhysicsComponents.h"
#include "CameraComponents.h"
#include "BlockInteractionComponents.h"
#include "PlayerStateComponents.h"
#include "PlayerStatsComponents.h"
#include "ItemComponents.h"
#include "ParticleComponents.h"
#include "AudioComponents.h"
#include "ModelComponents.h"

#endif
```

每个系统 .cpp 改为只 include 它需要的子头文件。例如：

```cpp
// CharacterPhysicsSystem.cpp
#include "CharacterPhysicsSystem.h"
#include "../../components/PhysicsComponents.h"
#include "../../components/TransformComponents.h"
#include "../../components/CameraComponents.h"
#include "../../components/PlayerStateComponents.h"
#include "../../components/Tags.h"
```

---

### 阶段四：安全化 GameplayServices

**目标**：消除裸指针 + 守卫检查，改用引用 + 构造时注入。

#### 4.1 重构 GameplayServices

```cpp
// GameplayServices.h
#ifndef MECRAFT_GAMEPLAY_SERVICES_H
#define MECRAFT_GAMEPLAY_SERVICES_H

#include <functional>
#include <optional>

// Forward declarations
class World;
class AudioEngine;
class InputContextManager;
class ResourceMgr;
class DropSystem;
class ParticleSystem;
class UIRenderer;
class CameraController;
namespace physics { class PhysicsSystem; }

namespace ecs {

/// Optional service wrapper — services that may not be available in all contexts.
template<typename T>
struct OptionalService {
    T* ptr = nullptr;
    [[nodiscard]] bool available() const { return ptr != nullptr; }
    T& operator*() const { return *ptr; }
    T* operator->() const { return ptr; }
};

struct GameplayServices {
    // Required services (always available during gameplay)
    World&                      world;
    AudioEngine&                audioEngine;
    InputContextManager&        inputContextManager;
    physics::PhysicsSystem&     physicsSystem;

    // Optional services (may be null in test/headless contexts)
    OptionalService<ResourceMgr>       resourceMgr;
    OptionalService<DropSystem>        dropSystem;
    OptionalService<ParticleSystem>    particleSystem;
    OptionalService<UIRenderer>        uiRenderer;
    OptionalService<CameraController>  cameraController;
};

} // namespace ecs
#endif
```

#### 4.2 外部构造

```cpp
// 在 GameplayState 或 main 中构造
ecs::GameplayServices services{
    .world = *worldPtr,
    .audioEngine = *audioEnginePtr,
    .inputContextManager = *inputCtxPtr,
    .physicsSystem = *physicsSystemPtr,
    .resourceMgr = {resourceMgrPtr},
    .dropSystem = {dropSystemPtr},
    .uiRenderer = {uiRendererPtr},
    .cameraController = {cameraControllerPtr},
};
```

#### 4.3 系统中的使用

```cpp
// 必需服务 — 直接使用，无需检查
void CharacterPhysicsSystem::update(SystemContext& ctx) {
    auto& physicsSystem = ctx.services.physicsSystem; // 引用，保证非空
    // ...
}

// 可选服务 — 显式检查
void BlockBreakSystem::update(SystemContext& ctx) {
    if (!ctx.services.uiRenderer.available()) return;
    auto& uiRenderer = *ctx.services.uiRenderer;
    // ...
}
```

---

### 阶段五：泛化事件缓冲 + 系统依赖声明

**目标**：统一帧内事件传递机制，建立系统间依赖的可验证声明。

#### 5.1 泛化 EventBus

```cpp
// util/EventBus.h
#ifndef MECRAFT_ECS_EVENT_BUS_H
#define MECRAFT_ECS_EVENT_BUS_H

#include <vector>
#include <type_traits>

namespace ecs {

/// Frame-scoped event bus for one-producer-to-many-consumer patterns.
/// Producers push events during the frame; consumers drain at frame end.
template<typename T>
struct EventBus {
    std::vector<T> events;

    void push(const T& event) { events.push_back(event); }
    void push(T&& event) { events.push_back(std::move(event)); }

    [[nodiscard]] const std::vector<T>& peek() const { return events; }
    [[nodiscard]] std::vector<T> drain() {
        std::vector<T> result = std::move(events);
        events.clear();
        return result;
    }
    void clear() { events.clear(); }
    [[nodiscard]] bool empty() const { return events.empty(); }
    [[nodiscard]] size_t size() const { return events.size(); }
};

} // namespace ecs
#endif
```

#### 5.2 用 EventBus 替换现有事件缓冲

```cpp
// 重构后的 AudioEventBuffer.h
struct PlaySoundEvent { ... };

using AudioEventBus = EventBus<PlaySoundEvent>;

// 在 GameplayRegistry 上下文中
// ctxSet<AudioEventBus>() 替代 ctxSet<AudioEventBuffer>()

// 生产者
auto& audioBus = registry.ctxGet<AudioEventBus>();
audioBus.push({"walk_grass1", pos, true, 1.0f});

// 消费者
auto events = registry.ctxGet<AudioEventBus>().drain();
for (const auto& e : events) { ... }
```

同理重构 `ParticleEventBuffer` → `EventBus<BlockBreakParticleEvent>`，`DropSpawnEventBuffer` → `EventBus<DropSpawnRequestEvent>`。

#### 5.3 系统依赖声明

```cpp
// 在 ISystem 中添加可选的类型级依赖声明
template<typename... RequiredComponents, typename... WrittenComponents>
struct SystemDependency {
    using Required = std::tuple<RequiredComponents...>;
    using Written = std::tuple<WrittenComponents...>;
};

// 系统声明示例
class BlockBreakSystem : public ISystem {
public:
    // 声明此系统读写的组件（可用于静态验证和文档）
    using Dependencies = SystemDependency<
        // Required (read)
        LocalPlayerTag, BlockActionIntentComponent, BlockTargetComponent,
        // Written
        BlockBreakComponent, BlockInteractionRuntimeComponent
    >;

    void update(SystemContext& ctx) override;
};
```

可在 `GameplayScene` 构造时添加 debug 模式的运行时验证：

```cpp
// Debug-only: validate system execution order respects component dependencies
#ifndef NDEBUG
void GameplayScene::validateSystemOrder() {
    // For each pair of consecutive systems, check that if system B
    // requires component X, then system A (which writes X) comes before B.
    // This catches ordering bugs at startup.
}
#endif
```

---

### 迁移路线图

```
Week 1:  阶段一 — SystemContext + ISystem + LegacySystemAdapter
         → GameplayScene 从手写调度迁移到系统列表驱动
         → 所有现有系统通过 adapter 先跑通

Week 2:  阶段二 — 拆分 BlockInteractionBridgeSystem
         → BlockTargetSystem + BlockBreakSystem + BlockPlaceSystem
         → 新增 DropSpawnEventBuffer

Week 3:  阶段二续 — 消除 DropCollectionBridgeSystem
         → 5个子系统提升为独立 ISystem
         → ItemSpawnSystem 消费 DropSpawnEventBuffer
         → ItemPickupSystem 自行查询 LocalPlayer
         → PlayerAudioBridgeSystem 改名 PlayerFootstepAudioSystem

Week 4:  阶段三 — 拆分 Components.h
         → 12个子头文件
         → 各系统 .cpp 精简 include

Week 5:  阶段四 — GameplayServices 安全化
         → 必需服务改为引用
         → 可选服务包装为 OptionalService

Week 6:  阶段五 — EventBus 泛化 + 依赖声明
         → 替换 3 个事件缓冲为 EventBus<T>
         → 添加 SystemDependency 声明
         → Debug 模式运行时验证
```

---

### 最终架构视图

```
GameplayScene
├── GameplayRegistry          (entt::registry facade)
├── GameplayServices          (ref + OptionalService<T>)
├── GameTickClock
├── m_fixedUpdateSystems[]    (ordered list of ISystem*)
│   ├── InputSamplingSystem
│   ├── PlayerIntentBuildSystem
│   ├── MobAISystem
│   ├── CharacterPhysicsSystem
│   ├── PlayerRuntimeUpdateSystem
│   ├── ViewBobSystem
│   ├── BlockTargetSystem        ← was part of Bridge
│   ├── BlockBreakSystem         ← was part of Bridge
│   ├── BlockPlaceSystem         ← was part of Bridge
│   ├── ItemSpawnSystem          ← was nested in Bridge
│   ├── ItemPhysicsSystem        ← was nested in Bridge
│   ├── ItemMergeSystem          ← was nested in Bridge
│   ├── ItemPickupSystem         ← was nested in Bridge
│   ├── ItemLifetimeSystem       ← was nested in Bridge
│   ├── ParticleSpawnSystem
│   ├── ParticleSimulationSystem
│   ├── ParticleCleanupSystem
│   ├── PlayerFootstepAudioSystem ← was Bridge
│   ├── FallRollEffectSystem
│   ├── AudioSyncSystem
│   ├── SteveSyncSystem
│   ├── SteveAnimationSystem
│   ├── MobAnimationSystem
│   └── TransformHierarchySystem
└── m_tickSystems[]
    └── FluidTickSystem
```

每个系统统一签名 `void update(SystemContext&)`，通过组件读写和 EventBus 解耦，无嵌套调度，无裸指针守卫。