#pragma once

#include <algorithm>
#include <cmath>

// GUI Scale preset options (Minecraft-style)
enum class GUIScale {
    Auto, // Auto-select based on screen resolution
    Small, // 0.5x
    Normal, // 1.0x
    Large, // 2.0x
    ExtraLarge // 3.0x (for 4K displays)
};

// Scale strategy for individual UI elements
enum class UIScaleStrategy {
    None, // No scaling (e.g., crosshair - always pixel-perfect)
    Uniform, // Uniform scaling with GUI scale (e.g., hotbar, inventory)
    TextOnly, // Text-adaptive scaling (DPI-aware, ignores user GUI scale)
    PixelPerfect // Integer scaling only (snap to nearest integer)
};

// UI Scale configuration computed per frame
struct UIScaleConfig {
    float autoScale = 1.0f; // Auto-calculated base scale from resolution
    float guiScale = 1.0f; // User-selected GUI scale multiplier
    float effectiveScale = 1.0f; // Final scale = autoScale * guiScale
    int virtualWidth = 0; // Virtual screen width for layout
    int virtualHeight = 0; // Virtual screen height for layout

    // Helper: Get scale for a specific strategy
    [[nodiscard]] float getScaleForStrategy(UIScaleStrategy strategy) const {
        switch (strategy) {
        case UIScaleStrategy::None: return 1.0f;
        case UIScaleStrategy::Uniform: return effectiveScale;
        case UIScaleStrategy::TextOnly: return autoScale; // Ignore user GUI scale preference
        case UIScaleStrategy::PixelPerfect: return std::floor(effectiveScale); // Snap to integer
        }
        return effectiveScale;
    }

    // Compute auto scale from screen dimensions (Minecraft-style tiers)
    [[nodiscard]] static float computeAutoScale(float actualW, float actualH) {
        const float minDim = std::min(actualW, actualH);

        // Resolution-based auto scaling tiers
        if (minDim >= 2160.0f)
            return 3.0f; // 4K+
        if (minDim >= 1440.0f)
            return 2.0f; // 2K/1440p
        if (minDim >= 720.0f)
            return 1.0f; // 1080p/720p
        return 0.5f; // Below 720p
    }

    // Convert GUI scale enum to multiplier
    [[nodiscard]] static float getGUIScaleMultiplier(GUIScale scale, float autoScaleValue) {
        switch (scale) {
        case GUIScale::Auto: return autoScaleValue;
        case GUIScale::Small: return 0.5f;
        case GUIScale::Normal: return 1.0f;
        case GUIScale::Large: return 2.0f;
        case GUIScale::ExtraLarge: return 3.0f;
        }
        return 1.0f;
    }

    // Create config from screen dimensions
    [[nodiscard]] static UIScaleConfig create(float actualW, float actualH, GUIScale userScale) {
        UIScaleConfig config;
        config.autoScale = computeAutoScale(actualW, actualH);

        // Resolve user GUI scale
        if (userScale == GUIScale::Auto) {
            config.guiScale = config.autoScale;
        } else {
            config.guiScale = getGUIScaleMultiplier(userScale, config.autoScale);
        }

        config.effectiveScale = config.guiScale;
        config.virtualWidth = static_cast<int>(std::round(actualW / config.effectiveScale));
        config.virtualHeight = static_cast<int>(std::round(actualH / config.effectiveScale));

        return config;
    }
};
