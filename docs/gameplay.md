Mecraft 游戏逻辑层调查报告

调查目录：D:\project\mecraft\src。以下按七个主题分述，所有路径均为绝对/项目相对路径。

       ---
       1. 世界系统 src/world/

       地形生成（噪声库与生成阶段）

       文件：src/world/gen/TerrainGenerator.{h,cpp}

       - 噪声库：没有用第三方库（不是 FastNoise/libnoise）。完全自研——基于哈希的 value noise（值噪声）+ fBm
       分形叠加。核心是 hash32() 整数哈希 + lattice2D/lattice3D 格点采样 + valueNoise2D/3D
       三线性平滑插值，fbm2D/fbm3D 做多倍频叠加。带 SSE2 / AVX2 SIMD 手写优化（fbm2D2/fbm2D4 一次算 2/4 列）。
       - 生成阶段（均在 generateChunk() 内，按列推进）：
         a. 地表高度与群系（sampleSurfaceAndMoistureScalar / finalizeSurfaceSample）：用 6 张fBm 图——continental（
       大陆，cell=320）、detail、rough、ridge（山脊）、mountainNoise、moisture（湿度，cell=420）。山脊噪声 ridge =
       1 - |2x-1| 制造山脉。
         b. 群系（biome）：枚举 TerrainBiome { Temperate, Arid, Mountain, HighMountain
       }，由大陆度/山脊/湿度阈值判定。决定顶层方块（草/沙）、填充层、覆盖深度。
         c. 基础地层填充：bedrock(y=0) → stone → 填充层 → 顶层；海平面以下填 water。
         d. 洞穴雕刻：buildCaveMaskColumn / shouldCarveCaveFromNoise，3D fBm 洞穴噪声（cell=44，3
       octave），阈值随深度递增（0.77 - depthFactor*0.18），范围 y∈[10, surfaceY-5]。
         e. 矿石：sampleOreBlock，按 y 分层概率——钻石(y≤16,
       0.45%)、金(y≤32)、铁(y≤64)、煤(y≤128)，纯哈希概率，无矿脉聚集。
         f. 植被：sampleVegetationBlock，草/玫瑰（TALL_GRASS / ROSE），按群系+湿度密度。
         g. 树木：sampleTreeCandidate /
       sampleTreeBlockFromCandidate，橡树/白桦（含树干+树叶半径2），跨区块扫描锚点种植。
       - 海平面：默认 63（m_seaLevel）。

       区块管理与加载

       文件：src/world/World.{h,cpp}、src/world/chunk/Chunk.{h,cpp}、SubChunk.{h,cpp}、BitPackedArray.{h,cpp}

       - 区块结构：Chunk = 16×256×16 的 ChunkColumn，沿 Y 轴切成 16 个 SubChunk（16³）。每 SubChunk 独立持有
       mesh；空气子区块不分配存储（nullptr）。方块数据用 调色板 + 位压缩数组（Palette + BitPackedAray）。
       - 管理容器：std::unordered_map<int64_t, std::shared_ptr<Chunk>> m_chunks，key 由 chunkKey(cx,cz) 打包。
       - 异步流式加载：updateStreaming() 多预算控制——submitChunkLoad（提交到 ThreadPool 后台生成）→
       m_completedGenQueue → finalizeChunkLoad。有每帧提交/finalize
       预算和时间预算（kMaxChunkLoadSubmitsPerFrame、kChunkLoadFinalizeTimeBudgetMs）。
       - 票据系统：ChunkTicketManager m_ticketManager（src/server/），供 GameServer 按客户端管理区块。
       - 存档集成：save::SaveManager，markChunkSaveDirty / flushSaves（关联 MEMORY 中的 Save System）。
       - 模拟距离：setSimulationDistance（实体/流体/随机刻半径），渲染距离默认 8。

       昼夜系统

       文件：src/world/DayNightSystem.{h,cpp}

       - 存在。一整天 = 20 分钟 = 1200 秒（SECONDS_PER_DAY）。提供 getTimeOfDay、getSkyIntensity（天空光强乘子
       0~1）、getCelestialAngleRadians（日月旋转角）、getMoonPhaseIndex（月相 0~7）、getElapsedDays。

       天气系统

       文件：src/world/WeatherSystem.{h,cpp}

       - 存在。状态枚举 WeatherType { Clear, Rain, Storm, Snow }。WeatherState（wetness/storm/aerialReduction）+
       WeatherDerived（precipitation、rainStrength、snowStrength、thunderStrength、surfaceWetness、skyWetness、fogW
       etness、cloudWetness、lightningFlash）。
       - 用 指数半衰期平滑目标值（kWetnessRiseHalflife=180s 等，注释指出移植自 DerivativeMain 光照包）。含闪电逻辑
       updateLightning。目前主要通过 debug preset 切换（setDebugWeatherPreset），派生值喂给渲染器 shader uniform。

       光照传播

       文件：src/world/light/ — LightService.{h,cpp}、LightSolver.{h,cpp}、LightCache.{h,cpp}、LightTypes.h

       - 算法：经典 BFS 体积光泛洪（Minecraft 风格）。LightSolver::solve() 用 WorkQueue 做
       BFS，支持光照增加与移除（RemovalNode）。
       - 两种光：LightKind { Sky, Block }（阳光 + 方块光），packed 成单字节高低 4 位（packedLight，sky=高4位
       block=低4位），每光级 0~15。
       - 阳光：buildCurentBasePacked 自顶向下灌注天空光（向下传播不衰减，水平/其他方向衰减 1，透明方块按 opacity
       衰减）。
       - 架构：多线程异步。LightService 在主线程维护脏区块状态/base-light 缓存，把 LightJob（含区块快照 + 4
       邻居快照 + 边界 inbox）丢到 ThreadPool，worker 跑 LightSolver 算出 LightResult（自身 delta + 跨区块边界
       BorderUpdateBatch），主线程 drainCompleted
       合并。支持交互式内联处理（processInteractiveJobsInline，玩家挖/放方块即时光照）。

       流体系统

       文件：src/world/fluid/ — FluidSystem、FluidFlow、FluidState、FluidRegistry

       - 水的流动用 计划方块刻（scheduled block tick）+
       优先队列（std::priority_queue<ScheduledBlockTick>）。FluidState 编码水位高度，物理系统读取 surfaceHeight
       做浮力/流向。

       主要类汇总

       World、TerrainGenerator、Chunk / SubChunk / BitPackedAray / Palette、LightService / LightSolver /
       LightCache、DayNightSystem、WeatherSystem、FluidSystem / FluidFlow /
       FluidState、BlockNeighborUpdateQueue、DropSystem、BlockStateRegistry / Block、ChunkTicketManager。

       ---
       2. ECS src/ecs/

       框架

       EnTT（#include <entt/entt.hpp>）。封装在 ecs::GameplayRegistry（src/ecs/GameplayRegistry.h）里，包了
       entt::registry，提供 create/destroy/emplace/get/viewctx 单例等。系统基类 ISystem（ISystem.h），上下文
       SystemContext（registry + services + dt + tickIndex）。

       Components（src/ecs/components/，按文件归类）

       - Tag（TagComponents.h）：LocalPlayerTag、DropItemTag、ProjectileTag、FallingBlockTag、ParticleTag、SteveTag
       、MobTag。
       - Input：MoveIntentComponent、LookIntentComponent、HotbarIntentComponent、BlockActionIntentComponent。
       - Transform：TransformComponent 等。
       - Physics：PhysicsBodyComponent、CharacterControllerComponent（含 PhysicsTuning +
       站立/潜行眼高）、FlightStateComponent、VelocityComponent、BoundsComponent、GroundedStateComponent。
       - Camera、Interaction（InventoryComponent、InventoryDataComponent、BlockTargetComponent、BlockBreakComponent
       、BlockInteractionRuntimeComponent）。
       - PlayerState：HealthComponent、FoodComponent、ArmorComponent、PlayerModeComponent（creative
       标志）、FootstepStateComponent、LandingStateComponent、FallRollComponent、HurtEffectComponent。
       - Drop、Projectile、Particle、Audio、Steve（玩家人形模型）、Combat、Network。

       Systems（src/ecs/systems/，分目录）

       分 fixedUpdate（60Hz） 与 tick（20 TPS） 两条流水线（GameplayPipeline.cpp），并区分 Client / Server
       profile：
       - player/：InputSamplingSystem、PlayerIntentBuildSystem、CharacterPhysicsSystem、PlayerRuntimeUpdateSystem、
       FallDamageSystem、FallRollEffectSystem、HungerDepletionSystem、ViewBobSystem。
       - interaction/：BlockTargetSystem、BlockBreakSystem、BlockPlaceSystem。
       - combat/：DamageSystem、DeathSystem、HurtEffectDecaySystem、PlayerMeleeSystem、ProjectileSystem。
       - item/：ItemSpawnSystem、ItemPhysicsSystem、ItemMergeSystem、ItemPickupSystem、ItemLifetimeSystem、ItemPlac
       ementResolveSystem。
       - mob/：MobAISystem、MobAnimationSystem。
       - particle/：ParticleSpawnSystem、ParticleSimulationSystem、ParticleCleanupSystem。
       - world/（tick 流水线）：FluidTickSystem、BlockSupportSystem、FallingBlockSpawnSystem、FallingBlockTickSyste
       m、FallingBlockInterpolateSystem。
       - steve/：SteveSyncSystem、SteveAnimationSystem、TransformHierarchySystem。
       - audio/：PlayerFootstepAudioSystem、AudioSyncSystem。
       - network/：NetworkInterpolationSystem。

       Entity 类型（src/ecs/entity/）

       EntityFactory（EntityFactory.{h,cpp}）生产：服务器玩家代理（createServerPlayerProxy）、怪物（createMob/creat
       eZombie，僵尸）、掉落物（createItemDrop）、下落方块（createFallingBlock）、抛射物（createProjectile/createAp
       pleProjectile 苹果）。另有
       EntityDefinitionRegistry（实体定义注册表）、MobModelFactory、SteveModelFactory（人形/怪物模型构建）。

       ---
       3. 物理 src/physics/

       文件：PhysicsSystem.{h,cpp}、PhysicsInfo.h

       - 碰撞检测：AABB + 分轴解析（separated-axis sweep）。moveAndCollideAxis() 对 Y→X→Z 三轴分别推进；每轴按
       kAxisStepLength=0.45 分子步，移动后用 overlapsSolid()（遍历 AABB 覆盖的方块格判
       isSolid）检测重叠，碰撞则回滚到上一有效位置并清零该轴速度。Y 轴向下碰撞置 isGrounded 并记录
       landingImpactSped。
       - 地面支撑探测：hasGroundSupportAt() 用 5 点（中心+四角）向下探测，用于潜行防掉落（protectLedge）。
       - 水体物理：queryWaterFillRatio（ABB 与水体体积重叠比）、queryWaterFlowVector（加权流向，调
       computeFluidFlowVector）、queryEyesInWater（眼睛是否没入水）。浮力/水中重力缩放/水流推力（waterFlowPush）。
       - 射线检测：src/world/WorldRaycast.cpp 的 raycastWorldView() 用 DA（Amanatides-Woo 体素遍历）——计算
       tDelta/tMax 逐格步进；命中后再用 rayIntersectsAab（slab 法）对方块的 BlockSelectionBox
       做精确求交，返回命中面法线（用于放置）。水体被跳过（FluidState::isWater）。
       - PhysicsTuning（可调参数）：gravity=20、jumpSpeed=8.5、moveSpeed=4.5、sprintMultiplier=1.3、airControl=0.35
       、groundFriction=10、terminalVelocity=30、waterGravityScale=0.25、swimSpeed=3.2、swimUpAccel=10、waterFlowPu
       sh=18。

       ---
       4. 玩家 src/player/

       文件：src/player/Inventory.{h,cpp}、src/player/ActionMap.{h,cpp}（玩家控制器逻辑主要在 ECS 的
       systems/player/）

       - 玩家控制器：不是单一 class，而是 ECS 系统链：InputSamplingSystem（采样输入）→
       PlayerIntentBuildSystem（构建 MoveIntent/LookIntent）→ CharacterPhysicsSystem（调 physics::PhysicsSystem）→
       PlayerRuntimeUpdateSystem。
       - 移动模式（由 MoveIntent 标志 + PhysicsSystem 实现，见 applyHorizontalControl/applyVerticalForces）：
         - 行走：moveSpeed。
         - 冲刺：wantsSprint → moveSpeed * sprintMultiplier。
         - 跳跃：wantsJump + grounded → jumpSpeed（支持按住连跳）。
         - 潜行：wantsCrouch → 降速 + 防掉落边缘 + 降低眼高（CharacterControllerComponent.crouchEyeHeight=1.0，站立
       1.62）。
         - 游泳：水中 swimSpeed + 水重力缩放 跳跃键上浮（全没入用
       swimUpAccel，半没入用正弦波浮动），isEyesInWater/isFullySubmerged 状态。
         - 创造模式飞行：FlightStateComponent.isFlying，由 CharacterPhysicsSystem 处理（双击跳跃切换，仅 creative
       模式可飞——isCreativeModeActive 检查 PlayerModeComponent.creative），飞行时无重力、跳跃/潜行键控制上下。
       - 背包系统（Inventory）：9 格快捷栏（HOTBAR_SIZE）+ 4 行×9 列共 36 格（INVENTORY_SIZE，含 3
       行主背包）。ItemStack 槽位数组。支持选中槽、滚轮切换（scrollSlot）、堆叠合并添加（addItem 返回未装下数量）、
       消耗选中物（consumeSelectedOne）、交换槽（swapSlots）、默认装备（initializeDefaultLoadout）。
       - 输入映射（ActionMap）：动作/轴绑定系统，区分 InputContextType { Gameplay, UI, Pause
       }，支持键盘/鼠标/手柄/滚轮，触发类型
       Pressed/Released/Held/DoubleTap（双击用于飞行切换）。可从文件加载（loadFromFile）。

       ---
       5. 物品与合成 src/item/ src/crafting/

       物品 src/item/Item.{h,cpp}

       - ItemID 是独立于 BlockID 的 RuntimeId。ItemStack（itemId + count + durability）。ItemDef（命名空间
       id、图标贴图、maxStack、放置方块 placeBlock、渲染方块 renderBlock、是否工具 isTool、maxDurability）。
       - 注册表 ItemRegistry：基于 NamespacedId（命名空间化 ID，如 minecraft:xx）+ IdRegistry。支持 Mod API
       注册（registerItem）、名称/命名空间查找、Block↔Item 双向映射（fromBlock/toPlaceBlock/toRenderBlock）。
       - 掉落表 BlockDropTable：BlockID → BlockDropEntry（dropItem + minCount/maxCount）。物品 ID 常量由宏
       MECRAFT_FOR_EACH_BUILTIN_PURE_ITEM 从 game/content/BuiltinIds.h 生成。

       合成 src/crafting/CraftingSystem.{h,cpp}

       - 配方 CraftingRecipe：二维 pattern（vector<vector<ItemID>>）+ result + resultCount + width/height。
       - 从 JSON 加载（loadRecipes(configPath)）。
       - 匹配 match(grid, gridWidth, gridHeight)：支持任意尺寸合成格（2×2、3×3）。核心是 trimGrid()
       自动裁剪空白行列取最小包围矩形，再 paternEquals()
       精确比对——即支持配方在格内任意位置摆放（位置无关匹配，shaped recipe）。返回
       CraftingResult（itemId/count/matched）。

       ---
       6. 粒子 src/particle/

       通用粒子 ParticleSystem.{h,cpp}

       - 与 ECS 绑定（bindRegistry，读 ParticleComponent）。emit(blockPos, blockType)
       发射（如挖方块碎屑）。buildVertices 构建朝向相机的 billboard 公告板顶点。
       - 渲染两路：普通 render，以及 renderToSceneResolved——把粒子渲进 SceneComposite，采样体素光（voxelLightTex）+
       GBuffer 深度，使粒子接受统一体积雾。最大 1000 粒子。ECS侧由 ParticleSpawn/Simulation/Cleanup 三系统驱动。

       雨雪渲染 RainRenderer.{h,cpp}

       - 独立于通用粒子系统。纹理化降水：用原版 rain.png/snow.png（64×256
       图集，每滴随机取一列条纹）。在相机周围圆柱体内生成 billboard（雨 4000 滴、雪 2500 滴，半径 24，高 20）。雨速
       18、雪速 6。
       - 含风动画（m_time）、绕相机 wrap 复用、软深度测试（采样 sceneDepthTex）。
       - renderWeatherMask：对应 DerivativeMain 光照包的 gbuffers_weather，把粒子 alpha 写入天气遮罩
       FBO（加法混合），供后处理使用。强度由 WeatherSystem 的 rainStrength/snowStrength 驱动。

       ---
       7. 音频 src/audio/

       - 库：**OpenAL（OpenAL Soft，#include <AL/al.h> <AL/alc.h> <AL/alext.h>）。
       - AudioEngine：初始化设备/上下文，管理 AudioSource 池（acquireSource/release
       复用）、AudioClip缓存、音频目录AudioCatalog`。
       - 3D 空间音频：playClip(name, position, lop, volume, spatial=true) 支持空间化；另有 playSound2D
       非空间化。AudioListener（静态）设置监听者 position/orientation(front,up)/velocity/gain——配合 OpenAL 实现 3D
       定位与多普勒。
       - 解码格式（AudioDecoders.cpp）：WAV（WavPcmDecoder，.wav）+ OGG
       Vorbis（OggVorbisDecoder，.ogg）。AudioDecoderRegistry 按扩展名分派。
       - 设备热插拔：支持 ALC_SOFT_* 扩展，运行时检测设备切换（s_needDeviceReopen 原子标志 + OnDeviceEvent 回调 +
       alcReopenDeviceSOFT）。
       - BGM：BgmSystem.{h,cpp}
       背景音乐系统。变体随机选择（chooseVariantIndex，同一音效多文件随机播放，如脚步声）。ECS 侧由
       PlayerFootstepAudioSystem、AudioSyncSystem 驱动游戏内发声。

       ---
       关键发现小结

       - 这是一个架构相当完整的体素引擎：自研噪声地形 + 16³ SubChunk 调色板压缩 + 多线程异步 BFS 光照 + EnTT ECS +
       C/S 多人架构 + 存档系统。
       - 噪声没有用第三方库，是手写 value-noise/fBm 并带 SSE2/AVX2 SIMD。
       - 物理是分轴 AABB sweep + DA 射线，标准体素做法。
       - 光照、天气系统的部分设计移植自 DerivativeMain 着色器包（与 MEMORY 中的移植目标一致）。
       - 玩家逻辑完全 ECS 化，src/player/ 仅剩 Inventory 与 ActionMap 两个非 ECS 工具类。