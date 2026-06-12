# 手柄输入调试指南

## 问题排查

你遇到的问题：
1. ✅ **无法移动** - 已修复：配置文件格式错误（缺少占位符 `-`）
2. ✅ **视角只能上下且颠倒** - 已修复：去除了 Y 轴的 Invert

## 已修复的配置

### 修复前（错误）：
```
Axis Gameplay Vertical Gamepad LStickY        # ❌ 缺少占位符
Axis Gameplay LookY Gamepad RStickY Invert    # ❌ 反向错误
```

### 修复后（正确）：
```
Axis Gameplay Vertical Gamepad LStickY -      # ✅ 添加占位符
Axis Gameplay LookY Gamepad RStickY -         # ✅ 移除 Invert
```

## 配置格式说明

手柄轴绑定必须是 **6 或 7 列**：

```
Axis Context AxisName Device AxisControl Placeholder [Invert]
 1     2        3       4         5           6         7(可选)
```

示例：
```
Axis Gameplay Vertical Gamepad LStickY -
Axis Gameplay LookX Gamepad RStickX -
Axis Gameplay LookY Gamepad RStickY - Invert
```

## 添加调试代码（可选）

如果问题仍然存在，可以在代码中添加调试输出：

### 1. 在 `InputManager.cpp` 的 `update()` 函数末尾添加：

```cpp
// 在 InputManager::update() 最后添加
#ifdef MECRAFT_DEBUG
if (m_gamepadConnected && m_handle != nullptr) {
    static int frameCounter = 0;
    if (++frameCounter % 60 == 0) {  // 每秒打印一次
        std::cout << "[Gamepad Debug] Connected: " << m_gamepadConnected << std::endl;
        std::cout << "  LStickX: " << m_gamepadAxes[GLFW_GAMEPAD_AXIS_LEFT_X] << std::endl;
        std::cout << "  LStickY: " << m_gamepadAxes[GLFW_GAMEPAD_AXIS_LEFT_Y] << std::endl;
        std::cout << "  RStickX: " << m_gamepadAxes[GLFW_GAMEPAD_AXIS_RIGHT_X] << std::endl;
        std::cout << "  RStickY: " << m_gamepadAxes[GLFW_GAMEPAD_AXIS_RIGHT_Y] << std::endl;
        
        // 按键状态
        if (m_gamepadButtons[GLFW_GAMEPAD_BUTTON_A]) {
            std::cout << "  A button pressed" << std::endl;
        }
    }
}
#endif
```

### 2. 在 `ActionMap.cpp` 的 `getAxisValue()` 开始处添加：

```cpp
float ActionMap::getAxisValue(Axis axis, InputContextType context, const InputSnapshot &input) const {
    auto it = m_axisBindings.find(axis);
    
    #ifdef MECRAFT_DEBUG
    static int debugCounter = 0;
    if (++debugCounter % 60 == 0 && axis == Axis::Vertical) {
        std::cout << "[ActionMap Debug] Vertical axis bindings: " 
                  << (it != m_axisBindings.end() ? it->second.size() : 0) << std::endl;
    }
    #endif
    
    if (it != m_axisBindings.end()) {
        // ... 原有代码
```

## 测试步骤

### 1. 重新启动游戏
配置文件已修复，需要重启游戏加载新配置。

### 2. 测试左摇杆（移动）
- 向前推（↑）→ 角色应该前进
- 向后拉（↓）→ 角色应该后退
- 向左推（←）→ 角色应该左移
- 向右推（→）→ 角色应该右移

### 3. 测试右摇杆（视角）
- 向左推（←）→ 视角向左转
- 向右推（→）→ 视角向右转
- 向上推（↑）→ 视角向上看
- 向下拉（↓）→ 视角向下看

### 4. 如果视角反向
如果向上推摇杆，视角向下看（反了），编辑配置文件：

```
Axis Gameplay LookY Gamepad RStickY - Invert
```

添加 `Invert` 关键字。

### 5. 如果 X 轴不工作
可能需要添加 Invert：

```
Axis Gameplay LookX Gamepad RStickX - Invert
```

## 常见问题

### 问题 1：移动和视角都不工作
**原因**：配置未加载或格式错误
**解决**：
1. 确认 `keybindings.txt` 格式正确（每个 Axis 行都有 6 列）
2. 检查控制台是否有加载错误
3. 确认手柄已连接

### 问题 2：只有一个轴工作
**原因**：配置缺失或格式错误
**解决**：确保每个轴都有独立的绑定行：

```
Axis Gameplay Vertical Gamepad LStickY -      # Y轴（前后）
Axis Gameplay Horizontal Gamepad LStickX -    # X轴（左右）
Axis Gameplay LookX Gamepad RStickX -         # 视角X
Axis Gameplay LookY Gamepad RStickY -         # 视角Y
```

### 问题 3：摇杆灵敏度太低或太高
**调整死区**：

编辑 `src/engine/input/InputManager.h`：

```cpp
struct GamepadState {
    // ...
    static constexpr float kStickDeadZone = 0.15f;  // 改为 0.1 或 0.2
```

### 问题 4：视角移动速度不对
这不是输入系统问题，而是游戏逻辑中的灵敏度设置。
手柄摇杆返回 [-1, 1] 的归一化值，可能需要在相机控制代码中调整乘数。

## 完整的手柄轴配置（参考）

```
# 移动轴 - 左摇杆
Axis Gameplay Vertical Gamepad LStickY -
Axis Gameplay Horizontal Gamepad LStickX -

# 视角轴 - 右摇杆（根据手感调整是否 Invert）
Axis Gameplay LookX Gamepad RStickX -
Axis Gameplay LookY Gamepad RStickY -

# 如果需要反向，添加 Invert：
# Axis Gameplay LookY Gamepad RStickY - Invert
```

## 验证配置是否生效

在游戏启动后，检查控制台输出是否有：
```
[ActionMap Debug] Loaded Axis binding: Gameplay/Vertical/Gamepad
[ActionMap Debug] Loaded Axis binding: Gameplay/Horizontal/Gamepad
```

如果没有这些输出，说明配置未加载成功。

## 下一步

1. **重启游戏** - 配置文件已修复
2. **测试移动和视角** - 应该都能工作了
3. **如果视角反向** - 添加 `Invert` 到 LookY 配置行
4. **如果仍有问题** - 添加上面的调试代码，查看实际输入值

祝测试顺利！
