//
// Created by Caiwe on 2026/3/22.
//

#include "ActionMap.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <map>

// Helper: Get active modifiers from snapshot
static int getActiveModifiers(const InputSnapshot& input) {
    int mods = 0;
    if (input.isKeyHeld(GLFW_KEY_LEFT_SHIFT) || input.isKeyHeld(GLFW_KEY_RIGHT_SHIFT)) mods |= GLFW_MOD_SHIFT;
    if (input.isKeyHeld(GLFW_KEY_LEFT_CONTROL) || input.isKeyHeld(GLFW_KEY_RIGHT_CONTROL)) mods |= GLFW_MOD_CONTROL;
    if (input.isKeyHeld(GLFW_KEY_LEFT_ALT) || input.isKeyHeld(GLFW_KEY_RIGHT_ALT)) mods |= GLFW_MOD_ALT;
    if (input.isKeyHeld(GLFW_KEY_LEFT_SUPER) || input.isKeyHeld(GLFW_KEY_RIGHT_SUPER)) mods |= GLFW_MOD_SUPER;
    return mods;
}

void ActionMap::bindKey(Action action, int keyCode, InputContextType context) {
    bindKey(action, keyCode, TriggerType::Held, context);
}

void ActionMap::bindAxisKey(Axis axis, int positiveKeyCode, int negativeKeyCode, InputContextType context, bool invert) {
    AxisBinding binding;
    binding.context = context;
    binding.positiveKey = positiveKeyCode;
    binding.negativeKey = negativeKeyCode;
    binding.invert = invert;
    m_axisBindings[axis].push_back(binding);
}

void ActionMap::bindNativeAxis(Axis axis, NativeAxis native, InputContextType context, bool invert) {
    AxisBinding binding;
    binding.context = context;
    binding.nativeAxis = native;
    binding.invert = invert;
    m_axisBindings[axis].push_back(binding);
}

void ActionMap::bindKey(Action action, int keyCode, TriggerType trigger, InputContextType context) {
    InputBinding binding;
    binding.context = context;
    binding.device = InputDevice::Keyboard;
    binding.control = keyCode;
    binding.trigger = trigger;
    binding.modifiers = 0;
    m_bindings[action].push_back(binding);
}

void ActionMap::bindMouseButton(Action action, int buttonCode, InputContextType context) {
    InputBinding binding;
    binding.context = context;
    binding.device = InputDevice::Mouse;
    binding.control = buttonCode;
    binding.trigger = TriggerType::Pressed; // Buttons usually trigger on press
    binding.modifiers = 0;
    m_bindings[action].push_back(binding);
}

void ActionMap::clearAll() {
    m_bindings.clear();
}

bool ActionMap::evaluateBinding(const InputBinding& binding, const InputSnapshot& input) const {
    // 1. Check modifiers match
    int currentMods = getActiveModifiers(input);

    // Strict check: current modifiers must exactly call for required modifiers?
    // Design doc: "required: 必须按下的修饰键集合". Usually this means (current & required) == required.
    // It also mentions "forbidden". Since we don't have forbidden field yet, we stick to required.
    // For now: (currentMods & binding.modifiers) == binding.modifiers
    if ((currentMods & binding.modifiers) != binding.modifiers) {
        return false;
    }

    // 2. Check control state
    bool active = false;
    if (binding.device == InputDevice::Keyboard) {
        switch (binding.trigger) {
            case TriggerType::Pressed:
                active = input.isKeyJustPressed(binding.control);
                break;
            case TriggerType::Released:
                active = input.isKeyJustReleased(binding.control);
                break;
            case TriggerType::Held:
                active = input.isKeyHeld(binding.control);
                break;
        }
    } else if (binding.device == InputDevice::Mouse) {
        switch (binding.trigger) {
            case TriggerType::Pressed:
                active = input.isMouseButtonJustPressed(binding.control);
                break;
            case TriggerType::Released:
                active = input.isMouseButtonJustReleased(binding.control);
                break;
            case TriggerType::Held:
                active = input.isMouseButtonHeld(binding.control);
                break;
        }
    } else if (binding.device == InputDevice::Scroll) {
        // control: 1 = scroll up, -1 = scroll down
        if (binding.control == 1 && input.scrollDelta > 0.0) {
            active = true;
        } else if (binding.control == -1 && input.scrollDelta < 0.0) {
            active = true;
        }
    }

    return active;
}

