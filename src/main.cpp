#include <spdlog/spdlog.h>
#include <boost/asio.hpp>
#include "ProxyServer/ProxyServer.hpp"

using boost::asio::ip::tcp;

int main() {
    try {
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
        spdlog::set_level(spdlog::level::info);

        unsigned short local_port = 6000;
        std::string db_host = "127.0.0.1";
        unsigned short db_port = 5432;

        boost::asio::io_context io_context;

        spdlog::info("--- [DB PROXY] Starting on 0.0.0.0:{} ---", local_port);
        spdlog::info("--- Redirecting to: {}:{} ---", db_host, db_port);

        ProxyServer server(io_context, local_port, db_host, db_port);
        io_context.run();

    } catch (std::exception& e) {
        spdlog::error("Unexpected error: {}", e.what());
        return 1;
    }

    return 0;
}