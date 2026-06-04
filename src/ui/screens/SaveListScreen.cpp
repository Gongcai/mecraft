#include "SaveListScreen.h"

#include "../widgets/UIButton.h"
#include "../widgets/UIPanel.h"
#include "../widgets/UIScrollArea.h"
#include "../widgets/UIText.h"
#include "../widgets/UIImage.h"
#include "../layout/UIStackLayout.h"
#include "../../resource/ResourceMgr.h"
#include "../../locale/LocaleManager.h"
#include "../../save/SaveManager.h"

#include <algorithm>

// ---------------------------------------------------------------------------
// ClickablePanel – a UIPanel that fires a callback on click.
// Defined locally to avoid modifying the shared UIPanel widget.
// ---------------------------------------------------------------------------

namespace {

class ClickablePanel : public UIPanel {
public:
    ClickablePanel() {
        interactive = true;
        focusable   = true;
    }

    void setOnClick(std::function<void()> cb) { m_onClick = std::move(cb); }
    bool isHovered() const { return m_hovered; }

    UIEventResult onInput(const UIInputEvent& event,
                          const UIRenderContext& ctx) override {
        if (!visible || !interactive) return UIEventResult::Ignored;

        // Let children handle first
        UIEventResult childResult = UIPanel::onInput(event, ctx);
        if (childResult == UIEventResult::Consumed) return UIEventResult::Consumed;

        bool inside = hitTest(event.x, event.y, ctx);

        switch (event.type) {
        case UIInputEventType::PointerMove:
            m_hovered = inside;
            return inside ? UIEventResult::Handled : UIEventResult::Ignored;

        case UIInputEventType::PointerDown:
            if (event.button == UIPointerButton::Primary && inside) {
                m_pressed = true;
                return UIEventResult::Handled;
            }
            break;

        case UIInputEventType::PointerUp:
            if (event.button == UIPointerButton::Primary && m_pressed && inside) {
                m_pressed = false;
                if (m_onClick) m_onClick();
                return UIEventResult::Consumed;
            }
            m_pressed = false;
            break;

        case UIInputEventType::Command:
            if (isFocused() && event.command == UICommand::Activate) {
                if (m_onClick) m_onClick();
                return UIEventResult::Consumed;
            }
            break;

        default:
            break;
        }
        return UIEventResult::Ignored;
    }

private:
    std::function<void()> m_onClick;
    bool m_hovered  = false;
    bool m_pressed  = false;
};

} // anonymous namespace

// ===========================================================================
// SaveListScreen implementation
// ===========================================================================

