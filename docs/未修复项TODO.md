# 未修复项 TODO

记录已确认但尚未修复的问题与已排期的优化项,便于后续处理。
(2026-07-24 核实修订:原第 1 项经核实不成立,移入"已核实排除";原第 2 项
的对照标准与修复建议有误,已改写。)

## 一、待修复问题

### 1. 低于 15 级的天光垂直下落不衰减(悬挑/侧向开口下方偏亮)

- **现象**:`LightSolver.cpp` 的 `propagateLevelFromOpacity` 中,天光向下分支
  `attenuation = opacity`,且对**任意等级**的天光生效。空气(opacity=0)中
  向下传播完全不衰减。
- **与原版的真实差异**:原版 MC 只有**等级恰为 15** 的天光垂直下落免衰减
  (露天竖井到底部都是 15——当前实现这部分是正确的,原 TODO 认为
  "天光每格 -1"适用于露天空气柱是误解);低于 15 的天光向下每格仍衰减
  `max(1, opacity)`。当前实现把免衰减扩大到了所有等级。
- **影响**:光从洞口水平进入、衰减到如 12 后,再沿竖直通道下落会一直保持
  12 直到底部——悬挑地形/侧向开口下方的深洞比原版亮。纯露天场景无差异。
- **修复方案**:向下天光衰减改为
  `attenuation = (level == 15 && opacity == 0) ? 0 : max(1, opacity)`。
  同一规则存在多处一致实现,必须同步修改,否则基础光缓存与 BFS 结果不一致:
  - `LightSolver.cpp`:`propagateLevelFromOpacity`(BFS)与
    `buildCurrentBasePacked`(天光列扫描);
  - `LightCache.cpp`:`buildBaseLightFromChunk` / `recomputeSkyColumn`;
  - `Chunk.cpp`:`seedInitialLightMap` 的天光列扫描。
  列扫描的等价写法:`skyLevel == 15 && opacity == 0` 时保持 15,否则
  `skyLevel = max(0, skyLevel - max(1, opacity))`(opacity >= 15 直接归 0)。
- **备注**:当前行为也可视为有意简化(规则自洽,地下更亮更友好),但需要
  明确决策;若决定保留现状,将本项移入"已核实排除"。

## 二、优化项

### 1. 光照快照按 section 整段捕获(非紧急;高度提升前必做)

- **现状**:每个光照 job 在主线程逐格调用 131,072 次——
  `captureBlockSnapshot` + `capturePackedLightSnapshot` 各 65,536 次,单次
  调用链为:边界检查 → section 定位 → BitPackedArray 位解包 → palette 查表 →
  BlockStateRegistry 转换。交互路径(`World::flushInteractiveLighting` →
  `processInteractiveJobsInline`)每次方块编辑同步付出该成本,直接计入当帧。
  典型地形约半数 section 为全空气,同样按逐格全价捕获。
- **实测量级**(2026-07-24,RelWithDebInfo,16 区块视距,dashboard
  `Light Capture` 行):单 job 平均 0.15–0.3 ms;加载潮下每个提交周期合计
  0.5–1.3 ms。注意统计在每次 `submitJobs` 时重置,时间加速(如 10×)时每
  渲染帧会跑 3–4 个 server tick,真实每帧成本约为读数的 3–4 倍;正常 1×
  速度下每帧约一个 tick,读数即帧成本。单次方块编辑约 0.2 ms,交互延迟
  可忽略(dashboard 为间隔采样,单帧瞬时值显示不出来,不代表成本为零)。
- **结论**:当前高度(256)下属于"值得做但不紧急"——加载潮帧成本约
  1 ms,占 16.6 ms 帧预算的 4–8%。定位为世界高度提升(256 → 512)的
  前置项(成本随高度线性翻倍),或加载潮出现帧尖刺时提前做。
- **方案**(硬性要求:结果与逐格捕获比特一致):
  - 光照:`SubChunk::m_lightMap` 是公开平坦数组(4 KB/段),16 段 memcpy;
  - 方块:每段只做一次 palette → BlockID 转换(通常几个~几十个词条),
    再展开 4096 个索引,取代 4096 次 registry 查询;
  - 空段(nullptr 或 `SubChunkType::Air`):方块 fill AIR,光照按高度图
    填隐式天光(复制 `Chunk::getImplicitSunlight` 的逻辑)。
- **预期**:捕获成本降约一个数量级(至 ~0.02 ms/job 量级)。

### 2. job 缓冲池复用(降低分配 churn)

