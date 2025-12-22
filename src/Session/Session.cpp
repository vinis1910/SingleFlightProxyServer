#include "Session.hpp"
#include <spdlog/spdlog.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>

Session::Session(tcp::socket client_socket, boost::asio::io_context& io_context)
    : io_context_(io_context),
      client_socket_(std::move(client_socket)), 
      server_socket_(io_context),
      client_ssl_context_(boost::asio::ssl::context::tlsv12_client),
      server_ssl_context_(boost::asio::ssl::context::tlsv12_server),
      ssl_enabled_(false) {
    client_ssl_context_.set_default_verify_paths();
    client_ssl_context_.set_verify_mode(boost::asio::ssl::verify_none);
    client_ssl_context_.set_options(
        boost::asio::ssl::context::default_workarounds |
        boost::asio::ssl::context::no_sslv2 |
        boost::asio::ssl::context::no_sslv3 |
        boost::asio::ssl::context::single_dh_use);
    
    setup_server_ssl_context();
}

void Session::setup_server_ssl_context() {
    server_ssl_context_.set_options(
        boost::asio::ssl::context::default_workarounds |
        boost::asio::ssl::context::no_sslv2 |
        boost::asio::ssl::context::no_sslv3 |
        boost::asio::ssl::context::single_dh_use);
    server_ssl_context_.set_verify_mode(boost::asio::ssl::verify_none);
    
    SSL_CTX* ctx = server_ssl_context_.native_handle();
    SSL_CTX_set_cipher_list(ctx, "DEFAULT:!aNULL:!eNULL:!MD5:!3DES:!DES:!RC4:!IDEA");
    
    EVP_PKEY* pkey = EVP_PKEY_new();
    RSA* rsa = RSA_new();
    BIGNUM* bn = BN_new();
    BN_set_word(bn, RSA_F4);
    RSA_generate_key_ex(rsa, 2048, bn, nullptr);
    EVP_PKEY_assign_RSA(pkey, rsa);
    
    X509* x509 = X509_new();
    X509_set_version(x509, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(x509), 1);
    X509_gmtime_adj(X509_get_notBefore(x509), 0);
    X509_gmtime_adj(X509_get_notAfter(x509), 31536000L);
    X509_set_pubkey(x509, pkey);
    
    X509_NAME* name = X509_get_subject_name(x509);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (unsigned char*)"localhost", -1, -1, 0);
    X509_set_issuer_name(x509, name);
    
    X509_sign(x509, pkey, EVP_sha256());
    
    SSL_CTX_use_certificate(ctx, x509);
    SSL_CTX_use_PrivateKey(ctx, pkey);
    
    if (!SSL_CTX_check_private_key(ctx)) {
        spdlog::error("[Session] Private key does not match certificate!");
    }
    
    X509_free(x509);
    EVP_PKEY_free(pkey);
    BN_free(bn);
}

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
                    spdlog::info("[Session] Client requested SSL. Checking server support");
                    startup_packet_.assign(client_buffer_.begin(), client_buffer_.begin() + length);
                    check_server_ssl_support();
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

void Session::read_client_startup_after_ssl() {
    auto self = shared_from_this();
    client_buffer_.resize(8192);
    
    client_ssl_socket_->async_read_some(boost::asio::buffer(client_buffer_),
        [this, self](boost::system::error_code ec, std::size_t length) {
            if (!ec) {
                boost::asio::async_write(*server_ssl_socket_,
                    boost::asio::buffer(client_buffer_, length),
                    [this, self](boost::system::error_code ec, std::size_t) {
                        if (!ec) {
                            spdlog::debug("[Session] Startup packet sent via SSL. Starting bridge mode.");
                            bridge_client_to_server();
                            bridge_server_to_client();
                        } else {
                            spdlog::error("[Session] Failed to send startup packet via SSL: {}", ec.message());
                        }
                    });
            } else {
                spdlog::error("[Session] Failed to read startup packet from client: {}", ec.message());
            }
        });
}

void Session::check_server_ssl_support() {
    auto self = shared_from_this();
    
    std::vector<char> ssl_request(8);
    ssl_request[0] = 0x00;
    ssl_request[1] = 0x00;
    ssl_request[2] = 0x00;
    ssl_request[3] = 0x08;
    ssl_request[4] = 0x04;
    ssl_request[5] = 0xd2;
    ssl_request[6] = 0x16;
    ssl_request[7] = 0x2f;
    
    boost::asio::async_write(server_socket_, boost::asio::buffer(ssl_request),
        [this, self](boost::system::error_code ec, std::size_t) {
            if (!ec) {
                auto buffer = std::make_shared<std::vector<char>>(1);
                boost::asio::async_read(server_socket_, boost::asio::buffer(*buffer),
                    [this, self, buffer](boost::system::error_code ec, std::size_t) {
                        if (!ec) {
                            if ((*buffer)[0] == 'S') {
                                spdlog::info("[Session] Server supports SSL. Performing SSL handshake...");
                                perform_ssl_handshake();
                            } else {
                                spdlog::info("[Session] Server does not support SSL. Responding 'N' to client.");
                                handle_ssl_request();
                            }
                        } else {
                            spdlog::error("[Session] Failed to read server SSL response: {}", ec.message());
                        }
                    });
            } else {
                spdlog::error("[Session] Failed to send SSL request to server: {}", ec.message());
            }
        });
}

