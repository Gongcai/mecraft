# 手柄 Tab 切换功能实现总结

## 问题描述

用户反馈：在设置界面等带 tabs 的 UI 中，无法通过手柄切换 Tab。最初尝试将 LB/RB 映射到 `Left/Right` 动作，但这些动作已被用于控制焦点移动和调整滑块，导致按 LB/RB 只会调整滑块而不能切换 Tab。

## 解决方案

添加专门的 `TabLeft` 和 `TabRight` 动作，独立于导航用的 `Left/Right`，专门用于 Tab 切换。

---

## 实现步骤

### 1. 添加新的 Action 枚举

**文件**: `src/player/ActionMap.h`

在 `Action` 枚举中添加：
```cpp
enum class Action {
    // ... 现有动作 ...
    ToggleViewMode,
    TabLeft,     // Switch to previous tab
    TabRight     // Switch to next tab
};
```

### 2. 更新 Action 字符串映射

**文件**: `src/player/ActionMap.cpp`

在 `stringToAction` 函数的 lookup 表中添加：
```cpp
{"TabLeft", Action::TabLeft},
{"TabRight", Action::TabRight}
```

### 3. 添加新的 UICommand

**文件**: `src/ui/core/UIInputEvent.h`

在 `UICommand` 枚举中添加：
```cpp
enum class UICommand {
    None,
    NavigateUp,
    NavigateDown,
    NavigateLeft,
    NavigateRight,
    Activate,
    Cancel,
    Home,
    End,
    TabLeft,      // 新增
    TabRight,     // 新增
};
```

### 4. 在输入适配器中路由新命令

**文件**: `src/ui/core/UIInputAdapter.cpp`

在 `routeInput` 函数末尾添加：
```cpp
if (context.isActionTriggered(Action::TabLeft)) {
    routeCommand(renderer, snapshot, routeResult, UICommand::TabLeft);
}
if (context.isActionTriggered(Action::TabRight)) {
    routeCommand(renderer, snapshot, routeResult, UICommand::TabRight);
}
```

### 5. 在 UITabControl 中处理命令

**文件**: `src/ui/widgets/UITabControl.cpp`

在 `onInput` 函数的 switch 语句中添加：
```cpp
case UIInputEventType::Command:
    if (event.command == UICommand::TabLeft) {
        const int newIndex = (m_activeIndex - 1 + static_cast<int>(m_tabs.size())) % static_cast<int>(m_tabs.size());
        setActiveTab(newIndex);
        return UIEventResult::Consumed;
    }
    if (event.command == UICommand::TabRight) {
        const int newIndex = (m_activeIndex + 1) % static_cast<int>(m_tabs.size());
        setActiveTab(newIndex);
        return UIEventResult::Consumed;
    }
    break;
```

**逻辑说明**：
- **TabLeft**: 切换到上一个 Tab，支持循环（从第一个切换到最后一个）
- **TabRight**: 切换到下一个 Tab，支持循环（从最后一个切换到第一个）
- 使用模运算确保索引始终有效

### 6. 更新配置文件

**文件**: `assets/config/keybindings.txt`

添加绑定：
```
# Tab navigation - Keyboard Tab / Gamepad bumpers
UI TabLeft Keyboard TAB Pressed SHIFT
UI TabLeft Gamepad LB Pressed -
UI TabRight Keyboard TAB Pressed -
UI TabRight Gamepad RB Pressed -
```

**配置说明**：
- **键盘**: Shift+Tab 切换到上一个，Tab 切换到下一个（标准 UI 惯例）
- **手柄**: LB 切换到上一个，RB 切换到下一个

---

## 与原有 Left/Right 动作的区别

| 动作 | 用途 | 触发场景 |
|------|------|----------|
| `Left` / `Right` | 焦点导航、滑块调整 | 在控件内部移动焦点、调整数值 |
| `TabLeft` / `TabRight` | Tab 切换 | 在不同 Tab 之间切换 |

**设计原理**：
- `Left/Right` 是控件级别的导航，焦点在单个控件内移动
- `TabLeft/TabRight` 是页面级别的导航，切换整个内容面板

这种分离保证了：
1. 在设置界面调整滑块时，不会误触 Tab 切换
2. 在任何有 Tab 的界面，LB/RB 都明确用于 Tab 切换
3. 符合用户的心智模型（LB/RB = 肩键 = 页面级操作）

---

## 修改的文件清单

