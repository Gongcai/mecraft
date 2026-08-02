#ifndef MECRAFT_GLOBAL_BINDLESS_SET_GLSL
#define MECRAFT_GLOBAL_BINDLESS_SET_GLSL

// These fixed binding numbers mirror renderer::contracts::GlobalBindlessBinding.
// Descriptor counts are fixed by the Vulkan layout while shader arrays remain runtime-indexed.
layout(set = 0, binding = 0) uniform texture2D globalBindlessTextures2D[];
layout(set = 0, binding = 1) uniform textureCube globalBindlessTexturesCube[];
layout(set = 0, binding = 2) uniform sampler globalBindlessSamplers[];
layout(std430, set = 0, binding = 3) buffer GlobalBindlessStorageBuffer {
    uint words[];
} globalBindlessStorageBuffers[];

// Ray-query shaders opt into the fixed TLAS binding and enable GL_EXT_ray_query before including this contract.
#if defined(RHI_GLOBAL_BINDLESS_RAY_QUERY)
layout(set = 0, binding = 4) uniform accelerationStructureEXT globalBindlessSceneTlas;
#endif

// Samples one two-dimensional texture with independently indexed texture and sampler descriptors.
// textureIndex selects the sampled-image descriptor, samplerIndex selects filtering state, and uv is normalized.
// The returned value is the filtered RGBA texel produced by Vulkan's combined sampler operation.
vec4 globalBindlessSample2D(uint textureIndex, uint samplerIndex, vec2 uv) {
    return texture(sampler2D(globalBindlessTextures2D[nonuniformEXT(textureIndex)],
                             globalBindlessSamplers[nonuniformEXT(samplerIndex)]),
                   uv);
}

// Samples one cube texture with independently indexed texture and sampler descriptors.
// textureIndex selects the cube-image descriptor, samplerIndex selects filtering state, and direction is the lookup vector.
// The returned value is the filtered RGBA texel produced by Vulkan's combined sampler operation.
vec4 globalBindlessSampleCube(uint textureIndex, uint samplerIndex, vec3 direction) {
    return texture(samplerCube(globalBindlessTexturesCube[nonuniformEXT(textureIndex)],
                               globalBindlessSamplers[nonuniformEXT(samplerIndex)]),
                   direction);
}

#endif // MECRAFT_GLOBAL_BINDLESS_SET_GLSL