void Session::handle_ssl_request() {
    auto self = shared_from_this();
    boost::asio::async_write(client_socket_, boost::asio::buffer("N", 1),
        [this, self](boost::system::error_code ec, std::size_t) {
            if (!ec) {
                relay_to_server(startup_packet_.size());
            } else {
                spdlog::error("[Session] Failed to send SSL response to client: {}", ec.message());
            }
        });
}

void Session::perform_ssl_handshake() {
    auto self = shared_from_this();
    
    client_ssl_socket_ = std::make_unique<ssl_socket>(std::move(client_socket_), server_ssl_context_);
    server_ssl_socket_ = std::make_unique<ssl_socket>(std::move(server_socket_), client_ssl_context_);
    
    client_ssl_socket_->set_verify_mode(boost::asio::ssl::verify_none);
    server_ssl_socket_->set_verify_mode(boost::asio::ssl::verify_none);
    
    boost::asio::async_write(client_ssl_socket_->next_layer(), boost::asio::buffer("S", 1),
        [this, self](boost::system::error_code ec, std::size_t) {
            if (!ec) {
                client_ssl_socket_->async_handshake(boost::asio::ssl::stream_base::server,
                    [this, self](boost::system::error_code ec) {
                        if (!ec) {
                            spdlog::info("[Session] SSL handshake with client completed.");
                            
                            server_ssl_socket_->async_handshake(boost::asio::ssl::stream_base::client,
                                [this, self](boost::system::error_code ec) {
                                    if (!ec) {
                                        spdlog::info("[Session] SSL handshake with server completed.");
                                        ssl_enabled_ = true;
                                        read_client_startup_after_ssl();
                                    } else {
                                        spdlog::error("[Session] SSL handshake with server failed: {}", ec.message());
                                    }
                                });
                        } else {
                            spdlog::error("[Session] SSL handshake with client failed: {}", ec.message());
                        }
                    });
            } else {
                spdlog::error("[Session] Failed to send SSL response to client: {}", ec.message());
            }
        });
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
    
    auto read_handler = [this, self](boost::system::error_code ec, std::size_t length) {
        if (!ec) {
            if (is_sql_query(client_buffer_, length)) {
                std::string sql_query = std::string(client_buffer_.begin(), client_buffer_.begin() + length);
                spdlog::info("[Session] SQL query: {}", sql_query);
            }
            
            auto write_handler = [this, self](boost::system::error_code ec, std::size_t) {
                if (!ec) {
                    bridge_client_to_server();
                } else {
                    spdlog::warn("[Session] Failed to write to server: {}", ec.message());
                }
            };
            
            if (ssl_enabled_ && server_ssl_socket_) {
                boost::asio::async_write(*server_ssl_socket_, boost::asio::buffer(client_buffer_, length), write_handler);
            } else {
                boost::asio::async_write(server_socket_, boost::asio::buffer(client_buffer_, length), write_handler);
            }
        } else {
            if (ec != boost::asio::error::eof) {
                spdlog::warn("[Session] Client read error: {}", ec.message());
            }
        }
    };
    
    if (ssl_enabled_ && client_ssl_socket_) {
        client_ssl_socket_->async_read_some(boost::asio::buffer(client_buffer_), read_handler);
    } else {
        client_socket_.async_read_some(boost::asio::buffer(client_buffer_), read_handler);
    }
}

void Session::bridge_server_to_client() {
    auto self = shared_from_this();
    
    auto read_handler = [this, self](boost::system::error_code ec, std::size_t length) {
        if (!ec) {
            auto write_handler = [this, self](boost::system::error_code ec, std::size_t) {
                if (!ec) {
                    bridge_server_to_client();
                } else {
                    spdlog::warn("[Session] Failed to write to client: {}", ec.message());
                }
            };
            
            if (ssl_enabled_ && client_ssl_socket_) {
                boost::asio::async_write(*client_ssl_socket_, boost::asio::buffer(server_buffer_, length), write_handler);
            } else {
                boost::asio::async_write(client_socket_, boost::asio::buffer(server_buffer_, length), write_handler);
            }
        } else {
            if (ec != boost::asio::error::eof) {
                spdlog::warn("[Session] Server read error: {}", ec.message());
            }
        }
    };
    
    if (ssl_enabled_ && server_ssl_socket_) {
        server_ssl_socket_->async_read_some(boost::asio::buffer(server_buffer_), read_handler);
    } else {
        server_socket_.async_read_some(boost::asio::buffer(server_buffer_), read_handler);
    }
}


bool Session::is_sql_query(std::vector<char> buffer, std::size_t length) {
    return length > 0 && buffer[0] == 'Q';
}