void SaveListScreen::buildUI(ResourceMgr& resourceMgr) {
    m_resMgr = &resourceMgr;

    // -- Background overlay --
    auto bgPanel = std::make_unique<UIPanel>();
    bgPanel->setBackgroundColor({0.0f, 0.0f, 0.0f, 0.75f});
    bgPanel->anchor    = Anchor::BottomLeft;
    bgPanel->width     = 9999.0f;
    bgPanel->height    = 9999.0f;
    m_bgPanel = bgPanel.get();
    addRoot(std::move(bgPanel));

    // -- Title --
    auto title = std::make_unique<UIText>();
    title->setText(getLocaleManager()
                       ? getLocaleManager()->tr("select_world")
                       : "SELECT WORLD");
    title->setTextScale(3.0f);
    title->setTextColor({1.0f, 1.0f, 1.0f, 1.0f});
    title->setAlignment(TextAlignment::Center);
    title->anchor        = Anchor::TopCenter;
    title->anchorOffsetY = -40.0f;
    title->width         = 400.0f;
    title->height        = 40.0f;
    m_title = title.get();
    addRoot(std::move(title));

    // -- Scroll area for save entries --
    auto scrollArea = std::make_unique<UIScrollArea>();
    scrollArea->width         = 700.0f;
    scrollArea->height        = 480.0f;
    scrollArea->anchor        = Anchor::Center;
    scrollArea->anchorOffsetY = 20.0f;
    m_scrollArea = scrollArea.get();

    auto stack = std::make_unique<UIStackLayout>();
    stack->setDirection(StackDirection::Vertical);
    stack->setSpacing(kEntrySpacing);
    stack->width = scrollArea->width - 20.0f;
    m_saveListStack = stack.get();

    scrollArea->addChild(std::move(stack));
    addRoot(std::move(scrollArea));

    // -- Empty placeholder --
    auto emptyText = std::make_unique<UIText>();
    emptyText->setText(getLocaleManager()
                           ? getLocaleManager()->tr("no_saves")
                           : "No saves found. Create a new world!");
    emptyText->setTextScale(1.5f);
    emptyText->setTextColor({0.6f, 0.6f, 0.6f, 1.0f});
    emptyText->setAlignment(TextAlignment::Center);
    emptyText->anchor        = Anchor::Center;
    emptyText->anchorOffsetY = 20.0f;
    emptyText->width         = 500.0f;
    emptyText->height        = 30.0f;
    emptyText->visible       = false;
    m_emptyText = emptyText.get();
    addRoot(std::move(emptyText));

    // -- "Create New World" button --
    auto createBtn = std::make_unique<UIButton>();
    createBtn->setText(getLocaleManager()
                           ? getLocaleManager()->tr("create_new_world")
                           : "CREATE NEW WORLD");
    createBtn->setTextScale(1.8f);
    createBtn->width         = 260.0f;
    createBtn->height        = 44.0f;
    createBtn->anchor        = Anchor::BottomCenter;
    createBtn->anchorOffsetX = 140.0f;
    createBtn->anchorOffsetY = 50.0f;
    createBtn->setNormalColor({0.2f, 0.55f, 0.2f, 0.9f});
    createBtn->setHoverColor({0.3f, 0.75f, 0.3f, 1.0f});
    createBtn->setOnClick([this]() {
        if (onCreateNewClicked) onCreateNewClicked();
    });
    m_createButton = createBtn.get();
    addRoot(std::move(createBtn));

    // -- "Back" button --
    auto backBtn = std::make_unique<UIButton>();
    backBtn->setText(getLocaleManager()
                         ? getLocaleManager()->tr("back")
                         : "BACK");
    backBtn->setTextScale(1.8f);
    backBtn->width         = 180.0f;
    backBtn->height        = 44.0f;
    backBtn->anchor        = Anchor::BottomCenter;
    backBtn->anchorOffsetX = -140.0f;
    backBtn->anchorOffsetY = 50.0f;
    backBtn->setNormalColor({0.4f, 0.4f, 0.4f, 0.9f});
    backBtn->setHoverColor({0.6f, 0.6f, 0.6f, 1.0f});
    backBtn->setOnClick([this]() {
        if (onBackClicked) onBackClicked();
    });
    m_backButton = backBtn.get();
    addRoot(std::move(backBtn));

    registerFloatTween(m_titleSlideY);
}

// ---------------------------------------------------------------------------
// Scene lifecycle
// ---------------------------------------------------------------------------

void SaveListScreen::onSceneEnter() {
    refreshSaveList();
    m_titleSlideY.start(-60.0f, -40.0f, kTitleDropDuration, EasingType::BackOut);
}

void SaveListScreen::updateAnimations(float dt) {
    UIScene::updateAnimations(dt);
    if (m_title) {
        m_title->anchorOffsetY = m_titleSlideY.value();
    }
}

// ---------------------------------------------------------------------------
// Scan saves directory
// ---------------------------------------------------------------------------

void SaveListScreen::refreshSaveList() {
    m_saves.clear();

    if (!m_savesRoot.empty()) {
        std::error_code ec;
        bool exists = std::filesystem::exists(m_savesRoot, ec);
        bool isDir = exists && std::filesystem::is_directory(m_savesRoot, ec);

        if (exists && isDir) {
            for (auto& entry : std::filesystem::directory_iterator(m_savesRoot, ec)) {
                if (!entry.is_directory(ec)) continue;

                const auto levelPath = entry.path() / "level.json";
                if (!std::filesystem::exists(levelPath, ec)) continue;

                SaveEntry save;
                save.folderName     = entry.path().filename().string();
                save.screenshotPath = (entry.path() / "thumb.png").string();

                // Read metadata via temporary SaveManager (read-only)
                save::SaveManager sm(entry.path());
                save::LevelMeta meta;
                if (sm.loadLevelMeta(meta)) {
                    save.displayName   = meta.displayName.empty()
                                             ? save.folderName
                                             : meta.displayName;
                    save.lastPlayedUtc = meta.lastSavedUtc;
                    save.createdUtc    = meta.createdUtc;
                    save.seed          = meta.seed;
                } else {
                    save.displayName = save.folderName;
                }

                m_saves.push_back(std::move(save));
            }
        }
    }

    // Newest-first sort (ISO 8601 strings sort lexicographically)
    std::sort(m_saves.begin(), m_saves.end(),
              [](const SaveEntry& a, const SaveEntry& b) {
                  return a.lastPlayedUtc > b.lastPlayedUtc;
              });

    rebuildList();
}

