#include "SettingsScreen.h"

#include "../widgets/UIPanel.h"
#include "../widgets/UIText.h"
#include "../widgets/UIButton.h"
#include "../widgets/UISlider.h"
#include "../widgets/UIToggle.h"
#include "../widgets/UIDropdown.h"
#include "../widgets/UITabControl.h"
#include "../widgets/UIScrollArea.h"
#include "../layout/UIStackLayout.h"
#include "../../locale/LocaleManager.h"
#include "../../renderer/core/RenderSettings.h"
#include "../../renderer/core/RenderScene.h"
#include "../../renderer/rhi/RhiDevice.h"
#include "../../renderer/rhi/RhiDeviceFactory.h"
#include "../../resource/ResourceMgr.h"
#include "../../world/World.h"
#include "../../app/AppSettings.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <sstream>

// ---------------------------------------------------------------------------
// Helper: get locale string with fallback
// ---------------------------------------------------------------------------
static std::string loc(const LocaleManager* lm, const char* key, const char* fallback) {
    return lm ? std::string(lm->tr(key)) : fallback;
}

static std::string formatSliderValue(float value, float step) {
    std::ostringstream out;
    if (step >= 1.0f) {
        out << static_cast<int>(std::round(value));
    } else if (step >= 0.01f) {
        out << std::fixed << std::setprecision(2) << value;
    } else {
        out << std::fixed << std::setprecision(4) << value;
    }
    return out.str();
}

static void resizeSettingsStack(UIStackLayout* stack,
                                const std::vector<UIStackLayout*>& rowWidgets,
                                float availableWidth);

namespace {
constexpr float kSettingsRowHeight = 48.0f;
constexpr float kSettingsRowTextScale = 1.55f;
constexpr float kSettingsValueTextScale = 1.40f;
constexpr float kSettingsSectionTextScale = 1.70f;
}

// ===========================================================================
// buildUI
// ===========================================================================

void SettingsScreen::buildUI(ResourceMgr& resourceMgr) {
    (void)resourceMgr;
    m_tabLayouts.clear();
    m_settingRows.clear();

    // -- Dark overlay covering the whole screen --
    auto overlay = std::make_unique<UIPanel>();
    overlay->setTone(UIPanelTone::Overlay);
    overlay->alpha = 0.0f;
    overlay->anchor = Anchor::BottomLeft;
    overlay->x = 0.0f;
    overlay->y = 0.0f;
    overlay->width = 9999.0f;
    overlay->height = 9999.0f;
    m_overlay = overlay.get();

    // -- "SETTINGS" title --
    auto title = std::make_unique<UIText>();
    title->setText(loc(getLocaleManager(), "settings", "SETTINGS"));
    title->setTextScale(2.6f);
    title->setTone(UITextTone::OnOverlay);
    title->setAlignment(TextAlignment::Center);
    title->anchor = Anchor::TopCenter;
    title->anchorOffsetY = -20.0f;
    title->width = 400.0f;
    title->height = 40.0f;
    m_title = title.get();

    // -- Tab control --
    auto tabs = std::make_unique<UITabControl>();
    tabs->anchor = Anchor::Center;
    tabs->anchorOffsetY = -10.0f;
    tabs->width = 780.0f;
    tabs->height = 500.0f;

    const char* tabNames[] = {"tab_general", "tab_shadows", "tab_lighting",
                              "tab_postprocess", "tab_volumetric", "tab_upscale"};
    const char* tabFallbacks[] = {"General", "Shadows", "Lighting",
                                  "Post Process", "Volumetric", "Upscale"};

    for (int i = 0; i < 6; ++i) {
        tabs->addTab(loc(getLocaleManager(), tabNames[i], tabFallbacks[i]));
    }

    // Build each tab's content
    buildGeneralTab(tabs->getContentPanel(0), resourceMgr);
    buildShadowsTab(tabs->getContentPanel(1), resourceMgr);
    buildLightingTab(tabs->getContentPanel(2), resourceMgr);
    buildPostProcessTab(tabs->getContentPanel(3), resourceMgr);
    buildVolumetricTab(tabs->getContentPanel(4), resourceMgr);
    buildUpscaleTab(tabs->getContentPanel(5), resourceMgr);

    m_tabControl = tabs.get();

    // -- Back button --
    auto backBtn = std::make_unique<UIButton>();
    backBtn->setText(loc(getLocaleManager(), "back", "BACK"));
    backBtn->setTextScale(1.8f);
    backBtn->width = 220.0f;
    backBtn->height = 42.0f;
    backBtn->anchor = Anchor::BottomCenter;
    backBtn->anchorOffsetY = 40.0f;
    backBtn->setTone(UIButtonTone::Secondary);
    backBtn->setOnClick([this]() {
        if (onBack) onBack();
    });
    m_backButton = backBtn.get();

    // Add roots (overlay first so it's behind everything)
    addRoot(std::move(overlay));
    addRoot(std::move(title));
    addRoot(std::move(tabs));
    addRoot(std::move(backBtn));

    // Register tweens
    registerFloatTween(m_overlayAlpha);
    registerFloatTween(m_contentSlideX);
}

// ===========================================================================
// Scene lifecycle
// ===========================================================================

void SettingsScreen::onSceneEnter() {
    m_overlayAlpha.start(0.0f, 1.0f, kOverlayFadeDuration, EasingType::EaseOut);
    m_contentSlideX.start(200.0f, 0.0f, kContentSlideDuration, EasingType::BackOut);
}

void SettingsScreen::onSceneExit() {
    m_overlayAlpha.start(1.0f, 0.0f, kOverlayFadeDuration * 0.5f, EasingType::EaseIn);
    m_contentSlideX.start(0.0f, 200.0f, kContentSlideDuration * 0.5f, EasingType::EaseIn);
}

