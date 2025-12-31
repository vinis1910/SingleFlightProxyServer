#include "Session.hpp"
#include "SharedSSLContext.hpp"
#include "../QueryCache/QueryCache.hpp"
#include <spdlog/spdlog.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <string>
#include <cstring>
#include <functional>
#include <memory>

static bool is_expected_ssl_error(const boost::system::error_code& ec) {
    if (ec == boost::asio::error::operation_aborted) {
        return true;
    }

    if (ec.category() == boost::asio::error::get_ssl_category()) {
        std::string msg = ec.message();
        if (msg.find("stream truncated") != std::string::npos ||
            msg.find("short read") != std::string::npos ||
            msg.find("protocol is shutdown") != std::string::npos ||
            msg.find("bad record mac") != std::string::npos) {
            return true;
        }
    }

    return false;
}

Session::Session(tcp::socket client_socket, boost::asio::io_context& io_context, 
                 const std::string& db_host, short db_port)
    : client_ssl_context_(SharedSSLContext::getInstance().getClientContext()),
      server_ssl_context_(SharedSSLContext::getInstance().getServerContext()),
      io_context_(io_context),
      client_socket_(std::move(client_socket)),
      server_socket_(io_context),
      db_host_(db_host),
      db_port_(db_port),
      ssl_enabled_(false),
      is_destroying_(false),
      client_closed_(false),
      server_closed_(false) {

    client_buffer_.resize(8192);
    server_buffer_.resize(8192);
    startup_packet_.resize(8192);
}

Session::~Session() {
    is_destroying_.store(true);
    cleanup_sockets();
    client_ssl_socket_.reset();
    server_ssl_socket_.reset();

    spdlog::debug("[Session] Destroyed");
}

void Session::close() {
    bool expected = false;
    if (!is_destroying_.compare_exchange_strong(expected, true)) {
        spdlog::debug("[Session] close() called but already closing/destroying");
        return;
    }

    spdlog::debug("[Session] Closing session");
    cleanup_sockets();
}

void Session::cleanup_sockets() {
    boost::system::error_code ec;

    if (client_ssl_socket_) {
        try {
            auto& lowest = client_ssl_socket_->lowest_layer();
            if (lowest.is_open()) {
                lowest.cancel(ec);
            }
        } catch (...) {}
    }

    if (server_ssl_socket_) {
        try {
            auto& lowest = server_ssl_socket_->lowest_layer();
            if (lowest.is_open()) {
                lowest.cancel(ec);
            }
        } catch (...) {}
    }

    if (client_socket_ && client_socket_->is_open()) {
        try {
            client_socket_->cancel(ec);
        } catch (...) {}
    }

    if (server_socket_ && server_socket_->is_open()) {
        try {
            server_socket_->cancel(ec);
        } catch (...) {}
    }

    if (client_ssl_socket_) {
        try {
            auto& lowest = client_ssl_socket_->lowest_layer();
            if (lowest.is_open()) {
                boost::system::error_code shutdown_ec;
                client_ssl_socket_->shutdown(shutdown_ec);
            }
        } catch (...) {}
    }

    if (server_ssl_socket_) {
        try {
            auto& lowest = server_ssl_socket_->lowest_layer();
            if (lowest.is_open()) {
                boost::system::error_code shutdown_ec;
                server_ssl_socket_->shutdown(shutdown_ec);
            }
        } catch (...) {}
    }

    if (client_ssl_socket_) {
        try {
            auto& lowest = client_ssl_socket_->lowest_layer();
            if (lowest.is_open()) {
                lowest.close(ec);
            }
        } catch (...) {}
    }

    if (server_ssl_socket_) {
        try {
            auto& lowest = server_ssl_socket_->lowest_layer();
            if (lowest.is_open()) {
                lowest.close(ec);
            }
        } catch (...) {}
    }

    if (client_socket_ && client_socket_->is_open()) {
        try {
            client_socket_->close(ec);
        } catch (...) {}
    }

    if (server_socket_ && server_socket_->is_open()) {
        try {
            server_socket_->close(ec);
        } catch (...) {}
    }
}


