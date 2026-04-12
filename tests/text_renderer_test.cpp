#include <cstdlib>
#include <iostream>

#include "../src/ui/TextRenderer.h"

namespace {
int fail(const char* message) {
    std::cerr << "[text_renderer_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}
}

int main() {
    TextRenderer text;

    text.setAdvanceFactor(-1.0f);
    if (text.getAdvanceFactor() != 0.5f) {
        return fail("advance factor should clamp to minimum");
    }

    text.setAdvanceFactor(9.0f);
    if (text.getAdvanceFactor() != 1.2f) {
        return fail("advance factor should clamp to maximum");
    }

    // Smoke: empty text should early return even without init.
    text.render("", 0.0f, 0.0f, 1.0f, {1.0f, 1.0f, 1.0f, 1.0f}, 1280.0f, 720.0f);
    text.shutdown();

    std::cout << "[text_renderer_test] PASS\n";
    return EXIT_SUCCESS;
}