void SettingsScreen::layout(const UIRenderContext& ctx) {
    UIScene::layout(ctx);

    const float screenW = static_cast<float>(std::max(1, ctx.screenWidth));
    const float screenH = static_cast<float>(std::max(1, ctx.screenHeight));
    const float sideMargin = std::clamp(screenW * 0.055f, 36.0f, 96.0f);
    const float topMargin = std::clamp(screenH * 0.045f, 24.0f, 42.0f);
    const float titleH = 40.0f;
    const float titleGap = std::clamp(screenH * 0.030f, 16.0f, 28.0f);
    const float bottomMargin = std::clamp(screenH * 0.045f, 30.0f, 54.0f);
    const float backH = std::clamp(screenH * 0.045f, 40.0f, 58.0f);
    const float backGap = std::clamp(screenH * 0.045f, 34.0f, 58.0f);

    if (m_overlay) {
        m_overlay->width = screenW;
        m_overlay->height = screenH;
        m_overlay->anchor = Anchor::BottomLeft;
        m_overlay->x = 0.0f;
        m_overlay->y = 0.0f;
        m_overlay->anchorOffsetX = 0.0f;
        m_overlay->anchorOffsetY = 0.0f;
    }

    if (m_title) {
        m_title->width = std::min(520.0f, std::max(240.0f, screenW - sideMargin * 2.0f));
        m_title->height = titleH;
        m_title->anchor = Anchor::TopCenter;
        m_title->anchorOffsetX = 0.0f;
        m_title->anchorOffsetY = -topMargin;
        m_title->x = 0.0f;
        m_title->y = 0.0f;
    }

    if (m_backButton) {
        m_backButton->width = std::clamp(screenW * 0.20f, 220.0f, 380.0f);
        m_backButton->height = backH;
        m_backButton->anchor = Anchor::BottomCenter;
        m_backButton->anchorOffsetX = 0.0f;
        m_backButton->anchorOffsetY = bottomMargin;
        m_backButton->x = 0.0f;
        m_backButton->y = 0.0f;
    }

    if (!m_tabControl) {
        return;
    }

    const float tabsTop = screenH - topMargin - titleH - titleGap;
    const float tabsBottom = bottomMargin + backH + backGap;
    const float maxTabsW = std::min(1120.0f, screenW - sideMargin * 2.0f);
    const float tabsW = std::max(320.0f, maxTabsW);
    const float tabsH = std::max(180.0f, tabsTop - tabsBottom);

    m_tabControl->anchor = Anchor::BottomCenter;
    m_tabControl->width = tabsW;
    m_tabControl->height = tabsH;
    m_tabControl->anchorOffsetX = 0.0f;
    m_tabControl->anchorOffsetY = tabsBottom;

    const float headerH = m_tabControl->getHeaderHeight(ctx);
    const float contentH = std::max(80.0f, tabsH - headerH);
    for (int i = 0; i < m_tabControl->getTabCount() && i < static_cast<int>(m_tabLayouts.size()); ++i) {
        const SettingsTabLayout& tabLayout = m_tabLayouts[static_cast<size_t>(i)];
        UIWidget* contentPanel = m_tabControl->getContentPanel(i);
        if (!contentPanel) {
            continue;
        }
        contentPanel->width = tabsW;
        contentPanel->height = contentH;
        UIScrollArea* scroll = tabLayout.scroll;
        UIStackLayout* stack = tabLayout.stack;
        if (!scroll || !stack) {
            continue;
        }

        scroll->width = tabsW;
        scroll->height = contentH;
        scroll->anchor = Anchor::BottomLeft;
        scroll->x = 0.0f;
        scroll->y = 0.0f;

        stack->x = 18.0f;
        resizeSettingsStack(stack, m_settingRows, std::max(1.0f, tabsW - 48.0f));
        stack->y = scroll->height - stack->height;
        scroll->setContentHeight(stack->height);
    }
}

void SettingsScreen::updateAnimations(float dt) {
    UIScene::updateAnimations(dt);

    if (m_overlay) {
        m_overlay->alpha = m_overlayAlpha.value();
    }
    if (m_tabControl) {
        m_tabControl->x = m_contentSlideX.value();
    }
}

// ===========================================================================
// Helper: add a scrollable stack to a tab content panel
// ===========================================================================

SettingsScreen::SettingsTabLayout SettingsScreen::setupScrollableTab(UIWidget* contentPanel,
                                                                     ResourceMgr& /*resourceMgr*/,
                                                                     float tabWidth,
                                                                     float tabHeight) {
    auto scroll = std::make_unique<UIScrollArea>();
    scroll->width = tabWidth;
    scroll->height = tabHeight;
    scroll->anchor = Anchor::BottomLeft;
    scroll->x = 0.0f;
    scroll->y = 0.0f;

    auto stack = std::make_unique<UIStackLayout>();
    stack->setDirection(StackDirection::Vertical);
    stack->setSpacing(10.0f);
    stack->anchor = Anchor::BottomLeft;
    stack->x = 18.0f;
    stack->width = tabWidth - 48.0f;

    UIStackLayout* stackPtr = stack.get();
    UIScrollArea* scrollPtr = scroll.get();
    scroll->addChild(std::move(stack));
    contentPanel->addChild(std::move(scroll));

    return SettingsScreen::SettingsTabLayout{scrollPtr, stackPtr};
}

// After all rows are added, call this to finalize the scroll area height
static void finalizeScrollTab(UIScrollArea* scroll, UIStackLayout* stack) {
    if (!scroll || !stack) {
        return;
    }

    stack->layout();
    stack->x = 18.0f;
    stack->y = scroll->height - stack->height;
    scroll->setContentHeight(stack->height);
    scroll->setScrollOffset(0.0f);
}

static UIStackLayout* findRegisteredSettingsRow(const UIStackLayout* stack,
                                                const UIWidget* child,
                                                const std::vector<UIStackLayout*>& rowWidgets) {
    if (!stack || !child) {
        return nullptr;
    }
    const auto it = std::find_if(rowWidgets.begin(), rowWidgets.end(),
                                 [stack, child](const UIStackLayout* row) {
                                     return row != nullptr && row->getParent() == stack && row == child;
                                 });
    return it == rowWidgets.end() ? nullptr : *it;
}

static void resizeSettingsStack(UIStackLayout* stack,
                                const std::vector<UIStackLayout*>& rowWidgets,
                                float availableWidth) {
    if (!stack) {
        return;
    }

    const float rowWidth = std::max(260.0f, availableWidth);
    for (const auto& child : stack->getChildren()) {
        UIStackLayout* row = findRegisteredSettingsRow(stack, child.get(), rowWidgets);
        if (row != nullptr) {
            auto& rowChildren = row->getChildren();
            const bool hasValueText = rowChildren.size() >= 3;
            const float spacing = row->getSpacing();
            const float valueW = hasValueText ? 120.0f : 0.0f;
            const float totalSpacing = spacing * static_cast<float>(rowChildren.size() > 0 ? rowChildren.size() - 1 : 0);
            const float labelW = std::clamp(rowWidth * 0.34f, 220.0f, 330.0f);
            const float controlW = std::max(180.0f, rowWidth - labelW - valueW - totalSpacing);

            row->width = rowWidth;
            row->height = kSettingsRowHeight;
            if (rowChildren.size() >= 1) {
                rowChildren[0]->width = labelW;
                rowChildren[0]->height = kSettingsRowHeight;
            }
            if (rowChildren.size() >= 2) {
                rowChildren[1]->width = controlW;
                rowChildren[1]->height = kSettingsRowHeight;
            }
            if (rowChildren.size() >= 3) {
                rowChildren[2]->width = valueW;
                rowChildren[2]->height = kSettingsRowHeight;
            }
            row->layout();
        } else {
            child->width = rowWidth;
        }
    }
    stack->width = rowWidth;
    stack->layout();
}

// ===========================================================================
// Helper implementations
// ===========================================================================

