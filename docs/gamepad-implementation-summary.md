# Xbox 手柄支持实现总结

## 实现概述

已成功为 mecraft 输入系统添加 Xbox 手柄支持。实现遵循现有的输入系统架构，无缝集成到 ActionMap 和 InputContextManager 中。

## 修改的文件

### 1. `src/engine/input/InputManager.h`
**新增内容：**
- `InputSnapshot::GamepadState` 结构体：存储手柄状态
  - 15 个按键状态（按下、刚按下、刚释放、双击）
  - 6 个轴值（左/右摇杆 X/Y、左/右扳机）
  - 死区常量（摇杆 0.15，扳机 0.1）
- 新增查询方法：
  - `isGamepadConnected()`
  - `isGamepadButtonHeld/JustPressed/JustReleased/DoubleTapped()`
  - `getGamepadAxis()`
- 私有成员变量：手柄按键/轴的双缓冲和追踪

### 2. `src/engine/input/InputManager.cpp`
**新增功能：**
- 实现 InputSnapshot 的手柄查询方法
- `update()` 中添加手柄状态轮询：
  - 使用 `glfwJoystickPresent()` 和 `glfwJoystickIsGamepad()` 检测连接
  - 使用 `glfwGetGamepadState()` 读取按键和轴状态
  - 对摇杆轴应用死区处理
  - 将扳机值从 [-1,1] 归一化到 [0,1]
  - 支持按键双击检测
- `applyDeadZone()` 辅助函数：线性死区映射

### 3. `src/player/ActionMap.h`
**扩展枚举：**
- `NativeAxis` 添加手柄轴：
  - `GamepadLeftStickX/Y`
  - `GamepadRightStickX/Y`
  - `GamepadLeftTrigger/RightTrigger`
- `InputBinding.control` 注释更新：支持 `GLFW_GAMEPAD_BUTTON_*`
- 新增方法：`bindGamepadButton()`

### 4. `src/player/ActionMap.cpp`
**扩展实现：**
- `evaluateBinding()` 添加 `InputDevice::Gamepad` 分支
  - 支持所有触发类型（Pressed/Released/Held/DoubleTap）
  - 检查手柄连接状态
- `getAxisValue()` 添加手柄轴支持
  - 读取摇杆和扳机值
  - 支持反向（Invert）
- `stringToGamepadButton()` 辅助函数：字符串到按键码映射
  - 支持 Xbox 命名（A/B/X/Y/LB/RB/START/BACK/GUIDE）
  - 支持 PlayStation 别名（CROSS/CIRCLE/SQUARE/TRIANGLE/L3/R3）
- `stringToNativeAxis()` 扩展：支持手柄轴名称解析
- `loadFromFile()` 扩展：
  - Action 行支持 `Gamepad` 设备类型
  - Axis 行支持 `Gamepad` 设备类型

## 新增文件

### 1. `assets/config/gamepad_example.txt`
完整的手柄配置示例文件，包含：
- 所有按键绑定示例
- 摇杆轴绑定
- 详细的注释和说明

### 2. `docs/gamepad-support.md`
完整的技术文档，涵盖：
- 功能概述
- 按键和轴映射表
- 配置格式说明
- 代码使用示例
- 技术细节（死区、归一化）
- 兼容性说明
- 故障排除指南

## 技术特性

### 自动连接检测
- 每帧轮询手柄连接状态
- 断开连接时自动清零所有状态
- 支持热插拔（运行时连接/断开）

### 死区处理
- **摇杆死区**: 0.15 (15%)
  - 线性映射：将 [-1, -0.15] 和 [0.15, 1] 映射到 [-1, 1]
  - 中间 [-0.15, 0.15] 区域返回 0
- **扳机阈值**: 0.1 (10%)
  - 用于未来可能的数字化判定

### 扳机归一化
GLFW 报告扳机值为 [-1, 1]，但物理扳机是单向的。
自动重映射公式：`(rawValue + 1.0f) * 0.5f`
- 未按下：-1 → 0
- 完全按下：1 → 1

### 双击支持
- 按键双击检测与键盘/鼠标相同
- 默认超时：0.3 秒
- 追踪每个按键的上次按下时间

### 多绑定支持
- 同一动作可以同时绑定键盘和手柄
- ActionMap 会检查所有绑定（OR 语义）
- 例如：跳跃可以用空格键或 A 键触发

## 配置示例

