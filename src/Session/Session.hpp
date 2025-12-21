#pragma once

#include <boost/asio.hpp>
#include <memory>
#include <vector>
#include <iostream>

using boost::asio::ip::tcp;

class Session : public std::enable_shared_from_this<Session> {
public:
    Session(tcp::socket client_socket, boost::asio::io_context& io_context);
    void start(const std::string& db_host, short db_port);

private:
    void read_client_startup();
    void relay_to_server(std::size_t length);
    void bridge_client_to_server();
    void bridge_server_to_client();

    tcp::socket client_socket_;
    tcp::socket server_socket_;
    std::vector<char> client_buffer_;
    std::vector<char> server_buffer_{std::vector<char>(8192)};
};