void SettingsScreen::addSectionHeader(UIWidget* parent, ResourceMgr& /*resourceMgr*/,
                                       const std::string& text) {
    auto header = std::make_unique<UIText>();
    header->setText(text);
    header->setTextScale(kSettingsSectionTextScale);
    header->setTone(UITextTone::Accent);
    header->width = 360.0f;
    header->height = 42.0f;
    parent->addChild(std::move(header));
}

void SettingsScreen::addToggle(UIWidget* parent, ResourceMgr& resourceMgr,
                                const std::string& label, bool checked,
                                std::function<void(bool)> onChanged,
                                const bool interactive) {
    (void)resourceMgr;
    auto toggle = std::make_unique<UIToggle>();
    toggle->setLabel(label);
    toggle->setLabelTextScale(kSettingsRowTextScale);
    toggle->setChecked(checked);
    toggle->interactive = interactive;
    toggle->width = 520.0f;
    toggle->height = kSettingsRowHeight;
    toggle->onChanged = std::move(onChanged);
    parent->addChild(std::move(toggle));
}

void SettingsScreen::addSliderRow(UIWidget* parent, ResourceMgr& resourceMgr,
                                   const std::string& label, float minVal, float maxVal,
                                   float currentVal, float step,
                                   std::function<void(float)> onValueChanged,
                                   const bool interactive) {
    (void)resourceMgr;
    auto row = std::make_unique<UIStackLayout>();
    row->setDirection(StackDirection::Horizontal);
    row->setSpacing(10.0f);
    row->width = 720.0f;
    row->height = kSettingsRowHeight;

    auto lbl = std::make_unique<UIText>();
    lbl->setText(label);
    lbl->setTextScale(kSettingsRowTextScale);
    lbl->setTone(UITextTone::Secondary);
    lbl->width = 280.0f;
    lbl->height = kSettingsRowHeight;

    auto slider = std::make_unique<UISlider>();
    slider->setRange(minVal, maxVal);
    slider->setStep(step);
    slider->setValue(currentVal);
    slider->interactive = interactive;
    slider->width = 390.0f;
    slider->height = kSettingsRowHeight;

    auto valueText = std::make_unique<UIText>();
    valueText->setText(formatSliderValue(currentVal, step));
    valueText->setTextScale(kSettingsValueTextScale);
    valueText->setTone(UITextTone::Secondary);
    valueText->width = 120.0f;
    valueText->height = kSettingsRowHeight;
    UIText* valueTextPtr = valueText.get();

    slider->setOnValueChanged([valueTextPtr, step, onValueChanged = std::move(onValueChanged)](float v) mutable {
        if (valueTextPtr) {
            valueTextPtr->setText(formatSliderValue(v, step));
        }
        if (onValueChanged) {
            onValueChanged(v);
        }
    });

    row->addChild(std::move(lbl));
    row->addChild(std::move(slider));
    row->addChild(std::move(valueText));
    row->layout();

    m_settingRows.push_back(row.get());
    parent->addChild(std::move(row));
}

void SettingsScreen::addDropdownRow(UIWidget* parent, ResourceMgr& resourceMgr,
                                     const std::string& label,
                                     const std::vector<std::string>& options,
                                     int currentIndex,
                                     std::function<void(int, const std::string&)> onSelectionChanged) {
    (void)resourceMgr;
    auto row = std::make_unique<UIStackLayout>();
    row->setDirection(StackDirection::Horizontal);
    row->setSpacing(10.0f);
    row->width = 720.0f;
    row->height = kSettingsRowHeight;

    auto lbl = std::make_unique<UIText>();
    lbl->setText(label);
    lbl->setTextScale(kSettingsRowTextScale);
    lbl->setTone(UITextTone::Secondary);
    lbl->width = 280.0f;
    lbl->height = kSettingsRowHeight;

    auto dropdown = std::make_unique<UIDropdown>();
    dropdown->setOptions(std::vector<std::string>(options));
    dropdown->setSelectedIndex(currentIndex);
    dropdown->width = 320.0f;
    dropdown->height = kSettingsRowHeight;
    dropdown->setOnSelectionChanged(std::move(onSelectionChanged));

    row->addChild(std::move(lbl));
    row->addChild(std::move(dropdown));
    row->layout();

    m_settingRows.push_back(row.get());
    parent->addChild(std::move(row));
}

// ===========================================================================
// Tab builders
// ===========================================================================

void SettingsScreen::buildGeneralTab(UIWidget* contentPanel, ResourceMgr& resourceMgr) {
    // Tab content area is approximately 700 x (460 - headerHeight)
    constexpr float tabContentH = 420.0f;
    const SettingsTabLayout tabLayout = setupScrollableTab(contentPanel, resourceMgr, 700.0f, tabContentH);
    m_tabLayouts.push_back(tabLayout);
    UIStackLayout* stack = tabLayout.stack;

    addSectionHeader(stack, resourceMgr,
                     loc(getLocaleManager(), "setting_render_distance", "Render Distance"));

    if (m_world) {
        const int rd = m_world->getRenderDistance();
        addSliderRow(stack, resourceMgr,
                     loc(getLocaleManager(), "setting_render_distance", "Render Distance"),
                     2.0f, 32.0f, static_cast<float>(rd), 1.0f,
                     [this](float val) {
                         const int distance = static_cast<int>(std::round(val));
                         if (m_renderDistanceSetter) {
                             m_renderDistanceSetter(distance);
                         } else if (m_world) {
                             m_world->setRenderDistance(distance);
                         }
                         app::saveRenderDistance(distance);
                     });
    }

    const RhiBackend currentBackend = resourceMgr.rhiDevice().backend();
    const app::RhiBackendSettingResult backendSetting = app::loadRhiBackend();
    const RhiBackend selectedBackend = backendSetting.backend.value_or(currentBackend);
    const std::array<RhiBackend, 2> knownBackends = {
        RhiBackend::OpenGL,
        RhiBackend::Vulkan,
    };
    std::vector<RhiBackend> availableBackends;
    std::vector<std::string> backendNames;
    int selectedIndex = 0;
    for (const RhiBackend backend : knownBackends) {
        if (!renderer::rhi::isRhiBackendAvailable(backend)) {
            continue;
        }
        if (backend == selectedBackend) {
            selectedIndex = static_cast<int>(availableBackends.size());
        }
        availableBackends.push_back(backend);
        backendNames.emplace_back(renderer::rhi::rhiBackendDisplayName(backend));
    }

    const std::string backendSection =
        loc(getLocaleManager(), "setting_graphics_backend", "Graphics Backend") +
        " - " + loc(getLocaleManager(), "setting_restart_to_apply", "Applies After Restart");
    addSectionHeader(stack, resourceMgr, backendSection);

    const std::string currentBackendLabel =
        loc(getLocaleManager(), "setting_current_backend", "Current") +
        ": " + renderer::rhi::rhiBackendDisplayName(currentBackend);
    addDropdownRow(stack, resourceMgr,
                   currentBackendLabel,
                   backendNames,
                   selectedIndex,
                   [availableBackends = std::move(availableBackends)](const int index, const std::string&) {
                       if (index < 0 || index >= static_cast<int>(availableBackends.size())) {
                           return;
                       }
                       app::saveRhiBackend(availableBackends[static_cast<size_t>(index)]);
                   });

    addSectionHeader(stack, resourceMgr,
                     loc(getLocaleManager(), "setting_display", "Display"));
    RhiDevice* const rhiDevice = &resourceMgr.rhiDevice();
    addToggle(stack, resourceMgr,
              loc(getLocaleManager(), "setting_vsync", "Vertical Sync"),
              rhiDevice->vsyncEnabled(),
              [rhiDevice](const bool enabled) {
                  if (!rhiDevice->setVsyncEnabled(enabled)) {
                      std::abort();
                  }
                  app::saveVsyncEnabled(enabled);
              },
              rhiDevice->capabilities().vsyncControl);

    finalizeScrollTab(tabLayout.scroll, stack);
}

