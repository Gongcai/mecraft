# Mecraft 线程池设计 — 区块网格生成多线程升级

## 1. 目标与范围

将区块网格生成从单工作线程升级为多线程池，提升网格构建吞吐量，缩短区块加载时的画面空洞时间。

- 目标：网格构建吞吐量提升至 N 倍（N = 工作线程数），消除区块加载积压。
- 目标：优先级调度——近处区块的网格生成始终优先于远处区块。
- 目标：线程池通用化，后续可零改动接入光照计算、世界生成等任务类别。
- 范围：**仅改造网格生成**，世界生成和光照计算暂不迁移，但设计预留扩展位。

## 2. 现状分析

### 2.1 当前架构

```
主线程                                单工作线程
┌───────────────────┐                ┌──────────────────┐
│ submitMeshingJobs │  ──submit()──> │  workerLoop()    │
│  无序遍历 chunks   │  m_pending     │  串行取 job      │
│  捕获 snapshot    │                │  buildMeshData() │
│  budget=8/帧      │                │                  │
│                   │ <─tryPop────── │  m_completed     │
│ drainMeshingRes.  │                │                  │
│  GPU upload       │                └──────────────────┘
└───────────────────┘
```

关键文件：
- `src/renderer/ChunkMeshingService.h/cpp` — 单工作线程服务，1 个 `std::thread`
- `src/renderer/ChunkMesher.h/cpp` — `captureSnapshot()` + `buildMeshData()`，纯静态方法
- `src/renderer/Renderer.h/cpp` — 调度层：`submitMeshingJobs()` / `drainMeshingResults()`

### 2.2 已有的线程安全机制

| 机制 | 位置 | 作用 |
|------|------|------|
| 快照隔离 | `ChunkMesher::captureSnapshot()` | 主线程拷贝完整数据，工作线程只读快照，无数据竞争 |
| revision 版本号 | `Chunk::m_meshRevision` | 结果回主线程后检查，过期则丢弃 |
| inFlight 集合 | `Renderer::m_meshingInFlight` | 防止同一 chunk 重复提交 |
| GPU 上传在主线程 | `Renderer::drainMeshingResults()` | OpenGL 上下文要求 |

**结论：快照隔离 + revision 丢弃机制天然支持多线程并行，无需对 Chunk/World 加锁。**

### 2.3 当前瓶颈

1. **单线程串行构建**：8 个 job 排队，每个 ~2ms → 一批 16ms，吞吐量 = 1 job / 2ms。
2. **无优先级**：`unordered_map` 遍历顺序随机，远处区块可能先于近处处理。
3. **budget 偏低**：8/帧 + 单线程消化，积压场景下恢复慢。

## 3. 新架构

### 3.1 整体结构

```
主线程                                    ThreadPool (N 个 worker)
┌───────────────────────────┐            ┌─────────────────────────┐
│ submitMeshingJobs()        │            │  worker 1: loop()       │
│  1. 收集 dirty chunks     │            │  worker 2: loop()       │
│  2. 按距玩家距离排序       │  submit()  │  worker 3: loop()       │
│  3. 捕获 snapshot         │ ──────────>│  ...                    │
│  4. 提交到线程池           │  优先级队列 │  worker N: loop()       │
│                           │            │                         │
│ drainMeshingResults()     │ <──────────│  完成后推入 completionQ │
│  取结果 → revision 检查   │ tryPop()   │                         │
│  GPU upload → setMesh     │            └─────────────────────────┘
└───────────────────────────┘
```

### 3.2 三层分离

| 层 | 职责 | 对应类 |
|----|------|--------|
| 调度层 | 决定提交谁、提交多少、按什么顺序 | `Renderer`（现有，改造提交逻辑） |
| 执行层 | 纯粹的任务执行器，不关心任务语义 | `ThreadPool`（新增） |
| 结果层 | 取回结果、版本检查、GPU 上传 | `Renderer`（现有，改造 drain 逻辑） |

## 4. ThreadPool 设计

### 4.1 类接口

```cpp
// 支持泛型任务的线程池
class ThreadPool {
public:
    explicit ThreadPool(int numThreads);
    ~ThreadPool();

    void start();
    void shutdown();

    // 提交任务，priority 越小越优先
    void submit(std::function<void()> task, int priority = 0);

    // 线程数查询
    int numWorkers() const;

    // 统计（debug）
    int pendingCount() const;
    int activeCount() const;

private:
    void workerLoop();

    std::vector<std::thread> m_workers;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;

    // 优先级队列：priority 小的先出
    struct PrioritizedTask {
        int priority;
        uint64_t sequence;  // 同优先级时 FIFO
        std::function<void()> func;
        bool operator>(const PrioritizedTask& other) const;
    };
    std::priority_queue<PrioritizedTask, std::vector<PrioritizedTask>,
                        std::greater<PrioritizedTask>> m_pending;

    std::atomic<int> m_activeCount{0};  // 当前正在执行的线程数
    uint64_t m_sequenceCounter = 0;
    bool m_running = false;
    bool m_stopping = false;
};
```