bool ActionMap::isActionTriggered(Action action, InputContextType context, const InputSnapshot& input) const {
    auto it = m_bindings.find(action);
    if (it == m_bindings.end()) return false;

    for (const auto& binding : it->second) {
        if (binding.context == context) {
            if (evaluateBinding(binding, input)) {
                return true;
            }
        }
    }
    return false;
}

float ActionMap::getAxisValue(Axis axis, InputContextType context, const InputSnapshot &input) const {
    auto it = m_axisBindings.find(axis);
    if (it != m_axisBindings.end()) {
        for (const auto& binding : it->second) {
            if (binding.context == context) {
                float value = 0.0f;

                if (binding.nativeAxis == NativeAxis::MouseX) {
                    value = static_cast<float>(input.mouseDelta.x);
                } else if (binding.nativeAxis == NativeAxis::MouseY) {
                    value = static_cast<float>(input.mouseDelta.y);
                } else {
                    if (input.isKeyHeld(binding.positiveKey)) value += 1.0f;
                    if (input.isKeyHeld(binding.negativeKey)) value -= 1.0f;
                }

                if (value != 0.0f) {
                    return binding.invert ? -value : value;
                }
            }
        }
    }
    return 0.0f;
}

// Helper to map string to Action
static Action stringToAction(const std::string& str) {
    static const std::map<std::string, Action> lookup = {
        {"Jump", Action::Jump},
        {"Sprint", Action::Sprint},
        {"Crouch", Action::Crouch},
        {"Attack", Action::Attack},
        {"UseItem", Action::UseItem},
        {"Inventory", Action::Inventory},
        {"Menu", Action::Menu},
        {"Hotbar1", Action::Hotbar1},
        {"Hotbar2", Action::Hotbar2},
        {"Hotbar3", Action::Hotbar3},
        {"Hotbar4", Action::Hotbar4},
        {"Hotbar5", Action::Hotbar5},
        {"Hotbar6", Action::Hotbar6},
        {"Hotbar7", Action::Hotbar7},
        {"Hotbar8", Action::Hotbar8},
        {"Hotbar9", Action::Hotbar9},
        {"HotbarScrollUp", Action::HotbarScrollUp},
        {"HotbarScrollDown", Action::HotbarScrollDown},
        {"Confirm", Action::Confirm},
        {"Cancel", Action::Cancel},
        {"Up", Action::Up},
        {"Down", Action::Down},
        {"Left", Action::Left},
        {"Right", Action::Right},
        {"Backspace",Action::Backspace},
        {"OpenCommand", Action::OpenCommand}
    };
    auto it = lookup.find(str);
    if (it != lookup.end()) return it->second;
    // Fallback or warning
    return Action::Menu;
}
// 在现有的 stringToAction 下面新增
static Axis stringToAxis(const std::string& str) {
    if (str == "Vertical") return Axis::Vertical;
    if (str == "Horizontal") return Axis::Horizontal;
    if (str == "LookX") return Axis::LookX;
    if (str == "LookY") return Axis::LookY;
    return Axis::Vertical; // 或者随便选个默认
}

// Helper for string to key code (Simplified)
static int stringToKey(const std::string& str) {
    if (str.length() == 1) {
        char c = std::toupper(str[0]);
        if (c >= 'A' && c <= 'Z') return GLFW_KEY_A + (c - 'A');
        if (c >= '0' && c <= '9') return GLFW_KEY_0 + (c - '0');
    }
    if (str == "SPACE") return GLFW_KEY_SPACE;
    if (str == "ESCAPE") return GLFW_KEY_ESCAPE;
    if (str == "ENTER") return GLFW_KEY_ENTER;
    if (str == "KP_ENTER") return GLFW_KEY_KP_ENTER;
    if (str == "LEFT_SHIFT") return GLFW_KEY_LEFT_SHIFT;
    if (str == "LEFT_CONTROL") return GLFW_KEY_LEFT_CONTROL;
    if (str == "TAB") return GLFW_KEY_TAB;
    if (str == "W") return GLFW_KEY_W; // specific override if length==1 fails
    if (str == "BACKSPACE") return GLFW_KEY_BACKSPACE;
    if (str == "SLASH") return GLFW_KEY_SLASH;

    // ... add more as needed
    return GLFW_KEY_UNKNOWN;
}