void SettingsScreen::buildShadowsTab(UIWidget* contentPanel, ResourceMgr& resourceMgr) {
    constexpr float tabContentH = 420.0f;
    const SettingsTabLayout tabLayout = setupScrollableTab(contentPanel, resourceMgr, 700.0f, tabContentH);
    m_tabLayouts.push_back(tabLayout);
    UIStackLayout* stack = tabLayout.stack;

    if (!m_renderScene) { finalizeScrollTab(tabLayout.scroll, stack); return; }
    RenderSettings s = m_renderScene->getSettings();

    addSectionHeader(stack, resourceMgr, "Shadows");

    addToggle(stack, resourceMgr, "Sun Shadows", s.shadow.enabled,
              [this](bool v) {
                  auto s = m_renderScene->getSettings(); s.shadow.enabled = v;
                  m_renderScene->setSettings(s);
              });
    addToggle(stack, resourceMgr, "Soft Shadows", s.shadow.softShadowsEnabled,
              [this](bool v) {
                  auto s = m_renderScene->getSettings(); s.shadow.softShadowsEnabled = v;
                  m_renderScene->setSettings(s);
              });
    addToggle(stack, resourceMgr, "PCSS Shadows", s.shadow.pcssShadowsEnabled,
              [this](bool v) {
                  auto s = m_renderScene->getSettings(); s.shadow.pcssShadowsEnabled = v;
                  m_renderScene->setSettings(s);
              });
    addToggle(stack, resourceMgr, "Contact Shadows", s.shadow.contactShadowsEnabled,
              [this](bool v) {
                  auto s = m_renderScene->getSettings(); s.shadow.contactShadowsEnabled = v;
                  m_renderScene->setSettings(s);
              });
    addToggle(stack, resourceMgr, "Cloud Shadows", s.cloud.shadowsEnabled,
              [this](bool v) {
                  auto s = m_renderScene->getSettings(); s.cloud.shadowsEnabled = v;
                  m_renderScene->setSettings(s);
              });

    addSliderRow(stack, resourceMgr, "Shadow Resolution",
                 512.0f, 4096.0f, static_cast<float>(s.shadow.resolution), 256.0f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.shadow.resolution = static_cast<int>(v);
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Shadow Distance",
                 64.0f, 192.0f, s.shadow.distance, 1.0f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.shadow.distance = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Shadow Softness",
                 0.1f, 4.0f, s.shadow.softness, 0.1f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.shadow.softness = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "PCSS Strength",
                 0.0f, 1.5f, s.shadow.pcssStrength, 0.01f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.shadow.pcssStrength = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Const Bias",
                 0.0f, 0.004f, s.shadow.constantBias, 0.0001f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.shadow.constantBias = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Slope Bias",
                 0.0f, 0.012f, s.shadow.slopeBias, 0.0001f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.shadow.slopeBias = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Normal Offset",
                 0.0f, 0.12f, s.shadow.normalOffset, 0.001f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.shadow.normalOffset = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Contact Shadow Strength",
                 0.0f, 0.6f, s.shadow.contactShadowStrength, 0.01f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.shadow.contactShadowStrength = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Cloud Shadow Strength",
                 0.0f, 0.8f, s.cloud.shadowStrength, 0.01f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.cloud.shadowStrength = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Cloud Shadow Scale",
                 0.001f, 0.02f, s.cloud.shadowScale, 0.001f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.cloud.shadowScale = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Cloud Shadow Speed",
                 0.0f, 0.08f, s.cloud.shadowSpeed, 0.001f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.cloud.shadowSpeed = v;
                     m_renderScene->setSettings(s);
                 });

    finalizeScrollTab(tabLayout.scroll, stack);
}

