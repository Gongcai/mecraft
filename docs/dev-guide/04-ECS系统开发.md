# 04 · ECS 系统开发

> 目标：理解 ECS 分层、学会新增组件与系统、正确使用服务注入与事件总线、写出可测试的玩法代码。
> 前置阅读：`02-核心数据流.md`（双管线机制）。涉及代码：`src/ecs/`。

---

## 目录

1. [ECS 架构速览](#1-ecs-架构速览)
2. [组件（Component）](#2-组件component)
3. [系统（System）](#3-系统system)
4. [注册系统到 Pipeline](#4-注册系统到-pipeline)
5. [服务注入：GameplayServices](#5-服务注入gameplayservices)
6. [上下文：SystemContext 与 Registry 上下文](#6-上下文systemcontext-与-registry-上下文)
7. [事件总线：帧内生产者-消费者模式](#7-事件总线帧内生产者-消费者模式)
8. [实体工厂与实体创建](#8-实体工厂与实体创建)
9. [本地玩家生命周期](#9-本地玩家生命周期)
10. [客户端 / 服务器 profile 差异](#10-客户端--服务器-profile-差异)
11. [性能与数据局部性准则](#11-性能与数据局部性准则)
12. [可测试性](#12-可测试性)
13. [完整示例：新增一个"冲刺跳跃粒子"系统](#13-完整示例新增一个冲刺跳跃粒子系统)
14. [系统与组件速查索引](#14-系统与组件速查索引)

---

## 1. ECS 架构速览

项目 ECS 是 EnTT 的薄封装，四件套：

```
GameplayScene          —— ECS 根对象：持有 Registry + Services + Pipeline
GameplayRegistry       —— entt::registry 封装（create/destroy/emplace/view/ctx）
GameplayServices       —— 外部服务注入（World/Client/Physics/UI…）
GameplayPipeline       —— 系统集合 + 双 profile（Client/Server）+ 执行顺序
```

关键文件：

| 文件 | 职责 |
| --- | --- |
| `src/ecs/GameplayScene.h` | `runFixedUpdate` / `runOneTick` 入口；`initLocalPlayer` |
| `src/ecs/GameplayRegistry.h` | Registry 封装（见下文第 6 节） |
| `src/ecs/GameplayServices.h` | `OptionalService<T>` 包装的服务槽 |
| `src/ecs/GameplayPipeline.cpp` | 系统注册表（Client/Server 两套） |
| `src/ecs/ISystem.h` | 系统基类 `update(SystemContext&)` |
| `src/ecs/SystemContext.h` | `{registry, services, dt, tickIndex}` |
| `src/ecs/components/` | 全部组件（按领域拆头文件） |
| `src/ecs/systems/` | 全部系统（按领域分子目录） |
| `src/ecs/entity/` | 实体工厂与定义注册表 |
| `src/ecs/util/` | 事件总线、tick 时钟、玩家查询等工具 |

**设计规则**：系统之间**不直接互相调用**，数据流只通过组件与事件总线。这让系统可以独立测试、独立替换。

---

## 2. 组件（Component）

### 2.1 定义位置与风格

按领域写在 `src/ecs/components/`（`Components.h` 汇总 include），纯数据 POD 结构体：

```cpp
// src/ecs/components/InputComponents.h
struct MoveIntentComponent {
    glm::vec3 moveDirection{0.0f};  // 归一化移动方向（世界系）
    bool jump = false;
    bool sprint = false;
    bool crouch = false;
};
```

组件分类：
- **状态组件**：`TransformComponent`、`HealthComponent`、`FoodComponent`、`InventoryComponent`（物品栏）。
- **意图组件**：`MoveIntentComponent`、`LookIntentComponent`、`BlockActionIntentComponent`（输入 → 意图，系统链第一步）。
- **运行时组件**：`BlockInteractionRuntimeComponent`（冷却、连击计时）、`SprintFovComponent`。
- **标签组件**：`LocalPlayerTag`、`MobTag`、`DropItemTag`、`ProjectileTag`、`SteveTag`（空结构体，用于 `view<Tag, ...>` 过滤）。

### 2.2 属性规则

1. **必须可默认构造**（EnTT 存储要求），用聚合初始化/成员默认值。
2. **不要在组件里放指针/引用**（生命周期难追踪）；需要访问外部对象用服务（第 5 节）或把数据拷进组件。
3. 频繁改写的组件（`TransformComponent`）保持小体积，避免缓存行浪费。
4. 新增组件后**无需改 CMakeLists**（头文件自动可见），但要在 `Components.h` 汇总 include 中登记。

---

## 3. 系统（System）

### 3.1 系统基类

```cpp
// src/ecs/ISystem.h
class ISystem {
public:
    virtual ~ISystem() = default;
    virtual void update(SystemContext& ctx) = 0;
};
```

### 3.2 系统骨架模板

```cpp
// src/ecs/systems/player/MySystem.h
#pragma once
#include "../../ISystem.h"
namespace ecs {
/// One-line doc: what this system does and when.
class MySystem : public ISystem {
public:
    void update(SystemContext& ctx) override;
};
}

// src/ecs/systems/player/MySystem.cpp
#include "MySystem.h"
#include "../../components/Components.h"
namespace ecs {
void MySystem::update(SystemContext& ctx) {
    auto& registry = ctx.registry;
    auto view = registry.view<LocalPlayerTag, MyComponent>();
    for (auto entity : view) {
        auto& comp = view.get<MyComponent>(entity);
        // ... read ctx.services / ctx.registry ...
    }
}
}
```

### 3.3 系统书写规范

1. **查询最小化**：只用 `view<必需组件>`，不要 `view.all()` 后逐实体 `has()`——会破坏 EnTT 的连续内存遍历。可用 `registry.registry().try_get<T>(e)` 做可选组件探测（如 `PlayerIntentBuildSystem.cpp` 里的健康检查）。
2. **只写自己的职责**：`InputSamplingSystem` 只写 `InputFrameState`；`PlayerIntentBuildSystem` 只写意图组件；下游系统只读意图。职责重叠 = 数据竞争隐患。
3. **服务器/客户端条件**：需要区分时用 `ctx.services.gameClient` 是否存在、`LocalPlayerTag` 是否存在、或 `services.world` 是否绑定（多人客户端 `world` 为空）。
4. **不要跨帧持有 ECS 句柄**（entity 可能被复用），需要跨帧状态放 registry 上下文或组件里。
5. 需要固定顺序依赖时，在 `GameplayPipeline` 注册顺序中体现（见第 4 节），必要时用 `PostSystemHook`。

---

## 4. 注册系统到 Pipeline

### 4.1 注册 API

`GameplayPipeline.h` 提供模板注册：

```cpp
addFixedUpdateSystem<TSystem>(category, postHook);
addTickSystem<TSystem>();
```

- `category`：`FixedUpdateDebugCategory::State / Drop / Particle`，决定 Dashboard 计时归类（默认 State）。
- `postHook`：`PostSystemHook::None` 或 `AfterDamageSystem`（系统跑完后额外回调渲染侧钩子）。

### 4.2 放哪个 Profile

| 系统类型 | 放 Client | 放 Server |
| --- | --- | --- |
| 输入采样/意图构建 | ✔（`buildClientFixedUpdateSystems`） | ✘ |
| AI、物理、战斗、物品 | ✔ | ✔（`buildServerFixedUpdateSystems`） |
| 方块交互（选块/破坏/放置） | ✔ | ✘（服务器通过网络包处理） |
| 粒子/音频/视觉同步 | ✔ | ✘ |
| 世界 tick（流体/红石/随机刻） | ✔（`buildClientTickSystems`） | ✘（服务器侧逻辑在其 tick 内） |

> **为什么服务器也有 ECS？** 单机模式服务器与客户端同进程但职责分离（权威 vs 呈现）；`dedicated_server` 只用 Server profile。服务器的方块交互不通过客户端 ECS 系统，而是 `GameServer` 收到 `ClientBlockAction` 后直接操作 `World`。

### 4.3 修改顺序的检查清单

加新系统时自问：
1. 它读的组件由谁在它之前写？—— 插在生产者之后。
2. 谁读它写的组件？—— 保证它们在后。
3. 它是否需要 tick 时钟？（20 TPS 世界模拟 → `addTickSystem`；60Hz 平滑物理 → `addFixedUpdateSystem`。）
4. 服务器也要跑吗？（要 → 两个 profile 都注册。）

---

## 5. 服务注入：GameplayServices

`src/ecs/GameplayServices.h` 定义了系统可访问的外部服务槽（`OptionalService<T>`）：

| 槽 | 类型 | 说明 |
| --- | --- | --- |
| `world` | `World*` | 权威世界（多人客户端为 **null**） |
| `worldView` | `const IWorldView*` | 只读世界视图（渲染/查询用，始终可用） |
| `gameClient` | `GameClient*` | 网络客户端 |
| `audioEngine` | `AudioEngine*` | 音频 |
| `inputContextManager` | `InputContextManager*` | 输入上下文 |
| `resourceMgr` | `ResourceMgr*` | 资源 |
| `dropSystem` | `DropSystem*` | 掉落物系统 |
| `particleSystem` | `ParticleSystem*` | 粒子系统 |
| `uiRenderer` | `UIRenderer*` | UI |
| `physicsSystem` | `PhysicsSystem*` | 物理 |
| `cameraController` | `CameraController*` | 相机 |

绑定发生在 `GameSession::initECS`（`src/game/session/GameSession.cpp:448`）。

访问方式：

```cpp
if (ctx.services.world) {                 // 存在性判断（多人客户端 world 为 null）
    ctx.services.world->setBlockState(...);
}
auto& worldView = ctx.services.worldView.require();  // 调试模式下空指针会断言
```

**规则**：系统新增服务依赖时，先想清楚——能通过现有服务拿到吗？能通过组件/事件传递吗？服务槽只在真正需要"全局单例"时新增，且要同时在 `GameSession::initECS` 绑定（否则就是 null 指针）。**严禁**系统直接 `#include` 并静态单例访问 `World` 等——那是历史遗留反模式，新代码禁止。

---

## 6. 上下文：SystemContext 与 Registry 上下文

### 6.1 SystemContext

```cpp
struct SystemContext {
    GameplayRegistry& registry;
    GameplayServices& services;
    float dt = 0.0f;              // fixed 步长（tick 系统里是 0）
    uint64_t tickIndex = 0;       // 当前 20 TPS tick 索引
};
```

### 6.2 Registry 上下文（ctx）

EnTT 的 `ctx()` 是"单例组件"机制，适合存放**帧级共享数据**：

| 已存在上下文对象 | 用途 |
| --- | --- |
| `InputFrameState` | 输入帧快照（`InputSamplingSystem` 写，意图系统读） |
| `EventBus<DamageEvent>` 等事件总线 | 见第 7 节 |
| `SmeltingSystem` | 熔炼配方（`reg.ctxSet<SmeltingSystem>()`） |
| `GameTickClock` | tick 节拍器（`GameplayScene` 持有） |

### 6.3 本地玩家定位

`PlayerQuery`（`src/ecs/util/PlayerQuery.h`）封装"找本地玩家实体"的常用查询：

```cpp
ecs::PlayerQuery query(reg);
auto player = query.getLocalPlayer();       // entt::entity
const auto pos = query.getPosition();       // glm::vec3
```

---

## 7. 事件总线：帧内生产者-消费者模式

`src/ecs/util/EventBus.h` 提供**帧作用域**事件总线：生产者在系统里 `push`，消费者（通常是渲染/掉落/粒子系统）在帧末 `drain`。

现有事件类型（`src/ecs/util/`）：

| 事件 | 生产者 | 消费者 |
| --- | --- | --- |
| `DamageEvent` | 伤害系统 | 生命/死亡处理、HUD |
| `DropSpawnRequestEvent` | 破坏方块/物品系统 | `DropSystem` |
| `BlockBreakParticleEvent` | 破坏方块系统 | `ParticleSystem` |
| `FallingBlockSpawnEvent` | `BlockSupportSystem` | 坠落方块实体系统 |
| `PlaySoundEvent` | 任意系统 | 音频同步 |
| `RedstoneEvent` | 红石系统 | 设备动作 |

示例（生产）：

```cpp
auto& bus = ensureDropSpawnEventBus(ctx.registry);
bus.push(DropSpawnRequestEvent{blockId, pos, velocity});
```

示例（消费，帧末 drain）：

```cpp
auto& bus = ensureDamageEventBus(ctx.registry);
for (const auto& evt : bus.drain()) { /* 处理 */ }
```

**新增事件总线三步**：① `src/ecs/util/` 新建 `XxxEventBuffer.h` 定义事件结构 + `ensureXxxEventBus`；② 生产者 `push`；③ 消费者 `drain`。**无需注册表/改 CMake**（头文件即用）。

---

## 8. 实体工厂与实体创建

`src/ecs/entity/EntityFactory.h` 提供静态工厂：

| 方法 | 创建 |
| --- | --- |
| `createServerPlayerProxy` | 服务器玩家代理实体 |
| `createDropItem` / 掉落参数结构 | 掉落物实体（`ItemDropSpawnParams`） |
| `createFallingBlock` | 坠落方块实体（`FallingBlockSpawnParams`） |
| `createMovingBlock` | 活塞移动方块实体（`MovingBlockSpawnParams`） |

`EntityFactory` 同时有基于**定义注册表**的通用创建路径：`EntityDefinitionRegistry`（`src/ecs/entity/EntityDefinitionRegistry.h`）支持按 ID 查实体定义并实例化；`EntityModelRegistry` / `EntitySkinLayout` 负责实体外观（人形皮肤布局）。

新增实体类型时优先复用工厂；需要新实体种类（如自定义生物）→ 加 `EntityDefinitionRegistry` 条目 + 工厂方法。

---

## 9. 本地玩家生命周期

`GameplayScene::initLocalPlayer`（`src/ecs/GameplayScene.cpp:11`）演示了"组装一个玩家实体"的完整清单：Tag + 意图组件 + 物理体 + 相机 + 背包 + 方块交互运行时 + 战斗 + 状态组件。**新玩家能力 = 给本地玩家加组件 + 注册系统**：

1. 定义组件（如 `DivingStateComponent`）。
2. `initLocalPlayer` 里 `emplace` 它（`GameplayScene.cpp`）。
3. `PlayerIntentBuildSystem` 里把输入映射进意图（如果需要输入）。
4. 新建系统消费意图更新状态。
5. 渲染侧需要时在 `GameplayPresentationSnapshot` 中暴露（`src/game/presentation/`）。

---

## 10. 客户端 / 服务器 profile 差异

| 维度 | Client profile | Server profile |
| --- | --- | --- |
| 系统集 | 全部（含输入/粒子/音频/视觉） | 仅权威玩法（AI/物理/战斗/物品） |
| 方块交互 | 客户端本地意图 + 发服务器 | 服务器收到 `ClientBlockAction` 后操作 World |
| 世界访问 | `world` 可能为 null（多人） | `world` 恒有效 |
| 渲染依赖 | 有 | 无（`MECRAFT_NO_TEXTURES`） |
| 时间 | 60Hz fixed + 20Hz tick | 20Hz tick 为主（`GameServer` 内） |

**调试提示**：单机模式两个 profile 并存（`GameplayScene` 是 Client profile，`GameServer` 内部逻辑是权威侧）。服务器权威逻辑在 `src/server/GameServer.cpp` 与 `GameplayPipeline` Server profile 的 tick 系统里，别把"客户端能跑"当成"服务器也对"。

---

## 11. 性能与数据局部性准则

1. **view 是主循环方式**：`registry.view<A, B>()` 顺序遍历连续数组。避免 `view.all()` + 逐实体 `has<T>()`。
2. **ctx 上的全局对象小写热点**：`InputFrameState` 每帧多次访问，保持 POD。
3. **事件总线避免大量事件**：每帧上万事件说明设计有问题，考虑批量/增量同步。
4. **不要在系统里做慢操作**（文件 IO、网络阻塞、世界全量遍历）。世界遍历（如光照、红石）在 tick 系统里按预算分帧处理（`BlockSupportSystem` 的 budget 参数是范例）。
5. `MECRAFT_DEBUG` 下 `runFixedUpdateProfiled` 按系统计时（Dashboard 可见），新增系统自动入表——保持系统粒度合理（一个系统一个职责），别把十几个逻辑塞一个系统。

---

## 12. 可测试性

现有 ECS 测试都是**纯逻辑测试**（不启动游戏）：构造 `GameplayRegistry` + 手动 `emplace` 组件 + 构造 `SystemContext`（services 可部分为空）→ 调 `system.update(ctx)` → 断言组件变化。示例见 `tests/`：

- `creative_flight_system_test.cpp` — 直接测 `CreativeFlightSystem`
- `drop_system_test.cpp`、`death_system_test.cpp`、`mob_ai_hurt_effect_test.cpp`
- `redstone_*_test.cpp`、`fluid_flow_test.cpp`

```cpp
ecs::GameplayRegistry reg;
auto player = reg.create();
reg.emplace<ecs::LocalPlayerTag>(player);
reg.emplace<ecs::TransformComponent>(player);
// ... 组装被测系统的输入 ...
ecs::SystemContext ctx{reg, services, 1.0f / 60.0f, 0};
system.update(ctx);
// 断言
```

services 可以传空结构体（`OptionalService` 空指针安全，调试断言只在调用 `require()`/`->` 时触发）。测试细节见 **07 篇**。

---

## 13. 完整示例：新增一个"冲刺跳跃粒子"系统

目标：玩家冲刺时跳跃，在落地前持续生成冲刺粒子。

**① 组件**（`components/PlayerStateComponents.h`）：

```cpp
struct SprintJumpParticleComponent {
    bool active = false;          // 冲刺跳跃激活中
    float spawnAccumulator = 0.0f; // 粒子生成计时
    glm::vec3 lastPosition{0.0f};
};
```

**② 系统**（`systems/player/SprintJumpParticleSystem.h/.cpp`）：

```cpp
class SprintJumpParticleSystem : public ISystem {
public:
    void update(SystemContext& ctx) override;
};
```

```cpp
void SprintJumpParticleSystem::update(SystemContext& ctx) {
    auto& registry = ctx.registry;
    auto view = registry.view<LocalPlayerTag, SprintJumpParticleComponent,
                              MoveIntentComponent, PhysicsBodyComponent, TransformComponent>();
    for (auto e : view) {
        auto& state = view.get<SprintJumpParticleComponent>(e);
        const auto& intent = view.get<MoveIntentComponent>(e);
        const auto& body = view.get<PhysicsBodyComponent>(e);
        const auto& transform = view.get<TransformComponent>(e);
        const bool airborne = !body.body.onGround;
        state.active = intent.sprint && intent.jump && airborne;
        if (!state.active) {
            continue;
        }
        state.spawnAccumulator += ctx.dt;
        if (state.spawnAccumulator >= 0.05f) {
            state.spawnAccumulator = 0.0f;
            auto& bus = ensureParticleEventBus(registry);   // 复用现有粒子事件
            bus.push(BlockBreakParticleEvent{transform.position, 2});
        }
    }
}
```

**③ 注册**（`GameplayPipeline.cpp`，Client profile 的 item/particle 段附近）：

```cpp
addFixedUpdateSystem<SprintJumpParticleSystem>(FixedUpdateDebugCategory::Particle);
```

**④ 组件挂到玩家**（`GameplayScene::initLocalPlayer`）：

```cpp
m_registry.emplace<SprintJumpParticleComponent>(m_localPlayer);
```

**⑤ 测试**（`tests/sprint_jump_particle_system_test.cpp`，见 07 篇注册方式）。

**⑥ 构建登记**：新 .cpp 加入 CMakeLists 对应 `SRC_ECS` 列表（01 篇第 9 节）。

---

## 14. 系统与组件速查索引

**组件头文件 → 内容**：

| 头文件 | 组件 |
| --- | --- |
| `TagComponents.h` | LocalPlayerTag / DropItemTag / ProjectileTag / FallingBlockTag / MovingBlockTag / ParticleTag / SteveTag / MobTag |
| `TransformComponents.h` | TransformComponent、TransformInterpolationComponent |
| `PhysicsComponents.h` | PhysicsBodyComponent、CharacterControllerComponent |
| `InputComponents.h` | MoveIntentComponent、LookIntentComponent、HotbarIntentComponent |
| `InteractionComponents.h` | BlockTargetComponent、BlockBreakComponent、BlockActionIntentComponent、BlockInteractionRuntimeComponent |
| `PlayerStateComponents.h` | FlightState、FootstepState、LandingState、FallRoll、ViewBob、SprintFov、PlayerMode |
| `CombatComponents.h` | Health、Armor、DamageEvent、HurtEffect |
| `InventoryComponents.h` | InventoryComponent、InventoryDataComponent、HotbarIntent |
| `CameraComponents.h` | CameraStateComponent、CameraInterpolationComponent |
| `ProjectileComponents.h` | ProjectileTag、ProjectileThrower |
| `NetworkComponents.h` | 网络插值组件 |
| `ParticleComponents.h` / `DropComponents.h` / `AudioComponents.h` / `SteveComponents.h` | 对应领域组件 |

**系统目录 → 职责**：

| 目录 | 系统 |
| --- | --- |
| `systems/player/` | InputSampling、PlayerIntentBuild、CharacterPhysics、FallDamage、FallRollEffect、HungerDepletion、PlayerRuntimeUpdate、ViewBob |
| `systems/interaction/` | BlockTarget、BlockBreak、BlockPlace、SoilTilling、BucketUse |
| `systems/combat/` | PlayerMelee、Damage、Death、HurtEffectDecay、Projectile |
| `systems/item/` | ItemSpawn、ItemPhysics、ItemMerge、ItemPickup、ItemLifetime |
| `systems/mob/` | MobAI、MobAnimation |
| `systems/world/` | FluidTick、FarmlandMoisture、RandomTick、BlockSupport、PressurePlate、Redstone、RedstoneDeviceAction、Hopper、FallingBlock*、MovingBlock |
| `systems/network/` | NetworkInterpolation |
| `systems/audio/` | AudioSync、PlayerFootstepAudio |
| `systems/particle/` | ParticleSpawn、ParticleSimulation、ParticleCleanup |
| `systems/steve/` | SteveSync、SteveAnimation |
| `systems/steve/`（Transform） | TransformHierarchy |

---

*下一节：`05-渲染管线开发.md`*
