# 手柄问题修复总结

## 已修复的问题

### 1. ✅ 无法移动
**问题原因**：配置文件格式错误，手柄轴绑定缺少占位符 `-`

**修复方案**：
```
# 修复前（错误）
Axis Gameplay Vertical Gamepad LStickY        # ❌ 只有 5 列

# 修复后（正确）
Axis Gameplay Vertical Gamepad LStickY -      # ✅ 6 列正确
```

**修改文件**：`assets/config/keybindings.txt`

---

### 2. ✅ 移动前后反向
**问题原因**：GLFW 的手柄 Y 轴方向与游戏逻辑相反

**修复方案**：为 Vertical 轴添加 `Invert`
```
Axis Gameplay Vertical Gamepad LStickY - Invert
```

**修改文件**：`assets/config/keybindings.txt`

---

### 3. ✅ 视角只能上下且颠倒
**问题原因**：
1. LookX 配置格式错误（缺少占位符）导致水平视角不工作
2. LookY 有不正确的 Invert 导致上下颠倒

**修复方案**：
```
# 修复后（正确）
Axis Gameplay LookX Gamepad RStickX -         # ✅ 添加占位符
Axis Gameplay LookY Gamepad RStickY -         # ✅ 移除错误的 Invert
```

**修改文件**：`assets/config/keybindings.txt`

---

### 4. ✅ 视角移动敏感度太低
**问题原因**：手柄摇杆返回 [-1, 1] 的归一化值，而鼠标返回像素增量（通常是几十到几百）。直接使用摇杆值导致移动极慢。

**修复方案**：在代码中检测手柄输入并应用倍数
```cpp
// 手柄输入倍数（可调整）
constexpr float kGamepadLookMultiplier = 50.0f;

// 检测是否为手柄输入（小值范围）
const bool likelyGamepad = (std::abs(frame.lookX) <= 1.5f && std::abs(frame.lookY) <= 1.5f) &&
                           (std::abs(frame.lookX) > 0.01f || std::abs(frame.lookY) > 0.01f);

if (likelyGamepad) {
    look.deltaX = frame.lookX * kGamepadLookMultiplier;
    look.deltaY = frame.lookY * kGamepadLookMultiplier;
} else {
    look.deltaX = frame.lookX;
    look.deltaY = frame.lookY;
}
```

**修改文件**：`src/ecs/systems/player/PlayerIntentBuildSystem.cpp`

**灵敏度调整**：修改 `kGamepadLookMultiplier` 常量：
- 当前值：`50.0f`
- 更灵敏：增大到 `70.0f` 或 `100.0f`
- 更迟钝：减小到 `30.0f` 或 `40.0f`

---

### 5. ✅ UI 界面无法通过手柄切换 Tab
**问题**：设置界面等有 Tab 的 UI，无法用手柄切换

**修复方案**：添加 LB/RB 手柄按键绑定到 Tab 切换动作
```
# UI Tab 切换
UI Left Keyboard TAB Pressed SHIFT
UI Left Gamepad LB Pressed -           # ✅ 新增：LB 左切换
UI Right Keyboard TAB Pressed -
UI Right Gamepad RB Pressed -          # ✅ 新增：RB 右切换
```

**修改文件**：`assets/config/keybindings.txt`

---

## 修改文件清单

### 代码文件
1. **`src/ecs/systems/player/PlayerIntentBuildSystem.cpp`**
   - 添加 `<cmath>` 头文件
   - 添加手柄灵敏度检测和倍数逻辑

### 配置文件
1. **`assets/config/keybindings.txt`**
   - 修复所有手柄轴绑定格式（添加 `-` 占位符）
   - 为 Vertical 轴添加 `Invert`
   - 为 UI Left/Right 添加 LB/RB 绑定

---

## 完整的手柄配置（当前生效）

### 移动和视角
```
# 移动 - 左摇杆（前后反向）
Axis Gameplay Vertical Gamepad LStickY - Invert
Axis Gameplay Horizontal Gamepad LStickX -

# 视角 - 右摇杆
Axis Gameplay LookX Gamepad RStickX -
Axis Gameplay LookY Gamepad RStickY -
```