### 4.2 关键设计决策

**Q: 为什么用 `std::function<void()>` 而不是模板/variant？**

A: 线程池是通用执行器，不应该感知任务类型。任务的输入/输出由调用方（调度层）管理。meshing 任务通过闭包捕获 snapshot，结果通过闭包推入完成队列。

**Q: 为什么用优先级队列而不是多队列？**

A: 单优先级队列让所有任务竞争同一个排序空间。近处 meshing 优先级 < 远处 meshing 优先级，保证近处先执行。后续接入 lighting/generation 时，统一在同一优先级空间排序，避免"meshing 队列空了但 lighting 队列积压，线程空闲"的问题。

**Q: sequence 计数器的作用？**

A: 同优先级的任务按提交顺序执行（FIFO），避免饥饿。

**Q: 线程数如何确定？**

A: `max(2, hardware_concurrency - 1)`，上限 8。留一个核心给主线程和 OS。可后续暴露为配置项。

### 4.3 工作循环伪代码

```
workerLoop():
    while true:
        lock:
            wait(m_cv) until (stopping || !m_pending.empty())
            if stopping && m_pending.empty(): return
            task = m_pending.top(); m_pending.pop()
        m_activeCount++
        task.func()               // 执行任务
        m_activeCount--
```

## 5. ChunkMeshingService 改造

### 5.1 改造策略：从"单线程服务"变为"线程池适配器"

`ChunkMeshingService` 不再自己管理线程，而是持有 `ThreadPool` 的引用，负责：
- 构建 meshing 任务的闭包
- 维护完成队列（`m_completed`）
- 提供 `tryPopCompleted()` 给 Renderer drain

### 5.2 改造后接口

```cpp
class ChunkMeshingService {
public:
    // 注入外部线程池
    void start(ThreadPool* pool);
    void shutdown();

    void submit(ChunkMeshingJob job, int priority);
    bool tryPopCompleted(ChunkMeshingResult& out);

    // 统计
    int inFlightCount() const;

private:
    ThreadPool* m_pool = nullptr;

    // 完成队列仍由本类管理（与 ThreadPool 解耦）
    mutable std::mutex m_completedMutex;
    std::queue<ChunkMeshingResult> m_completed;

    // 在飞任务计数（用于 debug / 流控）
    std::atomic<int> m_inFlight{0};
};
```

### 5.3 submit 伪代码

```
submit(job, priority):
    m_inFlight++
    m_pool->submit([job, this]() {
        // 在工作线程执行
        ChunkMeshingResult result;
        result.chunkKey = job.chunkKey;
        result.revision = job.revision;
        if (job.snapshot):
            result.meshData = ChunkMesher::buildMeshData(*job.snapshot);

        // 推入完成队列
        {
            lock_guard lock(m_completedMutex);
            m_completed.push(std::move(result));
        }
        m_inFlight--;
    }, priority);
```

## 6. Renderer 调度层改造

### 6.1 submitMeshingJobs — 加入优先级排序

**改造前**：遍历 `unordered_map`，遇 dirty 就提交，顺序随机。
**改造后**：先收集所有候选，按距玩家距离排序后提交。

```
submitMeshingJobs(world):
    candidates = []
    for (key, chunk) in activeChunks:
        if chunk.isDirty() && key not in m_meshingInFlight:
            dx = chunk.chunkX * 16 + 8 - cameraPos.x
            dz = chunk.chunkZ * 16 + 8 - cameraPos.z
            distSq = dx*dx + dz*dz
            candidates.append({key, distSq, chunk})

    sort candidates by distSq ascending  // 近处优先

    submitted = 0
    for c in candidates:
        if submitted >= m_meshingSubmitBudget: break

        job.chunkKey = c.key
        job.revision = c.chunk.getMeshRevision()
        job.snapshot = ChunkMesher::captureSnapshot(c.chunk, &world)

        // priority = 距离的整数部分，近处优先
        priority = int(sqrt(c.distSq))
        m_meshingService.submit(job, priority)

        m_meshingInFlight.insert(c.key)
        submitted++
```

### 6.2 提交预算调整

单线程时 budget=8 合理（串行消化）。多线程后吞吐量提升，budget 应同步提高：

| 线程数 | 建议 budget |
|--------|-----------|
| 1（当前） | 8 |
| 2 | 12 |
| 4 | 16 |
| 8 | 24 |

