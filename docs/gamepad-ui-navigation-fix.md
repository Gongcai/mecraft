# 手柄 UI 导航与滚动修复总结

## 问题描述

### 问题 1：滑块消费所有方向键
手柄的方向键（D-Pad 上下左右）都被滑块（slider）消费，导致无法使用上下键切换焦点到其他控件。

**原因**：`UISlider::onInput` 处理 `NavigateUp/Down/Left/Right` 四个方向的命令来调整滑块数值。

### 问题 2：无法滚动页面
在设置界面等有滚动区域的地方，没有手柄按键可以控制页面滚动。

**原因**：`UIScrollArea` 只支持鼠标滚轮，没有手柄输入支持。

---

## 解决方案

### 修复 1：滑块只消费左右方向键

**修改文件**：`src/ui/widgets/UISlider.cpp`

**修改前**：
```cpp
if (event.command == UICommand::NavigateLeft || event.command == UICommand::NavigateDown) {
    // 减小数值
}
if (event.command == UICommand::NavigateRight || event.command == UICommand::NavigateUp) {
    // 增加数值
}
```

**修改后**：
```cpp
// Only consume Left/Right for slider adjustment, let Up/Down pass through for focus navigation
if (event.command == UICommand::NavigateLeft) {
    // 减小数值
}
if (event.command == UICommand::NavigateRight) {
    // 增加数值
}
// Up/Down 不处理，让它们传递给焦点管理系统
```

**效果**：
- ✅ D-Pad 左右：调整滑块数值
- ✅ D-Pad 上下：在控件间切换焦点（不再被滑块拦截）

---

### 修复 2：左摇杆控制页面滚动

**修改文件**：`src/ui/core/UIInputAdapter.cpp`

**实现思路**：
1. 在 UI 输入适配器中检测手柄左摇杆的 Y 轴
2. 将摇杆输入转换为虚拟滚轮事件
3. 复用现有的 UIScrollArea 滚动逻辑

**添加的代码**：
```cpp
// Handle left stick Y-axis for scrolling in UI context
if (snapshot.isGamepadConnected()) {
    const float leftStickY = snapshot.getGamepadAxis(GLFW_GAMEPAD_AXIS_LEFT_Y);
    constexpr float kStickScrollDeadZone = 0.2f;
    constexpr float kStickScrollMultiplier = 3.0f;

    if (std::abs(leftStickY) > kStickScrollDeadZone) {
        // Create a synthetic scroll event
        UIInputEvent scrollEvent = makeScrollEvent(snapshot);
        scrollEvent.scrollY = -leftStickY * kStickScrollMultiplier;
        mergeResult(routeResult.aggregate, renderer.routeUIInput(scrollEvent));
    }
}
```

**参数说明**：
- **死区（Dead Zone）**：`0.2f` - 摇杆小于此值时不触发滚动，避免漂移
- **滚动倍数**：`3.0f` - 控制滚动速度，可调整
- **方向反转**：`-leftStickY` - 摇杆向上是负值，但滚动向上是正值

**效果**：
- ✅ 左摇杆向上推：页面向上滚动
- ✅ 左摇杆向下推：页面向下滚动
- ✅ 滚动速度与摇杆推动幅度成正比
- ✅ 兼容鼠标滚轮（不影响）

---

## 修改的文件清单

### 代码文件
1. **`src/ui/widgets/UISlider.cpp`**
   - 移除对 `NavigateUp/Down` 的处理
   - 只保留 `NavigateLeft/Right` 用于滑块调整

2. **`src/ui/core/UIInputAdapter.cpp`**
   - 添加 `<cmath>` 头文件
   - 添加左摇杆滚动逻辑

### 配置文件
- 无需修改配置文件（使用现有绑定）

---

## 手柄 UI 控制完整布局

### 设置界面

| 输入 | 功能 |
|------|------|
| **左摇杆 上下** | **滚动页面** ✨ |
| **D-Pad 上下** | **切换焦点** ✨ |
| D-Pad 左右 | 调整滑块数值 |
| LB / RB | 切换 Tab |
| A | 确认 |
| B | 返回 |

### 其他 UI 界面

| 输入 | 功能 |
|------|------|
| 左摇杆 上下 | 滚动列表/页面 |
| D-Pad 上下左右 | 导航焦点 |
| LB / RB | 切换 Tab（如果有） |
| A | 确认 |
| B | 取消 |

---

## 滚动速度调整

如果左摇杆滚动太快或太慢，编辑 `src/ui/core/UIInputAdapter.cpp` 第 220 行：

```cpp
constexpr float kStickScrollMultiplier = 3.0f;  // 改为 2.0f 或 4.0f
```

