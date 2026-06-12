# Xbox Gamepad Support

## Overview

The input system now supports Xbox controllers (and compatible gamepads) through GLFW's gamepad API. The system automatically detects connected gamepads and applies dead zones to analog inputs.

## Features

- **Button Input**: All 15 standard gamepad buttons (A/B/X/Y, bumpers, triggers, D-pad, etc.)
- **Analog Sticks**: Left and right stick with automatic dead zone (0.15)
- **Triggers**: Left and right triggers normalized to [0, 1] range
- **Trigger Types**: Supports Pressed, Released, Held, and DoubleTap for buttons
- **Hot-plug Support**: Automatically detects when gamepad is connected/disconnected

## Button Mapping

### Face Buttons
- `A` - South button (Xbox A / PlayStation Cross)
- `B` - East button (Xbox B / PlayStation Circle)
- `X` - West button (Xbox X / PlayStation Square)
- `Y` - North button (Xbox Y / PlayStation Triangle)

### Shoulder Buttons
- `LB` / `LEFT_BUMPER` - Left bumper (L1)
- `RB` / `RIGHT_BUMPER` - Right bumper (R1)

### System Buttons
- `START` - Start button
- `BACK` / `SELECT` - Back/Select button
- `GUIDE` / `HOME` - Xbox/PlayStation button

### Stick Buttons
- `LEFT_THUMB` / `L3` - Left stick press
- `RIGHT_THUMB` / `R3` - Right stick press

### D-Pad
- `DPAD_UP`, `DPAD_DOWN`, `DPAD_LEFT`, `DPAD_RIGHT`

## Axis Mapping

### Analog Sticks
- `LStickX` / `LeftStickX` - Left stick horizontal (-1 = left, +1 = right)
- `LStickY` / `LeftStickY` - Left stick vertical (-1 = down, +1 = up)
- `RStickX` / `RightStickX` - Right stick horizontal
- `RStickY` / `RightStickY` - Right stick vertical

### Triggers
- `LT` / `LeftTrigger` - Left trigger (0 = not pressed, 1 = fully pressed)
- `RT` / `RightTrigger` - Right trigger (0 = not pressed, 1 = fully pressed)

## Configuration Format

### Action Bindings
```
Context Action Device Control Trigger Modifiers
```

Example:
```
Gameplay Jump Gamepad A Pressed -
Gameplay Sprint Gamepad LB Held -
Gameplay Attack Gamepad RB Pressed -
```

### Axis Bindings
```
Axis Context AxisName Gamepad AxisControl [Invert]
```

Example:
```
Axis Gameplay Vertical Gamepad LStickY
Axis Gameplay Horizontal Gamepad LStickX
Axis Gameplay LookX Gamepad RStickX
Axis Gameplay LookY Gamepad RStickY Invert
```

## Usage in Code

### Query Button State
```cpp
const InputSnapshot& input = inputManager.snapshot();

// Check if gamepad is connected
if (input.isGamepadConnected()) {
    // Check button state
    if (input.isGamepadButtonJustPressed(GLFW_GAMEPAD_BUTTON_A)) {
        // Jump
    }
    
    if (input.isGamepadButtonHeld(GLFW_GAMEPAD_BUTTON_LEFT_BUMPER)) {
        // Sprint
    }
}
```

### Query Axis State
```cpp
// Get stick value (with dead zone applied)
float moveX = input.getGamepadAxis(GLFW_GAMEPAD_AXIS_LEFT_X);
float moveY = input.getGamepadAxis(GLFW_GAMEPAD_AXIS_LEFT_Y);

// Get trigger value (normalized to [0, 1])
float rightTrigger = input.getGamepadAxis(GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER);
```

### Using ActionMap
```cpp
// Through InputContextManager (recommended)
if (contextManager.isActionTriggered(Action::Jump)) {
    // Works for keyboard, mouse, or gamepad
}

float moveVertical = contextManager.getAxisValue(Axis::Vertical);
// Returns keyboard WASD or gamepad left stick, whichever is active
```

## Technical Details

### Dead Zone
- **Stick Dead Zone**: 0.15 (15% of full range)
- **Trigger Threshold**: 0.1 (10% of full range)
- Applied in `InputManager::applyDeadZone()`

Dead zones prevent stick drift and make controls feel more precise.

### Trigger Normalization
GLFW reports trigger values in range [-1, 1], but triggers are unipolar (0 when released).
The system automatically remaps to [0, 1]:
```cpp
normalizedValue = (rawValue + 1.0f) * 0.5f;
```

### Joystick Selection
Currently supports the first connected gamepad (GLFW_JOYSTICK_1).

### Connection Detection
The system polls `glfwJoystickPresent()` and `glfwJoystickIsGamepad()` every frame.
When disconnected, all button and axis values reset to zero.

## Example Configuration

See `assets/config/gamepad_example.txt` for a complete example configuration.

To add gamepad bindings to your main config, append the gamepad lines to `keybindings.txt`:

```
# Existing keyboard/mouse bindings
Gameplay Jump Keyboard SPACE Held -
Gameplay Jump Gamepad A Pressed -

# Both keyboard and gamepad will work simultaneously
Axis Gameplay Vertical Keyboard W S
Axis Gameplay Vertical Gamepad LStickY
```

## Testing

1. Connect an Xbox controller (or compatible gamepad) via USB or Bluetooth
2. Launch the game
3. The gamepad should be automatically detected
4. Test button inputs and analog stick movement

## Compatibility

- **Windows**: Xbox 360/One/Series controllers, generic XInput devices
- **Linux**: Xbox controllers via xpad driver
- **macOS**: Xbox controllers via native driver

GLFW's gamepad API uses SDL's game controller database, so most modern controllers work.

## Troubleshooting

### Gamepad Not Detected
- Ensure the controller is properly connected
- Check device manager (Windows) or `lsusb` (Linux)
- Try a different USB port

### Buttons Not Responding
- Verify the gamepad is detected: `input.isGamepadConnected()`
- Check button mappings in configuration file
- Enable debug logging to see raw input values

### Stick Drift
- Increase dead zone in `InputSnapshot::GamepadState::kStickDeadZone`
- Default is 0.15 (15%), can be increased to 0.2 or higher
