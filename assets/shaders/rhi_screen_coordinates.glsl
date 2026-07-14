#ifndef MECRAFT_RHI_SCREEN_COORDINATES_GLSL
#define MECRAFT_RHI_SCREEN_COORDINATES_GLSL

#if defined(RHI_VULKAN) && defined(RHI_OPENGL)
#error "Exactly one RHI backend macro must be defined"
#elif !defined(RHI_VULKAN) && !defined(RHI_OPENGL)
#error "An RHI backend macro must be defined"
#endif

// Converts top-left screen UV coordinates to the texture UV coordinates used by the active backend.
// screenUv identifies the same logical render-target point on every backend. The return value uses
// the native texture origin expected by texture sampling operations on the active graphics API.
vec2 rhiScreenUvToTextureUv(vec2 screenUv) {
#if defined(RHI_OPENGL)
    return vec2(screenUv.x, 1.0 - screenUv.y);
#else
    return screenUv;
#endif
}

// Converts top-left screen UV coordinates to bottom-left clip UV coordinates.
// screenUv is expressed in the RHI render-target domain. The return value is suitable for NDC,
// inverse-projection, and inverse-view-projection reconstruction performed by project shaders.
vec2 rhiScreenUvToClipUv(vec2 screenUv) {
    return vec2(screenUv.x, 1.0 - screenUv.y);
}

// Converts native framebuffer fragment coordinates to normalized top-left screen UV coordinates.
// fragCoord is gl_FragCoord.xy and extent is the current framebuffer size in pixels. The return
// value identifies the logical screen point without applying a second Vulkan framebuffer flip.
vec2 rhiNativeFragCoordToScreenUv(vec2 fragCoord, vec2 extent) {
    vec2 nativeUv = fragCoord / extent;
#if defined(RHI_OPENGL)
    return vec2(nativeUv.x, 1.0 - nativeUv.y);
#else
    return nativeUv;
#endif
}

// Converts normalized top-left screen UV coordinates to an integer native framebuffer texel.
// screenUv identifies a logical screen point and extent is the target size in texels. The returned
// coordinate is suitable for texelFetch and image operations that consume backend-native rows.
ivec2 rhiScreenUvToNativeTexel(vec2 screenUv, ivec2 extent) {
    return ivec2(rhiScreenUvToTextureUv(screenUv) * vec2(extent));
}

#endif // MECRAFT_RHI_SCREEN_COORDINATES_GLSL
