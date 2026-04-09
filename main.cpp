#include "src/core/Window.h"
#include "src/core/InputManager.h"
#include "src/core/Camera.h"
#include "src/renderer/Shader.h"
#ifndef NDEBUG
#include "src/renderer/TestCube.h"
#endif
#include <iostream>

#include "src/core/Game.h"

int main() {
    Game game;
    game.init(1280, 720, "Mecraft");
    game.run();
    game.shutdown();
    return 0;
}
