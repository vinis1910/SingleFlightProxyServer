#include "ProxyServer.hpp"
#include "../Session/Session.hpp"
#include <spdlog/spdlog.h>
#include <memory>
#include <boost/asio/ip/address.hpp>

ProxyServer::ProxyServer(boost::asio::io_context& io_context, 
                        const std::string& listen_address, short port, 
                        std::string db_host, short db_port)
    : io_context_(io_context),
      acceptor_(io_context, tcp::endpoint(boost::asio::ip::address::from_string(listen_address), port)),
      db_host_(std::move(db_host)), db_port_(db_port) {

    do_accept();
}

void ProxyServer::shutdown() {
    accepting_.store(false);
    boost::system::error_code ec;
    acceptor_.close(ec);
    spdlog::info("[ProxyServer] Stopped accepting new connections");
}

void ProxyServer::do_accept() {
    if (!accepting_.load()) {
        return;
    }
    
    acceptor_.async_accept([this](boost::system::error_code ec, tcp::socket socket) {
        if (!ec && accepting_.load()) {
            auto session = std::make_shared<Session>(std::move(socket), io_context_, 
                                                     db_host_, db_port_);
            session->start();
        }
        if (accepting_.load()) {
            do_accept();
        }
    });
}