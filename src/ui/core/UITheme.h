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
    Color dropdownItemSelected {0.15f, 0.45f, 0.55f, 0.35f};
    Color dropdownSeparator    {0.35f, 0.35f, 0.35f, 0.4f};
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
    Color screenBackground     {0.03f, 0.04f, 0.05f, 1.0f};
    Color overlaySurface       {0.22f, 0.22f, 0.26f, 0.92f};
    Color overlaySurfaceBorder {0.40f, 0.40f, 0.45f, 0.7f};

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

    // --- TextInput ---
    Color inputBackground      {0.15f, 0.15f, 0.15f, 0.9f};
    Color inputBorder          {0.40f, 0.40f, 0.40f, 0.7f};
    Color inputBorderFocused   {0.2f, 0.8f, 1.0f, 1.0f};
    Color inputText            {1.0f, 1.0f, 1.0f, 1.0f};
    Color inputPlaceholder     {0.5f, 0.5f, 0.5f, 0.8f};
    Color inputSelection       {0.2f, 0.5f, 0.9f, 0.4f};
    Color inputCursor          {1.0f, 1.0f, 1.0f, 0.9f};

    // --- Toggle ---
    Color toggleTrackOff       {0.25f, 0.25f, 0.25f, 0.9f};
    Color toggleTrackOn        {0.2f, 0.7f, 0.4f, 1.0f};
    Color toggleKnob           {0.9f, 0.9f, 0.9f, 1.0f};
    Color toggleKnobHover      {1.0f, 1.0f, 1.0f, 1.0f};
    float toggleWidth          = 44.0f;
    float toggleHeight         = 22.0f;

    // --- RadioButton ---
    Color radioOuter           {0.35f, 0.35f, 0.35f, 0.9f};
    Color radioOuterHover      {0.5f, 0.5f, 0.5f, 1.0f};
    Color radioInner           {0.2f, 0.8f, 1.0f, 1.0f};
    float radioSize            = 18.0f;

    // --- ProgressBar ---
    Color progressTrack        {0.2f, 0.2f, 0.2f, 0.9f};
    Color progressFill         {0.2f, 0.8f, 1.0f, 1.0f};
    Color progressText         {1.0f, 1.0f, 1.0f, 1.0f};

    // --- TabControl ---
    Color tabHeader            {0.20f, 0.20f, 0.20f, 0.9f};
    Color tabHeaderActive      {0.28f, 0.28f, 0.28f, 1.0f};
    Color tabHeaderHover       {0.25f, 0.25f, 0.25f, 1.0f};
    Color tabIndicator         {0.2f, 0.8f, 1.0f, 1.0f};
    Color tabContent           {0.18f, 0.18f, 0.18f, 0.85f};
    float tabHeaderHeight      = 36.0f;

    // --- ContextMenu ---
    Color contextMenuBackground{0.16f, 0.16f, 0.16f, 0.95f};
    Color contextMenuBorder    {0.35f, 0.35f, 0.35f, 0.7f};
    Color contextMenuItemHover {0.25f, 0.25f, 0.25f, 1.0f};
    Color contextMenuSeparator {0.30f, 0.30f, 0.30f, 0.5f};
    float contextMenuItemHeight = 28.0f;
    float contextMenuWidth     = 180.0f;

    // --- Toast ---
    Color toastBackground      {0.15f, 0.15f, 0.15f, 0.92f};
    Color toastText            {1.0f, 1.0f, 1.0f, 1.0f};
    Color toastInfo            {0.2f, 0.8f, 1.0f, 1.0f};
    Color toastSuccess         {0.3f, 0.7f, 0.3f, 1.0f};
    Color toastWarning         {0.9f, 0.7f, 0.2f, 1.0f};
    Color toastError           {0.7f, 0.3f, 0.3f, 1.0f};
    float toastWidth           = 300.0f;
    float toastHeight          = 40.0f;
};
