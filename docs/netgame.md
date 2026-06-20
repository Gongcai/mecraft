 Mecraft 网络联机与存档系统调查报告

       1. 网络层 src/net/

       网络库：使用 ENet（UDP 可靠/不可靠传输），仅在真实联机时使用；本机单机走进程内队列。

       传输层抽象（src/net/Transport.h）：核心接口 net::ITransportEndpoint，提供 send(Packet) / tryReceive(Packet&)
       / isConnected() / hasActiveRemote()。两种实现：
       - InProcessTransport（src/net/InProcessTransport.h/.cpp）：线程安全的进程内双向队列，createPair()
       返回一对互通端点，单机内嵌服务器用它做零拷贝（Packet::inProcessPayload 用 std::any 直接传
       shared_ptr<Chunk>，不做序列化）。
       - ENetTransport（src/net/ENetTransport.h/.cpp）：真实 UDP。connect() 客户端连接，listen(port, maxClients=32,
       channelCount=4) 服务端监听，takeAcceptedEndpoint() 取出每个新接入 peer 的独立端点，poll() 每帧处理事件。

       信道（PacketChannel，映射到 ENet 4 条 channel）：ReliableControl(0 登录/握手)、ReliableWorld(1
       区块/方块/物品栏)、UnreliableState(2 高频实体快照/玩家输入)、ReliableChat(3 聊天/命令)。

       消息类型（src/net/Protocol.h 的 MessageType，约 30 种）：
       - Client→Server：ClientHello、ClientInput、ClientReady、ClientViewConfig、ClientBlockAction(破坏/放置)、Clie
       ntChatMessage、ClientCommandRequest、ClientRespawnRequest
       - Server→Client：ServerHello、ChunkData、ChunkUnload、BlockUpdateBatch、ServerSnapshot(权威位置/血量)、Entit
       ySpawn/EntityDespawn/EntityImpact/EntitySnapshot、InventorySnapshot、ServerChatMessage/ServerSystemMessage、
       CommandResult、WorldStateSnapshot(时间/天气)、PlayerModeUpdate(生存/创造)
       - 双向：KeepAlive

       编码（src/net/PacketCodec.h）：自定义二进制小端格式，包头 6 字节
       [channel:u8][type:u8][payloadSize:u32]。每种消息有 encodeXxx/decodeXxx。区块走 RLE 压缩（按 subchunk
       分块，blocks u16 + 光照 u8 各自
       RLE），并对历史长度做向后兼容解析。注意：进程内路径不经过此编解码，直接传共享指针。

       2. 服务端 src/server/ 与 dedicated_server.cpp

       是 client/server 架构。server::GameServer（GameServer.h/.cpp）是权威服务端，拥有 World 并跑 tick 循环。

       服务端职责（均在 GameServer::tick(dt)，GameServer.cpp:685）：
       - 区块权威：ChunkTicketManager（src/server/ChunkTicketManager.h/.cpp）按客户端视距管理加载票据；sendNewChunk
       sToClients() 按客户端 sentChunks 集合增量发送，ChunkUnload 卸载。
       - 实体同步：syncEntitiesToClients() / syncPlayersToClients() 发 spawn/despawn/snapshot；服务端用 EnTT
       注册表（m_ecsRegistry / 可选自有 m_ownedGameplayRegistry + GameplayPipeline + PhysicsSystem），分配
       EntityNetId。
       - 交互权威：handleClientBlockAction()（GameServer.cpp:1236）权威处理破坏/放置，再 BlockUpdateBatch
       广播；ServerSnapshot 回传权威位置/速度/血量并 ack 输入序号；updatePlayerLifecycle() 处理死亡/重生/掉落物。
       - 聊天与命令：handleClientCommandRequest / executeServerCommand，回 CommandResult、广播系统消息与
       PlayerModeUpdate。
       - 存档：内部持有 save::SaveManager，每 tick 计时器到点自动保存（见下）。

       独立专用服务器入口存在：dedicated_server.cpp（编译目标 mecraft_server）。main(argc, argv) 参数为
       [port=25565] [seed=1234] [render_distance=8]，初始化 ENet → BlockRegistry::init → ThreadPool(4) →
       ENetTransport::listen → 创建 GameServer::init → 以 20 TPS（50ms/tick） 固定步长循环，每 tick
       takeAcceptedEndpoint() 接客户端并 acceptClient()，支持 Ctrl+C/SIGINT 优雅退出。

       3. 客户端 src/client/

       client::GameClient（GameClient.h/.cpp）：
       - 连接：connect(transport) 接管端点并立即 sendHello()；随后 sendViewConfig(renderDistance)。
       - 发送：sendInput()（位置/速度/朝向/跳跃/潜行/疾跑/动作位/热键槽）、sendBlockAction()、sendChatMessage()、se
       ndCommandRequest()、sendRespawnRequest()。
       - 接收：receiveMessages()（GameClient.cpp:153）switch 分发所有 Server→Client 消息。区块更新写入
       ClientWorld（ClientWorld.h/.cpp），实体走 ClientEntityStore（ClientEntityStore.h/.cpp，处理
       spawn/despawn/snapshot 与插值），方块更新走 handleChunkData / BlockUpdateBatch，并维护权威位置
       m_authPosition、最近快照 m_lastSnapshot（血量/死亡）。
       - 就绪判定：收满 kSpawnChunksThreshold = 25（5x5）区块即 areSpawnChunksReady()。

       4. C/S 架构完整度

       确实是「单机内嵌服务器 + 客户端」统一架构，单机与联机共用同一套 C/S
       代码（GameSession::init，GameSession.cpp:187）：
       - 单机：InProcessTransport::createPair() → server.acceptClient(serverTransport,1) +
       client.connect(clientTransport)，物理系统直接引用 server->world()。
       - 联机：ENetTransport::connect(serverAddress, serverPort)，客户端有握手等待重试逻辑（最多 4000
       次轮询、定期重发 hello/viewConfig），物理系统引用 client->clientWorld()。m_isMultiplayer 由
       config.isMultiplayer() 决定。

       已支持的同步：玩家移动（输入→权威快照+和解）、方块破坏/放置（权威+批量广播+光照增量
       patch）、掉落物（EntityKind::Drop）、生物实体（EntityKind::Mob，如僵尸 spawnZombieEntity）、抛射物（Projecti
       le）、物品栏快照、聊天/命令、世界状态（时间/天气）、生存/创造模式切换、死亡/重生。

       5. 存档系统 src/save/

       由 服务端的 save::SaveManager 编排（SaveManager.h/.cpp）。保存目录结构由
       SavePaths（SavePaths.h/.cpp）统一计算：<root>/<worldName>/ 下含
       level.json、chunks/、players/、entities/、block_entities/、thumb.png。

       保存的数据与格式：
       - 区块：自定义二进制 MCHK 格式（SaveFormat.h：24 字节头 magic MCHK+版本+CRC32，payload 为调色板编码
       palettized + BitPackedArray，含 block 层与 fluid 层）。ChunkSerializer（ChunkSerializer.h/.cpp）负责与内存
       Chunk 互转，RuntimeId↔NamespacedId。RegionFile（RegionFile.h/.cpp）区域文件缓存。文件名
       c.<cx>.<cz>.mchk，原子写（.tmp→重命名，保留 .bak）。
       - 世界元数据 level.json（JSON / nlohmann）：种子、显示名、出生点、时间、天数、天气、游戏模式、创建/最后保存
       UTC 时间戳、缩略图路径。
       - 玩家状态：PlayerSerializer（PlayerSerializer.h/.cpp，JSON）保存位置/速度/朝向/血量/护甲/饥饿/饱食/飞行/物
       品栏（仅非空槽，存 NamespacedId+数量+耐久）；本地玩家 players/local.json，联机按 clientId。
       - 持久实体（mob/掉落物，PersistentEntityData）和方块实体（箱子物品栏，BlockEntityData）：JSON，原子写到
       entities/ 和 block_entities/。

       自动保存：有。GameServer::tick 中 AUTOSAVE_INTERVAL_SECONDS = 300.0f（5 分钟）到点 flush
       区块、持久实体、方块实体、level.json（GameServer.cpp:692-702）。区块异步保存（submitSaveChunk 立即快照序列化
       + 线程池写盘），关闭时 flushPendingSaves() 阻塞等待。

       单元测试：tests/chunk_save_serializer_test.cpp（CLAUDE.md 指定的测试目标
       chunk_save_serializer_test），覆盖空/单/多 subchunk 往返、负坐标、混合方块、fluid 层、payload
       大小、SavePaths 名称清洗/路径/建目录、SaveManager 的 level meta / 区块往返 / 不存在区块 / 持久实体往返 /
       方块实体往返、PlayerSerializer 往返/文件往返/不存在文件。相关测试还有
       client_world_test.cpp、client_entity_store_test.cpp、game_server_test.cpp、in_process_transport_test.cpp、ch
       unk_ticket_manager_test.cpp、network_interpolation_system_test.cpp、drop_system_test.cpp。

       6. 应用结构 src/app/ 与 src/game/

       入口：main.cpp 极简——GameManager app; app.init(1280,720,"Mecraft"); app.run(); app.shutdown();

       应用层状态机 src/app/：GameManager（GameManager.h/.cpp）拥有窗口、输入、资源、音频、UI、线程池，并驱动
       AppStateMachine（AppStateMachine.h/.cpp，支持 push/pop/change，带延迟操作队列避免 dispatch
       中改栈）。应用状态（src/app/states/）：MainMenuAppState、LoadingAppState、GameplayAppState（各有对应
       .cpp），即主菜单 → 加载 → 游戏中的顶层流转。

       游戏层 src/game/：
       - Game（Game.h/.cpp）：游戏会话外壳，含分阶段加载状态机
       LoadPhase（NotStarted→Session→RenderRuntime→Ecs→InitialChunks→Complete/Failed），持有
       GameSession、GameplayRenderRuntime、GameFrameOrchestrator、GameplayHudPresenter、AudioListenerSyncSystem。
       - GameSession（session/GameSession.h/.cpp）：聚合一局游戏的核心对象——C/S 的
       server/client、physics、ECS（GameplayScene）、掉落、合成、粒子、雨、相机、表现层构建器，以及游戏内状态机
       GameStateMachine。
       - 游戏内状态机 src/game/states/：GameStateMachine（.h/.cpp）+ IGameState，状态包括
       GameplayState（游戏中）、UIState、CommandState，库存相关
       InventoryState/CreativeInventoryState/ChestInventoryState（src/game/inventory/），模式
       CreativeModeState（暂停/库存/命令等叠加态）。
       - 帧编排：GameFrameOrchestrator（orchestrator/）组织帧；fixedUpdate(fixedStep, accumulator) 固定步进 +
       updateFrame / renderFrame 可变帧；渲染由 GameplayRenderRuntime（render/）+
       GameplayPresentationBuilder（presentation/）快照式构建。

       关键文件路径汇总：D:\project\mecraft\main.cpp、D:\project\mecraft\dedicated_server.cpp、src\net\{Protocol.h,
       Transport.h, PacketCodec.h, ENetTransport.*, InProcessTransport.*}、src\server\{GameServer.*,
       ChunkTicketManager.*}、src\client\{GameClient.*, ClientWorld.*,
       ClientEntityStore.*}、src\save\{SaveManager.*, ChunkSerializer.*, SaveFormat.h, PlayerSerializer.*,
       RegionFile.*, SavePaths.*}、src\app\{GameManager.*, states\*}、src\game\{Game.*, session\GameSession.*,
       states\*, orchestrator\*}、tests\chunk_save_serializer_test.cpp。