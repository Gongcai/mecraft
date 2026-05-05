#include "UIScene.h"

#include <GLFW/glfw3.h>

#include <algorithm>

void UIScene::init(ResourceMgr& resourceMgr) {
    m_resourceMgr = &resourceMgr;
    buildUI(resourceMgr);
    for (auto& root : m_roots) {
        root->init(resourceMgr);
    }
    ensureFocusableSelection();
}

void UIScene::shutdown() {
    setFocusedWidget(nullptr);
    for (auto& root : m_roots) {
        root->shutdown();
    }
    m_roots.clear();
    m_animations.clear();
    m_resourceMgr = nullptr;
    m_currentContext = {};
    m_hasInputContext = false;
}

void UIScene::render(const UIRenderContext& context) const {
    const_cast<UIScene*>(this)->setInputContext(context);
    const_cast<UIScene*>(this)->applyPendingFocusRequests();
    const_cast<UIScene*>(this)->ensureFocusableSelection();

    for (const auto& root : m_roots) {
        root->render(context);
    }
}

UIEventResult UIScene::onInput(const UIInputEvent& event) {
    if (!m_hasInputContext) {
        return UIEventResult::Ignored;
    }

    if (event.type == UIInputEventType::KeyDown) {
        if (event.key == GLFW_KEY_UP || event.key == GLFW_KEY_LEFT) {
            moveFocusPrev();
            return UIEventResult::Handled;
        }
        if (event.key == GLFW_KEY_DOWN || event.key == GLFW_KEY_RIGHT) {
            moveFocusNext();
            return UIEventResult::Handled;
        }
    }

    UIEventResult aggregate = UIEventResult::Ignored;
    if (m_focusedWidget &&
        (event.type == UIInputEventType::KeyDown ||
         event.type == UIInputEventType::KeyUp ||
         event.type == UIInputEventType::TextInput ||
         event.type == UIInputEventType::Scroll)) {
        const UIEventResult focusedResult = m_focusedWidget->onInput(event, m_currentContext);
        if (focusedResult == UIEventResult::Consumed) {
            applyPendingFocusRequests();
            ensureFocusableSelection();
            return UIEventResult::Consumed;
        }
        if (focusedResult == UIEventResult::Handled) {
            aggregate = UIEventResult::Handled;
        }
    }

    // Dispatch to roots in reverse order (top-most first)
    for (auto it = m_roots.rbegin(); it != m_roots.rend(); ++it) {
        UIEventResult result = (*it)->onInput(event, m_currentContext);
        if (result == UIEventResult::Consumed) return UIEventResult::Consumed;
        if (result == UIEventResult::Handled) {
            aggregate = UIEventResult::Handled;
        }
    }

    applyPendingFocusRequests();
    ensureFocusableSelection();
    return aggregate;
}

void UIScene::setInputContext(const UIRenderContext& context) {
    m_currentContext = context;
    m_hasInputContext = true;
}

void UIScene::enterScene() {
    m_phase = Phase::Entering;
    onSceneEnter();
    ensureFocusableSelection();
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

void UIScene::moveFocusNext() {
    std::vector<UIWidget*> focusable;
    focusable.reserve(16);
    for (auto& root : m_roots) {
        root->collectFocusableWidgets(focusable);
    }

    if (focusable.empty()) {
        setFocusedWidget(nullptr);
        return;
    }

    const auto it = std::find(focusable.begin(), focusable.end(), m_focusedWidget);
    if (it == focusable.end()) {
        setFocusedWidget(focusable.front());
        return;
    }

    const std::size_t index = static_cast<std::size_t>(it - focusable.begin());
    setFocusedWidget(focusable[(index + 1) % focusable.size()]);
}

void UIScene::moveFocusPrev() {
    std::vector<UIWidget*> focusable;
    focusable.reserve(16);
    for (auto& root : m_roots) {
        root->collectFocusableWidgets(focusable);
    }

    if (focusable.empty()) {
        setFocusedWidget(nullptr);
        return;
    }

    const auto it = std::find(focusable.begin(), focusable.end(), m_focusedWidget);
    if (it == focusable.end()) {
        setFocusedWidget(focusable.back());
        return;
    }

    const std::size_t index = static_cast<std::size_t>(it - focusable.begin());
    const std::size_t prevIndex = (index == 0) ? focusable.size() - 1 : index - 1;
    setFocusedWidget(focusable[prevIndex]);
}

void UIScene::ensureFocusableSelection() {
    std::vector<UIWidget*> focusable;
    focusable.reserve(16);
    for (auto& root : m_roots) {
        root->collectFocusableWidgets(focusable);
    }

    if (focusable.empty()) {
        setFocusedWidget(nullptr);
        return;
    }

    if (std::find(focusable.begin(), focusable.end(), m_focusedWidget) == focusable.end()) {
        setFocusedWidget(focusable.front());
    }
}

void UIScene::applyPendingFocusRequests() {
    for (auto it = m_roots.rbegin(); it != m_roots.rend(); ++it) {
        if (UIWidget* requested = (*it)->consumeRequestedFocusDeep()) {
            setFocusedWidget(requested);
            return;
        }
    }
}

void UIScene::setFocusedWidget(UIWidget* widget) {
    if (m_focusedWidget == widget) {
        return;
    }

    if (m_focusedWidget) {
        m_focusedWidget->setFocused(false);
    }
    m_focusedWidget = widget;
    if (m_focusedWidget) {
        m_focusedWidget->setFocused(true);
    }
}