- 每个 job 堆分配约 256 KB(blockSnapshot 128 KB + packedLightSnapshot
  64 KB + baseLightPacked 64 KB),完成即释放。可用对象池复用,节省 malloc
  与内存带宽。注意:快照数组本身不能缩小——平坦数组是求解器的输入格式,
  空气格数据是天光传播的必要输入,"跳过空段不进快照"不成立。

### 3. job 记录 y 脏区间限定扫描(后置)

- 求解器侧 `computeDirtyMask`、seed 扫描等仍为全列遍历。job 携带脏 y 区间
  可缩小范围。改动面大,且与"整段捕获"收益部分重叠,放最后,做前先测。

## 三、已核实排除的项

- **跨区块垂直光照传播缺失**(原第 1 项,2026-07-24 排除):不成立。
  Chunk 本身就是全高度(16×256×16)柱状体,世界网格是纯二维
  `chunkKey(cx, cz)`,不存在垂直相邻 chunk;求解器 BFS 含 ±Y 六向传播、
  自由穿越 subchunk 分界(subchunk 只是网格重建/脏标记分段,不是光照单元)。
  区块顶部 y=255 即世界顶,天光从 15 起算定义正确。仅当将来引入 cubic
  chunks / 世界高度超出单 Chunk 时才需要垂直边界交换。
- **快照捕获移出 m_stateMutex**:无收益。该锁的全部使用方
  (submitJobs / drainCompleted / onBlockChanged / chunk 装卸)均在主线程,
  worker 线程只使用 m_completedMutex,锁本身无竞争。
- **光照计算单元 subchunk 化**:高度 ≤ 512 时不做。天光本质是列问题
  (高度图列扫描);16³ 单元需要 6 向边界消息与跨段天空可见性协调,垂直
  传播从单 job 内 BFS 直达退化为跨段消息接力,交互延迟反而变差;渲染与
  存储侧本就已是 subchunk 粒度。列内优化(第二节)可拿到大部分收益。
- **远景 LOD 采用连续高度场**:此前实践结论:接缝难处理、与体素世界观感
  割裂、难以上色成方块质感,弃用。远景 LOD 采用体素列降采样方案(第四节
  第 7 项),与 Distant Horizons 的实际结构一致(多段 column 体素近似,
  非高度场曲面)。

## 四、渲染优化(大视距帧率)

### 0. 现状事实(2026-07-24 核查)

- **MDI**:每 pass 一次 `vkCmdDrawIndirect`,但每个可见 subchunk × 材质
  是一条独立间接记录(`WorldRenderBuffer.cpp:874-899`),`baseInstance`
  携带 metadataIndex,VS 经 `gl_BaseInstanceARB` 从 SSBO 取 subchunk 原点。
  16 区块主视图约 3000-4000 条记录,O(r²) 增长。
- **非索引化**:`DrawArraysIndirectCommand`,每面 6 顶点 × 16 B
  (PackedBlockVertex),顶点零复用。
- **CPU 剔除**:region → column → subchunk 三级视锥+距离,每帧全量遍历,
  主视图与阴影 `collectShadowChunks` 各一遍(`TerrainRenderer.cpp`);
  32 区块约 13 万次 AABB 测试/帧。**无遮挡剔除**(地表行走时视锥内全部
  洞穴网格照常提交,靠 early-Z 兜底,VS 与前端成本照付)。
- **阴影**:4 级联 CPU binning,几何重复绘制 2-3 遍。
- **无 Render Graph**:`RhiRenderGraph.h` 仅有类型占位(RgTextureHandle
  等),无图实现、无使用者;pass 手工排序 + 手工 barrier
  (`DeferredPipeline.cpp`,~1300 行,每帧 7 处 `rhiDevice.submit`)。
- **全单线程**:RHI 强制单 device 线程(`VkRhiDevice::submit/presentFrame`
  校验 `m_deviceThread`),app/game 无独立渲染线程——世界更新、剔除、
  命令录制、7 次 vkQueueSubmit、present 全部串行于主线程。帧 in-flight
  为 2(`VkRhiInternal.h:149`),同步结构本身健康(timeline semaphore,
  提交路径无 CPU 等待;`vkQueueWaitIdle` 仅存在于纹理创建路径)。
- **GPU 占用率 50-60% 的结构解释**:CPU 帧时长 > GPU 帧时长,GPU 饥饿。
  确认方法:Dashboard 对比 CPU 帧分解(Frame Profiler)与 GPU
  timestamps 总和;若 GPU pass 之间存在大间隙则另计 barrier 串行因素。

### 1. 基线测量(先做,半天)

- Dashboard 加带宽换算行:Σ(各 pass drawn vertices) × 16 B × fps → GB/s
  (RenderWork 统计已有顶点数,只差换算显示);记录 16/24/32 区块下的
  顶点数、记录数、CPU 帧分解、GPU pass 时间基线。

