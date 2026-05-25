#ifndef MECRAFT_WORLDDRAWBATCH_H
#define MECRAFT_WORLDDRAWBATCH_H

#include <algorithm>
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

#include "../../world/chunk/SubChunk.h"

enum class TransparentBatchKind : uint8_t {
    Generic = 0,
    Water = 1
};

struct DrawBatchEntry {
    GpuMeshRange range;
    float distanceSq = 0.0f;
    TransparentBatchKind kind = TransparentBatchKind::Generic;
};

struct WorldDrawBatch {
    std::vector<DrawBatchEntry> entries;

    void clear() { entries.clear(); }

    void addOpaque(const GpuMeshRange& range) {
        entries.push_back({range, 0.0f, TransparentBatchKind::Generic});
    }

    void addCutout(const GpuMeshRange& range) {
        entries.push_back({range, 0.0f, TransparentBatchKind::Generic});
    }

    void addTransparent(const GpuMeshRange& range, float distanceSq) {
        entries.push_back({range, distanceSq, TransparentBatchKind::Generic});
    }

    void addWater(const GpuMeshRange& range, float distanceSq) {
        entries.push_back({range, distanceSq, TransparentBatchKind::Water});
    }

    void sortTransparentBackToFront() {
        std::sort(entries.begin(), entries.end(),
            [](const DrawBatchEntry& a, const DrawBatchEntry& b) {
                return a.distanceSq > b.distanceSq;
            });
    }
};

#endif // MECRAFT_WORLDDRAWBATCH_H
