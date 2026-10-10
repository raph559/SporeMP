#pragma once
#ifndef SECURITY_WIN32
#define SECURITY_WIN32
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <security.h>
#include <schannel.h>
#include <ncrypt.h>
#include <wincrypt.h>
#include "protocol.h"
#include <atomic>
#include <memory>

namespace sporemp::network::detail {
bool initialize_sockets(std::string&);
bool random_bytes(void*, size_t);

struct TlsIdentity {
    CredHandle credentials{};
    bool acquired = false;
    PCCERT_CONTEXT certificate = nullptr;
    NCRYPT_PROV_HANDLE provider = 0;
    NCRYPT_KEY_HANDLE key = 0;
    std::wstring key_name;
    Digest pin{};
    bool retained = false; // Durable universe owns the named CNG key.
    ~TlsIdentity();
    bool create(std::string&);
    std::vector<uint8_t> snapshot() const;
    bool restore(const std::vector<uint8_t>&, std::string&);
};

class TlsStream {
public:
    explicit TlsStream(SOCKET socket) : socket_(socket) {}

    ~TlsStream();
    bool handshake(bool server, TlsIdentity*, const Digest&, const std::string&, const std::atomic<bool>&,
                   std::string&);
    bool send(const uint8_t*, size_t, std::string&);
    bool receive(std::vector<uint8_t>&, std::string&);

    SOCKET socket() const noexcept {
        return socket_;
    }

private:
    SOCKET socket_ = INVALID_SOCKET;
    CredHandle credentials_{};
    bool own_credentials_ = false;
    CtxtHandle context_{};
    bool context_valid_ = false;
    SecPkgContext_StreamSizes sizes_{};
    std::vector<uint8_t> encrypted_;
    bool raw_send(const uint8_t*, size_t, std::string&);
    bool read_more(std::string&);
    void keep_unused(size_t unused);
    SECURITY_STATUS handshake_step(bool server, CredHandle* credential, bool first, std::wstring& target,
                                   SecBuffer& token, size_t& unused);
    bool send_token(const SecBuffer& token, std::string& error);
    bool verify_server_certificate(const Digest& pin, std::string& error);
    bool accept_connection(std::string& error);
};

SOCKET connect_socket(const std::string&, uint16_t, const std::atomic<bool>&, std::string&);
SOCKET listen_socket(const std::string&, uint16_t, uint16_t&, std::string&);
} // namespace sporemp::network::detail
