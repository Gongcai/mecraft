#ifndef MECRAFT_TEXTURE_CONTRACT_H
#define MECRAFT_TEXTURE_CONTRACT_H

#include <cstdint>

class RhiDevice;

namespace MecraftTextureContract {

// Six CSM shadow textures consumed by mecraft_shadow.glsl.
struct ShadowTextureBundle {
    uint32_t csmDepthCompare = 0;    // sampler2DArrayShadow uCsmShadowMap
    uint32_t csmDepthRaw = 0;        // sampler2DArray uCsmShadowDepthRaw
    uint32_t csmDepthAllCompare = 0; // sampler2DArrayShadow uCsmShadowDepthAll
    uint32_t csmDepthAllRaw = 0;     // sampler2DArray uCsmShadowDepthAllRaw
    uint32_t csmColor0 = 0;          // sampler2DArray uCsmShadowColor0
    uint32_t csmColor1 = 0;          // sampler2DArray uCsmShadowColor1
};

bool initializeNeutralShadowTextures(RhiDevice& rhiDevice);
void bindShadowSamplers(uint32_t program, int baseUnit, const ShadowTextureBundle& textures);

uint32_t neutralDepthCompare();
uint32_t neutralDepthRaw();
uint32_t neutralColor0();
uint32_t neutralColor1();
ShadowTextureBundle neutralBundle();
void bindNeutralShadowSamplers(uint32_t program, int baseUnit);
void destroyNeutralShadowTextures();

} // namespace MecraftTextureContract

#endif // MECRAFT_TEXTURE_CONTRACT_H
