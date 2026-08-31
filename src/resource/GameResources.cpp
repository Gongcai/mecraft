#include "GameResources.h"

void GameResources::init(RhiDevice& rhiDevice, RhiCommandListPool& commandListPool) {
    texture2D.init(rhiDevice);
    cubemaps.init(rhiDevice);
    environmentTextures.init(rhiDevice);
    blockTextures.init(rhiDevice, commandListPool);
    uiTextures.init(rhiDevice, commandListPool);
}

void GameResources::shutdown() {
    texture2D.shutdown();
    blockTextures.shutdown();
    uiTextures.shutdown();
    environmentTextures.shutdown();
    cubemaps.shutdown();
}
