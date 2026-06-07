#include "UITheme.h"

#include <fstream>
#include <iostream>

#include <nlohmann/json.hpp>

// Helper: read a Color ([r,g,b,a]) from JSON, return fallback if missing.
static Color readColor(const nlohmann::json& j, const char* key, const Color& fallback) {
    if (j.contains(key) && j[key].is_array() && j[key].size() == 4) {
        return {
            j[key][0].get<float>(),
            j[key][1].get<float>(),
            j[key][2].get<float>(),
            j[key][3].get<float>()
        };
    }
    return fallback;
}

// Helper: read a float from JSON, return fallback if missing.
static float readFloat(const nlohmann::json& j, const char* key, float fallback) {
    if (j.contains(key) && j[key].is_number()) {
        return j[key].get<float>();
    }
    return fallback;
}

// Helper: read an int from JSON, return fallback if missing.
static int readInt(const nlohmann::json& j, const char* key, int fallback) {
    if (j.contains(key) && j[key].is_number()) {
        return j[key].get<int>();
    }
    return fallback;
}

bool UITheme::loadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
#ifdef MECRAFT_DEBUG
        std::cerr << "[UITheme] Failed to open: " << path << std::endl;
#endif
        return false;
    }

    nlohmann::json j;
    try {
        file >> j;
    } catch (const std::exception& e) {
#ifdef MECRAFT_DEBUG
        std::cerr << "[UITheme] Failed to parse: " << path << " - " << e.what() << std::endl;
#endif
        return false;
    }

    // Panel
    panelBackground   = readColor(j, "panelBackground",   panelBackground);
    panelBorder       = readColor(j, "panelBorder",       panelBorder);
    panelBorderWidth  = readFloat(j, "panelBorderWidth",  panelBorderWidth);

    // Button
    buttonNormal      = readColor(j, "buttonNormal",      buttonNormal);
    buttonHover       = readColor(j, "buttonHover",       buttonHover);
    buttonPressed     = readColor(j, "buttonPressed",     buttonPressed);
    buttonDisabled    = readColor(j, "buttonDisabled",    buttonDisabled);
    buttonBorder      = readColor(j, "buttonBorder",      buttonBorder);
    buttonBorderWidth = readFloat(j, "buttonBorderWidth", buttonBorderWidth);

    // Text
    textPrimary       = readColor(j, "textPrimary",       textPrimary);
    textSecondary     = readColor(j, "textSecondary",     textSecondary);
    textDisabled      = readColor(j, "textDisabled",      textDisabled);
    textLink          = readColor(j, "textLink",          textLink);

    // Tooltip
    tooltipBackground = readColor(j, "tooltipBackground", tooltipBackground);
    tooltipBorder     = readColor(j, "tooltipBorder",     tooltipBorder);
    tooltipBorderWidth = readFloat(j, "tooltipBorderWidth", tooltipBorderWidth);

    // Slider
    sliderTrack       = readColor(j, "sliderTrack",       sliderTrack);
    sliderFill        = readColor(j, "sliderFill",        sliderFill);
    sliderHandle      = readColor(j, "sliderHandle",      sliderHandle);
    sliderHandleHover = readColor(j, "sliderHandleHover", sliderHandleHover);
    sliderTrackHeight = readFloat(j, "sliderTrackHeight", sliderTrackHeight);
    sliderHandleSize  = readFloat(j, "sliderHandleSize",  sliderHandleSize);

    // Checkbox
    checkboxBox       = readColor(j, "checkboxBox",       checkboxBox);
    checkboxBoxHover  = readColor(j, "checkboxBoxHover",  checkboxBoxHover);
    checkboxBoxBorder = readColor(j, "checkboxBoxBorder", checkboxBoxBorder);
    checkboxCheck     = readColor(j, "checkboxCheck",     checkboxCheck);
    checkboxSize      = readFloat(j, "checkboxSize",      checkboxSize);

    // Dropdown
    dropdownBackground   = readColor(j, "dropdownBackground",   dropdownBackground);
    dropdownBorder       = readColor(j, "dropdownBorder",       dropdownBorder);
    dropdownItemHover    = readColor(j, "dropdownItemHover",    dropdownItemHover);
    dropdownItemSelected = readColor(j, "dropdownItemSelected", dropdownItemSelected);
    dropdownSeparator    = readColor(j, "dropdownSeparator",    dropdownSeparator);
    dropdownArrow        = readColor(j, "dropdownArrow",        dropdownArrow);

    // Scrollbar
    scrollbarTrack      = readColor(j, "scrollbarTrack",      scrollbarTrack);
    scrollbarThumb      = readColor(j, "scrollbarThumb",      scrollbarThumb);
    scrollbarThumbHover = readColor(j, "scrollbarThumbHover", scrollbarThumbHover);
    scrollbarWidth      = readFloat(j, "scrollbarWidth",      scrollbarWidth);

    // Hotbar
    hotbarBackground  = readColor(j, "hotbarBackground",  hotbarBackground);
    hotbarBorder      = readColor(j, "hotbarBorder",      hotbarBorder);
    hotbarIconTint    = readColor(j, "hotbarIconTint",    hotbarIconTint);

    // Crosshair
    crosshair         = readColor(j, "crosshair",         crosshair);

    // Console
    consoleBox        = readColor(j, "consoleBox",        consoleBox);
    consoleTextNormal = readColor(j, "consoleTextNormal", consoleTextNormal);
    consoleTextWarning = readColor(j, "consoleTextWarning", consoleTextWarning);
    consoleTextSuccess = readColor(j, "consoleTextSuccess", consoleTextSuccess);

    // Overlay
    overlayDim        = readColor(j, "overlayDim",        overlayDim);
    screenBackground  = readColor(j, "screenBackground",  screenBackground);
    overlaySurface    = readColor(j, "overlaySurface",    overlaySurface);
    overlaySurfaceBorder = readColor(j, "overlaySurfaceBorder", overlaySurfaceBorder);

    // Accent
    accentPrimary     = readColor(j, "accentPrimary",     accentPrimary);
    accentSuccess     = readColor(j, "accentSuccess",     accentSuccess);
    accentDanger      = readColor(j, "accentDanger",      accentDanger);

    // Spacing
    spacingSmall      = readFloat(j, "spacingSmall",      spacingSmall);
    spacingMedium     = readFloat(j, "spacingMedium",     spacingMedium);
    spacingLarge      = readFloat(j, "spacingLarge",      spacingLarge);

    // Font
    fontPixelHeight   = readInt(j,   "fontPixelHeight",   fontPixelHeight);
    textScaleSmall    = readFloat(j, "textScaleSmall",    textScaleSmall);
    textScaleMedium   = readFloat(j, "textScaleMedium",   textScaleMedium);
    textScaleLarge    = readFloat(j, "textScaleLarge",    textScaleLarge);
    textScaleTitle    = readFloat(j, "textScaleTitle",    textScaleTitle);

    // TextInput
    inputBackground   = readColor(j, "inputBackground",   inputBackground);
    inputBorder       = readColor(j, "inputBorder",       inputBorder);
    inputBorderFocused = readColor(j, "inputBorderFocused", inputBorderFocused);
    inputText         = readColor(j, "inputText",         inputText);
    inputPlaceholder  = readColor(j, "inputPlaceholder",  inputPlaceholder);
    inputSelection    = readColor(j, "inputSelection",    inputSelection);
    inputCursor       = readColor(j, "inputCursor",       inputCursor);

    // Toggle
    toggleTrackOff    = readColor(j, "toggleTrackOff",    toggleTrackOff);
    toggleTrackOn     = readColor(j, "toggleTrackOn",     toggleTrackOn);
    toggleKnob        = readColor(j, "toggleKnob",        toggleKnob);
    toggleKnobHover   = readColor(j, "toggleKnobHover",   toggleKnobHover);
    toggleWidth       = readFloat(j, "toggleWidth",       toggleWidth);
    toggleHeight      = readFloat(j, "toggleHeight",      toggleHeight);

    // RadioButton
    radioOuter        = readColor(j, "radioOuter",        radioOuter);
    radioOuterHover   = readColor(j, "radioOuterHover",   radioOuterHover);
    radioInner        = readColor(j, "radioInner",        radioInner);
    radioSize         = readFloat(j, "radioSize",         radioSize);

    // ProgressBar
    progressTrack     = readColor(j, "progressTrack",     progressTrack);
    progressFill      = readColor(j, "progressFill",      progressFill);
    progressText      = readColor(j, "progressText",      progressText);

    // TabControl
    tabHeader         = readColor(j, "tabHeader",         tabHeader);
    tabHeaderActive   = readColor(j, "tabHeaderActive",   tabHeaderActive);
    tabHeaderHover    = readColor(j, "tabHeaderHover",    tabHeaderHover);
    tabIndicator      = readColor(j, "tabIndicator",      tabIndicator);
    tabContent        = readColor(j, "tabContent",        tabContent);
    tabHeaderHeight   = readFloat(j, "tabHeaderHeight",   tabHeaderHeight);

    // ContextMenu
    contextMenuBackground = readColor(j, "contextMenuBackground", contextMenuBackground);
    contextMenuBorder     = readColor(j, "contextMenuBorder",     contextMenuBorder);
    contextMenuItemHover  = readColor(j, "contextMenuItemHover",  contextMenuItemHover);
    contextMenuSeparator  = readColor(j, "contextMenuSeparator",  contextMenuSeparator);
    contextMenuItemHeight = readFloat(j, "contextMenuItemHeight", contextMenuItemHeight);
    contextMenuWidth      = readFloat(j, "contextMenuWidth",      contextMenuWidth);

    // Toast
    toastBackground   = readColor(j, "toastBackground",   toastBackground);
    toastText         = readColor(j, "toastText",         toastText);
    toastInfo         = readColor(j, "toastInfo",         toastInfo);
    toastSuccess      = readColor(j, "toastSuccess",      toastSuccess);
    toastWarning      = readColor(j, "toastWarning",      toastWarning);
    toastError        = readColor(j, "toastError",        toastError);
    toastWidth        = readFloat(j, "toastWidth",        toastWidth);
    toastHeight       = readFloat(j, "toastHeight",       toastHeight);

    return true;
}
