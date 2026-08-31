#pragma once

#include "../core/UIWidget.h"

struct GameResources;
struct TextureAtlas;

class HudControl : public UIWidget {
public:
    void init(GameResources& resources, RhiDevice& rhiDevice) override;
    void shutdown() override;

protected:
    void renderSelf(const UIRenderContext& context) const override;

private:
    void drawIconRow(const UIRenderContext& context, const TextureAtlas& atlas, float startX, float startY, int current,
                     int max, int fullIndex, int halfIndex, float iconSize) const;

    GameResources* m_resources = nullptr;

    // Cached atlas icon indices (resolved once in init).
    int m_heartFull = -1;
    int m_heartHalf = -1;
    int m_armorFull = -1;
    int m_armorHalf = -1;
    int m_foodFull = -1;
    int m_foodHalf = -1;
};
