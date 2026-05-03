# 子区块系统（16×16×16）引入方案

## 一、现状概述

当前系统采用 **16×256×16 整体 Chunk**，无子区块概念。核心数据：

| 维度 | 当前值 |
|------|--------|
| Chunk 尺寸 | 16×256×16 = 65536 方块 |
| 每列 draw call | ≤ 3（opaque/transparent/cutout） |
| 邻居 | 4 方向水平（无垂直） |
| 存储 | 单一 Palette + BitPackedArray |
| 光照 | 单一 `m_lightMap[65536]` |
| Meshing 快照 | 4 方向边界（无 Y 方向） |

---

## 二、收益分析

### 2.1 内存节省（★★★★★）

这是最大收益。当前 16×256×16 整体存储意味着：

- **全空气子区块**（如 y=128 以上的平原地形上方）仍占用 Palette 条目和 BitPackedArray 空间
- **全实心子区块**（如 y=0~48 的地下石层）同理

引入子区块后：

- **空子区块**：可用 sentinel 值标记（如 `nullptr` 或 `SubChunkType::Air`），零存储
- **均匀子区块**（全部同一种方块）：单值 Palette（1 bit/voxel 即可），无需完整数组
- 估算：典型地形中约 30-50% 的子区块为全空/均匀，**内存节省 30-50%**

### 2.2 Meshing 效率（★★★★☆）

- **脏粒度细化**：修改 y=5 的一个方块，当前标记整个 16×256×16 为 dirty；子区块只需标记 y=0 所在的 16×16×16
- **贪心网格更紧凑**：每个子区块独立做贪心合并，工作集更小，cache 友好度更高
- **可跳过无用子区块**：全实心（无暴露面）和全空的子区块完全跳过 meshing

### 2.3 惰性生成（★★★☆☆）

- 高海拔子区块可延迟到玩家接近或建造时才生成
- 减少初始加载的生成开销
- 但当前地形高度 range 8~248，大多数列都需要生成大部分子区块，实际收益有限

### 2.4 未来扩展性（★★★★☆）

- 支持 1.18+ 风格的无限高度（-64 ~ 320）时，子区块是必需的基础设施
- 支持模组添加更多维度/高度范围时，架构更灵活

---

## 三、风险分析

### 3.1 Draw Call 爆炸（★★★★★ — 最高风险）

**量化分析**：

| 场景 | 当前 | 子区块后 |
|------|------|----------|
| 渲染距离 12 chunks | 576 列 × 3 pass = **1728** | 576 列 × ~8 可见子区块 × 3 = **~13824** |
| 典型有地形区域 | 每列约 3-6 个有几何的子区块 | 多 3-6× draw calls |

**8 倍 draw call 增加**是不可接受的。必须配合分层剔除才能缓解。

### 3.2 代码改动范围极广（★★★★★）

受影响的模块几乎涵盖整个引擎核心：

| 模块 | 改动量 | 说明 |
|------|--------|------|
| `Chunk` | **重写** | 拆分为 ChunkColumn + SubChunk |
| `ChunkMesher` | **重大修改** | 快照需 6 方向边界，mesh 按 sub-chunk 粒度 |
| `ChunkMeshingService` | **重大修改** | job 粒度变为 sub-chunk，引用管理复杂化 |
| `LightEngine` | **重大修改** | 世界坐标访问需跨 sub-chunk 查找，垂直邻居传播 |
| `World` | **重大修改** | 区块管理从 `(cx,cz)` 到 `(cx,scy,cz)` |
| `Renderer` | **重大修改** | 视锥剔除层次重构，draw call 调度 |
| `TerrainGenerator` | **中等** | 按 sub-chunk 粒度填充 |
| `Palette/BitPackedArray` | **小** | 数据结构不变，但每个 sub-chunk 各持一份 |

### 3.3 跨子区块/跨列光照传播复杂度（★★★★☆）