void SettingsScreen::buildLightingTab(UIWidget* contentPanel, ResourceMgr& resourceMgr) {
    constexpr float tabContentH = 420.0f;
    const SettingsTabLayout tabLayout = setupScrollableTab(contentPanel, resourceMgr, 700.0f, tabContentH);
    m_tabLayouts.push_back(tabLayout);
    UIStackLayout* stack = tabLayout.stack;

    if (!m_renderScene) { finalizeScrollTab(tabLayout.scroll, stack); return; }
    RenderSettings s = m_renderScene->getSettings();

    addSectionHeader(stack, resourceMgr, "SSGI");

    addToggle(stack, resourceMgr, "SSGI", s.ssgi.enabled,
              [this](bool v) {
                  auto s = m_renderScene->getSettings(); s.ssgi.enabled = v;
                  m_renderScene->setSettings(s);
              });
    addToggle(stack, resourceMgr, "SSGI Temporal", s.ssgi.temporalEnabled,
              [this](bool v) {
                  auto s = m_renderScene->getSettings(); s.ssgi.temporalEnabled = v;
                  m_renderScene->setSettings(s);
              });
    addToggle(stack, resourceMgr, "SSGI Denoise", s.ssgi.denoiseEnabled,
              [this](bool v) {
                  auto s = m_renderScene->getSettings(); s.ssgi.denoiseEnabled = v;
                  m_renderScene->setSettings(s);
              });
    addSliderRow(stack, resourceMgr, "SSGI Radius",
                 0.5f, 24.0f, s.ssgi.radius, 0.05f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.ssgi.radius = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "SSGI Strength",
                 0.0f, 4.0f, s.ssgi.strength, 0.01f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.ssgi.strength = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "SSGI Samples",
                 1.0f, 32.0f, static_cast<float>(s.ssgi.samples), 1.0f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.ssgi.samples = static_cast<int>(v);
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "SSGI Max Distance",
                 1.0f, 48.0f, s.ssgi.maxDistance, 0.5f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.ssgi.maxDistance = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "SSGI Thickness",
                 0.1f, 8.0f, s.ssgi.thickness, 0.05f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.ssgi.thickness = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "SSGI Denoise Passes",
                 0.0f, 4.0f, static_cast<float>(s.ssgi.denoiseIterations), 1.0f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.ssgi.denoiseIterations = static_cast<int>(v);
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "SSGI Denoise Strength",
                 0.0f, 1.0f, s.ssgi.denoiseStrength, 0.01f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.ssgi.denoiseStrength = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "SSGI Radiance Filter",
                 0.0f, 1.0f, s.ssgi.radianceFilterStrength, 0.01f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.ssgi.radianceFilterStrength = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "SSGI Color Bleed",
                 0.0f, 1.0f, s.ssgi.colorBleedStrength, 0.01f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.ssgi.colorBleedStrength = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "SSGI History Weight",
                 0.0f, 0.98f, s.ssgi.historyWeight, 0.01f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.ssgi.historyWeight = v;
                     m_renderScene->setSettings(s);
                 });

    addSectionHeader(stack, resourceMgr, "Voxel GI");

    addToggle(stack, resourceMgr, "Voxel GI", s.voxelGi.enabled,
              [this](bool v) {
                  auto s = m_renderScene->getSettings(); s.voxelGi.enabled = v;
                  m_renderScene->setSettings(s);
              });
    addToggle(stack, resourceMgr, "Voxel GI Debug", s.voxelGi.debugEnabled,
              [this](bool v) {
                  auto s = m_renderScene->getSettings(); s.voxelGi.debugEnabled = v;
                  m_renderScene->setSettings(s);
              });
    addSliderRow(stack, resourceMgr, "Voxel GI Strength",
                 0.0f, 1.0f, s.voxelGi.strength, 0.01f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.voxelGi.strength = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Voxel GI Resolution",
                 16.0f, 128.0f, static_cast<float>(s.voxelGi.resolution), 16.0f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.voxelGi.resolution = static_cast<int>(v);
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Voxel GI Update Frames",
                 1.0f, 20.0f, static_cast<float>(s.voxelGi.updateInterval), 1.0f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.voxelGi.updateInterval = static_cast<int>(v);
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Voxel GI Cone Steps",
                 1.0f, 12.0f, static_cast<float>(s.voxelGi.coneSteps), 1.0f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.voxelGi.coneSteps = static_cast<int>(v);
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Voxel GI Origin Snap",
                 1.0f, 32.0f, static_cast<float>(s.voxelGi.originSnap), 1.0f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.voxelGi.originSnap = static_cast<int>(v);
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Voxel GI Voxel Size",
                 1.0f, 4.0f, s.voxelGi.voxelSize, 1.0f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.voxelGi.voxelSize = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Voxel GI Normal Bias",
                 0.0f, 2.0f, s.voxelGi.normalBias, 0.05f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.voxelGi.normalBias = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Voxel GI Sample Distance",
                 1.0f, 12.0f, s.voxelGi.sampleDistance, 0.25f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.voxelGi.sampleDistance = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Voxel GI Trace Distance",
                 4.0f, 48.0f, s.voxelGi.traceDistance, 1.0f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.voxelGi.traceDistance = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Voxel GI Cone Aperture",
                 0.05f, 1.50f, s.voxelGi.coneAperture, 0.01f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.voxelGi.coneAperture = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Voxel GI Occupancy Scale",
                 0.0f, 2.0f, s.voxelGi.occupancyScale, 0.01f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.voxelGi.occupancyScale = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Voxel GI Occlusion Strength",
                 0.0f, 4.0f, s.voxelGi.occlusionStrength, 0.01f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.voxelGi.occlusionStrength = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Voxel GI Sky Bounce",
                 0.0f, 2.0f, s.voxelGi.skyBounceStrength, 0.01f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.voxelGi.skyBounceStrength = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Voxel GI Sun Bounce",
                 0.0f, 3.0f, s.voxelGi.sunBounceStrength, 0.01f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.voxelGi.sunBounceStrength = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Voxel GI Shadow Boost",
                 0.0f, 2.0f, s.voxelGi.receiverShadowBoost, 0.01f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.voxelGi.receiverShadowBoost = v;
                     m_renderScene->setSettings(s);
                 });

    addSectionHeader(stack, resourceMgr, "SSAO");

    addToggle(stack, resourceMgr, "SSAO", s.ssao.enabled,
              [this](bool v) {
                  auto s = m_renderScene->getSettings(); s.ssao.enabled = v;
                  m_renderScene->setSettings(s);
              });
    addToggle(stack, resourceMgr, "SSAO Temporal", s.ssao.temporalEnabled,
              [this](bool v) {
                  auto s = m_renderScene->getSettings(); s.ssao.temporalEnabled = v;
                  m_renderScene->setSettings(s);
              });
    addSliderRow(stack, resourceMgr, "SSAO Radius",
                 0.25f, 8.0f, s.ssao.radius, 0.05f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.ssao.radius = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "SSAO Strength",
                 0.0f, 2.0f, s.ssao.strength, 0.01f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.ssao.strength = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "SSAO Samples",
                 1.0f, 64.0f, static_cast<float>(s.ssao.samples), 1.0f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.ssao.samples = static_cast<int>(v);
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "SSAO History Weight",
                 0.0f, 0.98f, s.ssao.historyWeight, 0.01f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.ssao.historyWeight = v;
                     m_renderScene->setSettings(s);
                 });

    addSectionHeader(stack, resourceMgr, "Lighting");

    addSliderRow(stack, resourceMgr, "Direct Sun Strength",
                 0.0f, 3.0f, s.postProcess.directSunStrength, 0.01f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.postProcess.directSunStrength = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Sky Ambient",
                 0.0f, 1.5f, s.postProcess.skyAmbientStrength, 0.01f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.postProcess.skyAmbientStrength = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Minimum Ambient",
                 0.0f, 0.4f, s.postProcess.minimumAmbient, 0.005f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.postProcess.minimumAmbient = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Shadow Min Light",
                 0.0f, 0.5f, s.postProcess.shadowMinLight, 0.01f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.postProcess.shadowMinLight = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Shadow Contrast",
                 0.5f, 2.5f, s.postProcess.shadowContrast, 0.01f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.postProcess.shadowContrast = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Block Light",
                 0.0f, 2.5f, s.postProcess.blockLightStrength, 0.01f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.postProcess.blockLightStrength = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Fake Bounce",
                 0.0f, 0.3f, s.postProcess.fakeBounceStrength, 0.005f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.postProcess.fakeBounceStrength = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Sun Warmth",
                 0.0f, 1.5f, s.postProcess.sunWarmth, 0.01f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.postProcess.sunWarmth = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Sky Coolness",
                 0.0f, 1.0f, s.postProcess.skyCoolness, 0.01f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.postProcess.skyCoolness = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Shadow Desaturation",
                 0.0f, 1.0f, s.postProcess.shadowDesaturation, 0.01f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.postProcess.shadowDesaturation = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Shadow Tint Strength",
                 0.0f, 0.8f, s.postProcess.shadowTintStrength, 0.01f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.postProcess.shadowTintStrength = v;
                     m_renderScene->setSettings(s);
                 });

    addSectionHeader(stack, resourceMgr, "Block Materials");

    addToggle(stack, resourceMgr, "Block PBR Maps", s.blockMaterialMaps.enabled,
              [this](bool v) {
                  auto s = m_renderScene->getSettings(); s.blockMaterialMaps.enabled = v;
                  m_renderScene->setSettings(s);
              });
    addToggle(stack, resourceMgr, "Block Normal Maps", s.blockMaterialMaps.normalMapsEnabled,
              [this](bool v) {
                  auto s = m_renderScene->getSettings(); s.blockMaterialMaps.normalMapsEnabled = v;
                  m_renderScene->setSettings(s);
              });
    addToggle(stack, resourceMgr, "Block Specular Maps", s.blockMaterialMaps.specularMapsEnabled,
              [this](bool v) {
                  auto s = m_renderScene->getSettings(); s.blockMaterialMaps.specularMapsEnabled = v;
                  m_renderScene->setSettings(s);
              });
    addToggle(stack, resourceMgr, "Block Parallax Maps", s.blockMaterialMaps.parallaxMapsEnabled,
              [this](bool v) {
                  auto s = m_renderScene->getSettings(); s.blockMaterialMaps.parallaxMapsEnabled = v;
                  m_renderScene->setSettings(s);
              });
    addSliderRow(stack, resourceMgr, "Block Parallax Depth",
                 0.0f, 0.12f, s.blockMaterialMaps.parallaxDepth, 0.001f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.blockMaterialMaps.parallaxDepth = v;
                     m_renderScene->setSettings(s);
                 });

    addSectionHeader(stack, resourceMgr, "Transparent");

    addToggle(stack, resourceMgr, "Water Effects", s.transparent.waterEffectsEnabled,
              [this](bool v) {
                  auto s = m_renderScene->getSettings(); s.transparent.waterEffectsEnabled = v;
                  m_renderScene->setSettings(s);
              });
    addToggle(stack, resourceMgr, "Transparent Composite", s.transparent.compositeEnabled,
              [this](bool v) {
                  auto s = m_renderScene->getSettings(); s.transparent.compositeEnabled = v;
                  m_renderScene->setSettings(s);
              });

    addSectionHeader(stack, resourceMgr, "Rain Surfaces");

    addToggle(stack, resourceMgr, "Wet Surfaces", s.weather.wetSurfacesEnabled,
              [this](bool v) {
                  auto s = m_renderScene->getSettings(); s.weather.wetSurfacesEnabled = v;
                  m_renderScene->setSettings(s);
              });
    addToggle(stack, resourceMgr, "Surface Ripples", s.weather.surfaceRipplesEnabled,
              [this](bool v) {
                  auto s = m_renderScene->getSettings(); s.weather.surfaceRipplesEnabled = v;
                  m_renderScene->setSettings(s);
              });

    finalizeScrollTab(tabLayout.scroll, stack);
}

