#include <cstdlib>
#include <iostream>

#include "../src/ui/font/TextRenderer.h"

namespace {
int fail(const char* message) {
    std::cerr << "[text_renderer_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}
}

int main() {
    TextRenderer text;

    // Smoke: empty text should early return even without init.
    text.render("", 0.0f, 0.0f, 1.0f, {1.0f, 1.0f, 1.0f, 1.0f}, 1280.0f, 720.0f);

    // measureText on empty string should return zero.
    auto m = text.measureText("", 1.0f);
    if (m.width != 0.0f || m.height != 0.0f) {
        return fail("measureText on empty string should return zero");
    }

    text.shutdown();

    std::cout << "[text_renderer_test] PASS\n";
    return EXIT_SUCCESS;
}