当前 `LightEngine` 的 BFS 使用世界坐标 `getSkyLight(wx,wy,wz)`，内部做 `worldToChunkCoord()` → 查找 chunk → 读取光照。子区块后：

- 查找链变长：`worldCoord → (cx, scy, cz) → SubChunk → lightMap[localIdx]`
- 垂直传播需要跨子区块查找：同一列内 y 跨越子区块边界时，需要相邻子区块指针
- 需要增加 **垂直邻居** 引用（上下子区块），传播逻辑分支增加

### 3.4 快照机制复杂度增加（★★★☆☆）

当前快照捕获 4 方向水平边界。子区块后需要 **6 方向边界**（+X, -X, +Y, -Y, +Z, -Z）：

- `posYBorder[16*16]`, `negYBorder[16*16]` — 新增
- 对应光照边界也需新增
- 单个子区块快照体积：`4096(方块) + 6×256(边界) = 5632`（比当前的 81920 小得多，但条目更多）

### 3.5 性能回归风险（★★★☆☆）

- 指针追踪增多：每次方块/光照访问多一层间接
- 渲染循环中遍历更多 mesh 对象
- 邻居管理从 4 个指针变为 6 个（含上下）

---

## 四、Draw Call 爆炸解决方案 — 分层视锥剔除

### 4.1 当前三级剔除 → 改为四级剔除

```
Region(4×4列) → Column(1列) → SubChunk(16³) → Mesh Pass(opaque/trans/cutout)
```

### 4.2 核心思路：语义标记提前剔除

大部分子区块可以通过 **语义标记** 零开销跳过，无需进入视锥测试：

```cpp
enum class SubChunkType : uint8_t {
    Air,        // 全空气 — 无需渲染、无 mesh
    Solid,      // 全实心且无暴露面 — 无需渲染
    Normal      // 有混合内容 — 需要视锥测试和渲染
};
```

- **Air 子区块**：直接跳过，0 draw call
- **Solid 子区块**：需要检查是否有暴露面（与 Air/Normal 子区块相邻的边界面），但通常地下 Solid 子区块完全被包围，0 draw call
- **Normal 子区块**：才进入视锥 AABB 测试

**估算**：典型地形中，只有地表附近的 2-4 个子区块为 Normal，其余为 Air 或 Solid。实际参与渲染的子区块 ≈ 当前的 1-2 倍，draw call 增长可控。

### 4.3 四级剔除算法

```cpp
void Renderer::renderChunks() {
    for (auto& region : regions) {
        // Level 1: Region AABB 测试
        if (!isAABBInFrustum(region.bounds)) continue;

        for (auto& column : region.columns) {
            // Level 2: Column AABB 测试 (整列 16×256×16)
            if (!isAABBInFrustum(column.bounds)) continue;

            for (auto& subChunk : column.subChunks) {
                // Level 3: 语义跳过 (零开销)
                if (subChunk.type == SubChunkType::Air
                 || subChunk.type == SubChunkType::Solid) continue;

                // Level 4: SubChunk AABB 测试 (使用紧凑包围盒)
                if (!isAABBInFrustum(subChunk.mesh.bounds)) continue;

                // 绘制
                drawMesh(subChunk.mesh);
            }
        }
    }
}
```

### 4.4 进一步优化：Multi-Draw Indirect

如果 draw call 仍偏高，可考虑：

- **glMultiDrawElementsIndirect**：将同一 pass 的所有可见子区块合并为一次 indirect draw call
- 前提：所有子区块 mesh 的 VBO 格式统一，可用同一 shader
- 这要求重构 VBO 布局为全局统一缓冲区，改动较大，可作为后续优化

### 4.5 贪心网格跨子区块合并的权衡

| 方案 | 优点 | 缺点 |
|------|------|------|
| **不跨子区块合并**（推荐） | 实现简单，子区块完全独立，修改局部化 | 边界处面数略多（最多 2×面），但实际影响小 |
| **跨子区块合并** | 面数最优 | 复杂度极高，一个子区块修改可能级联影响邻居 |

