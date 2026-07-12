#pragma once

#include "../core/UIWidget.h"

class ResourceMgr;
struct TextureAtlas;

class HudControl : public UIWidget {
public:
    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;

protected:
    void renderSelf(const UIRenderContext& context) const override;

private:
    void drawIconRow(const UIRenderContext& context,
                       const TextureAtlas& atlas,
                       float startX, float startY,
                       int current, int max,
                       int fullIndex, int halfIndex,
                       float iconSize) const;

    ResourceMgr* m_resourceMgr = nullptr;

    // Cached atlas icon indices (resolved once in init).
    int m_heartFull = -1;
    int m_heartHalf = -1;
    int m_armorFull = -1;
    int m_armorHalf = -1;
    int m_foodFull = -1;
    int m_foodHalf = -1;

};