void Session::start() {
    if (is_destroying_.load()) {
        return;
    }

    auto self = shared_from_this();

    if (!client_socket_) {
        spdlog::error("[Session] Client socket not available");
        return;
    }

    spdlog::debug("[Session] Connecting to database server asynchronously...");
    connect_to_database([this, self](boost::system::error_code ec) {
        if (is_destroying_.load()) {
            return;
        }
        
        if (ec) {
            spdlog::error("[Session] Failed to connect to {}:{} - {}", db_host_, db_port_, ec.message());
            close();
            return;
        }
        
        spdlog::debug("[Session] Connected to database server. Waiting for SSLRequest");
        read_client_startup();
    });
}

void Session::read_client_startup() {
    if (is_destroying_.load()) {
        return;
    }

    auto self = shared_from_this();

    if (!client_socket_) {
        spdlog::error("[Session] Client socket not available in read_client_startup");
        return;
    }

    client_buffer_.resize(8192); 

    client_socket_->async_read_some(boost::asio::buffer(client_buffer_),
        [this, self](boost::system::error_code ec, std::size_t length) {
            if (is_destroying_.load()) {
                return;
            }
            if (!ec) {
                if (length == 8 &&
                    static_cast<unsigned char>(client_buffer_[4]) == 0x04 &&
                    static_cast<unsigned char>(client_buffer_[5]) == 0xd2) {
                    spdlog::info("[Session] Client requested SSL. Checking server support");
                    startup_packet_.assign(client_buffer_.begin(), client_buffer_.begin() + length);
                    check_server_ssl_support();
                } else {
                    relay_to_server(length);
                }
            } else {
                if (!is_expected_ssl_error(ec)) {
                    spdlog::warn("[Session] Read error: {}", ec.message());
                    close();
                }
            }
        });
}

void Session::read_client_startup_after_ssl() {
    if (is_destroying_.load()) {
        return;
    }

    auto self = shared_from_this();

    if (!client_ssl_socket_) {
        spdlog::error("[Session] Client SSL socket not available");
        return;
    }

    spdlog::debug("[Session] Waiting for startup packet from client after SSL handshake");
    client_buffer_.resize(8192);

    client_ssl_socket_->async_read_some(boost::asio::buffer(client_buffer_),
        [this, self](boost::system::error_code ec, std::size_t length) {
            if (is_destroying_.load()) {
                return;
            }
            if (!ec) {
                spdlog::debug("[Session] Received {} bytes from client after SSL handshake", length);
                auto* server_sock = getServerSslSocket();
                if (!server_sock) {
                    spdlog::error("[Session] Server SSL socket not available");
                    return;
                }

                boost::asio::async_write(*server_sock,
                    boost::asio::buffer(client_buffer_, length),
                    [this, self](boost::system::error_code ec, std::size_t) {
                        if (is_destroying_.load()) {
                            return;
                        }
                        if (!ec) {
                            spdlog::info("[Session] Startup packet sent via SSL. Starting bridge mode.");
                            bridge_server_to_client();
                            bridge_client_to_server();
                        } else {
                            spdlog::error("[Session] Failed to send startup packet via SSL: {}", ec.message());
                            close();
                        }
                    });
            } else {
                if (!is_expected_ssl_error(ec)) {
                    spdlog::error("[Session] Failed to read startup packet from client: {} (code: {})", ec.message(), ec.value());
                    close();
                } else {
                    spdlog::debug("[Session] Expected SSL error while reading startup packet: {}", ec.message());
                }
            }
        });
}

