#include "src/core/GameManager.h"

int main() {
    GameManager app;
    app.init(1280, 720, "Mecraft");
    app.run();
    app.shutdown();
    return 0;
}
