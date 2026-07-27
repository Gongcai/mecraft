#ifndef MECRAFT_MODEL_SCENE_IDS_H
#define MECRAFT_MODEL_SCENE_IDS_H

#include <cstdint>

namespace scene {

using SceneEntityId = uint64_t;
using SceneAssetId = uint64_t;

constexpr SceneEntityId kInvalidSceneEntityId = 0u;
constexpr SceneAssetId kInvalidSceneAssetId = 0u;

} // namespace scene

#endif // MECRAFT_MODEL_SCENE_IDS_H
