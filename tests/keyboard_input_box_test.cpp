#include <cstdlib>
#include <iostream>

#include "../src/ui/KeyboardInputBox.h"

namespace {
int fail(const char* message) {
    std::cerr << "[keyboard_input_box_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

InputSnapshot makePressedKeySnapshot(int key) {
    InputSnapshot snapshot;
    snapshot.keys[key] = true;
    snapshot.keysJustPressed[key] = true;
    return snapshot;
}

InputSnapshot makePressedShiftKeySnapshot(int key) {
    InputSnapshot snapshot;
    snapshot.keys[GLFW_KEY_LEFT_SHIFT] = true;
    snapshot.keys[key] = true;
    snapshot.keysJustPressed[key] = true;
    return snapshot;
}
}

int main() {
    KeyboardInputBox inputBox(8);

    inputBox.open();
    inputBox.update(makePressedKeySnapshot(GLFW_KEY_H));
    inputBox.update(makePressedKeySnapshot(GLFW_KEY_I));
    if (inputBox.getText() != "hi") {
        return fail("letters should append as lowercase when shift is not held");
    }

    inputBox.update(makePressedShiftKeySnapshot(GLFW_KEY_1));
    if (inputBox.getText() != "hi!") {
        return fail("shift + number should append symbol");
    }

    inputBox.update(makePressedKeySnapshot(GLFW_KEY_BACKSPACE));
    if (inputBox.getText() != "hi") {
        return fail("backspace should remove the last character");
    }

    inputBox.setText("12345678");
    inputBox.update(makePressedKeySnapshot(GLFW_KEY_A));
    if (inputBox.getText() != "12345678") {
        return fail("text should not grow beyond max length");
    }

    std::string submitted;
    inputBox.update(makePressedKeySnapshot(GLFW_KEY_ENTER));
    if (!inputBox.consumeSubmit(&submitted)) {
        return fail("enter should trigger submit event");
    }
    if (submitted != "12345678") {
        return fail("submitted text should match current input");
    }
    if (inputBox.consumeSubmit(nullptr)) {
        return fail("submit event should be consumable once");
    }

    inputBox.open("abc");
    inputBox.update(makePressedKeySnapshot(GLFW_KEY_ESCAPE));
    if (!inputBox.consumeCancel()) {
        return fail("escape should trigger cancel event");
    }
    if (inputBox.consumeCancel()) {
        return fail("cancel event should be consumable once");
    }

    std::cout << "[keyboard_input_box_test] PASS\n";
    return EXIT_SUCCESS;
}

