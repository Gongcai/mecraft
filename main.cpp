#include "src/core/Window.h"
#include "src/core/InputManager.h"
#include "src/core/Camera.h"
#include "src/renderer/Shader.h"
#include <iostream>

#include "src/core/GameManager.h"

int main() {
    GameManager app;
    app.init(1280, 720, "Mecraft");
    app.run();
    app.shutdown();
    return 0;
}
