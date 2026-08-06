#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {

[[nodiscard]] bool requireTrue(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        return false;
    }
    return true;
}

} // namespace

int main() {
    const std::string managerPath = std::string(MECRAFT_TEST_SOURCE_DIR) + "/src/app/GameManager.cpp";
    std::ifstream managerFile(managerPath, std::ios::binary);
    if (!requireTrue(managerFile.is_open(), "GameManager source must be readable")) {
        return 1;
    }

    const std::string source{std::istreambuf_iterator<char>(managerFile), std::istreambuf_iterator<char>()};
    const size_t shutdown = source.find("void GameManager::shutdown()");
    const size_t waitIdle = source.find("m_rhiDevice->waitIdle();", shutdown);
    const size_t destroyStates = source.find("while (!m_appStateMachine.isEmpty())", shutdown);
    const size_t deviceShutdown = source.find("m_rhiDevice->shutdown();", shutdown);

    const bool orderIsValid = shutdown != std::string::npos && waitIdle != std::string::npos &&
                              destroyStates != std::string::npos && deviceShutdown != std::string::npos &&
                              waitIdle < destroyStates && destroyStates < deviceShutdown;
    return requireTrue(orderIsValid, "GPU work must finish before application states destroy their render resources")
               ? 0
               : 1;
}
