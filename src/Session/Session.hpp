#pragma once

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <memory>
#include <vector>
#include <iostream>
#include <optional>
#include <atomic>

using boost::asio::ip::tcp;
using ssl_socket = boost::asio::ssl::stream<tcp::socket>;

class Session : public std::enable_shared_from_this<Session> {
public:
    Session(tcp::socket client_socket, boost::asio::io_context& io_context, 
            const std::string& db_host, short db_port);
    ~Session();
    void start();

private:
    boost::asio::ssl::context client_ssl_context_;
    boost::asio::ssl::context server_ssl_context_;

    std::unique_ptr<ssl_socket> client_ssl_socket_;
    std::unique_ptr<ssl_socket> server_ssl_socket_;

    std::vector<char> client_buffer_;
    std::vector<char> server_buffer_{std::vector<char>(8192)};
    std::vector<char> startup_packet_;
    std::string current_query_;
    std::vector<char> cached_response_;

    boost::asio::io_context& io_context_;
    std::optional<tcp::socket> client_socket_;
    std::optional<tcp::socket> server_socket_;
    std::string db_host_;
    short db_port_;
    bool ssl_enabled_;
    std::atomic<bool> is_destroying_;
    std::atomic<bool> client_closed_;
    std::atomic<bool> server_closed_;

    void read_client_startup();
    void read_client_startup_after_ssl();
    void handle_ssl_request();
    void check_server_ssl_support();
    void perform_ssl_handshake();
    void relay_to_server(std::size_t length);
    void bridge_client_to_server();
    void bridge_server_to_client();
    bool is_sql_query(std::vector<char>& buffer, std::size_t length);
    std::string extract_sql_query(std::vector<char>& buffer, std::size_t length);
    void close();
    void setup_server_ssl_context();
    void cleanup_sockets();
    
    ssl_socket* getServerSslSocket();
    tcp::socket* getServerSocket();
    bool isServerSslEnabled() const;
};