# Mecraft 简单物理系统设计 (MVP)

## 1. 目标与边界
本方案用于快速落地一版稳定、可调、可扩展的基础物理系统，优先覆盖玩家基础体验：移动、重力、碰撞、水中处理。

- 目标：先实现“能跑、能跳、能下落、能入水”的可玩闭环。
- 目标：保持体素世界碰撞稳定，减少穿模和地面判定抖动。
- 目标：后续可平滑扩展到怪物/NPC 复用，不强依赖完整 ECS 改造。
- 边界：`World/Chunk/Block` 继续作为静态场景数据来源。
- 边界：本阶段仅实现“动态实体 vs 静态体素”碰撞，实体间推挤可延后。

## 2. 系统模块
建议按以下最小模块拆分：

- `PhysicsState`：存放实体物理状态（速度、接地、水中等）。
- `Movement`：把输入意图转换为目标速度/加速度。
- `Force`：施加重力、阻尼、地面摩擦等通用力。
- `WorldCollision`：与体素方块做 AABB 碰撞并解算。
- `Fluid`：检测水体状态并切换水中参数。

## 3. 最小数据结构

```cpp
struct PhysicsBody {
    glm::vec3 position;       // 世界坐标
    glm::vec3 velocity;       // m/s
    glm::vec3 halfExtents;    // AABB 半尺寸
    glm::vec3 colliderOffset; // 相对 position 的碰撞盒偏移

    bool isGrounded = false;
    bool isInWater = false;
    bool hitWall = false;
};

struct MoveIntent {
    glm::vec2 move;           // x=左右, y=前后, [-1, 1]
    bool wantsJump = false;
    bool wantsSprint = false;
};

struct PhysicsTuning {
    float gravity = 20.0f;
    float jumpSpeed = 8.5f;
    float moveSpeed = 4.5f;
    float airControl = 0.35f;
    float groundFriction = 10.0f;
    float airDrag = 1.0f;
    float terminalVelocity = 30.0f;

    float waterGravityScale = 0.25f;
    float waterDrag = 6.0f;
    float swimSpeed = 3.2f;
    float swimUpAccel = 10.0f;
};
```

## 4. 固定步长更新顺序
建议在固定物理 Tick 中执行，例如：`fixedDeltaTime = 1/60`。

1. 读取输入并生成 `MoveIntent`
2. 计算水平目标速度（地面/空中/水中）
3. 施加重力、阻尼、摩擦
4. 半隐式欧拉积分（先速度、后位置）
5. 体素碰撞求解（`Y -> X -> Z`）
6. 回写状态（`isGrounded`、`isInWater`、`hitWall`）

## 5. 关键公式与运动规则

- 受力与积分：
  - `a = sumForces / mass`
  - `v = v + a * dt`
  - `p = p + v * dt`
- 阻尼（简化）：
  - `v *= max(0, 1 - k * dt)`
- 重力：
  - 空气中：`vy -= gravity * dt`
  - 水中：`vy -= gravity * waterGravityScale * dt`
- 跳跃：
  - 地面起跳：`vy = jumpSpeed`
  - 水中按跳跃：`vy += swimUpAccel * dt`（持续上浮，不用单次冲量）
- 终端速度：
  - `vy = clamp(vy, -terminalVelocity, terminalVelocity)`

## 6. 碰撞检测与解算 (实体 vs 体素)

### 6.1 粗阶段
- 使用“当前 AABB + 本帧位移”得到扩展 AABB（swept AABB）。
- 仅遍历扩展范围覆盖到的方块坐标，查询 `BlockDef::isSolid`。

### 6.2 精细阶段
采用轴分离求解，顺序固定为 `Y -> X -> Z`：

1. 尝试沿当前轴移动
2. 检测与候选固体方块是否相交
3. 相交则回退到方块边界
4. 将该轴速度清零

说明：Y 轴优先可稳定处理落地、起跳、台阶边缘接触。

### 6.3 方块判定
- 通过 `BlockDef::isSolid` 判定是否参与碰撞。
- 本阶段默认方块为完整立方体，半砖/斜坡留到后续扩展。

## 7. 水中处理 (MVP)

- 判定：实体 AABB 与水方块重叠比例超过阈值（例如 0.2）即视为在水中。
- 重力：使用 `waterGravityScale` 降低有效重力。
- 阻力：切换到更强的 `waterDrag`，抑制速度。
- 移动：水平目标速度切换为 `swimSpeed`。
- 上浮：按住跳跃转为持续上浮加速度。
- 抖动抑制：建议加滞回阈值（入水 0.2，出水 0.1）。

## 8. 参数建议（首版可调）

- `gravity = 18 ~ 24`
- `moveSpeed = 4.0 ~ 5.5`
- `jumpSpeed = 7.5 ~ 9.5`
- `airControl = 0.25 ~ 0.45`
- `waterGravityScale = 0.2 ~ 0.4`
- `waterDrag = 4.0 ~ 8.0`
- `swimSpeed = 2.5 ~ 3.8`

建议先固定一个中位值做手感基线，再逐项微调，避免多参数同时变化。

## 9. 集成建议

- 输入系统只写 `MoveIntent`，不要直接改 `position`。
- 物理系统集中更新 `PhysicsBody` 并回写实体位置。
- 渲染系统仅读取最终位置，不参与物理求解。
- `World` 提供只读查询：`GetBlock(x, y, z)`、`GetBlocksInAABB(min, max)`。

## 10. 实施里程碑

### M1: 基础重力与落地
- 接入固定步长 Tick。
- 完成重力、地面检测、起跳。

### M2: 稳定碰撞
- 完成 `Y -> X -> Z` 碰撞求解。
- 修复常见穿模与贴墙抖动。

### M3: 水中行为
- 完成入水判定、重力缩放、阻力、游泳上浮。
- 调整空气/水中切换手感。

### M4: 优化与扩展
- 加入实体间推挤（可选）。
- 评估高速物体子步进或 CCD。

## 11. 伪代码示例

```cpp
void PhysicsTick(float dt, PhysicsBody& body, const MoveIntent& intent, World& world, const PhysicsTuning& cfg) {
    body.hitWall = false;

    // 1) 水中状态
    body.isInWater = QueryInWater(body, world);

    // 2) 水平控制
    glm::vec3 wishDir = ComputeWishDir(intent.move);
    float maxSpeed = body.isInWater ? cfg.swimSpeed : cfg.moveSpeed;
    glm::vec3 targetVelXZ = wishDir * maxSpeed;
    ApplyHorizontalControl(body.velocity, targetVelXZ, body.isGrounded, body.isInWater, cfg, dt);

    // 3) 重力与跳跃/上浮
    float g = cfg.gravity * (body.isInWater ? cfg.waterGravityScale : 1.0f);
    body.velocity.y -= g * dt;
    if (body.isInWater && intent.wantsJump) {
        body.velocity.y += cfg.swimUpAccel * dt;
    } else if (body.isGrounded && intent.wantsJump) {
        body.velocity.y = cfg.jumpSpeed;
    }

    // 4) 阻尼与终速
    ApplyDrag(body.velocity, body.isInWater ? cfg.waterDrag : cfg.airDrag, dt);
    body.velocity.y = glm::clamp(body.velocity.y, -cfg.terminalVelocity, cfg.terminalVelocity);

    // 5) 逐轴移动与碰撞
    MoveAndCollideY(body, world, dt);
    MoveAndCollideX(body, world, dt);
    MoveAndCollideZ(body, world, dt);

    // 6) 刷新接地等状态
    UpdateContactFlags(body, world);
}
```
