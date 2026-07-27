#ifndef MECRAFT_MODEL_SCENE_COMMAND_HISTORY_H
#define MECRAFT_MODEL_SCENE_COMMAND_HISTORY_H

#include "ModelSceneDocument.h"

#include <cstddef>
#include <optional>
#include <variant>
#include <vector>

class ModelSceneRuntime;

namespace scene {

/// Stores reversible editor commands without rebuilding model GPU assets.
class ModelSceneCommandHistory {
public:
    void clear();
    void markSaved();

    [[nodiscard]] bool canUndo() const;
    [[nodiscard]] bool canRedo() const;
    [[nodiscard]] bool isAtSavedState() const;

    /// Records an exact entity state transition such as rename, transform, or reparent.
    void recordEntityState(const SceneEntityDocument& before,
                           const SceneEntityDocument& after);

    /// Records a hierarchy that was created and currently exists in the runtime.
    void recordCreatedSubtree(std::vector<SceneEntityDocument> states);

    /// Records a hierarchy that was removed from the runtime.
    void recordDeletedSubtree(std::vector<SceneEntityDocument> states);

    [[nodiscard]] bool undo(ModelSceneRuntime& runtime);
    [[nodiscard]] bool redo(ModelSceneRuntime& runtime);

private:
    struct EntityStateCommand {
        SceneEntityDocument before;
        SceneEntityDocument after;
    };

    struct EntityPresenceCommand {
        std::vector<SceneEntityDocument> states;
        bool created = false;
    };

    using Command = std::variant<EntityStateCommand, EntityPresenceCommand>;

    static constexpr std::size_t kMaximumCommands = 256u;

    void record(Command command);
    [[nodiscard]] static bool apply(ModelSceneRuntime& runtime,
                                    const Command& command,
                                    bool forward);

    std::vector<Command> m_commands;
    std::size_t m_cursor = 0u;
    std::optional<std::size_t> m_savedCursor = 0u;
};

} // namespace scene

#endif // MECRAFT_MODEL_SCENE_COMMAND_HISTORY_H
