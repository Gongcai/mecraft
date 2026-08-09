#ifndef MECRAFT_VALIDATION_VOXEL_FIXTURE_H
#define MECRAFT_VALIDATION_VOXEL_FIXTURE_H

#include "app/validation/ValidationSceneContract.h"

#include <cstdint>
#include <optional>
#include <string>

class IWorldView;
class World;

namespace app::validation {

inline constexpr const char* kValidationVoxelFixtureNoneId = "mecraft.none";
inline constexpr const char* kValidationVoxelFixtureWindowRoomId = "mecraft.window_room";
inline constexpr const char* kValidationVoxelFixtureCaveTurnId = "mecraft.cave_turn";
inline constexpr const char* kValidationVoxelFixtureLocalLightVillageId = "mecraft.local_light_village";
inline constexpr const char* kValidationVoxelFixtureForestCutoutId = "mecraft.forest_cutout";
inline constexpr uint32_t kValidationVoxelFixtureVersion = 1u;

/// Identifies a deterministic fixture construction or verification failure.
enum class ValidationVoxelFixtureError : uint8_t {
    None,
    UnsupportedFixture,
    MissingBlockDefinition,
    ChunkNotLoaded,
    StateMismatch
};

/// Returns one explicit result for fixture construction and client synchronization.
struct ValidationVoxelFixtureResult final {
    ValidationVoxelFixtureError error = ValidationVoxelFixtureError::None;
    std::string detail;

    /// Reports whether the complete fixture operation succeeded.
    /// @return True only when no fixture error was recorded.
    [[nodiscard]] bool succeeded() const;
};

/// Computes the semantic content hash of one built-in fixture recipe.
/// @param fixture Supported fixture identity whose final block edits are hashed.
/// @return Stable non-zero hash for a supported identity, otherwise no value.
[[nodiscard]] std::optional<renderer::contracts::StableContentHash>
validationVoxelFixtureContentHash(const ValidationVoxelFixtureIdentity& fixture);

/// Applies every final fixture block state to the authoritative loaded world.
/// @param world Authoritative local-server world receiving deterministic edits.
/// @param fixture Supported fixture identity verified by the scene contract.
/// @return Complete application status without partially editing unloaded chunks.
[[nodiscard]] ValidationVoxelFixtureResult applyValidationVoxelFixture(World& world,
                                                                       const ValidationVoxelFixtureIdentity& fixture);

/// Verifies that one rendered world view contains every final fixture block state.
/// @param worldView Server or client world snapshot to inspect.
/// @param fixture Supported fixture identity whose final states must match.
/// @return Success only after all fixture chunks and block states are synchronized.
[[nodiscard]] ValidationVoxelFixtureResult verifyValidationVoxelFixture(const IWorldView& worldView,
                                                                        const ValidationVoxelFixtureIdentity& fixture);

/// Returns the stable identifier used by diagnostics and validation failures.
/// @param error Fixture error to identify.
/// @return Process-lifetime stable identifier.
[[nodiscard]] const char* validationVoxelFixtureErrorStableId(ValidationVoxelFixtureError error);

} // namespace app::validation

#endif // MECRAFT_VALIDATION_VOXEL_FIXTURE_H
