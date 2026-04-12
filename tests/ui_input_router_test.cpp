#include <cstdlib>
#include <iostream>

#include "../src/ui/UIInputRouter.h"

namespace {
int fail(const char* message) {
    std::cerr << "[ui_input_router_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

class StubControl : public IUIControl {
public:
    explicit StubControl(UIEventResult fixedResult) : m_fixedResult(fixedResult) {}

    void init(ResourceMgr&) override {}
    void shutdown() override {}
    void render(const UIRenderContext&) const override {}
    UIEventResult onInput(const UIInputEvent&) override {
        ++m_callCount;
        return m_fixedResult;
    }
    [[nodiscard]] bool isVisible() const override { return m_visible; }

    void setVisible(bool visible) { m_visible = visible; }
    [[nodiscard]] int callCount() const { return m_callCount; }

private:
    UIEventResult m_fixedResult = UIEventResult::Ignored;
    bool m_visible = true;
    int m_callCount = 0;
};
}

int main() {
    UIInputRouter router;
    StubControl handled(UIEventResult::Handled);
    StubControl consumed(UIEventResult::Consumed);

    router.registerControl(&handled);
    router.registerControl(&consumed);

    const UIEventResult result = router.route({UIInputEventType::PointerMove, 10.0f, 10.0f, 0});
    if (result != UIEventResult::Consumed) {
        return fail("router should return consumed when top-most control consumes event");
    }
    if (handled.callCount() != 0) {
        return fail("router should stop propagation after consumed event");
    }
    if (consumed.callCount() != 1) {
        return fail("top-most control should receive input exactly once");
    }

    consumed.setVisible(false);
    if (router.route({UIInputEventType::PointerMove, 0.0f, 0.0f, 0}) != UIEventResult::Handled) {
        return fail("router should aggregate handled when visible controls handle input");
    }

    router.clear();
    if (router.route({UIInputEventType::PointerMove, 0.0f, 0.0f, 0}) != UIEventResult::Ignored) {
        return fail("router should ignore input with no controls");
    }

    std::cout << "[ui_input_router_test] PASS\n";
    return EXIT_SUCCESS;
}