void Session::check_server_ssl_support() {
    if (is_destroying_.load()) {
        return;
    }

    auto self = shared_from_this();

    if (!server_socket_) {
        spdlog::error("[Session] Server socket not available");
        return;
    }

    if (!server_socket_->is_open()) {
        spdlog::warn("[Session] Server socket closed, reconnecting...");
        connect_to_database([this, self](boost::system::error_code ec) {
            if (is_destroying_.load()) {
                return;
            }
            
            if (ec) {
                spdlog::error("[Session] Failed to reconnect to {}:{} - {}", db_host_, db_port_, ec.message());
                close();
                return;
            }
            spdlog::debug("[Session] Reconnected to database server");
            check_server_ssl_support();
        });
        return;
    }

    std::vector<unsigned char> ssl_request_data = {
        0x00, 0x00, 0x00, 0x08, 
        static_cast<unsigned char>(0x04), 
        static_cast<unsigned char>(0xd2), 
        static_cast<unsigned char>(0x16), 
        static_cast<unsigned char>(0x2f)
    };
    auto ssl_request = std::make_shared<std::vector<unsigned char>>(std::move(ssl_request_data));

    boost::asio::async_write(*server_socket_, boost::asio::buffer(*ssl_request),
        [this, self, ssl_request](boost::system::error_code ec, std::size_t) {
            if (is_destroying_.load()) {
                return;
            }
            if (!ec) {
                auto buffer = std::make_shared<std::vector<char>>(1);
                boost::asio::async_read(*server_socket_, boost::asio::buffer(*buffer),
                    [this, self, buffer](boost::system::error_code ec, std::size_t) {
                        if (is_destroying_.load()) {
                            return;
                        }
                        if (!ec) {
                            if ((*buffer)[0] == 'S') {
                                spdlog::info("[Session] Server supports SSL. Performing SSL handshake");
                                perform_ssl_handshake();
                            } else {
                                spdlog::info("[Session] Server does not support SSL. Responding 'N' to client.");
                                handle_ssl_request();
                            }
                        } else {
                            if (!is_expected_ssl_error(ec)) {
                                spdlog::error("[Session] Failed to read server SSL response: {}", ec.message());
                                close();
                            }
                        }
                    });
            } else {
                if (!is_expected_ssl_error(ec)) {
                    spdlog::error("[Session] Failed to send SSL request to server: {}", ec.message());
                    close();
                }
            }
        });
}

void Session::handle_ssl_request() {
    if (is_destroying_.load()) {
        return;
    }

    auto self = shared_from_this();

    if (!client_socket_) {
        spdlog::error("[Session] Client socket not available");
        return;
    }

    boost::asio::async_write(*client_socket_, boost::asio::buffer("N", 1),
        [this, self](boost::system::error_code ec, std::size_t) {
            if (is_destroying_.load()) {
                return;
            }
            if (!ec) {
                relay_to_server(startup_packet_.size());
            } else {
                if (!is_expected_ssl_error(ec)) {
                    spdlog::error("[Session] Failed to send SSL response to client: {}", ec.message());
                    close();
                }
            }
        });
}

void Session::perform_ssl_handshake() {
    if (is_destroying_.load()) {
        return;
    }

    auto self = shared_from_this();

    if (!client_socket_ || !server_socket_) {
        spdlog::error("[Session] Sockets not available for SSL handshake");
        close();
        return;
    }

    boost::asio::async_write(*client_socket_, boost::asio::buffer("S", 1),
        [this, self](boost::system::error_code ec, std::size_t) {
            if (is_destroying_.load()) {
                return;
            }
            if (ec) {
                if (!is_expected_ssl_error(ec)) {
                    spdlog::error("[Session] Failed to send SSL response: {}", ec.message());
                    close();
                }
                return;
            }

            try {
                client_ssl_socket_ = std::make_unique<ssl_socket>(std::move(*client_socket_), server_ssl_context_);
                server_ssl_socket_ = std::make_unique<ssl_socket>(std::move(*server_socket_), client_ssl_context_);

                client_socket_.reset();
                server_socket_.reset();

                client_ssl_socket_->set_verify_mode(boost::asio::ssl::verify_none);
                server_ssl_socket_->set_verify_mode(boost::asio::ssl::verify_none);
            } catch (const std::exception& e) {
                spdlog::error("[Session] Failed to create SSL streams: {}", e.what());
                close();
                return;
            }

            spdlog::debug("[Session] Starting SSL handshake with client (server mode)");
            client_ssl_socket_->async_handshake(boost::asio::ssl::stream_base::server,
                [this, self](boost::system::error_code ec) {
                    if (is_destroying_.load()) {
                        return;
                    }
                    if (ec) {
                        if (is_expected_ssl_error(ec)) {
                            spdlog::debug("[Session] Client closed connection during SSL handshake: {}", ec.message());
                        } else {
                            spdlog::error("[Session] Client handshake failed: {}", ec.message());
                        }
                        close();
                        return;
                    }

                    spdlog::debug("[Session] Client SSL handshake completed. Starting server handshake");
                    if (!server_ssl_socket_) {
                        spdlog::error("[Session] Server SSL socket not available");
                        close();
                        return;
                    }

                    server_ssl_socket_->async_handshake(boost::asio::ssl::stream_base::client,
                        [this, self](boost::system::error_code ec) {
                            if (is_destroying_.load()) {
                                return;
                            }
                            if (!ec) {
                                ssl_enabled_ = true;
                                spdlog::info("[Session] Both SSL handshakes completed. Waiting for client startup packet");
                                read_client_startup_after_ssl();
                            } else {
                                if (!is_expected_ssl_error(ec)) {
                                    spdlog::error("[Session] Server handshake failed: {}", ec.message());
                                    close();
                                }
                            }
                        });
                });
        });
}

