# mecraft 输入系统设计稿（不改代码版）

## 1. 目标与范围

本文档定义一套可渐进落地的输入系统升级方案，解决以下问题：

1. 在游戏内如何进行 `gameplay -> ui -> pause` 等 context 切换，以及切换逻辑应放在哪一层。
2. 将 Binding 结构从 `{Type, code}` 升级为 `{device, control, modifiers, triggerType}`，并说明为何这样设计、如何落地。

约束：
- 本稿仅为设计，不修改现有源码。
- 以当前项目结构为前提：`Game` 主循环、`InputManager` 快照、`Player` 直接读原始键位、`ActionMap` 已具备基础映射能力。

---

## 2. 现状与问题

### 2.1 当前输入链路（已实现）

- `src/core/Game.cpp`：主循环中调用 `pollEvents -> Time::update -> InputManager::update -> Player::update`。
- `src/core/InputManager.*`：维护键盘/鼠标状态，产出 `InputSnapshot`。
- `src/player/Player.cpp`：直接读取 `WASD/SPACE/SHIFT/CTRL` 等原始键位进行移动。
- `src/player/ActionMap.*`：支持 `Action -> [Key/Mouse]` 基础映射与配置加载，但尚未接入主循环消费路径。

### 2.2 主要痛点

- 无 context 概念：打开 UI 后，`WASD` 仍会被 `Player` 消费。
- 绑定表达力不足：`{Type, code}` 无法表达修饰键、触发语义、设备扩展。
- 扩展成本高：后续接手柄、组合键、双击/长按等时会堆叠特判。

---

## 3. 总体架构（分层与职责）

建议输入系统分 4 层，职责单向清晰：

1. **设备采样层（InputManager）**
   - 负责采样设备状态，生成 `InputSnapshot`。
   - 不关心业务语义（Move/Jump/Menu）。

2. **动作解析层（ActionMap + ActionResolver）**
   - 将 `InputBinding` 与 `InputSnapshot` 匹配，产出动作触发结果。
   - 负责 `Held/Pressed/Released/Tap/Hold` 等判定。

3. **上下文路由层（InputContextManager）**
   - 管理 context 栈与优先级（如 `Pause > UI > Gameplay`）。
   - 对外提供：查询某动作在“当前有效 context”是否触发。

4. **游戏状态层（Game）**
   - 维护游戏模式状态机（Gameplay/UI/Pause）。
   - 决定何时 `push/pop/switch context`，并同步鼠标捕获模式。

说明：
- `Player` 与 `UI` 只消费动作查询结果，不直接读物理键位。
- context 的“决策权”在 `Game`，输入层仅做“规则执行”。

---

## 4. Context 切换设计

## 4.1 推荐上下文模型

定义 context（示例）：
- `Gameplay`
- `UI`
- `Pause`
- （可选）`Console` / `PhotoMode`

管理方式：
- 使用栈结构：底层常驻 `Gameplay`，上层可叠加 `UI/Pause`。
- 生效顺序：从栈顶向下查询动作，命中即停止。

示例：
- 正常游玩：`[Gameplay]`
- 打开背包：`[Gameplay, UI]`
- 按下暂停：`[Gameplay, UI, Pause]`（或切换为 `[Pause]`，按需求定）

## 4.2 切换触发与归属

切换归属：**由 `Game` 统一提交**。

- `Game` 在每帧输入阶段判断关键动作：`Inventory/Menu/Cancel`。
- 触发后调用 context 管理接口：
  - `pushContext(UI)`
  - `popContext()`
  - `switchTo(Pause)` 或 `pushContext(Pause)`
- 同步调用 `InputManager::captureMouse(true/false)` 以匹配当前模式。

原因：
- `Game` 同时掌握渲染、更新、UI 生命周期，是最合适的状态协调层。
- 避免 `Player` 或 `UI` 组件互相改输入状态导致耦合。

## 4.3 每帧时序（建议）

每帧顺序建议如下：

1. `Window::pollEvents()`
2. `InputManager::update()` 生成本帧快照
3. `InputContextManager::beginFrame(snapshot)` 计算动作触发缓存
4. `Game` 处理全局动作（`Menu`、`Pause`、`Inventory`）并可能切换 context
5. 按 context 分发更新：
   - 顶层是 `UI/Pause`：优先更新 UI
   - `Gameplay` 有效时：更新 `Player/World`
6. 渲染阶段

关键点：
- “切换 context”应在“玩家控制更新”之前完成，避免同帧误输入。

---

## 5. Binding V2 设计

## 5.1 结构升级目标

将当前 `InputBinding { Type, code }` 升级为：

- `device`：设备类型（Keyboard/Mouse/Gamepad）
- `control`：设备控件（例如 `Key_W`、`Mouse_Left`、`Pad_A`、`Pad_LStickX+`）
- `modifiers`：修饰条件（Shift/Ctrl/Alt/Meta，可含必须按下与必须未按下）
- `triggerType`：触发语义（Pressed/Released/Held/Tap/Hold/DoubleTap）

## 5.2 字段语义说明

### `device`

作用：将输入来源设备化，避免 `int code` 混合键盘/鼠标/手柄。

建议枚举：
- `Keyboard`
- `Mouse`
- `Gamepad`

### `control`

作用：描述具体控件，而非裸整数。

建议形式：
- 枚举或字符串 ID（推荐内部枚举 + 配置字符串映射）
- 示例：`W`、`LEFT_SHIFT`、`MOUSE_LEFT`、`GAMEPAD_FACE_BOTTOM`

### `modifiers`

作用：表达附加条件，避免散落业务特判。

建议规则：
- `required`: 必须按下的修饰键集合
- `forbidden`: 必须未按下的修饰键集合

