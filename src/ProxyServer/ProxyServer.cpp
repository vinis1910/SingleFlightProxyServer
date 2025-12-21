#include "ProxyServer.hpp"
#include "../Session/Session.hpp"
#include <memory>

ProxyServer::ProxyServer(boost::asio::io_context& io_context, short port, 
                        std::string db_host, short db_port)
    : io_context_(io_context),
      acceptor_(io_context, tcp::endpoint(tcp::v4(), port)),
      db_host_(std::move(db_host)), db_port_(db_port) {
    
    do_accept();
}

void ProxyServer::do_accept() {
    acceptor_.async_accept([this](boost::system::error_code ec, tcp::socket socket) {
        if (!ec) {
            std::make_shared<Session>(std::move(socket), io_context_)
                ->start(db_host_, db_port_);
        }
        do_accept();
    });
}