void Session::connect_to_database(std::function<void(boost::system::error_code)> callback) {
    if (is_destroying_.load()) {
        return;
    }

    auto self = shared_from_this();

    auto resolver = std::make_shared<tcp::resolver>(io_context_);
    resolver->async_resolve(db_host_, std::to_string(db_port_),
        [this, self, resolver, callback](boost::system::error_code ec, tcp::resolver::results_type endpoints) {
            if (is_destroying_.load()) {
                return;
            }
            
            if (ec) {
                spdlog::error("[Session] Failed to resolve {}:{} - {}", db_host_, db_port_, ec.message());
                callback(ec);
                return;
            }

            boost::asio::async_connect(*server_socket_, endpoints,
                [this, self, callback](boost::system::error_code ec, tcp::endpoint) {
                    if (is_destroying_.load()) {
                        return;
                    }
                    callback(ec);
                });
        });
}

void Session::relay_to_server(std::size_t length) {
    if (is_destroying_.load()) {
        return;
    }

    auto self = shared_from_this();

    if (!server_socket_) {
        spdlog::error("[Session] Server socket not available");
        return;
    }

    boost::asio::async_write(*server_socket_, boost::asio::buffer(client_buffer_, length),
        [this, self](boost::system::error_code ec, std::size_t) {
            if (is_destroying_.load()) {
                return;
            }
            if (!ec) {
                spdlog::debug("[Session] Initial packet relayed to server. Starting bridge mode.");
                bridge_client_to_server();
                bridge_server_to_client();
            } else {
                if (!is_expected_ssl_error(ec)) {
                    spdlog::error("[Session] Failed to relay packet to server: {}", ec.message());
                    close();
                }
            }
        });
}