static InputContextType stringToContext(const std::string& str) {
    if (str == "UI") return InputContextType::UI;
    if (str == "Pause") return InputContextType::Pause;
    return InputContextType::Gameplay;
}

static TriggerType stringToTrigger(const std::string& str) {
    if (str == "Pressed") return TriggerType::Pressed;
    if (str == "Released") return TriggerType::Released;
    return TriggerType::Held;
}

// 兼容旧格式：Action BindingType KeyName
// 新格式：Context Action Device Control Trigger Modifiers
void ActionMap::loadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
#ifndef NDEBUG
        std::cerr << "Failed to open keybindings file: " << path << std::endl;
#endif
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::stringstream ss(line);
        std::string token;
        std::vector<std::string> tokens;
        while (ss >> token) tokens.push_back(token);

        if (tokens.empty()) continue;

        // 如果明确标注这是虚拟轴的配置
        // 格式: Axis Context AxisName Device PositiveKey/NativeAxis NegativeKey/Ignored [Invert]
        // 例如: Axis Gameplay Vertical Keyboard W S
        if (tokens[0] == "Axis" && tokens.size() >= 6) {
            InputContextType ctx = stringToContext(tokens[1]);
            Axis axis = stringToAxis(tokens[2]);

            bool invert = (tokens.size() >= 7 && tokens[6] == "Invert");

            if (tokens[3] == "Keyboard") {
                int posKey = stringToKey(tokens[4]);
                int negKey = stringToKey(tokens[5]);
                bindAxisKey(axis, posKey, negKey, ctx, invert);
            }
            else if (tokens[3] == "Mouse") {
                NativeAxis nAxis = NativeAxis::None;
                if (tokens[4] == "X") nAxis = NativeAxis::MouseX;
                else if (tokens[4] == "Y") nAxis = NativeAxis::MouseY;

                bindNativeAxis(axis, nAxis, ctx, invert);
            }
            continue;
        }

        // 以往 Action 配置逻辑，兼容 6个Token情况
        // 因为没加前缀，所以直接处理
        if (tokens.size() >= 6) {
             InputBinding binding;
             binding.context = stringToContext(tokens[0]);
             Action action = stringToAction(tokens[1]);

             if (tokens[2] == "Keyboard") binding.device = InputDevice::Keyboard;
             else if (tokens[2] == "Mouse") binding.device = InputDevice::Mouse;
             else if (tokens[2] == "Scroll") binding.device = InputDevice::Scroll;

             if (binding.device == InputDevice::Keyboard) binding.control = stringToKey(tokens[3]);
             else if (binding.device == InputDevice::Mouse) {
                 if (tokens[3] == "LEFT") binding.control = GLFW_MOUSE_BUTTON_LEFT;
                 else if (tokens[3] == "RIGHT") binding.control = GLFW_MOUSE_BUTTON_RIGHT;
             } else if (binding.device == InputDevice::Scroll) {
                 if (tokens[3] == "UP") binding.control = 1;
                 else if (tokens[3] == "DOWN") binding.control = -1;
             }

             binding.trigger = stringToTrigger(tokens[4]);

             binding.modifiers = 0;
             if (tokens[5].find("SHIFT") != std::string::npos) binding.modifiers |= GLFW_MOD_SHIFT;
             if (tokens[5].find("CTRL") != std::string::npos) binding.modifiers |= GLFW_MOD_CONTROL;
             if (tokens[5].find("ALT") != std::string::npos) binding.modifiers |= GLFW_MOD_ALT;

             m_bindings[action].push_back(binding);
        }
    }
}

