#include "LocalShadowContract.h"

#include <algorithm>
#include <array>
#include <limits>
#include <unordered_set>

namespace renderer::contracts {
namespace {

struct PendingRequest final {
    uint32_t sceneLightIndex = 0u;
    StableLightId lightId;
    LocalShadowType type = LocalShadowType::Spot;
    GpuLightShadowPolicy policy = GpuLightShadowPolicy::None;
};

[[nodiscard]] bool rasterPolicy(const GpuLightShadowPolicy policy) {
    return policy == GpuLightShadowPolicy::RasterDynamic ||
           policy == GpuLightShadowPolicy::RasterCached;
}

[[nodiscard]] bool validSceneRecord(const SceneLight& sceneLight) {
    const GpuLight& light = sceneLight.light;
    const uint32_t type = light.classificationAndIdentity.x;
    return type <= static_cast<uint32_t>(GpuLightType::Rect) &&
           light.classificationAndIdentity.y != 0u &&
           light.classificationAndIdentity.z ==
               static_cast<uint32_t>(GpuLightShadowPolicy::None) &&
           light.classificationAndIdentity.w ==
               kGpuLightInvalidResourceIndex &&
           light.resourcesAndFlags.w == kGpuLightContractVersion &&
           gpuLightPackedRangeValid(light);
}

[[nodiscard]] uint32_t capacityForType(const LocalShadowType type) {
    return type == LocalShadowType::Spot
        ? kLocalShadowMaxSpotLightCount
        : kLocalShadowMaxPointLightCount;
}

[[nodiscard]] LocalShadowAllocationError capacityError(
    const LocalShadowType type) {
    return type == LocalShadowType::Spot
        ? LocalShadowAllocationError::SpotCapacityExceeded
        : LocalShadowAllocationError::PointCapacityExceeded;
}

} // namespace

bool LocalShadowStableAllocator::allocate(
    const std::vector<SceneLight>& sceneLights,
    std::vector<LocalShadowAllocation>& allocations) {
    if (sceneLights.size() > std::numeric_limits<uint32_t>::max()) {
        m_failure = {LocalShadowAllocationError::InvalidSceneLight, {}};
        return false;
    }

    std::unordered_set<uint32_t> sceneIds;
    sceneIds.reserve(sceneLights.size());
    std::vector<PendingRequest> requests;
    requests.reserve(sceneLights.size());
    for (uint32_t index = 0u;
         index < static_cast<uint32_t>(sceneLights.size()); ++index) {
        const SceneLight& sceneLight = sceneLights[index];
        const StableLightId lightId{
            sceneLight.light.classificationAndIdentity.y};
        if (!validSceneRecord(sceneLight)) {
            m_failure = {
                LocalShadowAllocationError::InvalidSceneLight, lightId};
            return false;
        }
        if (!sceneIds.insert(lightId.value).second) {
            m_failure = {
                LocalShadowAllocationError::DuplicateStableId, lightId};
            return false;
        }
        if (sceneLight.requestedShadowPolicy ==
            GpuLightShadowPolicy::None) {
            continue;
        }
        if (sceneLight.requestedShadowPolicy ==
            GpuLightShadowPolicy::RayQuery) {
            m_failure = {
                LocalShadowAllocationError::RayQueryUnavailable, lightId};
            return false;
        }
        if (!rasterPolicy(sceneLight.requestedShadowPolicy)) {
            m_failure = {
                LocalShadowAllocationError::InvalidSceneLight, lightId};
            return false;
        }

        const GpuLightType gpuType = static_cast<GpuLightType>(
            sceneLight.light.classificationAndIdentity.x);
        LocalShadowType localType;
        if (gpuType == GpuLightType::Spot) {
            localType = LocalShadowType::Spot;
        } else if (gpuType == GpuLightType::Point) {
            localType = LocalShadowType::Point;
        } else {
            m_failure = {
                LocalShadowAllocationError::UnsupportedLightType, lightId};
            return false;
        }
        requests.push_back({index, lightId, localType,
                            sceneLight.requestedShadowPolicy});
    }

    std::unordered_set<uint32_t> requestedIds;
    requestedIds.reserve(requests.size());
    for (const PendingRequest& request : requests) {
        requestedIds.insert(request.lightId.value);
    }

    std::unordered_map<uint32_t, Record> next = m_records;
    for (auto it = next.begin(); it != next.end();) {
        if (requestedIds.find(it->first) == requestedIds.end()) {
            it = next.erase(it);
        } else {
            ++it;
        }
    }

    std::array<std::array<bool, kLocalShadowMaxSpotLightCount>, 2> used{};
    for (const PendingRequest& request : requests) {
        const auto existing = next.find(request.lightId.value);
        if (existing == next.end()) {
            continue;
        }
        if (existing->second.type != request.type) {
            m_failure = {
                LocalShadowAllocationError::StableIdTypeChanged,
                request.lightId};
            return false;
        }
        const uint32_t typeIndex = static_cast<uint32_t>(request.type);
        if (existing->second.slot >= capacityForType(request.type) ||
            used[typeIndex][existing->second.slot]) {
            m_failure = {
                LocalShadowAllocationError::InvalidSceneLight,
                request.lightId};
            return false;
        }
        used[typeIndex][existing->second.slot] = true;
    }

    std::vector<PendingRequest> newRequests;
    for (const PendingRequest& request : requests) {
        if (next.find(request.lightId.value) == next.end()) {
            newRequests.push_back(request);
        }
    }
    std::sort(
        newRequests.begin(), newRequests.end(),
        [](const PendingRequest& lhs, const PendingRequest& rhs) {
            return lhs.lightId.value < rhs.lightId.value;
        });
    for (const PendingRequest& request : newRequests) {
        const uint32_t typeIndex = static_cast<uint32_t>(request.type);
        const uint32_t capacity = capacityForType(request.type);
        uint32_t slot = 0u;
        while (slot < capacity && used[typeIndex][slot]) {
            ++slot;
        }
        if (slot == capacity) {
            m_failure = {capacityError(request.type), request.lightId};
            return false;
        }
        used[typeIndex][slot] = true;
        next.emplace(request.lightId.value, Record{request.type, slot});
    }

    std::vector<LocalShadowAllocation> committed;
    committed.reserve(requests.size());
    for (const PendingRequest& request : requests) {
        const Record& record = next.at(request.lightId.value);
        committed.push_back({
            request.sceneLightIndex, request.lightId, request.type,
            request.policy, record.slot,
            localShadowMetadataIndex(request.type, record.slot)});
    }
    std::sort(
        committed.begin(), committed.end(),
        [](const LocalShadowAllocation& lhs,
           const LocalShadowAllocation& rhs) {
            return lhs.sceneLightIndex < rhs.sceneLightIndex;
        });

    m_records = std::move(next);
    allocations = std::move(committed);
    m_failure = {};
    return true;
}

void LocalShadowStableAllocator::reset() {
    m_records.clear();
    m_failure = {};
}

const char* localShadowAllocationErrorStableId(
    const LocalShadowAllocationError error) {
    switch (error) {
        case LocalShadowAllocationError::None: return "None";
        case LocalShadowAllocationError::InvalidSceneLight:
            return "InvalidSceneLight";
        case LocalShadowAllocationError::DuplicateStableId:
            return "DuplicateStableId";
        case LocalShadowAllocationError::UnsupportedLightType:
            return "UnsupportedLightType";
        case LocalShadowAllocationError::RayQueryUnavailable:
            return "RayQueryUnavailable";
        case LocalShadowAllocationError::StableIdTypeChanged:
            return "StableIdTypeChanged";
        case LocalShadowAllocationError::SpotCapacityExceeded:
            return "SpotCapacityExceeded";
        case LocalShadowAllocationError::PointCapacityExceeded:
            return "PointCapacityExceeded";
    }
    return "InvalidLocalShadowAllocationError";
}

} // namespace renderer::contracts
