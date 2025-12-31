#include "SharedSSLContext.hpp"
#include <spdlog/spdlog.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>

SharedSSLContext::SharedSSLContext()
    : client_context_(boost::asio::ssl::context::tlsv12_client),
      server_context_(boost::asio::ssl::context::tlsv12_server) {
    
    setup_client_context();
    setup_server_context();
    
    spdlog::info("[SharedSSLContext] SSL contexts initialized and ready for reuse");
}

void SharedSSLContext::setup_client_context() {
    client_context_.set_default_verify_paths();
    client_context_.set_verify_mode(boost::asio::ssl::verify_none);
    client_context_.set_options(
        boost::asio::ssl::context::default_workarounds |
        boost::asio::ssl::context::no_sslv2 |
        boost::asio::ssl::context::no_sslv3 |
        boost::asio::ssl::context::single_dh_use);
}

void SharedSSLContext::setup_server_context() {
    server_context_.set_options(
        boost::asio::ssl::context::default_workarounds |
        boost::asio::ssl::context::no_sslv2 |
        boost::asio::ssl::context::no_sslv3 |
        boost::asio::ssl::context::single_dh_use);
    server_context_.set_verify_mode(boost::asio::ssl::verify_none);

    SSL_CTX* ctx = server_context_.native_handle();
    SSL_CTX_set_cipher_list(ctx, "DEFAULT:!aNULL:!eNULL:!MD5:!3DES:!DES:!RC4:!IDEA");

    EVP_PKEY* pkey = EVP_PKEY_new();
    RSA* rsa = RSA_new();
    BIGNUM* bn = BN_new();
    BN_set_word(bn, RSA_F4);
    RSA_generate_key_ex(rsa, 2048, bn, nullptr);
    EVP_PKEY_set1_RSA(pkey, rsa);
    RSA_free(rsa);
    BN_free(bn);

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

    int cert_result = SSL_CTX_use_certificate(ctx, x509);
    int key_result = SSL_CTX_use_PrivateKey(ctx, pkey);

    if (cert_result != 1 || key_result != 1) {
        spdlog::error("[SharedSSLContext] Failed to set certificate or private key!");
        X509_free(x509);
        EVP_PKEY_free(pkey);
        return;
    }

    if (!SSL_CTX_check_private_key(ctx)) {
        spdlog::error("[SharedSSLContext] Private key does not match certificate!");
        X509_free(x509);
        EVP_PKEY_free(pkey);
        return;
    }

    X509_free(x509);
    EVP_PKEY_free(pkey);
    
    spdlog::debug("[SharedSSLContext] Server SSL context configured with self-signed certificate");
}

