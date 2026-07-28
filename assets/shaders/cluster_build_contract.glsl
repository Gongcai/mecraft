#ifndef MECRAFT_CLUSTER_BUILD_CONTRACT_GLSL
#define MECRAFT_CLUSTER_BUILD_CONTRACT_GLSL

const uint CLUSTER_STATS_TOTAL_INDEX_COUNT = 0u;
const uint CLUSTER_STATS_MAX_LIGHTS_PER_CLUSTER = 1u;
const uint CLUSTER_STATS_NON_EMPTY_CLUSTER_COUNT = 2u;
const uint CLUSTER_STATS_BUILD_ERROR = 3u;
const uint CLUSTER_STATS_CLUSTER_COUNT = 4u;
const uint CLUSTER_STATS_LIGHT_COUNT = 5u;
const uint CLUSTER_STATS_INDEX_CAPACITY = 6u;
const uint CLUSTER_STATS_CONTRACT_VERSION = 7u;
const uint CLUSTER_STATS_WORD_COUNT = 8u;

const uint CLUSTER_BUILD_ERROR_PREFIX_CAPACITY = 1u << 0u;
const uint CLUSTER_BUILD_ERROR_FILL_CAPACITY = 1u << 1u;
const uint CLUSTER_BUILD_ERROR_CURSOR_MISMATCH = 1u << 2u;

struct ClusterLightBounds {
    uvec4 minCluster;
    uvec4 maxCluster;
};

struct ClusterRecord {
    uint offset;
    uint count;
};

uint clusteredLightClusterIndex(uvec3 cluster, uvec3 gridSize) {
    return (cluster.z * gridSize.y + cluster.y) * gridSize.x + cluster.x;
}

#endif