### 代码文件
1. `src/player/ActionMap.h` - 添加 TabLeft/TabRight 枚举
2. `src/player/ActionMap.cpp` - 添加字符串映射
3. `src/ui/core/UIInputEvent.h` - 添加 UICommand 枚举
4. `src/ui/core/UIInputAdapter.cpp` - 路由新命令
5. `src/ui/widgets/UITabControl.cpp` - 处理 Tab 切换逻辑

### 配置文件
1. `assets/config/keybindings.txt` - 添加手柄和键盘绑定

---

## 测试验证

### 测试步骤

1. **进入设置界面**
   - 按手柄 Start 键打开菜单
   - 选择"设置"

2. **测试 Tab 切换**
   - 按 **RB** → 应该切换到下一个 Tab（阴影 → 后处理 → 体积渲染 → 升频）
   - 按 **LB** → 应该切换到上一个 Tab（升频 → 体积渲染 → 后处理 → 阴影）
   - 在最后一个 Tab 按 RB → 应该循环到第一个 Tab
   - 在第一个 Tab 按 LB → 应该循环到最后一个 Tab

3. **确认不影响滑块**
   - 在任意 Tab 中，用 D-Pad 导航到滑块控件
   - 按 **Left/Right** → 应该调整滑块数值，**不影响 Tab**
   - 按 **LB/RB** → 应该切换 Tab，**不影响滑块数值**

4. **键盘测试**
   - 按 **Tab** → 切换到下一个 Tab
   - 按 **Shift+Tab** → 切换到上一个 Tab

### 预期结果

✅ LB/RB 可以正确切换 Tab  
✅ 切换时支持循环（首尾相连）  
✅ 不影响 Left/Right 的焦点导航和滑块调整  
✅ 键盘 Tab/Shift+Tab 也能正常工作  

---

## 架构优势

### 1. 语义清晰
- `TabLeft/TabRight` 名称明确表达用途
- 不与现有的导航动作混淆

### 2. 易于扩展
- 如果未来需要其他页面级操作（如 PageUp/PageDown），可以继续添加新的 UICommand
- Tab 切换逻辑封装在 UITabControl 内部，其他 UI 组件不受影响

### 3. 配置灵活
- 用户可以通过配置文件重新绑定键位
- 手柄和键盘使用不同的按键，但映射到相同的动作

### 4. 符合惯例
- 键盘 Tab/Shift+Tab 是标准 UI 操作
- 手柄肩键（LB/RB）通常用于页面/标签切换（参考 Xbox/PlayStation 游戏惯例）

---

## 潜在改进

### 短期
1. 添加 Tab 切换音效反馈
2. 在 HUD 上显示当前 Tab 索引（如 "1/4"）

### 中期
3. 支持手柄左摇杆快速切换（左右滑动切换 Tab）
4. 添加 Tab 切换动画（淡入淡出或滑动效果）

### 长期
5. 支持 Tab 分组（一组 Tab 中再嵌套子 Tab）
6. 游戏内按键提示（底部显示 "LB/RB: 切换标签"）

---

## 编译状态

✅ 所有修改已编译通过，无错误

---

## 完整的手柄 UI 控制布局

### Settings 界面（和其他带 Tab 的界面）

| 按键 | 功能 |
|------|------|
| D-Pad 上下 | 在控件间导航（焦点移动） |
| D-Pad 左右 | 调整滑块数值 |
| **LB** | **切换到上一个 Tab** |
| **RB** | **切换到下一个 Tab** |
| A | 确认/激活 |
| B | 返回 |
| Start | 打开菜单（Gameplay 模式下） |

### 其他 UI 界面

| 按键 | 功能 |
|------|------|
| D-Pad | 导航 |
| A | 确认 |
| B | 取消 |
| LB/RB | 切换 Tab（如果有） |

---

## 总结

通过添加专门的 `TabLeft/TabRight` 动作，成功解决了手柄 Tab 切换与焦点导航/滑块调整的冲突问题。实现符合以下原则：

1. **单一职责**：每个动作有明确的用途
2. **不破坏现有功能**：Left/Right 继续用于导航和滑块
3. **符合用户预期**：LB/RB 肩键用于页面级操作是游戏惯例
4. **易于维护**：代码结构清晰，修改点集中

用户现在可以在设置界面流畅地使用手柄切换 Tab，同时保留对滑块和其他控件的完整控制能力。