**建议选择不跨子区块合并**。边界处多出的面数在 16×16 面积上可忽略，但换来的是子区块完全自治。

---

## 五、各子系统改造方案

### 5.1 数据结构

```cpp
// 新增：子区块语义标记
enum class SubChunkType : uint8_t {
    Air,        // 全空气
    Solid,      // 全实心（单一方块，无暴露面）
    Normal      // 混合内容
};

// 新增：子区块
class SubChunk {
    static constexpr int SIZE = 16;
    static constexpr std::size_t BLOCK_COUNT = 16 * 16 * 16; // 4096

    Palette m_palette;
    BitPackedArray m_blockData;                  // 4096 entries
    std::array<uint8_t, BLOCK_COUNT> m_lightMap{};

    SubChunkType m_type = SubChunkType::Air;     // 语义标记
    ChunkMesh m_mesh;
    bool m_dirty = true;
    uint64_t m_meshRevision = 1;

    // 6 方向邻居指针
    // [0]=+X, [1]=-X, [2]=+Y, [3]=-Y, [4]=+Z, [5]=-Z
    SubChunk* neighbors[6] = {};
};

// Chunk 重命名为 ChunkColumn
class ChunkColumn {
    static constexpr int NUM_SUB_CHUNKS = 16;    // 256 / 16

    std::array<std::unique_ptr<SubChunk>, NUM_SUB_CHUNKS> m_subChunks;
    std::array<int, 16 * 16> m_heightMap{};      // 仍在列级

    // 4 方向水平列级邻居
    ChunkColumn* neighbors[4] = {};
};
```

### 5.2 Meshing 改造

**快照结构**：

```cpp
struct SubChunkMeshingSnapshot {
    // 方块数据（可能只有 4096 或更少）
    std::array<BlockID, 4096> blocks{};
    std::array<uint8_t, 4096> lightMap{};

    // 6 方向边界（每方向 16×16 = 256 个方块）
    std::array<BlockID, 256> posXBorder, negXBorder;  // Z×Y 面
    std::array<BlockID, 256> posYBorder, negYBorder;  // X×Z 面
    std::array<BlockID, 256> posZBorder, negZBorder;  // X×Y 面

    // 对应光照边界
    std::array<uint8_t, 256> posXLightBorder, negXLightBorder;
    std::array<uint8_t, 256> posYLightBorder, negYLightBorder;
    std::array<uint8_t, 256> posZLightBorder, negZLightBorder;
};
```

**改造要点**：

- 快照粒度从 65536 → 4096，更小更快
- 边界捕获从 4 方向 → 6 方向（+Y/-Y 各 256 个方块）
- 贪心网格不跨子区块边界合并
- **关键优化**：Air/Solid 子区块直接跳过 meshing
- `getNeighborAwareBlock()` / `getNeighborAwareLight()` 扩展为 6 方向查找

### 5.3 光照改造

**访问链变化**：

```
当前: worldCoord → World::getChunk(cx,cz) → Chunk::getSunlight(localIdx)
改造: worldCoord → World::getChunkColumn(cx,cz) → ChunkColumn::getSubChunk(scy) → SubChunk::getSunlight(localIdx)
```

**改造要点**：

- 同列内上下邻居直接通过 `SubChunk::neighbors[2/3]` 访问，无需通过 World 查找
- BFS 传播逻辑不变，只是查找多一层间接
- `onChunkLoaded` 仍按列级初始化（因为天空光需要从顶向下扫描整列）
- 高度图保留在 `ChunkColumn` 级，用于天空光快速初始化
- `propagateBorderInto` 需扩展为处理垂直边界上的所有子区块对

### 5.4 区块生成改造

- `TerrainGenerator::generateChunk` → 生成整个列的所有非空子区块
- 惰性生成：可为高海拔子区块延迟调用 `generateSubChunk`
- 子区块类型推断：生成后扫描是否全空/均匀，设置 `SubChunkType`

