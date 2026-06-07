#include "LoadingScreen.h"

#include "../widgets/UIPanel.h"
#include "../widgets/UIProgressBar.h"
#include "../widgets/UIText.h"

#include <algorithm>

void LoadingScreen::buildUI(ResourceMgr& resourceMgr) {
    (void)resourceMgr;

    auto background = std::make_unique<UIPanel>();
    background->setBackgroundColor({0.03f, 0.04f, 0.05f, 1.0f});
    background->anchor = Anchor::BottomLeft;
    background->width = 9999.0f;
    background->height = 9999.0f;
    addRoot(std::move(background));

    auto title = std::make_unique<UIText>();
    title->setText("MECRAFT");
    title->setTextScale(3.0f);
    title->setTone(UITextTone::OnOverlay);
    title->setAlignment(TextAlignment::Center);
    title->anchor = Anchor::Center;
    title->anchorOffsetY = 96.0f;
    title->width = 520.0f;
    title->height = 48.0f;
    m_title = title.get();
    addRoot(std::move(title));

    auto status = std::make_unique<UIText>();
    status->setText("Loading world");
    status->setTextScale(1.35f);
    status->setTone(UITextTone::OnOverlaySecondary);
    status->setAlignment(TextAlignment::Center);
    status->anchor = Anchor::Center;
    status->anchorOffsetY = 28.0f;
    status->width = 520.0f;
    status->height = 30.0f;
    m_status = status.get();
    addRoot(std::move(status));

    auto progress = std::make_unique<UIProgressBar>();
    progress->width = 520.0f;
    progress->height = 24.0f;
    progress->anchor = Anchor::Center;
    progress->anchorOffsetY = -18.0f;
    progress->setShowPercent(true);
    progress->setTone(UIProgressBarTone::Success);
    m_progressBar = progress.get();
    addRoot(std::move(progress));

    auto detail = std::make_unique<UIText>();
    detail->setText("");
    detail->setTextScale(1.0f);
    detail->setTone(UITextTone::OnOverlayMuted);
    detail->setAlignment(TextAlignment::Center);
    detail->anchor = Anchor::Center;
    detail->anchorOffsetY = -58.0f;
    detail->width = 520.0f;
    detail->height = 24.0f;
    m_detail = detail.get();
    addRoot(std::move(detail));
}

void LoadingScreen::setProgress(const float progress) {
    if (m_progressBar) {
        m_progressBar->setProgress(std::clamp(progress, 0.0f, 1.0f));
    }
}

void LoadingScreen::setStatusText(const std::string& text) {
    if (m_status) {
        m_status->setText(text);
    }
}

void LoadingScreen::setDetailText(const std::string& text) {
    if (m_detail) {
        m_detail->setText(text);
    }
}