示例：
- `Ctrl + 1`：`control=KEY_1`, `required={CTRL}`
- `Shift+W` 冲刺前进：可绑定在 Sprint 或 MoveForward 变体上

### `triggerType`

作用：定义“何时触发”，统一 Action 查询语义。

建议最小集合：
- `Pressed`：本帧由未按下到按下
- `Released`：本帧由按下到未按下
- `Held`：当前帧按下

扩展集合（后续）：
- `Tap`：按下持续时间小于阈值
- `Hold`：按住超过阈值后触发
- `DoubleTap`：双击窗口内第二次按下触发

## 5.3 判定流程（动作解析）

对每个 binding 按以下流程判定：

1. 设备可用性检查（如手柄在线）
2. 控件状态读取（基于 snapshot）
3. 修饰键条件检查（required/forbidden）
4. 按 `triggerType` 执行触发判定
5. 任一 binding 命中则该 action 命中（OR 语义）

---

## 6. 配置方案与迁移

## 6.1 配置目标

- 可读、可扩展、可兼容现有 `assets/config/keybindings.txt`。
- 支持多 context、多绑定、触发语义与修饰键。

## 6.2 推荐格式（v2 文本）

建议新格式按行定义，字段如下：

`Context  Action  Device  Control  Trigger  Modifiers`

示例：

- `Gameplay MoveForward Keyboard W Held -`
- `Gameplay Sprint Keyboard LEFT_CONTROL Held -`
- `Gameplay Attack Mouse LEFT Pressed -`
- `UI       Confirm Keyboard ENTER Pressed -`
- `UI       Close   Keyboard ESCAPE Pressed -`

`Modifiers` 可用 `CTRL+SHIFT` 或 `-` 表示无。

## 6.3 兼容旧格式策略

旧格式：`Action BindingType KeyName`

迁移策略：
1. 优先尝试读取 v2；
2. 若行仅 3 列，则按旧格式解析；
3. 旧格式默认映射为：
   - `Context=Gameplay`
   - `Trigger=Held`（或按动作白名单定制，如 Attack 默认 Pressed）
   - `Modifiers=-`
4. 运行时可输出迁移日志（仅一次），便于后续转存为 v2。

---

## 7. 冲突检测与重绑定流程

## 7.1 冲突定义

在同一 context 内，若两个动作满足以下条件则视为冲突：
- `device` 相同
- `control` 相同
- `triggerType` 相同
- `modifiers` 等价（集合相同）

例外策略（可配置）：
- 允许白名单共享（如 `Menu` 与 `Pause`）
- 不同 context 可复用同物理键位

## 7.2 重绑定交互流程

1. 用户点击“重绑定某动作”进入捕获模式。
2. 界面显示“请按任意键/按钮，ESC 取消”。
3. 捕获第一个有效输入，构造候选 binding。
4. 执行冲突检测：
   - 无冲突：应用并保存
   - 有冲突：提示“替换/交换/取消”
5. 写回配置并热加载。

约束建议：
- 保留键位不可解绑（例如 `ESC` 可设为系统保留）。
- 至少保留一个可关闭 UI/Pause 的动作，防止软锁。

---

## 8. 分阶段迁移计划（M1-M3）

## M1：接入 Context 骨架（低风险）

范围：
- 引入 `InputContextManager`（仅管理 context 栈和 ActionMap 选择）。
- `Game` 管理 `Gameplay/UI/Pause` 切换与鼠标捕获切换。
- 先保留 `Player` 原始键位逻辑，不立即替换。

验收标准：
- 打开 UI 时，鼠标可释放；关闭 UI 后恢复捕获。
- context 栈行为正确（push/pop/switch）。

## M2：动作消费替换（核心）

范围：
- `Player` 从读取原始键位改为读取动作（Move/Jump/Sprint）。
- `ActionMap` 正式接入主循环。

验收标准：
- `Gameplay` 中移动、跳跃、冲刺行为与旧版一致。
- `UI` 激活时玩家不再响应移动动作。

## M3：Binding V2 与重绑定（增强）

范围：
- 上线 `{device, control, modifiers, triggerType}`。
- 配置兼容旧格式 + 新格式读写。
- 实现冲突检测与重绑定流程。

验收标准：
- 支持同动作多绑定（键鼠并存）。
- 冲突提示与替换策略可用。
- 配置热更新后即时生效。

---

## 9. 风险点与测试清单

## 9.1 主要风险

- 同帧切换导致误触发（切换前后动作重复消费）。
- 鼠标捕获切换后出现首帧大 delta。
- 旧配置迁移默认触发类型不合预期（Held/Pressed 语义差异）。
- 多 context 叠加时，优先级规则不一致。

## 9.2 测试清单

功能测试：
- Gameplay 中基础动作：移动、冲刺、跳跃、攻击。
- UI 打开/关闭：输入焦点切换正确。
- Pause 覆盖层：暂停时 gameplay 动作失效。

边界测试：
- 快速连按 `ESC`、`Inventory` 的状态抖动。
- 重绑为修饰键组合后是否可稳定触发。
- 冲突键位替换/交换/取消流程。

回归测试：
- 保证 `Menu/Close` 永远可达，避免卡死在 UI。
- 配置文件损坏时回退默认绑定。

---

## 10. 结论

推荐路线是：
- 先建立 `Game` 驱动的 context 状态机（M1），
- 再将 `Player` 输入消费切到动作层（M2），
- 最后升级 Binding V2 与重绑定能力（M3）。

该路线对现有架构侵入小，能快速解决 `gameplay/ui` 输入串扰问题，并为后续手柄、组合键与高级触发器打下统一基础。

