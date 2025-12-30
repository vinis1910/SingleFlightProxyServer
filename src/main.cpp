#include <spdlog/spdlog.h>
#include <boost/asio.hpp>
#include "ProxyServer/ProxyServer.hpp"
#include "Config/Config.hpp"
#include "QueryCache/QueryCache.hpp"
#include <algorithm>
#include <cctype>
#include <thread>
#include <vector>
#include <signal.h>
#include <csignal>
#include <memory>

using boost::asio::ip::tcp;

static boost::asio::io_context* g_io_context = nullptr;
static boost::asio::executor_work_guard<boost::asio::io_context::executor_type>* g_work_guard = nullptr;
static ProxyServer* g_server = nullptr;

void signal_handler(int signal) {
    if (g_io_context && g_work_guard) {
        spdlog::info("[SINGLEFLIGHT PROXY] Received signal {}, shutting down gracefully...", signal);
        if (g_server) {
            g_server->shutdown();
        }
        g_work_guard->reset();
        g_io_context->stop();
    }
}

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
        
        spdlog::info("[SINGLEFLIGHT PROXY] Starting on {}:{}", 
                     config.getListenAddress(), config.getListenPort());
        spdlog::info("[SINGLEFLIGHT PROXY] Redirecting to: {}:{}", 
                     config.getDbHost(), config.getDbPort());
        spdlog::info("[SINGLEFLIGHT PROXY] Using {} worker threads", 
                     config.getNumThreads());
        
        ProxyServer server(io_context, 
                          config.getListenAddress(),
                          config.getListenPort(),
                          config.getDbHost(), 
                          config.getDbPort());

        boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_guard(
            boost::asio::make_work_guard(io_context));

        g_io_context = &io_context;
        g_work_guard = &work_guard;
        g_server = &server;

        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);

        std::vector<std::thread> worker_threads;
        unsigned int num_threads = config.getNumThreads();
        
        for (unsigned int i = 0; i < num_threads; i++) {
            worker_threads.emplace_back([&io_context, i] {
                try {
                    spdlog::debug("[SINGLEFLIGHT PROXY] Worker thread {} started", i);
                    io_context.run();
                    spdlog::debug("[SINGLEFLIGHT PROXY] Worker thread {} finished", i);
                } catch (const std::exception& e) {
                    spdlog::error("[SINGLEFLIGHT PROXY] Worker thread {} error: {}", i, e.what());
                }
            });
        }

        spdlog::info("[SINGLEFLIGHT PROXY] All {} worker threads started. Server running...", num_threads);

        for (auto& thread : worker_threads) {
            thread.join();
        }

        server.shutdown();

        g_io_context = nullptr;
        g_work_guard = nullptr;
        g_server = nullptr;

        spdlog::info("[SINGLEFLIGHT PROXY] All threads joined. Shutting down.");

    } catch (std::exception& e) {
        spdlog::error("[SINGLEFLIGHT PROXY] Unexpected error: {}", e.what());
        return 1;
    }
    return 0;
}