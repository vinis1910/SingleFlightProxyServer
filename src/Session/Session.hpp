#pragma once

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <memory>
#include <vector>
#include <iostream>

using boost::asio::ip::tcp;
using ssl_socket = boost::asio::ssl::stream<tcp::socket>;

class Session : public std::enable_shared_from_this<Session> {
public:
    Session(tcp::socket client_socket, boost::asio::io_context& io_context);
    void start(const std::string& db_host, short db_port);

private:
    void read_client_startup();
    void read_client_startup_after_ssl();
    void handle_ssl_request();
    void check_server_ssl_support();
    void perform_ssl_handshake();
    void relay_to_server(std::size_t length);
    void bridge_client_to_server();
    void bridge_server_to_client();
    bool is_sql_query(std::vector<char> buffer, std::size_t length);

    boost::asio::io_context& io_context_;
    tcp::socket client_socket_;
    tcp::socket server_socket_;
    
    boost::asio::ssl::context client_ssl_context_;
    boost::asio::ssl::context server_ssl_context_;
    std::unique_ptr<ssl_socket> client_ssl_socket_;
    std::unique_ptr<ssl_socket> server_ssl_socket_;
    bool ssl_enabled_;
    
    void setup_server_ssl_context();
    
    std::vector<char> client_buffer_;
    std::vector<char> server_buffer_{std::vector<char>(8192)};
    std::vector<char> startup_packet_;
};