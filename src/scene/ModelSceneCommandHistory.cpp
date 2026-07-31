#include "ModelSceneCommandHistory.h"

#include "ModelSceneRuntime.h"

#include <algorithm>
#include <type_traits>
#include <utility>

namespace scene {
namespace {

[[nodiscard]] bool equalTransform(const SceneTransformDocument& lhs, const SceneTransformDocument& rhs) {
    return lhs.position == rhs.position && lhs.rotation == rhs.rotation && lhs.scale == rhs.scale;
}

[[nodiscard]] bool equalEntity(const SceneEntityDocument& lhs, const SceneEntityDocument& rhs) {
    return lhs.id == rhs.id && lhs.name == rhs.name && lhs.parentId == rhs.parentId && lhs.assetId == rhs.assetId &&
           equalTransform(lhs.transform, rhs.transform);
}

[[nodiscard]] bool equalSubtree(const std::vector<SceneEntityDocument>& lhs,
                                const std::vector<SceneEntityDocument>& rhs) {
    return lhs.size() == rhs.size() &&
           std::equal(lhs.begin(), lhs.end(), rhs.begin(),
                      [](const SceneEntityDocument& left, const SceneEntityDocument& right) {
                          return equalEntity(left, right);
                      });
}

[[nodiscard]] bool removeExpectedSubtree(ModelSceneRuntime& runtime, const std::vector<SceneEntityDocument>& expected) {
    if (expected.empty()) {
        return false;
    }
    const entt::entity root = runtime.findEntity(expected.front().id);
    std::vector<SceneEntityDocument> current;
    if (root == entt::null || !runtime.captureEntitySubtree(root, current) || !equalSubtree(current, expected)) {
        return false;
    }
    runtime.destroyEntity(root);
    return true;
}

} // namespace

void ModelSceneCommandHistory::clear() {
    m_commands.clear();
    m_cursor = 0u;
    m_savedCursor = 0u;
}

void ModelSceneCommandHistory::markSaved() {
    m_savedCursor = m_cursor;
}

bool ModelSceneCommandHistory::canUndo() const {
    return m_cursor > 0u;
}

bool ModelSceneCommandHistory::canRedo() const {
    return m_cursor < m_commands.size();
}

bool ModelSceneCommandHistory::isAtSavedState() const {
    return m_savedCursor.has_value() && *m_savedCursor == m_cursor;
}

void ModelSceneCommandHistory::recordEntityState(const SceneEntityDocument& before, const SceneEntityDocument& after) {
    if (equalEntity(before, after)) {
        return;
    }
    record(EntityStateCommand{before, after});
}

void ModelSceneCommandHistory::recordCreatedSubtree(std::vector<SceneEntityDocument> states) {
    if (!states.empty()) {
        record(EntityPresenceCommand{std::move(states), true});
    }
}

void ModelSceneCommandHistory::recordDeletedSubtree(std::vector<SceneEntityDocument> states) {
    if (!states.empty()) {
        record(EntityPresenceCommand{std::move(states), false});
    }
}

bool ModelSceneCommandHistory::undo(ModelSceneRuntime& runtime) {
    if (!canUndo()) {
        return false;
    }
    if (!apply(runtime, m_commands[m_cursor - 1u], false)) {
        return false;
    }
    --m_cursor;
    return true;
}

bool ModelSceneCommandHistory::redo(ModelSceneRuntime& runtime) {
    if (!canRedo()) {
        return false;
    }
    if (!apply(runtime, m_commands[m_cursor], true)) {
        return false;
    }
    ++m_cursor;
    return true;
}

void ModelSceneCommandHistory::record(Command command) {
    if (m_cursor < m_commands.size()) {
        m_commands.erase(m_commands.begin() + static_cast<std::ptrdiff_t>(m_cursor), m_commands.end());
        if (m_savedCursor.has_value() && *m_savedCursor > m_cursor) {
            m_savedCursor.reset();
        }
    }
    m_commands.push_back(std::move(command));
    ++m_cursor;
    if (m_commands.size() <= kMaximumCommands) {
        return;
    }
    m_commands.erase(m_commands.begin());
    --m_cursor;
    if (m_savedCursor.has_value()) {
        if (*m_savedCursor == 0u) {
            m_savedCursor.reset();
        } else {
            --*m_savedCursor;
        }
    }
}

bool ModelSceneCommandHistory::apply(ModelSceneRuntime& runtime, const Command& command, const bool forward) {
    return std::visit(
        [&runtime, forward](const auto& typedCommand) {
            using Type = std::decay_t<decltype(typedCommand)>;
            if constexpr (std::is_same_v<Type, EntityStateCommand>) {
                const SceneEntityDocument& state = forward ? typedCommand.after : typedCommand.before;
                if (!runtime.applyEntityState(state)) {
                    return false;
                }
                runtime.setSelectedEntity(runtime.findEntity(state.id));
                return true;
            } else {
                const bool shouldExist = forward ? typedCommand.created : !typedCommand.created;
                if (shouldExist) {
                    return runtime.restoreEntitySubtree(typedCommand.states) != entt::null;
                }
                return removeExpectedSubtree(runtime, typedCommand.states);
            }
        },
        command);
}

} // namespace scene