### 基础动作绑定
```
Gameplay Jump Gamepad A Pressed -
Gameplay Sprint Gamepad LB Held -
Gameplay Attack Gamepad RB Pressed -
```

### 摇杆轴绑定
```
Axis Gameplay Vertical Gamepad LStickY
Axis Gameplay Horizontal Gamepad LStickX
Axis Gameplay LookX Gamepad RStickX
Axis Gameplay LookY Gamepad RStickY Invert
```

### 混合键鼠和手柄
```
# 跳跃：空格键或 A 键
Gameplay Jump Keyboard SPACE Held -
Gameplay Jump Gamepad A Pressed -

# 移动：WASD 或左摇杆
Axis Gameplay Vertical Keyboard W S
Axis Gameplay Vertical Gamepad LStickY
```

## 兼容性

### 支持的控制器
- Xbox 360 控制器
- Xbox One 控制器
- Xbox Series X|S 控制器
- 其他 XInput 兼容设备

### 平台支持
- **Windows**: 通过 XInput API
- **Linux**: 通过 xpad 驱动
- **macOS**: 通过原生驱动

GLFW 使用 SDL 游戏控制器数据库，因此大多数现代控制器都能工作。

## 使用方式

### 直接查询（低级）
```cpp
const InputSnapshot& input = inputManager.snapshot();

if (input.isGamepadConnected()) {
    if (input.isGamepadButtonJustPressed(GLFW_GAMEPAD_BUTTON_A)) {
        player.jump();
    }
    
    float moveX = input.getGamepadAxis(GLFW_GAMEPAD_AXIS_LEFT_X);
    player.move(moveX, 0);
}
```

### 通过 ActionMap（推荐）
```cpp
// 自动支持键盘/鼠标/手柄，取决于配置
if (contextManager.isActionTriggered(Action::Jump)) {
    player.jump();
}

float moveVertical = contextManager.getAxisValue(Axis::Vertical);
player.move(0, moveVertical);
```

## 测试建议

1. **基础连接测试**
   - 连接手柄后启动游戏
   - 检查 `isGamepadConnected()` 返回 true
   - 测试断开/重连

2. **按键测试**
   - 测试所有面按钮（A/B/X/Y）
   - 测试肩键（LB/RB）
   - 测试系统按钮（START/BACK）
   - 测试摇杆按下（L3/R3）
   - 测试方向键

3. **摇杆测试**
   - 测试左摇杆移动
   - 测试右摇杆视角
   - 验证死区是否足够（无漂移）
   - 测试反向（Invert）设置

4. **扳机测试**
   - 测试模拟量读取（0-1 范围）
   - 测试扳机作为按键使用

5. **混合输入测试**
   - 同时使用键盘和手柄
   - 验证两者都能触发相同动作
   - 测试上下文切换（Gameplay/UI）

## 后续改进建议

### 短期
1. **多手柄支持**: 当前只支持第一个手柄（GLFW_JOYSTICK_1）
2. **可调死区**: 允许用户在设置中调整死区
3. **振动支持**: 添加震动反馈（需要 GLFW 3.4+）

### 中期
4. **扳机数字化**: 支持将扳机作为数字按键（超过阈值触发）
5. **组合键**: 支持手柄按键修饰符（如 LB + A）
6. **轴反向死区**: 为扳机单独配置死区

### 长期
7. **自定义映射 UI**: 游戏内按键重绑界面
8. **配置文件切换**: 支持多个预设配置
9. **手柄识别**: 显示连接的手柄名称

## 性能影响

- **CPU 开销**: 极小（每帧一次 GLFW API 调用）
- **内存开销**: 约 200 字节（状态追踪数组）
- **无额外线程**: 完全同步轮询

## 总结

Xbox 手柄支持已完全集成到现有输入系统中，无需修改游戏逻辑代码。所有使用 `InputContextManager` 和 `ActionMap` 的代码自动获得手柄支持，只需在配置文件中添加手柄绑定即可。

实现遵循以下原则：
- ✅ 最小化侵入性：只修改输入层，不影响业务逻辑
- ✅ 架构一致性：与现有键盘/鼠标处理保持一致
- ✅ 向后兼容：不破坏现有配置和代码
- ✅ 扩展性：易于添加新设备类型
- ✅ 文档完整：代码注释为英文，用户文档为中文

编译成功，无错误，仅有少量无关警告。
