#include <cstdlib>
#include <iostream>

#include <GLFW/glfw3.h>

#include "../src/ui/widgets/UITextInput.h"

namespace {

int fail(const char* message) {
    std::cerr << "[ui_text_input_modifiers_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

class TestTextInput final : public UITextInput {
public:
    using UITextInput::onInput;
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

    std::cout << "[ui_text_input_modifiers_test] PASS\n";
    return EXIT_SUCCESS;
}
