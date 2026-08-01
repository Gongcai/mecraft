#ifndef MECRAFT_GPU_SCENE_CONTRACT_GLSL
#define MECRAFT_GPU_SCENE_CONTRACT_GLSL

const uint GPU_SCENE_CONTRACT_VERSION = 1u;
const uint GPU_SCENE_INVALID_TABLE_INDEX = 0xffffffffu;
const uint GPU_SCENE_INVALID_RAY_TRACING_INSTANCE_ID = 0xffffffffu;
const uint GPU_SCENE_MAX_RAY_TRACING_INSTANCE_ID = 0x00ffffffu;

const uint GPU_SCENE_INSTANCE_FLAG_ENABLED = 1u << 0u;
const uint GPU_SCENE_INSTANCE_FLAG_DYNAMIC_TRANSFORM = 1u << 1u;
const uint GPU_SCENE_INSTANCE_FLAG_SHADOW_CASTER = 1u << 2u;
const uint GPU_SCENE_INSTANCE_FLAG_REFLECTION_VISIBLE = 1u << 3u;
const uint GPU_SCENE_INSTANCE_FLAG_RAY_TRACING_VISIBLE = 1u << 4u;
const uint GPU_SCENE_INSTANCE_FLAG_FIRST_PERSON = 1u << 5u;

const uint GPU_SCENE_INDEX_TYPE_UINT16 = 0u;
const uint GPU_SCENE_INDEX_TYPE_UINT32 = 1u;

const uint GPU_SCENE_GEOMETRY_FLAG_OPAQUE = 1u << 0u;
const uint GPU_SCENE_GEOMETRY_FLAG_CUTOUT = 1u << 1u;
const uint GPU_SCENE_GEOMETRY_FLAG_TRANSPARENT = 1u << 2u;
const uint GPU_SCENE_GEOMETRY_FLAG_SHADOW_CASTER = 1u << 3u;
const uint GPU_SCENE_GEOMETRY_FLAG_REFLECTION_VISIBLE = 1u << 4u;
const uint GPU_SCENE_GEOMETRY_FLAG_RAY_TRACING_VISIBLE = 1u << 5u;
const uint GPU_SCENE_GEOMETRY_FLAG_DYNAMIC_VERTICES = 1u << 6u;
const uint GPU_SCENE_GEOMETRY_FLAG_DOUBLE_SIDED = 1u << 7u;

// The field order mirrors the C++ alignas records and produces exact 192-byte
// instance and 128-byte geometry strides in std430 storage buffers.
struct GpuSceneAffineTransform {
    vec4 row0;
    vec4 row1;
    vec4 row2;
};

struct GpuSceneInstance {
    GpuSceneAffineTransform worldFromObject;
    GpuSceneAffineTransform previousWorldFromObject;
    GpuSceneAffineTransform objectFromWorld;
    vec4 worldBoundsCenterAndRadius;
    uvec4 geometryMaterialAndFlags;
    uvec4 identityAndVersion;
};

struct GpuSceneGeometry {
    uvec2 vertexAddress;
    uvec2 indexAddress;
    uvec2 primitiveMetadataAddress;
    uvec2 meshletAddress;
    uvec4 vertexLayoutAndFlags;
    uvec4 indexRangeAndType;
    uvec4 materialAndIdentity;
    uvec4 primitiveMeshletAndRevision;
    vec4 localBoundsMin;
    vec4 localBoundsMax;
};

vec3 gpuSceneTransformPoint(GpuSceneAffineTransform transform, vec3 point) {
    vec4 homogeneous = vec4(point, 1.0);
    return vec3(dot(transform.row0, homogeneous), dot(transform.row1, homogeneous),
                dot(transform.row2, homogeneous));
}

#endif // MECRAFT_GPU_SCENE_CONTRACT_GLSL
