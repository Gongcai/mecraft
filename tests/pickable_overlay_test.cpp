#include <cstdlib>
#include <iostream>

#include "../src/ui/PickableOverlay.h"

namespace {
int fail(const char* message) {
    std::cerr << "[pickable_overlay_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}
}

int main() {
    const Pickable::SlotInfo slots[] = {
        {10, 10, 20, 1},
        {40, 10, 20, 2},
    };

    if (Pickable::hitTest(slots, 2, 12.0f, 12.0f) != 0) {
        return fail("hitTest should match first slot");
    }
    if (Pickable::hitTest(slots, 2, 41.0f, 15.0f) != 1) {
        return fail("hitTest should match second slot");
    }
    if (Pickable::hitTest(slots, 2, 0.0f, 0.0f) != -1) {
        return fail("hitTest should return -1 when not hovering a slot");
    }

    PickableOverlay overlay;
    // Smoke: should no-op before init.
    overlay.render(slots, 2, 12.0f, 12.0f);
    overlay.shutdown();

    std::cout << "[pickable_overlay_test] PASS\n";
    return EXIT_SUCCESS;
}

