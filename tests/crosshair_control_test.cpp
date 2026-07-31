#include <cstdlib>
#include <iostream>

#include "../src/ui/hud/CrosshairControl.h"

namespace {
int fail(const char* message) {
    std::cerr << "[crosshair_control_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}
} // namespace

int main() {
    CrosshairControl crosshair;

    crosshair.setSize(-10.0f);
    if (crosshair.getSize() != 0.5f) {
        return fail("size should clamp to minimum");
    }

    crosshair.setSize(99.0f);
    if (crosshair.getSize() != 4.0f) {
        return fail("size should clamp to maximum");
    }

    const std::array<float, 4> color{0.2f, 0.4f, 0.6f, 0.8f};
    crosshair.setColor(color);
    if (crosshair.getColor() != color) {
        return fail("color setter/getter mismatch");
    }

    // Smoke: should be safe without init.
    crosshair.shutdown();

    std::cout << "[crosshair_control_test] PASS\n";
    return EXIT_SUCCESS;
}
