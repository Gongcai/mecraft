#ifndef MECRAFT_ITEMMODELMESH_H
#define MECRAFT_ITEMMODELMESH_H

#include <vector>

#include "../resource/ResourceMgr.h"

struct ItemModelVertex {
    float x;
    float y;
    float z;
    float u;
    float v;
    float shade;
    float nx;
    float ny;
    float nz;
};

bool buildExtrudedItemMesh(const TextureAtlas& atlas,
                           const std::vector<unsigned char>& atlasPixels,
                           int tileIndex,
                           std::vector<ItemModelVertex>& outVertices);

#endif // MECRAFT_ITEMMODELMESH_H