void SettingsScreen::buildPostProcessTab(UIWidget* contentPanel, ResourceMgr& resourceMgr) {
    constexpr float tabContentH = 420.0f;
    const SettingsTabLayout tabLayout = setupScrollableTab(contentPanel, resourceMgr, 700.0f, tabContentH);
    m_tabLayouts.push_back(tabLayout);
    UIStackLayout* stack = tabLayout.stack;

    if (!m_renderScene) { finalizeScrollTab(tabLayout.scroll, stack); return; }
    RenderSettings s = m_renderScene->getSettings();

    addSectionHeader(stack, resourceMgr, "Bloom");

    addToggle(stack, resourceMgr, "Bloom", s.postProcess.bloomEnabled,
              [this](bool v) {
                  auto s = m_renderScene->getSettings(); s.postProcess.bloomEnabled = v;
                  m_renderScene->setSettings(s);
              });
    addSliderRow(stack, resourceMgr, "Bloom Threshold",
                 0.0f, 3.0f, s.postProcess.bloomThreshold, 0.01f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.postProcess.bloomThreshold = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Bloom Strength",
                 0.0f, 20.0f, s.postProcess.bloomStrength, 0.1f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.postProcess.bloomStrength = v;
                     m_renderScene->setSettings(s);
                 });

    addSectionHeader(stack, resourceMgr, "Exposure");

    addToggle(stack, resourceMgr, "Auto Exposure", s.postProcess.autoExposureEnabled,
              [this](bool v) {
                  auto s = m_renderScene->getSettings(); s.postProcess.autoExposureEnabled = v;
                  m_renderScene->setSettings(s);
              });
    addSliderRow(stack, resourceMgr, "Auto Exp Min",
                 0.001f, 1.0f, s.postProcess.autoExposureMin, 0.001f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.postProcess.autoExposureMin = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Auto Exp Max",
                 1.0f, 64.0f, s.postProcess.autoExposureMax, 0.1f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.postProcess.autoExposureMax = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Auto Exp Speed",
                 0.1f, 6.0f, s.postProcess.autoExposureSpeed, 0.1f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.postProcess.autoExposureSpeed = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Auto Exp Bias",
                 -2.0f, 2.0f, s.postProcess.autoExposureBias, 0.05f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.postProcess.autoExposureBias = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Manual Exposure",
                 0.1f, 50.0f, s.postProcess.exposure, 0.1f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.postProcess.exposure = v;
                     m_renderScene->setSettings(s);
                 });

    addSectionHeader(stack, resourceMgr, "Tonemap & Color");

    addDropdownRow(stack, resourceMgr, "Tonemap Mode",
                   {"Reinhard", "AcademyFit", "Filmic", "AgX Minimal", "AcademyFull", "AgX Full"},
                   s.postProcess.tonemapMode,
                   [this](int idx, const std::string&) {
                       auto s = m_renderScene->getSettings(); s.postProcess.tonemapMode = idx;
                       m_renderScene->setSettings(s);
                   });
    addSliderRow(stack, resourceMgr, "Gamma",
                 1.0f, 3.0f, s.postProcess.gamma, 0.01f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.postProcess.gamma = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Saturation",
                 0.0f, 2.0f, s.postProcess.saturation, 0.01f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.postProcess.saturation = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Contrast",
                 0.5f, 2.0f, s.postProcess.contrast, 0.01f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.postProcess.contrast = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Color Temperature",
                 0.0f, 2.0f, s.postProcess.colorTemperature, 0.01f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.postProcess.colorTemperature = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Vibrance",
                 -0.5f, 0.8f, s.postProcess.vibrance, 0.01f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.postProcess.vibrance = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Highlight Compression",
                 0.0f, 1.5f, s.postProcess.highlightCompression, 0.01f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.postProcess.highlightCompression = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Vignette",
                 0.0f, 0.5f, s.postProcess.vignetteStrength, 0.01f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.postProcess.vignetteStrength = v;
                     m_renderScene->setSettings(s);
                 });

    addSectionHeader(stack, resourceMgr, "Effects");

    addToggle(stack, resourceMgr, "Sun Rays", s.postProcess.sunRaysEnabled,
              [this](bool v) {
                  auto s = m_renderScene->getSettings(); s.postProcess.sunRaysEnabled = v;
                  m_renderScene->setSettings(s);
              });
    addSliderRow(stack, resourceMgr, "Sun Ray Strength",
                 0.0f, 0.6f, s.postProcess.sunRayStrength, 0.01f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.postProcess.sunRayStrength = v;
                     m_renderScene->setSettings(s);
                 });
    addToggle(stack, resourceMgr, "Aerial Perspective", s.postProcess.aerialPerspectiveEnabled,
              [this](bool v) {
                  auto s = m_renderScene->getSettings(); s.postProcess.aerialPerspectiveEnabled = v;
                  m_renderScene->setSettings(s);
              });
    addSliderRow(stack, resourceMgr, "Aerial Strength",
                 0.0f, 1.5f, s.postProcess.aerialStrength, 0.01f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.postProcess.aerialStrength = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Horizon Scatter",
                 0.0f, 1.5f, s.postProcess.horizonScatterStrength, 0.01f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.postProcess.horizonScatterStrength = v;
                     m_renderScene->setSettings(s);
                 });
    addToggle(stack, resourceMgr, "Bloomy Fog", s.postProcess.bloomyFogEnabled,
              [this](bool v) {
                  auto s = m_renderScene->getSettings(); s.postProcess.bloomyFogEnabled = v;
                  m_renderScene->setSettings(s);
              });
    addToggle(stack, resourceMgr, "Purkinje Shift", s.postProcess.purkinjeShiftEnabled,
              [this](bool v) {
                  auto s = m_renderScene->getSettings(); s.postProcess.purkinjeShiftEnabled = v;
                  m_renderScene->setSettings(s);
              });
    addToggle(stack, resourceMgr, "Shaderpack Grading", s.postProcess.shaderpackGradingEnabled,
              [this](bool v) {
                  auto s = m_renderScene->getSettings(); s.postProcess.shaderpackGradingEnabled = v;
                  m_renderScene->setSettings(s);
              });
    addSliderRow(stack, resourceMgr, "CAS Sharpen",
                 0.0f, 0.5f, s.postProcess.sharpenStrength, 0.01f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.postProcess.sharpenStrength = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Noise Dither",
                 0.0f, 0.05f, s.postProcess.noiseDitherStrength, 0.001f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.postProcess.noiseDitherStrength = v;
                     m_renderScene->setSettings(s);
                 });

    addSectionHeader(stack, resourceMgr, "Depth of Field");

    addToggle(stack, resourceMgr, "Depth of Field", s.postProcess.dofEnabled,
              [this](bool v) {
                  auto s = m_renderScene->getSettings(); s.postProcess.dofEnabled = v;
                  m_renderScene->setSettings(s);
              });
    addSliderRow(stack, resourceMgr, "DoF Focus Distance",
                 0.5f, 50.0f, s.postProcess.dofFocusDistance, 0.5f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.postProcess.dofFocusDistance = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "DoF Aperture",
                 0.8f, 22.0f, s.postProcess.dofAperture, 0.1f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.postProcess.dofAperture = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "DoF Intensity",
                 0.0f, 1.0f, s.postProcess.dofIntensity, 0.01f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.postProcess.dofIntensity = v;
                     m_renderScene->setSettings(s);
                 });

    finalizeScrollTab(tabLayout.scroll, stack);
}

