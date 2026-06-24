#include "app.hpp"
#include "http.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

std::string env_or(const char *name, const std::string &fallback) {
    const char *value = std::getenv(name);
    return value && *value ? value : fallback;
}

int env_port_or(int fallback) {
    const char *value = std::getenv("PORT");
    if (!value || !*value) return fallback;
    try {
        return std::stoi(value);
    } catch (...) {
        return fallback;
    }
}

} // namespace

int main() {
    const std::string host = env_or("HOST", "0.0.0.0");
    const int port = env_port_or(8000);
    const std::filesystem::path data_path = env_or("DATA_FILE", "data/chat_state.json");
    const std::filesystem::path dist_dir = env_or("DIST_DIR", "frontend/dist");

    chat::ChatApp app(data_path, dist_dir);
    chat::HttpServer server([&app](const chat::HttpRequest &request) {
        return app.handle(request);
    });

    if (!server.listen(host, port)) {
        std::cerr << "failed to start C++ chat server\n";
        return 1;
    }
    return 0;
}
