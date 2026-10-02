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
    void drawMeterRow(const UIRenderContext& context, const TextureAtlas& atlas, float startX, float startY, int current,
                      int max, int fullIndex, int halfIndex, int emptyIndex, float iconSize, float iconStride) const;
    void drawAirRow(const UIRenderContext& context, const TextureAtlas& atlas, float startX, float startY, int current,
                    int max, float iconSize, float iconStride) const;

    GameResources* m_resources = nullptr;

    // Cached atlas icon indices (resolved once in init).
    int m_heartFull = -1;
    int m_heartHalf = -1;
    int m_heartContainer = -1;
    int m_armorFull = -1;
    int m_armorHalf = -1;
    int m_armorEmpty = -1;
    int m_foodFull = -1;
    int m_foodHalf = -1;
    int m_foodEmpty = -1;
    int m_airFull = -1;
    int m_airEmpty = -1;
};