公式：`budget = 8 + (numWorkers - 1) * 2`，可后续根据实测调整。

### 6.3 drainMeshingResults — 无需大改

现有逻辑已是正确的：
1. `tryPopCompleted()` 循环取结果
2. revision 不匹配则丢弃
3. GPU upload + setMesh

唯一调整：`tryPopCompleted()` 内部从 `m_completedMutex` 保护的队列取（而非全局 `m_mutex`），与 ThreadPool 完全解耦。

### 6.4 Renderer 新增成员

```cpp
// Renderer.h 新增
ThreadPool m_threadPool;  // 替代旧的单线程，Renderer 拥有生命周期

// 移除
// ChunkMeshingService m_meshingService;  → 改为引用 ThreadPool
ChunkMeshingService m_meshingService;     // 内部不再持有线程，引用 m_threadPool
```

### 6.5 生命周期

```cpp
// Renderer::init()
m_threadPool.start();              // 启动线程池
m_meshingService.start(&m_threadPool);  // 注入线程池

// Renderer::shutdown()
m_meshingService.shutdown();       // 等待所有在飞 meshing 任务完成
m_threadPool.shutdown();           // join 所有工作线程
```

## 7. 文件改动清单

| 文件 | 改动类型 | 改动内容 |
|------|---------|---------|
| `src/thread/ThreadPool.h` | **新增** | 通用线程池，优先级队列，N 个 worker |
| `src/thread/ThreadPool.cpp` | **新增** | start / shutdown / submit / workerLoop 实现 |
| `src/renderer/ChunkMeshingService.h` | **修改** | 移除 `std::thread m_worker`，新增 `ThreadPool*` 引用，`submit` 加 priority 参数 |
| `src/renderer/ChunkMeshingService.cpp` | **修改** | submit 通过 ThreadPool 调度，workerLoop 删除，完成队列独立 mutex |
| `src/renderer/Renderer.h` | **修改** | 新增 `ThreadPool m_threadPool`，调整 budget 逻辑 |
| `src/renderer/Renderer.cpp` | **修改** | `submitMeshingJobs` 加优先级排序，生命周期调整 |
| `CMakeLists.txt` | **修改** | 添加新源文件 |

## 8. 扩展性设计（预留，本期不实现）

### 8.1 后续接入光照计算

光照计算的接入方式：

1. 定义 `LightingJob` / `LightingResult` 结构
2. 创建 `LightingService` 类（类似 `ChunkMeshingService`），持有 ThreadPool 引用
3. 主线程 `captureLightSnapshot()`，构建闭包，通过 `ThreadPool::submit()` 提交
4. 完成队列独立，主线程 drain

无需修改 ThreadPool 任何代码。

### 8.2 后续接入世界生成

同理，定义 `GenerationJob` / `GenerationResult`，创建 `GenerationService`。

### 8.3 统一优先级空间

当多类任务共存时，优先级公式：

```
finalPriority = chunkDistance * 100 + categoryBase
```

| Category | categoryBase | 效果 |
|----------|-------------|------|
| Meshing | 0 | 同距离内最优先 |
| Lighting | 50 | 中等 |
| Generation | 100 | 同距离内最低优先 |

近处 meshing（dist=1, priority=100）仍然优先于远处 generation（dist=5, priority=600）。

## 9. 测试策略

### 9.1 正确性验证

| 测试项 | 方法 |
|--------|------|
| 无数据竞争 | ThreadSanitizer / Helgrind 运行 |
| 无重复网格化 | 验证 `m_meshingInFlight` 去重逻辑 |
| 过期结果丢弃 | 修改方块后确认旧 revision 的结果被跳过 |
| 邻居边界正确 | 站在 chunk 边界放置/破坏方块，观察接缝处渲染 |

### 9.2 性能验证

| 场景 | 指标 |
|------|------|
| 静止状态 | FPS 不低于改造前（线程池空转开销应可忽略） |
| 区块加载（飞行） | 积压深度 / 恢复时间对比单线程 |
| 大量方块修改 | 每帧完成 mesh 数量对比 |

### 9.3 回退方案

`ThreadPool` 构造函数 `numThreads=1` 时等效于当前单线程行为。如遇问题可立即回退，无需代码回滚。

## 10. 实施步骤

1. **新建 `ThreadPool`**：实现优先级队列线程池，独立于任何业务逻辑。
2. **改造 `ChunkMeshingService`**：移除自有线程，改为引用 ThreadPool，submit 加 priority。
3. **改造 `Renderer`**：添加 ThreadPool 成员，`submitMeshingJobs` 加排序，调整 budget 和生命周期。
4. **测试**：先 `numThreads=1` 验证正确性，再 `numThreads=N` 验证性能。
5. **调优**：根据实测调整 budget、线程数、优先级策略。
