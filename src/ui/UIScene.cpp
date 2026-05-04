#include "UIScene.h"

void UIScene::init(ResourceMgr& resourceMgr) {
    m_resourceMgr = &resourceMgr;
    buildUI(resourceMgr);
    for (auto& root : m_roots) {
        root->init(resourceMgr);
    }
}

void UIScene::shutdown() {
    for (auto& root : m_roots) {
        root->shutdown();
    }
    m_roots.clear();
    m_animations.clear();
    m_resourceMgr = nullptr;
}

void UIScene::render(const UIRenderContext& context) const {
    // Store context for hit testing in onInput
    m_currentContext = context;

    // Update animations (mutable cast for animation tick in const render)
    // This is acceptable because animations are visual state, not logical state
    const_cast<UIScene*>(this)->m_animations.updateAll(0.0f); // dt=0, already updated in updateAnimations

    for (const auto& root : m_roots) {
        root->render(context);
    }
}

UIEventResult UIScene::onInput(const UIInputEvent& event) {
    // Dispatch to roots in reverse order (top-most first)
    for (auto it = m_roots.rbegin(); it != m_roots.rend(); ++it) {
        UIEventResult result = (*it)->onInput(event, m_currentContext);
        if (result == UIEventResult::Consumed) return UIEventResult::Consumed;
    }
    return UIEventResult::Ignored;
}

void UIScene::enterScene() {
    m_phase = Phase::Entering;
    onSceneEnter();
}

void UIScene::exitScene() {
    m_phase = Phase::Exiting;
    onSceneExit();
}

void UIScene::updateAnimations(float dt) {
    m_animations.updateAll(dt);
    for (auto& root : m_roots) {
        root->updateAnimations(dt);
    }
}