void SettingsScreen::buildVolumetricTab(UIWidget* contentPanel, ResourceMgr& resourceMgr) {
    constexpr float tabContentH = 420.0f;
    const SettingsTabLayout tabLayout = setupScrollableTab(contentPanel, resourceMgr, 700.0f, tabContentH);
    m_tabLayouts.push_back(tabLayout);
    UIStackLayout* stack = tabLayout.stack;

    if (!m_renderScene) { finalizeScrollTab(tabLayout.scroll, stack); return; }
    RenderSettings s = m_renderScene->getSettings();

    addSectionHeader(stack, resourceMgr, "Volumetric Light & Fog");

    addToggle(stack, resourceMgr, "Volumetric Light", s.volumetric.lightEnabled,
              [this](bool v) {
                  auto s = m_renderScene->getSettings(); s.volumetric.lightEnabled = v;
                  m_renderScene->setSettings(s);
              });
    addToggle(stack, resourceMgr, "Volumetric Fog", s.volumetric.fogEnabled,
              [this](bool v) {
                  auto s = m_renderScene->getSettings(); s.volumetric.fogEnabled = v;
                  m_renderScene->setSettings(s);
              });
    addToggle(stack, resourceMgr, "VFog Sky Ray March", s.volumetric.skyRayEnabled,
              [this](bool v) {
                  auto s = m_renderScene->getSettings(); s.volumetric.skyRayEnabled = v;
                  m_renderScene->setSettings(s);
              });
    addToggle(stack, resourceMgr, "VFog Time Fade", s.volumetric.timeFadeEnabled,
              [this](bool v) {
                  auto s = m_renderScene->getSettings(); s.volumetric.timeFadeEnabled = v;
                  m_renderScene->setSettings(s);
              });
    addToggle(stack, resourceMgr, "VFog Temporal", s.volumetric.temporalEnabled,
              [this](bool v) {
                  auto s = m_renderScene->getSettings(); s.volumetric.temporalEnabled = v;
                  m_renderScene->setSettings(s);
              });
    addDropdownRow(stack, resourceMgr, "VFog Quality",
                   {"Low", "Medium", "High", "Ultra"},
                   s.volumetric.qualityTier,
                   [this](int idx, const std::string&) {
                       auto s = m_renderScene->getSettings(); s.volumetric.qualityTier = idx;
                       m_renderScene->setSettings(s);
                   });
    addSliderRow(stack, resourceMgr, "VFog Samples",
                 2.0f, 50.0f, static_cast<float>(s.volumetric.fogSamples), 1.0f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.volumetric.fogSamples = static_cast<int>(v);
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "VFog Temporal Weight",
                 0.0f, 0.99f, s.volumetric.temporalWeight, 0.01f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.volumetric.temporalWeight = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "VFog Center Height",
                 0.0f, 255.0f, s.volumetric.fogCenterHeight, 1.0f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.volumetric.fogCenterHeight = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "VFog Height Spread",
                 1.0f, 200.0f, s.volumetric.fogHeightSpread, 1.0f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.volumetric.fogHeightSpread = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "VFog Noise Scale",
                 0.001f, 0.200f, s.volumetric.fogNoiseScale, 0.001f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.volumetric.fogNoiseScale = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "VFog Light Strength",
                 0.0f, 1.0f, s.volumetric.fogLightStrength, 0.01f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.volumetric.fogLightStrength = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "VFog Density Scale",
                 0.0f, 10.0f, s.volumetric.fogDensityScale, 0.1f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.volumetric.fogDensityScale = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "VFog Strength",
                 0.0f, 2.0f, s.volumetric.fogStrength, 0.01f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.volumetric.fogStrength = v;
                     m_renderScene->setSettings(s);
                 });
    addToggle(stack, resourceMgr, "UW Volumetric Light", s.volumetric.uwLightEnabled,
              [this](bool v) {
                  auto s = m_renderScene->getSettings(); s.volumetric.uwLightEnabled = v;
                  m_renderScene->setSettings(s);
              });
    addSliderRow(stack, resourceMgr, "UW VL Strength",
                 0.0f, 2.0f, s.volumetric.underwaterLightStrength, 0.01f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.volumetric.underwaterLightStrength = v;
                     m_renderScene->setSettings(s);
                 });

    addSectionHeader(stack, resourceMgr, "Clouds");

    addToggle(stack, resourceMgr, "Cloud Shadows", s.cloud.shadowsEnabled,
              [this](bool v) {
                  auto s = m_renderScene->getSettings(); s.cloud.shadowsEnabled = v;
                  m_renderScene->setSettings(s);
              });
    addSliderRow(stack, resourceMgr, "Cloud Coverage",
                 0.0f, 1.0f, s.cloud.coverage, 0.01f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.cloud.coverage = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Cloud Density",
                 0.0f, 2.5f, s.cloud.density, 0.01f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.cloud.density = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Cloud Height",
                 300.0f, 9000.0f, s.cloud.height, 50.0f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.cloud.height = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Cloud Thickness",
                 100.0f, 3000.0f, s.cloud.thickness, 25.0f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.cloud.thickness = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Cloud Shadow Strength",
                 0.0f, 0.8f, s.cloud.shadowStrength, 0.01f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.cloud.shadowStrength = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Cloud Shadow Scale",
                 0.001f, 0.02f, s.cloud.shadowScale, 0.001f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.cloud.shadowScale = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Cloud Time Scale",
                 0.05f, 2.0f, s.cloud.timeScale, 0.05f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.cloud.timeScale = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Cloud Composite",
                 0.0f, 1.0f, s.cloud.sceneCloudCompositeStrength, 0.01f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.cloud.sceneCloudCompositeStrength = v;
                     m_renderScene->setSettings(s);
                 });

    finalizeScrollTab(tabLayout.scroll, stack);
}