void Session::bridge_client_to_server() {
    if (is_destroying_.load()) {
        return;
    }

    auto self = shared_from_this();

    auto read_handler = [this, self](boost::system::error_code ec, std::size_t length) {
        if (is_destroying_.load()) {
            return;
        }
        if (!ec) {
            spdlog::debug("[Session] Received {} bytes from client, forwarding to server", length);
            if (is_sql_query(client_buffer_, length)) {
                std::string sql_query = extract_sql_query(client_buffer_, length);
                spdlog::debug("[Session] SQL query: {}", sql_query);
                
                auto& cache = QueryCache::getInstance();
                
                auto flight_result = cache.doSingleFlight(sql_query, 
                    [this, self, sql_query](const std::string& result) {
                        if (is_destroying_.load()) return;
                        
                        spdlog::info("[Session] Received result for query: {} ({} bytes)", 
                                    sql_query, result.size());
                        cached_response_.assign(result.begin(), result.end());
                        
                        auto send_result = [this, self](const std::vector<char>& response) {
                            if (ssl_enabled_ && client_ssl_socket_) {
                                boost::asio::async_write(*client_ssl_socket_,
                                    boost::asio::buffer(response),
                                    [this, self](boost::system::error_code ec, std::size_t) {
                                        if (is_destroying_.load()) return;
                                        if (!ec) {
                                            spdlog::debug("[Session] Response sent to client");
                                            bridge_client_to_server();
                                        } else {
                                            spdlog::warn("[Session] Failed to send response: {}", ec.message());
                                        }
                                    });
                            } else if (client_socket_) {
                                boost::asio::async_write(*client_socket_,
                                    boost::asio::buffer(response),
                                    [this, self](boost::system::error_code ec, std::size_t) {
                                        if (is_destroying_.load()) return;
                                        if (!ec) {
                                            spdlog::debug("[Session] Response sent to client");
                                            bridge_client_to_server();
                                        } else {
                                            spdlog::warn("[Session] Failed to send response: {}", ec.message());
                                        }
                                    });
                            }
                        };
                        
                        send_result(cached_response_);
                    });
                
                if (flight_result == QueryCache::FlightResult::CACHE_HIT) {
                    spdlog::info("[Session] Cache HIT for query: {}", sql_query);
                    return;
                }
                
                if (flight_result == QueryCache::FlightResult::IS_WAITER) {
                    spdlog::info("[Session] Waiting for SingleFlight result for query: {}", sql_query);
                    return;
                }
                
                spdlog::info("[Session] SingleFlight LEADER - sending query to server: {}", sql_query);
                current_query_ = sql_query;
            }

            auto write_handler = [this, self, length](boost::system::error_code ec, std::size_t) {
                if (is_destroying_.load()) {
                    spdlog::debug("[Session] Write handler (client->server): session is destroying, ignoring");
                    return;
                }
                if (!ec) {
                    spdlog::debug("[Session] Successfully forwarded {} bytes to server, continuing bridge", length);
                    bridge_client_to_server();
                } else {
                    if (!is_expected_ssl_error(ec)) {
                        spdlog::warn("[Session] Failed to write to server: {} (code: {})", ec.message(), ec.value());
                        close();
                    } else {
                        spdlog::debug("[Session] Expected SSL error while writing to server: {}", ec.message());
                    }
                }
            };

            if (server_closed_.load()) {
                spdlog::debug("[Session] Server closed, cannot forward client data");
                return;
            }

            if (isServerSslEnabled()) {
                auto* server_sock = getServerSslSocket();
                if (server_sock) {
                    boost::asio::async_write(*server_sock, boost::asio::buffer(client_buffer_, length), write_handler);
                } else {
                    spdlog::warn("[Session] No server SSL socket available for writing");
                    close();
                }
            } else {
                auto* server_sock = getServerSocket();
                if (server_sock) {
                    boost::asio::async_write(*server_sock, boost::asio::buffer(client_buffer_, length), write_handler);
                } else {
                    spdlog::warn("[Session] No server socket available for writing");
                    close();
                }
            }
        } else {
            if (ec == boost::asio::error::eof) {
                spdlog::info("[Session] Client closed connection (EOF)");
                client_closed_.store(true);
                if (server_closed_.load()) {
                    close();
                }
            } else if (!is_expected_ssl_error(ec)) {
                spdlog::warn("[Session] Client read error: {} (code: {})", ec.message(), ec.value());
                close();
            } else {
                spdlog::debug("[Session] Client expected SSL error: {} (code: {})", ec.message(), ec.value());
            }
        }
    };

    if (ssl_enabled_ && client_ssl_socket_) {
        spdlog::debug("[Session] Waiting for data from client (SSL)");
        client_ssl_socket_->async_read_some(boost::asio::buffer(client_buffer_), read_handler);
    } else if (client_socket_) {
        spdlog::debug("[Session] Waiting for data from client (plain)");
        client_socket_->async_read_some(boost::asio::buffer(client_buffer_), read_handler);
    } else {
        spdlog::warn("[Session] No client socket available for reading");
        close();
    }
}

