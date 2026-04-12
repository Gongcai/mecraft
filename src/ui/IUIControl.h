#pragma once

#include "UIEventResult.h"
#include "UIInputEvent.h"
#include "UIRenderContext.h"

class ResourceMgr;

class IUIControl {
public:
    virtual ~IUIControl() = default;

    virtual void init(ResourceMgr& resourceMgr) = 0;
    virtual void shutdown() = 0;
    virtual void render(const UIRenderContext& context) const = 0;
    virtual UIEventResult onInput(const UIInputEvent& event) = 0;
    [[nodiscard]] virtual bool isVisible() const = 0;
};