### 2. 索引化 + 共享 quad 索引缓冲(高优,单项收益最大)

- `DrawIndexedIndirect` + 静态共享索引缓冲(`0,1,2,2,1,3` 重复模式,
  `vertexOffset = firstVertex`),mesher 每面输出 4 顶点。
- 顶点数据 96 B → 64 B/面,VS 调用 -33%;索引缓冲全场共享、常驻缓存。

### 3. 顶点格式瘦身(per-quad 拆分)

- 16 B/顶点中 UV 为 16.16 定点(过宽),tint/anim 等属性四顶点重复。
  把 per-quad 属性挪入按 `gl_VertexIndex / 4` 索引的 SSBO(quad-pulling),
  可再省 30-50% 顶点拉取带宽。与第 2 项配合做。

### 4. GPU-driven 剔除 + HiZ 遮挡(32+ 区块的正解)

- 全部存活 subchunk 的 {AABB, ranges} 常驻 GPU,compute 做视锥 + HiZ
  两相遮挡剔除,原子写出紧凑 indirect buffer,`vkCmdDrawIndirectCount`。
- 一并解决:CPU 每帧 13 万次 AABB 测试归零、前端只见幸存记录、阴影
  4 级联 binning 上 GPU、洞穴/山体背面几何整段剔除(当前完全未吃到的
  30-50%)。

### 5. CPU/GPU 并行度改造(GPU 占用率的根治)

- 短期:剔除遍历并行化(ThreadPool 按 region 切分);7 次 submit 合并为
  1-2 次(每次 submit 有 registry 锁 + 资源引用解析开销);复查手工
  barrier 是否过保守造成 pass 间 GPU 气泡。
- 中期:**渲染线程分离**(快照式:游戏线程产出可见集/uniform 快照,
  渲染线程独占 RHI 录制+提交)——GPU 饥饿的根治;RHI 单 device 线程
  约束不变,整体搬到渲染线程即可。
- 远期:把 `RhiRenderGraph.h` 落地为真 Render Graph(自动 barrier、
  资源别名、pass 重排),收益是可维护性与 async compute(FSR/SSAO/GI
  走 compute 队列与光栅重叠,进一步抬 GPU 占用)。**已定为
  `通用模型渲染方案.md` 的前置项(Phase 0)**——通用模型的新 pass
  直接注册进图,不再手工扩 barrier/pass 排序。

### 6. 阴影几何减负

- 远级联降低更新频率(静态地形每 N 帧重录/仅在 chunk 变化时);
  远级联直接采用第 7 项的 LOD 网格。

### 7. 远距离 LOD:体素列降采样(非高度场)

- **方案定调**(2026-07-24):不用连续高度场(见"已核实排除");采用
  DH 式体素近似,且**运行时生成**(DH 的 Distant Generation 本就是
  运行时后台线程跑生成器,磁盘只是缓存)。
- **数据结构**:多段 column——每列存若干 {起始高度, 结束高度, 代表
  方块/颜色} 段,保留悬崖、悬空岩层、大型洞口等非单值垂直结构;
  水平按 2×2 / 4×4 / 8×8 四叉树逐级合并,段做合并/主导色处理。
  极远处退化为粗粒度色块体素,与方块世界观感连续。
- **数据来源**:已访问区域后台读 MCHK 存档降采样;未访问区域走生成器
  快速路径——列内按 LOD 垂直步长采样密度/地表(跳过装饰、结构细节、
  光照 BFS),比整 chunk 生成便宜 2-3 个数量级,单 tile 亚毫秒级,
  单个后台线程可支撑飞行速度的环带增量更新。
- **网格化**:对多段 column 做相邻列可见面剔除,输出方块质感(每段
  顶/底/侧面 + 代表色);环间与真实区块边界用裙边(skirt)+ 雾过渡。
- **渲染**:独立 pass,几条 draw;光照用天光近似(顶面满、按朝向衰减)
  + 大气/雾融合,不参与光照 BFS;远级联阴影可直接采样 LOD 网格。
- **渐进填充**:首次进入世界数秒内由近及远填环 + 雾遮罩;LOD tile
  可选落盘缓存加速二次进入。

### 建议顺序

① 基线测量 → ② 索引化 → ③ GPU-driven 剔除 + HiZ → ④ 并行度改造
(短期项可与 ②③ 穿插)→ ⑤ LOD 环。顶点格式瘦身穿插在 ② 后任意时点;
渲染线程分离建议在 ②③ 摊薄单帧 CPU 成本后再动,收益判断更准。
