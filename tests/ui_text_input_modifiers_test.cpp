#include <cstdlib>
#include <cmath>
#include <iostream>

#include <GLFW/glfw3.h>

#include "../src/ui/widgets/UITextInput.h"
#include "../src/ui/widgets/UINumericSpinner.h"

namespace {

int fail(const char* message) {
    std::cerr << "[ui_text_input_modifiers_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

class TestTextInput final : public UITextInput {
public:
    using UITextInput::onInput;
};

class TestNumericSpinner final : public UINumericSpinner {
public:
    using UINumericSpinner::onInput;
};

} // namespace

int main() {
    UIRenderContext context;
    context.screenWidth = 400;
    context.screenHeight = 240;

    TestTextInput input;
    input.setText("abc");
    input.setFocused(true);

    UIInputEvent selectAll;
    selectAll.type = UIInputEventType::KeyDown;
    selectAll.key = GLFW_KEY_A;
    selectAll.modifiers = uiInputModifierMask(UIInputModifier::Control);
    if (input.onInput(selectAll, context) != UIEventResult::Consumed) {
        return fail("Ctrl+A should be consumed by focused text input");
    }

    UIInputEvent replacement;
    replacement.type = UIInputEventType::TextInput;
    replacement.codepoint = static_cast<std::uint32_t>('x');
    if (input.onInput(replacement, context) != UIEventResult::Consumed) {
        return fail("text input should be consumed by focused text input");
    }
    if (input.getText() != "x") {
        return fail("Ctrl+A should select all text so typed character replaces it");
    }

    input.setText("abc");
    UIInputEvent plainA;
    plainA.type = UIInputEventType::KeyDown;
    plainA.key = GLFW_KEY_A;
    if (input.onInput(plainA, context) == UIEventResult::Consumed) {
        return fail("plain A keydown should not trigger select-all");
    }

    TestNumericSpinner spinner;
    spinner.setRange(0.0f, 99999.0f);
    spinner.setValue(12345.0f);
    spinner.setFocused(true);
    spinner.x = 20.0f;
    spinner.y = 20.0f;
    spinner.width = 120.0f;
    spinner.height = 28.0f;

    UIInputEvent pointerDown;
    pointerDown.type = UIInputEventType::PointerDown;
    pointerDown.x = 80.0f;
    pointerDown.y = 4.0f;
    pointerDown.button = UIPointerButton::Primary;
    if (spinner.onInput(pointerDown, context) != UIEventResult::Consumed) {
        return fail("numeric spinner value area should enter editing mode");
    }

    UIInputEvent backspaceDown;
    backspaceDown.type = UIInputEventType::KeyDown;
    backspaceDown.key = GLFW_KEY_BACKSPACE;
    if (spinner.onInput(backspaceDown, context) != UIEventResult::Consumed) {
        return fail("numeric spinner should consume routed backspace press");
    }
    spinner.update(0.1f);
    spinner.update(0.1f);
    spinner.onInput(backspaceDown, context);
    spinner.update(0.1f);

    UIInputEvent backspaceUp;
    backspaceUp.type = UIInputEventType::KeyUp;
    backspaceUp.key = GLFW_KEY_BACKSPACE;
    spinner.onInput(backspaceUp, context);
    spinner.update(1.0f);

    UIInputEvent enter;
    enter.type = UIInputEventType::KeyDown;
    enter.key = GLFW_KEY_ENTER;
    spinner.onInput(enter, context);
    if (std::abs(spinner.getValue() - 12.0f) > 0.001f) {
        return fail("numeric spinner backspace repeat should stop after routed key release");
    }

    std::cout << "[ui_text_input_modifiers_test] PASS\n";
    return EXIT_SUCCESS;
}