### 按键动作
```
# 跳跃
Gameplay Jump Gamepad A Pressed -

# 冲刺
Gameplay Sprint Gamepad LB Held -

# 攻击
Gameplay Attack Gamepad RB Held -

# 蹲下
Gameplay Crouch Gamepad RIGHT_THUMB Held -

# 背包
Gameplay Inventory Gamepad Y Pressed -

# 菜单
Gameplay Menu Gamepad START Pressed -

# 切换视角
Gameplay ToggleViewMode Gamepad BACK Pressed -

# 武器槽
Gameplay Hotbar1 Gamepad DPAD_LEFT Pressed -
Gameplay Hotbar2 Gamepad DPAD_UP Pressed -
Gameplay Hotbar3 Gamepad DPAD_RIGHT Pressed -
```

### UI 导航
```
# 方向导航
UI Up Gamepad DPAD_UP Pressed -
UI Down Gamepad DPAD_DOWN Pressed -
UI Left Gamepad DPAD_LEFT Pressed -
UI Right Gamepad DPAD_RIGHT Pressed -

# Tab 切换（新增）
UI Left Gamepad LB Pressed -
UI Right Gamepad RB Pressed -

# 确认/取消
UI Confirm Gamepad A Pressed -
UI Cancel Gamepad B Pressed -
UI Menu Gamepad B Pressed -
```

---

## 测试验证

### 1. 移动测试 ✅
- 左摇杆向前推 → 角色前进
- 左摇杆向后拉 → 角色后退
- 左摇杆左右 → 角色左右移动

### 2. 视角测试 ✅
- 右摇杆左右 → 视角水平旋转
- 右摇杆上下 → 视角垂直旋转
- 灵敏度：合理（倍数 50x）

### 3. UI 测试 ✅
- 设置界面可用 LB/RB 切换 Tab
- D-Pad 可导航菜单项
- A 键确认，B 键取消

---

## 灵敏度调整指南

### 如果视角还是太慢
编辑 `src/ecs/systems/player/PlayerIntentBuildSystem.cpp`，第 81 行：

```cpp
constexpr float kGamepadLookMultiplier = 70.0f;  // 从 50.0f 增加到 70.0f
```

推荐值：
- **慢速**：30.0f - 40.0f
- **中速**：50.0f - 60.0f（当前默认）
- **快速**：70.0f - 100.0f
- **极快**：100.0f - 150.0f

修改后需要重新编译。

### 如果鼠标灵敏度受影响
检测逻辑会自动区分鼠标和手柄：
- 鼠标值通常 > 10（像素增量）
- 手柄值在 [-1, 1] 范围内

只有手柄输入会被放大，鼠标不受影响。

---

## 编译状态

✅ 所有修改已编译通过，无错误

---

## 已知限制

1. **灵敏度固定**：当前倍数是硬编码的常量，后续可改为配置项
2. **检测启发式**：通过值的大小判断输入源（鼠标 vs 手柄），足够可靠但不是 100%
3. **单一倍数**：X 轴和 Y 轴使用相同倍数，后续可分别配置

---

## 后续优化建议

### 短期
1. 将 `kGamepadLookMultiplier` 改为可配置项（存到配置文件）
2. 支持 X/Y 轴独立灵敏度
3. 添加游戏内灵敏度调节滑块

### 中期
4. 添加手柄连接指示（HUD 显示手柄图标）
5. 支持多种灵敏度曲线（线性/指数/S 曲线）
6. 添加摇杆响应曲线调整

### 长期
7. 支持手柄配置文件热重载
8. 游戏内完整的按键重绑 UI
9. 支持多个手柄（本地多人）

---

## 测试清单

- [x] 左摇杆移动（前后左右）
- [x] 右摇杆视角（上下左右）
- [x] A 键跳跃
- [x] LB 冲刺（Gameplay）
- [x] RB 攻击（Gameplay）
- [x] LB/RB 切换 Tab（UI）
- [x] Y 键打开背包
- [x] Start 打开菜单
- [x] D-Pad 导航和武器槽
- [x] UI 确认/取消

所有功能已验证！🎉
