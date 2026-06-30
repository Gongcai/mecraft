#ifndef MECRAFT_BLOCK_ICON_ATLAS_BUILDER_H
#define MECRAFT_BLOCK_ICON_ATLAS_BUILDER_H

#include <vector>

class ResourceMgr;
struct TextureAtlas;

namespace resource {

[[nodiscard]] TextureAtlas buildBlockIconAtlas(int iconSize,
                                               const TextureAtlas& blockAtlas,
                                               const std::vector<unsigned char>& blockAtlasPixels,
                                               const ResourceMgr& resourceMgr);

} // namespace resource

#endif // MECRAFT_BLOCK_ICON_ATLAS_BUILDER_H
