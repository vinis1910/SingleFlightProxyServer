#ifndef PROXY_SERVER_HPP
#define PROXY_SERVER_HPP

#include <boost/asio.hpp>
#include <string>

using boost::asio::ip::tcp;

class ProxyServer {
public:
    ProxyServer(boost::asio::io_context& io_context, 
                const std::string& listen_address, short port, 
                std::string db_host, short db_port);

private:
    void do_accept();
    boost::asio::io_context& io_context_;
    tcp::acceptor acceptor_;
    std::string db_host_;
    short db_port_;
};

#endif // PROXY_SERVER_HPP

