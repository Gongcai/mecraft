#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>

#include "../src/ui/layout/UIGridLayout.h"

namespace {

int fail(const char* message) {
    std::cerr << "[ui_grid_layout_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

bool almostEqual(float a, float b) {
    return std::fabs(a - b) < 0.001f;
}

class LayoutProbe : public UIWidget {
public:
    void layout(const UIRenderContext& ctx) override {
        (void)ctx;
        ++layoutCalls;
        laidOutWidth = width;
        laidOutHeight = height;
    }

    int layoutCalls = 0;
    float laidOutWidth = 0.0f;
    float laidOutHeight = 0.0f;
};

std::unique_ptr<LayoutProbe> probe() {
    return std::make_unique<LayoutProbe>();
}

} // namespace

int main() {
    UIGridLayout grid;
    grid.setColumns(2);
    grid.setCellWidth(30.0f);
    grid.setCellHeight(20.0f);
    grid.setSpacing(5.0f);

    auto first = probe();
    auto second = probe();
    auto third = probe();
    LayoutProbe* firstPtr = first.get();
    LayoutProbe* secondPtr = second.get();
    LayoutProbe* thirdPtr = third.get();
    grid.addChild(std::move(first));
    grid.addChild(std::move(second));
    grid.addChild(std::move(third));

    UIRenderContext ctx{};
    grid.layout(ctx);

    if (!almostEqual(firstPtr->x, 0.0f) || !almostEqual(firstPtr->y, 0.0f) || !almostEqual(secondPtr->x, 35.0f) ||
        !almostEqual(secondPtr->y, 0.0f) || !almostEqual(thirdPtr->x, 0.0f) || !almostEqual(thirdPtr->y, 25.0f)) {
        return fail("grid should place children by column, row, cell size, and spacing");
    }

    if (!almostEqual(grid.width, 65.0f) || !almostEqual(grid.height, 45.0f)) {
        return fail("grid should size itself to the occupied rows and configured columns");
    }

    if (firstPtr->layoutCalls != 1 || secondPtr->layoutCalls != 1 || thirdPtr->layoutCalls != 1 ||
        !almostEqual(firstPtr->laidOutWidth, 30.0f) || !almostEqual(thirdPtr->laidOutHeight, 20.0f)) {
        return fail("layout(ctx) should recurse into children after assigning grid cells");
    }

    std::cout << "[ui_grid_layout_test] PASS\n";
    return EXIT_SUCCESS;
}
