#pragma once

#include <array>
#include <string>

using Color = std::array<float, 4>;

struct UITheme {
    // Load theme from a JSON file. Missing fields keep their default values.
    // Returns true on success, false on failure (file not found / parse error).
    bool loadFromFile(const std::string& path);

    // --- Panel / Container ---
    Color panelBackground      {0.18f, 0.18f, 0.18f, 0.85f};
    Color panelBorder          {0.35f, 0.35f, 0.35f, 0.7f};
    float panelBorderWidth     = 1.0f;

    // --- Button ---
    Color buttonNormal         {0.28f, 0.28f, 0.28f, 0.92f};
    Color buttonHover          {0.42f, 0.42f, 0.42f, 1.0f};
    Color buttonPressed        {0.20f, 0.20f, 0.20f, 1.0f};
    Color buttonDisabled       {0.22f, 0.22f, 0.22f, 0.5f};
    Color buttonBorder         {0.50f, 0.50f, 0.50f, 0.4f};
    float buttonBorderWidth    = 2.0f;

    // --- Text ---
    Color textPrimary          {1.0f, 1.0f, 1.0f, 1.0f};
    Color textSecondary        {0.7f, 0.7f, 0.7f, 1.0f};
    Color textDisabled         {0.45f, 0.45f, 0.45f, 1.0f};
    Color textLink             {0.3f, 0.7f, 1.0f, 1.0f};

    // --- Tooltip ---
    Color tooltipBackground    {0.12f, 0.12f, 0.12f, 0.94f};
    Color tooltipBorder        {0.40f, 0.40f, 0.40f, 0.8f};
    float tooltipBorderWidth   = 1.0f;

    // --- Slider ---
    Color sliderTrack          {0.25f, 0.25f, 0.25f, 1.0f};
    Color sliderFill           {0.3f, 0.6f, 1.0f, 1.0f};
    Color sliderHandle         {0.85f, 0.85f, 0.85f, 1.0f};
    Color sliderHandleHover    {1.0f, 1.0f, 1.0f, 1.0f};
    float sliderTrackHeight    = 4.0f;
    float sliderHandleSize     = 14.0f;

    // --- Checkbox ---
    Color checkboxBox          {0.25f, 0.25f, 0.25f, 0.9f};
    Color checkboxBoxHover     {0.35f, 0.35f, 0.35f, 1.0f};
    Color checkboxBoxBorder    {0.5f, 0.5f, 0.5f, 0.5f};
    Color checkboxCheck        {0.3f, 0.8f, 0.4f, 1.0f};
    float checkboxSize         = 20.0f;

    // --- Dropdown ---
    Color dropdownBackground   {0.22f, 0.22f, 0.22f, 0.95f};
    Color dropdownBorder       {0.40f, 0.40f, 0.40f, 0.7f};
    Color dropdownItemHover    {0.30f, 0.30f, 0.30f, 1.0f};
    Color dropdownArrow        {0.7f, 0.7f, 0.7f, 1.0f};

    // --- Scrollbar ---
    Color scrollbarTrack       {0.15f, 0.15f, 0.15f, 0.6f};
    Color scrollbarThumb       {0.45f, 0.45f, 0.45f, 0.8f};
    Color scrollbarThumbHover  {0.60f, 0.60f, 0.60f, 0.9f};
    float scrollbarWidth       = 8.0f;

    // --- Hotbar ---
    Color hotbarBackground     {1.0f, 1.0f, 1.0f, 1.0f};
    Color hotbarBorder         {1.0f, 1.0f, 1.0f, 0.9f};
    Color hotbarIconTint       {1.0f, 1.0f, 1.0f, 1.0f};

    // --- Crosshair ---
    Color crosshair            {1.0f, 1.0f, 1.0f, 1.0f};

    // --- Console ---
    Color consoleBox           {0.0f, 0.0f, 0.0f, 0.55f};
    Color consoleTextNormal    {0.95f, 0.95f, 0.95f, 1.0f};
    Color consoleTextWarning   {0.95f, 0.35f, 0.35f, 1.0f};
    Color consoleTextSuccess   {0.45f, 0.90f, 0.50f, 1.0f};

    // --- Overlay ---
    Color overlayDim           {0.0f, 0.0f, 0.0f, 0.6f};

    // --- Accent / Brand ---
    Color accentPrimary        {0.2f, 0.8f, 1.0f, 1.0f};
    Color accentSuccess        {0.3f, 0.7f, 0.3f, 1.0f};
    Color accentDanger         {0.7f, 0.3f, 0.3f, 1.0f};

    // --- Spacing ---
    float spacingSmall         = 4.0f;
    float spacingMedium        = 8.0f;
    float spacingLarge         = 16.0f;

    // --- Font ---
    int fontPixelHeight        = 32;
    float textScaleSmall       = 1.0f;
    float textScaleMedium      = 2.0f;
    float textScaleLarge       = 4.0f;
    float textScaleTitle       = 6.0f;
};