```cpp
void TerrainGenerator::generateChunkColumn(ChunkColumn& column, int cx, int cz) {
    // 1. 计算该列所有 16×16 列的地表高度
    // 2. 按子区块范围填充方块
    for (int scy = 0; scy < NUM_SUB_CHUNKS; scy++) {
        int yStart = scy * 16;
        int yEnd = yStart + 16;

        // 检查这个子区块范围是否有任何内容
        if (isSubChunkEmpty(cx, cz, yStart, yEnd)) {
            // 不创建子区块对象，保持 nullptr
            continue;
        }

        auto subChunk = std::make_unique<SubChunk>();
        fillSubChunk(*subChunk, cx, cz, yStart, yEnd);
        subChunk->m_type = inferSubChunkType(*subChunk);
        column.m_subChunks[scy] = std::move(subChunk);
    }

    // 3. 计算列级高度图
    computeHeightMap(column);
}
```

### 5.5 跨区块光照传播

- 同列内子区块间光照传播通过直接指针，无需特殊处理
- 跨列传播：需要同时处理水平边界上的所有子区块对

```cpp
void LightEngine::propagateBorderInto(ChunkColumn* column) {
    for (int scy = 0; scy < NUM_SUB_CHUNKS; scy++) {
        SubChunk* sub = column->getSubChunk(scy);
        if (!sub || sub->m_type == SubChunkType::Air) continue;

        // 水平方向：对每个子区块独立做边界传播
        propagateHorizontalBorders(sub, scy);

        // 垂直方向：同列内直接通过邻居指针传播
        // 跨列垂直传播在水平传播中自然覆盖
    }
}
```

---

## 六、改造实施路线

### Phase 1：数据结构重构（最核心，影响最大）

1. 定义 `SubChunk` 类、`SubChunkType` 枚举
2. 将 `Chunk` 重命名为 `ChunkColumn`，内部持 `SubChunk` 数组
3. 实现 `SubChunk` 的方块读写（Palette + BitPackedArray，复用现有实现）
4. 改造 `World` 的区块管理（key 仍为 `(cx,cz)`，但内部按 sub-chunk 操作）
5. 实现子区块类型推断 `inferSubChunkType()`
6. **验证点**：确保方块读写正确、Air/Solid 子区块被正确标记

### Phase 2：渲染管线适配

7. 改造 `ChunkMesher` 支持 6 方向快照和子区块粒度 meshing
8. 改造 `ChunkMeshingService` 的 job 粒度为 sub-chunk
9. 实现四级视锥剔除（Region → Column → SubChunkType → AABB）
10. Air/Solid 子区块的渲染跳过
11. **验证点**：渲染结果与改造前一致，draw call 增长 ≤ 2×

### Phase 3：光照系统适配

12. 改造 `LightEngine` 的跨子区块访问链
13. 添加垂直邻居指针和传播
14. 改造 `onChunkLoaded` / `onBlockChanged` 适配子区块粒度
15. **验证点**：光照结果与改造前一致

### Phase 4：优化

16. `SubChunkType` 增量维护（方块修改时更新类型标记）
17. 考虑 Multi-Draw Indirect 降低 draw call
18. 惰性子区块生成
19. 性能基准测试和调优

---

## 七、结论

| 维度 | 评价 |
|------|------|
| **总体收益** | ★★★★☆ — 内存节省显著，架构扩展性大幅提升，为无限高度做准备 |
| **总体风险** | ★★★★☆ — 改动范围极广，draw call 爆炸需精心设计 |
| **是否建议引入** | **是，但需分阶段实施**，Phase 1 先完成数据结构重构并验证正确性，再逐步推进渲染和光照 |
| **最关键的设计决策** | `SubChunkType` 语义标记 + 不跨子区块贪心合并 + 四级视锥剔除 — 这三者组合可将 draw call 控制在当前的 1.5-2 倍以内 |
