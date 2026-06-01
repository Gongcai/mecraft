# 方块注册系统贴图不一致 & 新方块添加需改代码 — 重构方案

## 问题分析

### 问题 1：三个渲染路径贴图不一致

项目中有**两套并行的贴图索引系统**，它们对同一个 texture name 产出不同的整数 ID：

| 系统 | 用途 | 索引方式 | 存储字段 |
|------|------|---------|---------|
| **TextureAtlas**（2D 图集） | UI 图标生成 (`buildBlockIconAtlas`) | `m_textures[name] = i` — 文件在排序后列表中的位置 | `BlockDef::texTop/texBottom/...` |
| **TextureArray**（3D 纹理数组） | 世界渲染 (`ChunkMesher`) + 手持方块预览 (`FirstPersonHeldItemRenderer`) | `m_textureArrayLayers[name] = currentLayer` — 带动画帧偏移的 layer 索引 | `BlockDef::worldTop/worldBottom/...`（`AnimatedTextureRef::firstLayer`） |

**根因**：在 [Block.cpp:521-588](file:///d:/project/mecraft/src/world/block/Block.cpp#L521-L588) 中，`resolveTexName()` 调用 `resourceMgr->getTexture(name)` 返回的是 **Atlas tile index**，而 `resolveWorldTexture()` 调用 `resourceMgr->getTextureArrayLayer(name)` 返回的是 **Array layer index**。

由于 TextureArray 有动画帧会插入额外 layer，**两个系统对同一张贴图返回的数字不同**。例如：
- 假设 `lava_still.png`（32帧动画）排在 `oak_log.png` 之前
- Atlas: `lava_still = 5`, `oak_log = 6`
- Array: `lava_still = 5` (占 layer 5~36), `oak_log = 37`
- 结果：`def.texTop = 6`（Atlas 索引），`def.worldTop.firstLayer = 37`（Array layer）

**世界渲染基本正确**：[ChunkMesher.cpp:514](file:///d:/project/mecraft/src/renderer/mesh/ChunkMesher.cpp#L514) 使用 `BlockStateRegistry::getStateTextures(blockId)` → 取 `worldTop.firstLayer`，绑定 `GL_TEXTURE_2D_ARRAY` ✅。但存在例外：火把等特殊 mesh 仍有直接使用 `textures.texTop` 的路径（如 [ChunkMesher.cpp:2235](file:///d:/project/mecraft/src/renderer/mesh/ChunkMesher.cpp#L2235)），统一字段时必须同步迁移。

**手持方块预览错误**：[FirstPersonHeldItemRenderer.cpp:858-955](file:///d:/project/mecraft/src/renderer/renderers/FirstPersonHeldItemRenderer.cpp#L858-L955) 使用 `def.texTop`（Atlas 索引）作为 layer，但绑定的是 `GL_TEXTURE_2D_ARRAY` ❌

**图标生成**：[ResourceMgr.cpp:1247-1258](file:///d:/project/mecraft/src/resource/ResourceMgr.cpp#L1247-L1258) 使用 `def.texTop/texRight/texFront`（Atlas 索引）从 Atlas 像素数据读取——这里 Atlas 内部是自洽的，但存在与上面相同的根因：维护两套索引增加了不一致风险。

**掉落物渲染错误**：[DropRenderer.cpp:63-76](file:///d:/project/mecraft/src/renderer/renderers/DropRenderer.cpp#L63-L76) 与手持方块完全一样的问题，使用 `def.texTop`（Atlas 索引）作为 TextureArray layer ❌

**其他直接依赖 `texXxx` 的路径**：
- [HotbarControl.cpp:326](file:///d:/project/mecraft/src/ui/hud/HotbarControl.cpp#L326)：legacy atlas fallback 仍直接读取 `blockDef.texFront/texTop`
- [ParticleSpawnSystem.cpp:73](file:///d:/project/mecraft/src/ecs/systems/particle/ParticleSpawnSystem.cpp#L73)：方块破坏粒子直接读取六个 `blockDef.texXxx`
- [BlockStateRegistry.cpp:71-121](file:///d:/project/mecraft/src/world/block/BlockStateRegistry.cpp#L71-L121)：`axis` 状态旋转逻辑同时使用 Atlas index 与 `worldXxx` 判断 end/side 纹理

> [!IMPORTANT]
> 核心问题：`BlockDef` 同时存储了两套语义不同的索引（`texXxx` = Atlas tile index, `worldXxx` = Array layer index），但 held item renderer、drop renderer、部分特殊 mesh/粒子路径会把 Atlas index 当 Array layer 使用或继续依赖旧字段。

### 问题 2：添加新方块必须改 C++ 代码

当前流程：
1. [BuiltinIds.h](file:///d:/project/mecraft/src/game/content/BuiltinIds.h) 中的 `MECRAFT_FOR_EACH_BUILTIN_BLOCK` 宏定义所有内置方块
2. [IdRegistry.cpp:40-46](file:///d:/project/mecraft/src/engine/registry/IdRegistry.cpp#L40-L46) 用宏预注册所有方块 ID
3. [Block.cpp:340](file:///d:/project/mecraft/src/world/block/Block.cpp#L340) 先注册内置 ID，再从 JSON 加载

**JSON 已可添加新方块**：[Block.cpp:429-432](file:///d:/project/mecraft/src/world/block/Block.cpp#L429-L432) 中，如果 JSON 中的 `id` 不在内置表中，会调用 `registerBlock()` 动态注册新 ID。所以添加新方块只需编辑 `blocks.json` 即可。

**但有隐患**：
- 内置方块的 RuntimeId 顺序由宏列表顺序决定，与 JSON 无关
- 新方块（纯 JSON）的 RuntimeId 在内置范围之后分配，不会冲突
- 但如果未来想在 BuiltinIds.h 中间插入新条目，会改变所有后续方块的 RuntimeId

> [!NOTE]
> JSON-only 方块添加路径已经可用。优化点是将 BuiltinIds.h 精简为少数核心方块（C++ 代码中确实引用的），其余全部由 JSON 驱动。

### 存档格式设计（前瞻）

当前没有存档系统。未来设计存档时，为保证兼容性和高效性：
- **方块数据序列化**：每个区块使用 **调色板 + 位压缩数组** 格式（与当前 Palette + BitPackedArray 内存结构一致）
- **调色板条目**：使用 **NamespacedId 字符串**（如 `"minecraft:oak_log"`）而非 RuntimeId 整数
- **加载时**：根据调色板字符串查询 `BlockRegistry::getId()` 重建 RuntimeId 映射
- 这样 RuntimeId 的分配顺序变化不会影响存档兼容性
- 实体/物品等也用 NamespacedId 序列化

---

## Proposed Changes

### 组件一：统一贴图索引，消除双系统

> 目标：`BlockDef` 只存储一套贴图索引，基于 **TextureArray layer**（世界渲染使用的索引）。UI 图标生成改用 Array layer → Atlas tile 的映射。

#### [MODIFY] [Block.h](file:///d:/project/mecraft/src/world/block/Block.h)

- 从 `BlockDef` 中**移除** `texTop/texBottom/texLeft/texRight/texFront/texBack` 这 6 个 Atlas tile 索引字段
- 只保留 `worldTop/worldBottom/...`（`AnimatedTextureRef`），重命名为 `faceTop/faceBottom/...` 以表示它们是唯一的贴图引用
- 添加便捷方法 `int getFaceLayer(int face) const` 返回 `faceXxx.firstLayer`

#### [MODIFY] [Block.cpp](file:///d:/project/mecraft/src/world/block/Block.cpp)

- 移除 `resolveTexName` lambda（Atlas 索引解析）
- 保留并简化 `resolveWorldTexture` → 重命名为 `resolveTexture`
- `setAllFaces` 只设置 `AnimatedTextureRef` 字段
- JSON 解析只产出 Array layer 索引

#### [MODIFY] [BlockStateRegistry.h](file:///d:/project/mecraft/src/world/block/BlockStateRegistry.h) / [BlockStateRegistry.cpp](file:///d:/project/mecraft/src/world/block/BlockStateRegistry.cpp)

- `StateTextureIndices` 中移除 `texTop/texBottom/...`，只保留 `AnimatedTextureRef` 字段（已重命名）
- 对应修改填充逻辑
- `axis` 状态旋转逻辑不能再通过 `texTop/texFront` 比较来判断 end/side；改为直接旋转 `AnimatedTextureRef`：
  - `endRef = faceTop`
  - `sideRef` 优先使用 `faceFront`，若与 `endRef` 同层则退回 `faceLeft`，再退回 `faceBottom`
  - `x/z/y` 三种轴向只重排 `AnimatedTextureRef`，不再维护 Atlas index 副本
- 判断两个纹理引用是否相同使用 `firstLayer/frameCount/fps/isAnimated` 的显式比较，避免只比较整数 layer 后丢掉动画信息

#### [MODIFY] [FirstPersonHeldItemRenderer.cpp](file:///d:/project/mecraft/src/renderer/renderers/FirstPersonHeldItemRenderer.cpp)

- `getFaceTextureIndex` 和 `buildBlockMesh` 改为使用 `BlockDef::getFaceLayer(face)` 而非 `def.texTop` 等
- Cross/Torch 形状同理

#### [MODIFY] [ResourceMgr.cpp](file:///d:/project/mecraft/src/resource/ResourceMgr.cpp)

- `buildBlockIconAtlas` 改为使用 Array layer → Atlas tile 映射（**方案 A**）
- 新增 `int arrayLayerToAtlasTile(int arrayLayer) const` 方法
- 新增 `m_arrayLayerToAtlasTile` 映射表，在 `buildTextureArray` 结束时构建
- 映射表规则必须显式记录，不依赖“非动画时 tile index == array layer”的隐含关系：
  - `buildTextureArray` 遍历排序后的 image list 时，同时知道 `imageIndex`（Atlas tile）与 `currentLayer`（Array first layer）
  - 对静态纹理：`m_arrayLayerToAtlasTile[currentLayer] = imageIndex`
  - 对动画纹理：该纹理占用的所有 frame layer 都映射回同一个 `imageIndex`，至少保证 `firstLayer` 可映射
  - `arrayLayerToAtlasTile()` 未命中时返回 `0` 或明确 fallback tile，并在 debug build 输出警告

#### [MODIFY] [ChunkMesher.cpp](file:///d:/project/mecraft/src/renderer/mesh/ChunkMesher.cpp)

- 大部分 cube/cross/world face 路径已正确使用 `StateTextureIndices` 中的 `AnimatedTextureRef`
- 仍需迁移特殊 mesh 路径中残留的 `textures.texTop`（例如 torch cuboid），改为读取 `textures.faceTop.firstLayer` 或 `getFaceLayer(FACE_TOP)`

#### [MODIFY] [DropRenderer.cpp](file:///d:/project/mecraft/src/renderer/renderers/DropRenderer.cpp)

- **已确认** [DropRenderer.cpp:63-76](file:///d:/project/mecraft/src/renderer/renderers/DropRenderer.cpp#L63-L76) 的 `getFaceTextureIndex` 使用 `def.texTop` 等 Atlas 索引
- 改为使用统一的 `def.getFaceLayer(face)` — 与 FirstPersonHeldItemRenderer 相同的修改

#### [MODIFY] [HotbarControl.cpp](file:///d:/project/mecraft/src/ui/hud/HotbarControl.cpp)

- legacy atlas fallback 不再直接读取 `blockDef.texFront/texTop`
- 优先使用 `buildBlockIconAtlas` 生成的 item icon atlas
- 如必须保留 legacy atlas fallback，则通过 `ResourceMgr::arrayLayerToAtlasTile(blockDef.getFaceLayer(face))` 转换为 Atlas tile 后再取 UV

#### [MODIFY] [ParticleSpawnSystem.cpp](file:///d:/project/mecraft/src/ecs/systems/particle/ParticleSpawnSystem.cpp)

- 方块破坏粒子当前使用 Atlas tile index 生成粒子贴图
- 若粒子 shader 绑定的是 `GL_TEXTURE_2D_ARRAY`：直接改为随机选择 `blockDef.faceXxx.firstLayer`
- 若粒子 shader 绑定的是 2D Atlas：保留 atlas 采样，但通过 `arrayLayerToAtlasTile()` 将 face layer 转回 Atlas tile
- 在实现前确认粒子渲染器绑定的纹理类型，避免把修复方向反过来

---

### 组件二：BuiltinIds 评估

经搜索全部 `BlockIds::XXX` 引用，以下方块在 C++ 中被直接使用：

| 符号 | 使用位置 |
|------|----------|
| `AIR` | 到处都用（哨兵值） |
| `WATER` | 流体系统、SubChunk、World |
| `GRASS` | 世界生成（地表） |
| `DIRT` | 世界生成（地下填充） |
| `STONE` | 世界生成（基岩以上基质） |
| `SAND` | 世界生成（沙漠/河岸） |
| `BEDROCK` | 世界生成（底层） |
| `WOOD` | 世界生成（橡树原木） |
| `BIRCH_LOG` | 世界生成（白桦原木）+ 默认背包 |
| `OAK_LEAVES` | 世界生成（橡树树叶） |
| `BIRCH_LEAVES` | 世界生成（白桦树叶）+ 默认背包 |
| `TALL_GRASS` | 世界生成（草地装饰） |
| `ROSE` | 世界生成（花朵装饰） |
| `COAL_ORE` | 世界生成（矿石）+ 默认背包 |
| `IRON_ORE` | 世界生成（矿石）+ 默认背包 |
| `GOLD_ORE` | 世界生成（矿石） |
| `DIAMOND_ORE` | 世界生成（矿石）+ 默认背包 |
| `GLASS` | 默认背包 |
| `TORCH` | 默认背包 |
| `BLUE_WOOL` | 默认背包 |

同时，当前 BuiltinIds 中也存在暂未被 C++ 直接引用的内容（例如各类 planks、`BROWN_MUSHROOM` 等）。这些条目不是热路径必须项，而是当前内置内容集/默认注册顺序的一部分。

**决策：保留 BuiltinIds 列表不变**，因为：
1. 大部分基础方块确实被世界生成、流体系统、默认背包等 C++ 代码直接引用
2. 未引用条目仍可作为“内置稳定内容集”保留，避免本次贴图重构混入 ID 注册策略调整
3. 新增不需要 C++ 符号的装饰方块只需添加到 `blocks.json`，已经可以工作
4. 如果未来要精简 BuiltinIds，应作为单独迁移处理，并配套 RuntimeId/存档兼容策略

> [!TIP]
> **性能说明**：`BlockIds::XXX` 是 `uint16_t` 变量，在 `BlockIds::init()` 时一次性通过名称查询赋值。世界生成的热路径中只做整数比较/赋值，与是否在宏列表中无关。未来新增方块到 BuiltinIds.h 只影响注册顺序，不影响运行性能。
>
> 如果某个方块只在 JSON 中定义（不在 BuiltinIds 中），但世界生成需要用到，推荐模式：
> ```cpp
> // TerrainGenerator.h 中缓存 ID
> BlockID m_myBlock = 0;
> // init() 中查询一次
> m_myBlock = BlockRegistry::findByName("my_block");
> // 热路径中用缓存的整数
> chunk.setBlock(x, y, z, m_myBlock);
> ```
> 这与 `BlockIds::XXX` 的运行时性能完全一致——都是 `uint16_t` 比较。

---

## 已解决的设计决策

- ✅ **存档格式**：当前无存档系统。已在方案中加入前瞻性设计（Palette + NamespacedId 字符串序列化）
- ✅ **DropRenderer**：已确认受影响（[DropRenderer.cpp:63-76](file:///d:/project/mecraft/src/renderer/renderers/DropRenderer.cpp#L63-L76)），已纳入修改范围
- ✅ **图标生成方案**：采用方案 A（保留 2D Atlas 供图标生成，构建 ArrayLayer→AtlasTile 映射表）
- ✅ **BuiltinIds 精简**：本次不精简。当前列表同时包含 C++ 直接引用的基础方块和暂未直接引用的内置内容；精简会改变 RuntimeId 顺序，应单独设计迁移
- ✅ **世界生成性能**：无影响。`BlockIds::XXX` 是运行时 `uint16_t` 变量，热路径只做整数操作

---

## Verification Plan

### Automated Tests
- 编译通过（`cmake --build`）
- 确认 `BlockDef` 和 `StateTextureIndices` 不再有 `texTop/texBottom/texLeft/texRight/texFront/texBack` 字段（grep 确认无引用）
- 确认所有渲染/粒子/UI 代码不再把 Atlas tile index 当作 TextureArray layer 使用：
  - `FirstPersonHeldItemRenderer`
  - `DropRenderer`
  - `ChunkMesher` 特殊 mesh 路径（torch 等）
  - `ParticleSpawnSystem`
  - `HotbarControl` legacy fallback
- 增加/执行一个轻量断言或 debug 日志检查：动画纹理后面的静态纹理，其 `arrayLayerToAtlasTile(firstLayer)` 能正确回到原 Atlas tile
- 若已有测试框架可用，补一个纯函数级测试覆盖 `arrayLayerToAtlasTile()` 的静态纹理、动画首帧、动画中间帧、未命中 fallback

### Manual Verification
- 运行游戏，检查：
  1. 世界中的方块贴图正确（应与改动前一致）
  2. 手持方块预览贴图与世界方块一致（oak_log 应显示橡木贴图）
  3. 掉落物贴图与世界方块一致
  4. 物品栏/创造模式中方块图标正确
  5. 火把等特殊 mesh 贴图正确
  6. 方块破坏粒子贴图正确
- 在 `blocks.json` 中添加一个纯 JSON 定义的新方块，验证无需修改任何 C++ 代码即可使用