void Session::bridge_server_to_client() {
    if (is_destroying_.load()) {
        return;
    }

    auto self = shared_from_this();

    auto read_handler = [this, self](boost::system::error_code ec, std::size_t length) {
        if (is_destroying_.load()) {
            return;
        }
        if (!ec) {
            spdlog::debug("[Session] Received {} bytes from server, forwarding to client", length);
            
            if (!current_query_.empty()) {
                std::string response_str(server_buffer_.begin(), server_buffer_.begin() + length);
                
                auto& cache = QueryCache::getInstance();
                cache.notifyFlightResult(current_query_, response_str);
                spdlog::info("[Session] SingleFlight LEADER - notified result for query: {} ({} bytes)", 
                            current_query_, length);
                
                current_query_.clear();
            }
            
            auto write_handler = [this, self, length](boost::system::error_code ec, std::size_t) {
                if (is_destroying_.load()) {
                    spdlog::debug("[Session] Write handler: session is destroying, ignoring");
                    return;
                }
                if (!ec) {
                    spdlog::debug("[Session] Successfully forwarded {} bytes to client, continuing bridge", length);
                    bridge_server_to_client();
                } else {
                    if (!is_expected_ssl_error(ec)) {
                        spdlog::warn("[Session] Failed to write to client: {} (code: {})", ec.message(), ec.value());
                        close();
                    } else {
                        spdlog::debug("[Session] Expected SSL error while writing to client: {}", ec.message());
                    }
                }
            };

            if (client_closed_.load()) {
                spdlog::debug("[Session] Client closed, cannot forward server data");
                return;
            }

            if (ssl_enabled_ && client_ssl_socket_) {
                boost::asio::async_write(*client_ssl_socket_, 
                    boost::asio::buffer(server_buffer_.data(), length), 
                    write_handler);
            } else if (client_socket_) {
                boost::asio::async_write(*client_socket_, 
                    boost::asio::buffer(server_buffer_.data(), length), 
                    write_handler);
            } else {
                spdlog::warn("[Session] No client socket available for writing");
                close();
            }
        } else {
            if (ec == boost::asio::error::eof) {
                spdlog::info("[Session] Server closed connection (EOF)");
                server_closed_.store(true);
                if (client_closed_.load()) {
                    close();
                }
            } else if (!is_expected_ssl_error(ec)) {
                spdlog::warn("[Session] Server read error: {} (code: {})", ec.message(), ec.value());
                close();
            } else {
                spdlog::debug("[Session] Server expected SSL error: {} (code: {})", ec.message(), ec.value());
            }
        }
    };

    if (server_closed_.load()) {
        spdlog::debug("[Session] Server already closed, skipping read");
        return;
    }

    if (isServerSslEnabled()) {
        auto* server_sock = getServerSslSocket();
        if (server_sock) {
            spdlog::debug("[Session] Waiting for data from server (SSL)");
            server_sock->async_read_some(boost::asio::buffer(server_buffer_), read_handler);
        } else {
            spdlog::warn("[Session] No server SSL socket available for reading");
            close();
        }
    } else {
        auto* server_sock = getServerSocket();
        if (server_sock) {
            spdlog::debug("[Session] Waiting for data from server (plain)");
            server_sock->async_read_some(boost::asio::buffer(server_buffer_), read_handler);
        } else {
            spdlog::warn("[Session] No server socket available for reading");
            close();
        }
    }
}

bool Session::is_sql_query(const std::vector<char>& buffer, std::size_t length) const {
    return length > 0 && static_cast<unsigned char>(buffer[0]) == 'Q';
}

std::string Session::extract_sql_query(const std::vector<char>& buffer, std::size_t length) const {
    if (length < 5) {
        return "";
    }
    
    const char* data = buffer.data();
    const char* query_start = data + 5;
    const char* query_end = data + length;
    
    for (const char* p = query_start; p < query_end; ++p) {
        if (*p == '\0') {
            query_end = p;
            break;
        }
    }
    
    if (query_end <= query_start) {
        return "";
    }
    
    return std::string(query_start, query_end);
}

ssl_socket* Session::getServerSslSocket() {
    return server_ssl_socket_.get();
}

tcp::socket* Session::getServerSocket() {
    if (server_socket_ && server_socket_->is_open()) {
        return &(*server_socket_);
    }
    return nullptr;
}

bool Session::isServerSslEnabled() const {
    return ssl_enabled_ && server_ssl_socket_ != nullptr;
}
