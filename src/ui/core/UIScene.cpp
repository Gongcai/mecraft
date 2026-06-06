#include "UIScene.h"

#include <algorithm>

void UIScene::init(ResourceMgr& resourceMgr) {
    if (m_initialized) {
        shutdown();
    }
    m_resourceMgr = &resourceMgr;
    buildUI(resourceMgr);
    for (auto& root : m_roots) {
        root->init(resourceMgr);
    }
    ensureFocusableSelection();
    m_initialized = true;
}

void UIScene::shutdown() {
    if (!m_initialized && m_roots.empty() && m_resourceMgr == nullptr) {
        return;
    }
    setFocusedWidget(nullptr);
    m_focusEngaged = false;
    m_lastPointerX = -1.0f;
    m_lastPointerY = -1.0f;
    for (auto& root : m_roots) {
        root->shutdown();
    }
    m_roots.clear();
    m_animations.clear();
    m_resourceMgr = nullptr;
    m_currentContext = {};
    m_initialized = false;
    m_hasInputContext = false;
}

void UIScene::layout(const UIRenderContext& context) {
    for (auto& root : m_roots) {
        root->layout(context);
    }
}

void UIScene::render(const UIRenderContext& context) const {
    const_cast<UIScene*>(this)->setInputContext(context);
    const_cast<UIScene*>(this)->layout(context);
    const_cast<UIScene*>(this)->applyPendingFocusRequests();
    const_cast<UIScene*>(this)->ensureFocusableSelection();

    for (const auto& root : m_roots) {
        root->render(context);
    }
    for (const auto& root : m_roots) {
        root->renderOverlay(context);
    }
}

UIEventResult UIScene::onInput(const UIInputEvent& event, const UIRenderContext& /*ctx*/) {
    if (!m_hasInputContext) {
        return UIEventResult::Ignored;
    }

    if (event.type == UIInputEventType::PointerMove) {
        if (m_focusEngaged &&
            (event.x != m_lastPointerX || event.y != m_lastPointerY)) {
            m_focusEngaged = false;
            setFocusedWidget(nullptr);
        }
        m_lastPointerX = event.x;
        m_lastPointerY = event.y;
    }

    // Focused widget gets priority for keyboard, command, text, and scroll events
    UIEventResult aggregate = UIEventResult::Ignored;
    if (m_focusedWidget &&
        (event.type == UIInputEventType::KeyDown ||
         event.type == UIInputEventType::KeyUp ||
         event.type == UIInputEventType::Command ||
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
        if (result == UIEventResult::Consumed) {
            applyPendingFocusRequests();
            ensureFocusableSelection();
            return UIEventResult::Consumed;
        }
        if (result == UIEventResult::Handled) {
            aggregate = UIEventResult::Handled;
        }
    }

    // Navigation commands move focus only if nothing else handled the event
    if (event.type == UIInputEventType::Command && aggregate == UIEventResult::Ignored) {
        std::vector<UIWidget*> focusable;
        focusable.reserve(16);
        collectAllFocusable(focusable);

        if (event.command == UICommand::NavigateUp || event.command == UICommand::NavigateLeft) {
            m_focusEngaged = true;
            moveFocusPrev(&focusable);
            applyPendingFocusRequests();
            ensureFocusableSelection(&focusable);
            return UIEventResult::Handled;
        }
        if (event.command == UICommand::NavigateDown || event.command == UICommand::NavigateRight) {
            m_focusEngaged = true;
            moveFocusNext(&focusable);
            applyPendingFocusRequests();
            ensureFocusableSelection(&focusable);
            return UIEventResult::Handled;
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
    m_focusEngaged = false;
    m_lastPointerX = -1.0f;
    m_lastPointerY = -1.0f;
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

void UIScene::collectAllFocusable(std::vector<UIWidget*>& out) const {
    for (auto& root : m_roots) {
        root->collectFocusableWidgets(out);
    }
}

void UIScene::moveFocusNext(const std::vector<UIWidget*>* cached) {
    std::vector<UIWidget*> local;
    if (!cached) {
        local.reserve(16);
        collectAllFocusable(local);
        cached = &local;
    }
    const auto& focusable = *cached;

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

void UIScene::moveFocusPrev(const std::vector<UIWidget*>* cached) {
    std::vector<UIWidget*> local;
    if (!cached) {
        local.reserve(16);
        collectAllFocusable(local);
        cached = &local;
    }
    const auto& focusable = *cached;

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

void UIScene::ensureFocusableSelection(const std::vector<UIWidget*>* cached) {
    std::vector<UIWidget*> local;
    if (!cached) {
        local.reserve(16);
        collectAllFocusable(local);
        cached = &local;
    }
    const auto& focusable = *cached;

    if (focusable.empty()) {
        setFocusedWidget(nullptr);
        return;
    }

    if (std::find(focusable.begin(), focusable.end(), m_focusedWidget) == focusable.end()) {
        if (m_focusEngaged) {
            setFocusedWidget(focusable.front());
        } else {
            setFocusedWidget(nullptr);
        }
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
