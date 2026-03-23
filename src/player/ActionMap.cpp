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

// Helper to map string to Action
static Action stringToAction(const std::string& str) {
    static const std::map<std::string, Action> lookup = {
        {"MoveForward", Action::MoveForward},
        {"MoveBackward", Action::MoveBackward},
        {"MoveLeft", Action::MoveLeft},
        {"MoveRight", Action::MoveRight},
        {"Jump", Action::Jump},
        {"Sprint", Action::Sprint},
        {"Crouch", Action::Crouch},
        {"Attack", Action::Attack},
        {"UseItem", Action::UseItem},
        {"Inventory", Action::Inventory},
        {"Menu", Action::Menu},
        {"Confirm", Action::Confirm},
        {"Cancel", Action::Cancel},
        {"Up", Action::Up},
        {"Down", Action::Down},
        {"Left", Action::Left},
        {"Right", Action::Right}
    };
    auto it = lookup.find(str);
    if (it != lookup.end()) return it->second;
    // Fallback or warning
    return Action::Menu;
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
    if (str == "LEFT_SHIFT") return GLFW_KEY_LEFT_SHIFT;
    if (str == "LEFT_CONTROL") return GLFW_KEY_LEFT_CONTROL;
    if (str == "TAB") return GLFW_KEY_TAB;
    if (str == "W") return GLFW_KEY_W; // specific override if length==1 fails

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
        std::cerr << "Failed to open keybindings file: " << path << std::endl;
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::stringstream ss(line);
        std::string token;
        std::vector<std::string> tokens;
        while (ss >> token) tokens.push_back(token);

        if (tokens.size() >= 6) {
             // V2 Format
             InputBinding binding;
             binding.context = stringToContext(tokens[0]);
             Action action = stringToAction(tokens[1]);
             // tokens[2] is Device
             if (tokens[2] == "Keyboard") binding.device = InputDevice::Keyboard;
             else if (tokens[2] == "Mouse") binding.device = InputDevice::Mouse;

             // tokens[3] is Control
             if (binding.device == InputDevice::Keyboard) binding.control = stringToKey(tokens[3]);
             else {
                 if (tokens[3] == "LEFT") binding.control = GLFW_MOUSE_BUTTON_LEFT;
                 else if (tokens[3] == "RIGHT") binding.control = GLFW_MOUSE_BUTTON_RIGHT;
             }

             binding.trigger = stringToTrigger(tokens[4]);

             // tokens[5] Modifiers
             binding.modifiers = 0;
             if (tokens[5].find("SHIFT") != std::string::npos) binding.modifiers |= GLFW_MOD_SHIFT;
             if (tokens[5].find("CTRL") != std::string::npos) binding.modifiers |= GLFW_MOD_CONTROL;
             if (tokens[5].find("ALT") != std::string::npos) binding.modifiers |= GLFW_MOD_ALT;

             m_bindings[action].push_back(binding);

        } else if (tokens.size() >= 3) {
            // Old Format: Action BindingType KeyName
            // Map to default context (Gameplay) and default trigger (Held)
            Action action = stringToAction(tokens[0]);
            std::string type = tokens[1];
            std::string keyName = tokens[2];

            if (type == "Key") {
                bindKey(action, stringToKey(keyName), InputContextType::Gameplay);
            } else if (type == "MouseButton") {
                int btn = (keyName == "Left") ? GLFW_MOUSE_BUTTON_LEFT : GLFW_MOUSE_BUTTON_RIGHT;
                bindMouseButton(action, btn, InputContextType::Gameplay);
            }
        }
    }
}
