#include <spdlog/spdlog.h>
#include <boost/asio.hpp>
#include "ProxyServer/ProxyServer.hpp"
#include "Config/Config.hpp"
#include "QueryCache/QueryCache.hpp"
#include <algorithm>
#include <cctype>

using boost::asio::ip::tcp;

int main(int argc, char* argv[]) {
    try {
        auto& config = Config::getInstance();
        std::string config_path = "config.yaml";
        
        if (argc > 1) {
            config_path = argv[1];
        }
        
        if (!config.loadFromFile(config_path)) {
            spdlog::error("Failed to load configuration: {}", config.getError());
            if (!config.isValid()) {
                return 1;
            }
        }
        
        spdlog::set_pattern(config.getLogPattern());
        
        std::string level_str = config.getLogLevel();
        std::transform(level_str.begin(), level_str.end(), level_str.begin(), 
                      [](unsigned char c) { return std::tolower(c); });
        
        if (level_str == "trace") {
            spdlog::set_level(spdlog::level::trace);
        } else if (level_str == "debug") {
            spdlog::set_level(spdlog::level::debug);
        } else if (level_str == "info") {
            spdlog::set_level(spdlog::level::info);
        } else if (level_str == "warn") {
            spdlog::set_level(spdlog::level::warn);
        } else if (level_str == "error") {
            spdlog::set_level(spdlog::level::err);
        } else {
            spdlog::set_level(spdlog::level::info);
        }
        
        auto& cache = QueryCache::getInstance();
        
        cache.setL1MaxSize(config.getL1MaxSize());
        cache.setL1Enabled(config.isL1Enabled());
        
        if (config.isRedisEnabled()) {
            cache.setRedisConfig(
                config.getRedisHost(),
                config.getRedisPort(),
                config.getRedisTimeout()
            );
        }
        
        boost::asio::io_context io_context;
        
        spdlog::info("--- [SINGLEFLIGHT PROXY] Starting on {}:{} ---", 
                     config.getListenAddress(), config.getListenPort());
        spdlog::info("--- Redirecting to: {}:{} ---", 
                     config.getDbHost(), config.getDbPort());
        
        ProxyServer server(io_context, 
                          config.getListenAddress(),
                          config.getListenPort(),
                          config.getDbHost(), 
                          config.getDbPort());
        io_context.run();
        
    } catch (std::exception& e) {
        spdlog::error("Unexpected error: {}", e.what());
        return 1;
    }
    
    return 0;
}