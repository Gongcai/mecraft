#pragma once

#include <string>

class ResourceMgr;
class Inventory;
class TextRenderer;

struct UIRenderContext {
    int screenWidth = 0;
    int screenHeight = 0;
    float timeSeconds = 0.0f;
    ResourceMgr* resourceMgr = nullptr;
    const Inventory* inventory = nullptr;
    const TextRenderer* textRenderer = nullptr;
    const std::string* commandInputText = nullptr;
    bool commandInputVisible = false;
    float pointerX = 0.0f;
    float pointerY = 0.0f;
    bool hasDraggedItem = false;
    int draggedItemId = 0;
};