void SettingsScreen::buildUpscaleTab(UIWidget* contentPanel, ResourceMgr& resourceMgr) {
    constexpr float tabContentH = 420.0f;
    const SettingsTabLayout tabLayout = setupScrollableTab(contentPanel, resourceMgr, 700.0f, tabContentH);
    m_tabLayouts.push_back(tabLayout);
    UIStackLayout* stack = tabLayout.stack;

    if (!m_renderScene) { finalizeScrollTab(tabLayout.scroll, stack); return; }
    RenderSettings s = m_renderScene->getSettings();
    const bool fsr1Supported = m_renderScene->isFsr1Supported();

    addSectionHeader(stack, resourceMgr, "Upscaling");

    addToggle(stack, resourceMgr, "FSR1 Upscale (OpenGL only)",
              fsr1Supported && s.upscale.fsr1Enabled,
              [this](bool v) {
                  auto s = m_renderScene->getSettings(); s.upscale.fsr1Enabled = v;
                  m_renderScene->setSettings(s);
              }, fsr1Supported);
    addSliderRow(stack, resourceMgr, "FSR1 Render Scale",
                 0.50f, 1.0f, s.upscale.renderScale, 0.01f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.upscale.renderScale = v;
                     m_renderScene->setSettings(s);
                 }, fsr1Supported);
    addSliderRow(stack, resourceMgr, "FSR1 Sharpness",
                 0.0f, 2.0f, s.upscale.sharpness, 0.01f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.upscale.sharpness = v;
                     m_renderScene->setSettings(s);
                 }, fsr1Supported);

    addSectionHeader(stack, resourceMgr, "Anti-Aliasing");

    addToggle(stack, resourceMgr, "TAA", s.taa.enabled,
              [this](bool v) {
                  auto s = m_renderScene->getSettings(); s.taa.enabled = v;
                  m_renderScene->setSettings(s);
              });

    addSectionHeader(stack, resourceMgr, "Reflection");

    addToggle(stack, resourceMgr, "Reflection Filter", s.reflection.filterEnabled,
              [this](bool v) {
                  auto s = m_renderScene->getSettings(); s.reflection.filterEnabled = v;
                  m_renderScene->setSettings(s);
              });
    addToggle(stack, resourceMgr, "Reflection Temporal", s.reflection.temporalEnabled,
              [this](bool v) {
                  auto s = m_renderScene->getSettings(); s.reflection.temporalEnabled = v;
                  m_renderScene->setSettings(s);
              });
    addSliderRow(stack, resourceMgr, "Reflection Filter Strength",
                 0.0f, 2.0f, s.reflection.filterStrength, 0.01f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.reflection.filterStrength = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Reflection History Weight",
                 0.0f, 0.98f, s.reflection.historyWeight, 0.01f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.reflection.historyWeight = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Reflection Composite",
                 0.0f, 1.0f, s.reflection.sceneReflectionCompositeStrength, 0.01f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings();
                     s.reflection.sceneReflectionCompositeStrength = v;
                     m_renderScene->setSettings(s);
                 });

    addSectionHeader(stack, resourceMgr, "Motion Blur");

    addToggle(stack, resourceMgr, "Motion Blur", s.postProcess.motionBlurEnabled,
              [this](bool v) {
                  auto s = m_renderScene->getSettings(); s.postProcess.motionBlurEnabled = v;
                  m_renderScene->setSettings(s);
              });
    addSliderRow(stack, resourceMgr, "Motion Blur Strength",
                 0.0f, 3.0f, s.postProcess.motionBlurStrength, 0.1f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings(); s.postProcess.motionBlurStrength = v;
                     m_renderScene->setSettings(s);
                 });
    addSliderRow(stack, resourceMgr, "Motion Blur Samples",
                 2.0f, 16.0f, static_cast<float>(s.postProcess.motionBlurSamples), 1.0f,
                 [this](float v) {
                     auto s = m_renderScene->getSettings();
                     s.postProcess.motionBlurSamples = static_cast<int>(v);
                     m_renderScene->setSettings(s);
                 });

    finalizeScrollTab(tabLayout.scroll, stack);
}
