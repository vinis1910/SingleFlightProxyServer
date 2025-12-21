#include "Session.hpp"
#include <spdlog/spdlog.h>

Session::Session(tcp::socket client_socket, boost::asio::io_context& io_context)
    : client_socket_(std::move(client_socket)), 
      server_socket_(io_context) {}

void Session::start(const std::string& db_host, short db_port) {
    auto self = shared_from_this();
    
    tcp::resolver resolver(client_socket_.get_executor());
    auto endpoints = resolver.resolve(db_host, std::to_string(db_port));

    boost::asio::async_connect(server_socket_, endpoints,
        [this, self](boost::system::error_code ec, tcp::endpoint) {
            if (!ec) {
                spdlog::info("[Session] Connected to PostgreSQL. Waiting for SSLRequest...");
                read_client_startup();
            } else {
                spdlog::error("[Session] Failed to connect to PostgreSQL: {}", ec.message());
            }
        });
}

void Session::read_client_startup() {
    auto self = shared_from_this();
    client_buffer_.resize(8192); 
    
    client_socket_.async_read_some(boost::asio::buffer(client_buffer_),
        [this, self](boost::system::error_code ec, std::size_t length) {
            if (!ec) {
                if (length == 8 && 
                    static_cast<unsigned char>(client_buffer_[4]) == 0x04 && 
                    static_cast<unsigned char>(client_buffer_[5]) == 0xd2) {
                    spdlog::info("[Session] Client requested SSL. Responding 'N' (No).");
                    
                    boost::asio::async_write(client_socket_, boost::asio::buffer("N", 1),
                        [this, self](boost::system::error_code ec, std::size_t) {
                            if (!ec) {
                                read_client_startup();
                            } else {
                                spdlog::error("[Session] Failed to send SSL response: {}", ec.message());
                            }
                        });
                } else {
                    relay_to_server(length);
                }
            } else {
                if (ec != boost::asio::error::eof) {
                    spdlog::warn("[Session] Read error: {}", ec.message());
                }
            }
            }
        );
}

void Session::relay_to_server(std::size_t length) {
    auto self = shared_from_this();
    boost::asio::async_write(server_socket_, boost::asio::buffer(client_buffer_, length),
        [this, self](boost::system::error_code ec, std::size_t) {
            if (!ec) {
                spdlog::debug("[Session] Initial packet relayed to server. Starting bridge mode.");
                bridge_client_to_server();
                bridge_server_to_client();
            } else {
                spdlog::error("[Session] Failed to relay packet to server: {}", ec.message());
            }
        });
}

void Session::bridge_client_to_server() {
    auto self = shared_from_this();
    client_socket_.async_read_some(boost::asio::buffer(client_buffer_),
        [this, self](boost::system::error_code ec, std::size_t length) {
            if (!ec) {
                boost::asio::async_write(server_socket_, boost::asio::buffer(client_buffer_, length),
                    [this, self](boost::system::error_code ec, std::size_t) {
                        if (!ec) {
                            bridge_client_to_server();
                        } else {
                            spdlog::warn("[Session] Failed to write to server: {}", ec.message());
                        }
                    });
            } else {
                if (ec != boost::asio::error::eof) {
                    spdlog::warn("[Session] Client read error: {}", ec.message());
                }
            }
        });
}

void Session::bridge_server_to_client() {
    auto self = shared_from_this();
    server_socket_.async_read_some(boost::asio::buffer(server_buffer_),
        [this, self](boost::system::error_code ec, std::size_t length) {
            if (!ec) {
                boost::asio::async_write(client_socket_, boost::asio::buffer(server_buffer_, length),
                    [this, self](boost::system::error_code ec, std::size_t) {
                        if (!ec) {
                            bridge_server_to_client();
                        } else {
                            spdlog::warn("[Session] Failed to write to client: {}", ec.message());
                        }
                    });
            } else {
                if (ec != boost::asio::error::eof) {
                    spdlog::warn("[Session] Server read error: {}", ec.message());
                }
            }
        });
}