// ---------------------------------------------------------------------------
// Populate the (initially empty) stack with save entry widgets.
// Called once per scene lifetime from onSceneEnter → refreshSaveList.
// ---------------------------------------------------------------------------

void SaveListScreen::rebuildList() {
    if (!m_saveListStack || !m_scrollArea || !m_resMgr) {
        return;
    }

    if (m_saves.empty()) {
        if (m_emptyText) m_emptyText->visible = true;
        m_scrollArea->setContentHeight(0.0f);
        return;
    }

    if (m_emptyText) m_emptyText->visible = false;

    const float entryWidth = m_saveListStack->width;

    for (const auto& save : m_saves) {
        // -- Entry panel (clickable) --
        auto panel = std::make_unique<ClickablePanel>();
        panel->width  = entryWidth;
        panel->height = kEntryHeight;
        panel->setBackgroundColor({0.22f, 0.22f, 0.26f, 0.92f});
        panel->setBorderColor({0.40f, 0.40f, 0.45f, 0.7f});
        panel->setBorderWidth(1.0f);
        panel->setUseLocalColors(true);

        // -- Thumbnail --
        // Constrain the image to a kThumbSize x kThumbSize square.
        // After loadTexture, width/height hold the real image dimensions;
        // we override them to the target slot size so the quad is always
        // correctly sized, then apply a uniform scale to fit.
        auto thumb = std::make_unique<UIImage>();
        {
            std::error_code ec;
            bool thumbLoaded = !save.screenshotPath.empty() &&
                               std::filesystem::exists(save.screenshotPath, ec);
            if (thumbLoaded) {
                thumb->loadTexture(*m_resMgr,
                                   "thumb_" + save.folderName,
                                   save.screenshotPath);
                float tw = thumb->width  > 0 ? thumb->width  : kThumbSize;
                float th = thumb->height > 0 ? thumb->height : kThumbSize;
                // Override dimensions to the target slot size
                thumb->width  = kThumbSize;
                thumb->height = kThumbSize;
                // Uniform scale: fit the larger dimension to the slot
                float s = kThumbSize / std::max(tw, th);
                thumb->scaleX = s;
                thumb->scaleY = s;
            } else {
                thumb->width  = kThumbSize;
                thumb->height = kThumbSize;
                thumb->setSolidColor({0.35f, 0.55f, 0.35f, 1.0f});
            }
        }
        thumb->x = 8.0f;
        thumb->y = (kEntryHeight - kThumbSize) * 0.5f;
        panel->addChild(std::move(thumb));

        // -- Display name (Y = bottom of text in OpenGL coords) --
        auto nameText = std::make_unique<UIText>();
        nameText->setText(save.displayName);
        nameText->setTextScale(1.6f);
        nameText->setTextColor({1.0f, 1.0f, 1.0f, 1.0f});
        nameText->x = kThumbSize + 24.0f;
        nameText->y = kEntryHeight - 30.0f;
        nameText->width  = entryWidth - kThumbSize - 36.0f;
        nameText->height = 24.0f;
        panel->addChild(std::move(nameText));

        // -- Info line (seed + last played) --
        auto infoText = std::make_unique<UIText>();
        {
            std::string info = "Seed: " + std::to_string(save.seed);
            if (!save.lastPlayedUtc.empty()) {
                info += "  |  " + save.lastPlayedUtc;
            }
            infoText->setText(info);
        }
        infoText->setTextScale(1.1f);
        infoText->setTextColor({0.65f, 0.65f, 0.65f, 1.0f});
        infoText->x = kThumbSize + 24.0f;
        infoText->y = kEntryHeight - 56.0f;
        infoText->width  = entryWidth - kThumbSize - 36.0f;
        infoText->height = 18.0f;
        panel->addChild(std::move(infoText));

        // -- Click handler --
        const std::string folder = save.folderName;
        panel->setOnClick([this, folder]() {
            if (onSaveSelected) onSaveSelected(folder);
        });

        // Init the panel and all its children.
        // UIPanel::init() creates GPU resources for the panel itself,
        // then recurses into children (UIImage gets VAO + shaders, etc.).
        panel->init(*m_resMgr);

        m_saveListStack->addChild(std::move(panel));
    }

    // Lay out entries vertically
    m_saveListStack->layout();

    // Tell the scroll area how tall the content is
    float totalH = 0.0f;
    for (const auto& child : m_saveListStack->getChildren()) {
        totalH += child->height + kEntrySpacing;
    }
    m_scrollArea->setContentHeight(totalH);
    m_scrollArea->setScrollOffset(0.0f);
}
