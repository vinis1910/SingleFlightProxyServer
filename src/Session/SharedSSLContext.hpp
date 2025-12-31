#pragma once

#include <boost/asio/ssl.hpp>
#include <memory>
#include <mutex>

class SharedSSLContext {
public:
    static SharedSSLContext& getInstance() {
        static SharedSSLContext instance;
        return instance;
    }

    boost::asio::ssl::context& getClientContext() {
        return client_context_;
    }

    boost::asio::ssl::context& getServerContext() {
        return server_context_;
    }

    SharedSSLContext(const SharedSSLContext&) = delete;
    SharedSSLContext& operator=(const SharedSSLContext&) = delete;

private:
    SharedSSLContext();
    ~SharedSSLContext() = default;

    void setup_client_context();
    void setup_server_context();

    boost::asio::ssl::context client_context_;
    boost::asio::ssl::context server_context_;
    std::once_flag init_flag_;
};