**推荐值**：
- **慢速**：1.5 - 2.0
- **中速**：2.5 - 3.5（当前默认 3.0）
- **快速**：4.0 - 5.0

修改后需要重新编译。

---

## 死区调整

如果摇杆静止时页面仍在轻微滚动（漂移），增大死区：

```cpp
constexpr float kStickScrollDeadZone = 0.2f;  // 改为 0.25f 或 0.3f
```

---

## 技术细节

### 为什么不用轴绑定系统？

**方案对比**：

| 方案 | 优点 | 缺点 |
|------|------|------|
| 轴绑定到 Up/Down 动作 | 配置统一 | Up/Down 是离散动作，不适合模拟输入 |
| 直接读取摇杆并生成滚轮事件 | 滚动平滑，复用现有滚轮逻辑 | 硬编码在适配器中 |

**选择**：方案 2 - 因为：
1. 滚动是**连续**操作，需要模拟量
2. 复用 UIScrollArea 的现有滚轮处理逻辑
3. 不与导航系统冲突

### 事件合成（Synthetic Events）

```cpp
UIInputEvent scrollEvent = makeScrollEvent(snapshot);
scrollEvent.scrollY = -leftStickY * kStickScrollMultiplier;
```

这创建了一个"假的"滚轮事件，UI 系统认为是真实的鼠标滚轮输入。

**优点**：
- 不需要修改 UIScrollArea
- 所有滚动区域自动支持
- 与鼠标滚轮行为一致

---

## 测试验证

### 测试 1：滑块焦点导航

1. 进入设置界面
2. 用 D-Pad 下键导航到第一个滑块
3. **按 D-Pad 下键** → 应该移动到下一个控件 ✅
4. **不应该**停留在滑块上无法移动 ❌

### 测试 2：滑块数值调整

1. 导航到滑块（获得焦点）
2. **按 D-Pad 左键** → 数值减小 ✅
3. **按 D-Pad 右键** → 数值增加 ✅

### 测试 3：页面滚动

1. 进入设置界面（有多个选项需要滚动）
2. **左摇杆向上推** → 页面向上滚动 ✅
3. **左摇杆向下推** → 页面向下滚动 ✅
4. **松开摇杆** → 滚动停止 ✅

### 测试 4：不影响鼠标

1. 使用鼠标滚轮滚动页面 → 应该正常工作 ✅
2. 左摇杆和滚轮交替使用 → 都应该正常 ✅

---

## 已知限制

### 当前实现
1. **左摇杆滚动在所有 UI 界面生效**：包括背包、菜单等
2. **滚动速度固定**：倍数是常量，不随内容类型变化
3. **摇杆不适合精确滚动**：适合快速浏览，精确定位用 D-Pad

### 不是问题
1. ✅ 摇杆和 D-Pad 可以同时使用（不冲突）
2. ✅ 滑块有焦点时，左摇杆仍然滚动页面（不调整滑块）
3. ✅ 滚动到边界时自动停止（由 UIScrollArea 处理）

---

## 未来改进建议

### 短期
1. 添加滚动加速（推得越久滚得越快）
2. 支持横向滚动（右摇杆 X 轴）

### 中期
3. 滚动速度可配置（加入设置界面）
4. 不同 UI 界面独立的滚动速度
5. 滚动惯性（松开摇杆后缓慢停止）

### 长期
6. 智能滚动对齐（自动对齐到选项行）
7. 滚动时显示位置指示器
8. 触觉反馈（震动提示到达边界）

---

## 架构优势

### 1. 最小侵入
- 只修改 2 个文件
- UIScrollArea 逻辑完全不变
- 所有滚动区域自动支持手柄

### 2. 职责清晰
- **UIInputAdapter**：输入转换层（摇杆 → 滚轮事件）
- **UIScrollArea**：滚动逻辑层（处理滚轮事件）
- **UISlider**：控件层（只处理相关命令）

### 3. 易于调试
- 死区和倍数是常量，容易调整
- 滚动行为与鼠标一致，便于测试

---

## 编译状态

✅ 所有修改已编译通过，无错误

---

## 总结

通过两个针对性的修复：

1. **滑块只消费左右**：让 D-Pad 上下可以用于焦点导航
2. **左摇杆生成滚轮事件**：让手柄可以滚动页面

实现了完整的手柄 UI 控制：
- ✅ D-Pad：焦点导航 + 滑块调整
- ✅ 左摇杆：页面滚动
- ✅ LB/RB：Tab 切换
- ✅ A/B：确认/取消

用户现在可以完全用手柄操作 UI，无需切换到键鼠。🎮
