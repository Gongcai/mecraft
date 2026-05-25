#pragma once

#include <vector>

#include "Tween.h"

class TweenGroup {
public:
    void addFloat(Tween<float>& tween) {
        m_floatTweens.push_back(&tween);
    }

    void addColor(Tween<std::array<float, 4>>& tween) {
        m_colorTweens.push_back(&tween);
    }

    void updateAll(float dt) {
        for (auto* t : m_floatTweens) {
            t->tick(dt);
        }
        for (auto* t : m_colorTweens) {
            t->tick(dt);
        }
    }

    [[nodiscard]] bool allDone() const {
        for (auto* t : m_floatTweens) {
            if (!t->isDone()) return false;
        }
        for (auto* t : m_colorTweens) {
            if (!t->isDone()) return false;
        }
        return true;
    }

    void clear() {
        m_floatTweens.clear();
        m_colorTweens.clear();
    }

private:
    std::vector<Tween<float>*> m_floatTweens;
    std::vector<Tween<std::array<float, 4>>*> m_colorTweens;
};
