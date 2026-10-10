#include "tls.h"
#include "state_codec.h"
#include <bcrypt.h>
#include <algorithm>
#include <cstring>
#include <chrono>
#include <mutex>
#include <sstream>

namespace sporemp::network::detail {
namespace {
// The persisted key of an identity is named with this prefix and the hex of 32 random bytes. `restore` opens no key
// named otherwise.
constexpr wchar_t key_name_prefix[] = L"SporeMP-M06-";

std::string status_error(const char* step, long code) {
    std::ostringstream text;
    text << step << ":0x" << std::hex << static_cast<unsigned long>(code);
    return text.str();
}

void socket_options(SOCKET socket) {
    DWORD timeout = 1000;
    setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    timeout = 100;
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    BOOL yes = TRUE;
    setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&yes), sizeof(yes));
}

bool readable(SOCKET socket, long microseconds) {
    fd_set sockets;
    FD_ZERO(&sockets);
    FD_SET(socket, &sockets);
    timeval timeout{0, microseconds};
    return select(0, &sockets, nullptr, nullptr, &timeout) > 0;
}

bool certificate_sha256(PCCERT_CONTEXT certificate, Digest& digest) {
    DWORD size = static_cast<DWORD>(digest.size());
    return CryptHashCertificate2(BCRYPT_SHA256_ALGORITHM, 0, nullptr, certificate->pbCertEncoded,
                                 certificate->cbCertEncoded, digest.data(), &size) != FALSE;
}

// Which persisted key a certificate belongs to. The strings are borrowed from `key_name` and the provider's name.
CRYPT_KEY_PROV_INFO key_provider_info(std::wstring& key_name) {
    CRYPT_KEY_PROV_INFO info{};
    info.pwszContainerName = key_name.data();
    info.pwszProvName = const_cast<LPWSTR>(MS_KEY_STORAGE_PROVIDER);
    info.dwKeySpec = 0; // CNG NCryptOpenKey legacy key specification, not CERT_KEY_CONTEXT key type.
    return info;
}

// TLS 1.2 server credentials for the identity's certificate.
SECURITY_STATUS acquire_server_credentials(TlsIdentity& identity) {
    SCHANNEL_CRED settings{};
    settings.dwVersion = SCHANNEL_CRED_VERSION;
    settings.cCreds = 1;
    settings.paCred = &identity.certificate;
    settings.grbitEnabledProtocols = SP_PROT_TLS1_2_SERVER;
    settings.dwFlags = SCH_USE_STRONG_CRYPTO | SCH_CRED_NO_SYSTEM_MAPPER;
    TimeStamp expires{};
    return AcquireCredentialsHandleW(nullptr, const_cast<LPWSTR>(UNISP_NAME_W), SECPKG_CRED_INBOUND, nullptr, &settings,
                                     nullptr, nullptr, &identity.credentials, &expires);
}

// TLS 1.2 client credentials without a certificate. The client validates the server itself, against its pin.
SECURITY_STATUS acquire_client_credentials(CredHandle& credentials) {
    SCHANNEL_CRED settings{};
    settings.dwVersion = SCHANNEL_CRED_VERSION;
    settings.grbitEnabledProtocols = SP_PROT_TLS1_2_CLIENT;
    settings.dwFlags = SCH_CRED_MANUAL_CRED_VALIDATION | SCH_CRED_NO_DEFAULT_CREDS | SCH_USE_STRONG_CRYPTO;
    TimeStamp expires{};
    return AcquireCredentialsHandleW(nullptr, const_cast<LPWSTR>(UNISP_NAME_W), SECPKG_CRED_OUTBOUND, nullptr,
                                     &settings, nullptr, nullptr, &credentials, &expires);
}

// The form `TlsIdentity::snapshot` writes: a schema word, the key name one character to a word, then the encoded
// certificate.
bool read_identity_snapshot(const std::vector<uint8_t>& bytes, std::wstring& name, std::vector<uint8_t>& encoded,
                            std::string& error) {
    state::Reader in{bytes};
    if (in.word() != 1) {
        error = "tls_identity_schema";
        return false;
    }
    const auto length = in.word();
    if (length != 76) {
        error = "tls_identity_key_name_length";
        return false;
    }
    for (uint32_t n = 0; n < length; ++n) {
        const auto character = in.word();
        if (!character || character > 127) {
            error = "tls_identity_key_name";
            return false;
        }
        name += wchar_t(character);
    }
    encoded = in.data(64 * 1024);
    if (!in.done() || encoded.empty() || name.substr(0, 12) != key_name_prefix ||
        name.substr(12).find_first_not_of(L"0123456789abcdef") != std::wstring::npos) {
        error = "tls_identity_invalid";
        return false;
    }
    return true;
}

// The recovered certificate has to be within its validity period and to belong to the key already opened.
bool bind_recovered_certificate(TlsIdentity& identity, const std::vector<uint8_t>& encoded, std::string& error) {
    identity.certificate =
        CertCreateCertificateContext(X509_ASN_ENCODING, encoded.data(), static_cast<DWORD>(encoded.size()));
    if (!identity.certificate || CertVerifyTimeValidity(nullptr, identity.certificate->pCertInfo) != 0) {
        error = "tls_recovery_certificate_invalid_or_expired";
        return false;
    }
    CRYPT_KEY_PROV_INFO info = key_provider_info(identity.key_name);
    if (!CertSetCertificateContextProperty(identity.certificate, CERT_KEY_PROV_INFO_PROP_ID, 0, &info)) {
        error = "tls_recovery_key_binding";
        return false;
    }
    HCRYPTPROV_OR_NCRYPT_KEY_HANDLE matching = 0;
    DWORD spec = 0;
    BOOL release = FALSE;
    if (!CryptAcquireCertificatePrivateKey(identity.certificate,
                                           CRYPT_ACQUIRE_ONLY_NCRYPT_KEY_FLAG | CRYPT_ACQUIRE_COMPARE_KEY_FLAG, nullptr,
                                           &matching, &spec, &release)) {
        error = "tls_recovery_key_mismatch";
        return false;
    }
    if (release) {
        NCryptFreeObject(matching);
    }
    return true;
}

// A new persisted 2048-bit RSA key under a random name.
bool create_persisted_key(TlsIdentity& identity, std::string& error) {
    Digest random{};
    if (!random_bytes(random.data(), random.size())) {
        error = "random_failed";
        return false;
    }
    const auto suffix = hex(random);
    identity.key_name = key_name_prefix + std::wstring(suffix.begin(), suffix.end());
    auto status = NCryptOpenStorageProvider(&identity.provider, MS_KEY_STORAGE_PROVIDER, 0);
    if (status) {
        error = status_error("NCryptOpenStorageProvider", status);
        return false;
    }
    status = NCryptCreatePersistedKey(identity.provider, &identity.key, NCRYPT_RSA_ALGORITHM, identity.key_name.c_str(),
                                      0, 0);
    if (status) {
        error = status_error("NCryptCreatePersistedKey", status);
        return false;
    }
    DWORD length = 2048;
    status =
        NCryptSetProperty(identity.key, NCRYPT_LENGTH_PROPERTY, reinterpret_cast<PBYTE>(&length), sizeof(length), 0);
    if (status || NCryptFinalizeKey(identity.key, 0)) {
        error = "certificate_key_generation_failed";
        return false;
    }
    return true;
}

// A certificate for the identity's key, signed with that key.
bool create_self_signed_certificate(TlsIdentity& identity, std::string& error) {
    constexpr wchar_t subject_name[] = L"CN=SporeMP private M06 session";
    DWORD subject_size = 0;
    CertStrToNameW(X509_ASN_ENCODING, subject_name, CERT_X500_NAME_STR, nullptr, nullptr, &subject_size, nullptr);
    std::vector<BYTE> subject(subject_size);
    if (!subject_size || !CertStrToNameW(X509_ASN_ENCODING, subject_name, CERT_X500_NAME_STR, nullptr, subject.data(),
                                         &subject_size, nullptr)) {
        error = "certificate_subject_failed";
        return false;
    }
    CERT_NAME_BLOB name{subject_size, subject.data()};
    CRYPT_KEY_PROV_INFO info = key_provider_info(identity.key_name);
    CRYPT_ALGORITHM_IDENTIFIER algorithm{};
    algorithm.pszObjId = const_cast<LPSTR>(szOID_RSA_SHA256RSA);
    identity.certificate =
        CertCreateSelfSignCertificate(identity.key, &name, 0, &info, &algorithm, nullptr, nullptr, nullptr);
    if (!identity.certificate) {
        error = status_error("CertCreateSelfSignCertificate", GetLastError());
        return false;
    }
    return true;
}

// Waits up to five seconds for a non-blocking connect to finish. Zero when the socket connected; SOCKET_ERROR when
// the connect failed, did not finish in time or the caller stopped.
int await_connect(SOCKET candidate, const std::atomic<bool>& stop) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!stop && std::chrono::steady_clock::now() < deadline) {
        fd_set writable, failed;
        FD_ZERO(&writable);
        FD_ZERO(&failed);
        FD_SET(candidate, &writable);
        FD_SET(candidate, &failed);
        timeval timeout{0, 10000};
        if (select(0, nullptr, &writable, &failed, &timeout) > 0) {
            int socket_error = 0;
            int size = sizeof(socket_error);
            getsockopt(candidate, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&socket_error), &size);
            return socket_error ? SOCKET_ERROR : 0;
        }
    }
    return SOCKET_ERROR;
}

// A blocking socket connected to one resolved address, or INVALID_SOCKET. The connect itself runs non-blocking so
// that it can be bounded and stopped.
SOCKET connect_address(const addrinfo& address, const std::atomic<bool>& stop) {
    const SOCKET candidate = socket(address.ai_family, address.ai_socktype, address.ai_protocol);
    if (candidate == INVALID_SOCKET) {
        return INVALID_SOCKET;
    }
    u_long nonblocking = 1;
    ioctlsocket(candidate, FIONBIO, &nonblocking);
    int result = connect(candidate, address.ai_addr, static_cast<int>(address.ai_addrlen));
    if (result == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) {
        result = await_connect(candidate, stop);
    }
    if (result == 0) {
        nonblocking = 0;
        ioctlsocket(candidate, FIONBIO, &nonblocking);
        return candidate;
    }
    closesocket(candidate);
    return INVALID_SOCKET;
}
} // namespace

bool initialize_sockets(std::string& error) {
    static std::once_flag once;
    static int result = 0;
    std::call_once(once, [] {
        WSADATA data{};
        result = WSAStartup(MAKEWORD(2, 2), &data);
    });
    if (result) {
        error = status_error("WSAStartup", result);
        return false;
    }
    return true;
}

bool random_bytes(void* bytes, size_t count) {
    return count <= 0xffffffffULL && BCryptGenRandom(nullptr, static_cast<PUCHAR>(bytes), static_cast<ULONG>(count),
                                                     BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
}

TlsIdentity::~TlsIdentity() {
    if (acquired) {
        FreeCredentialsHandle(&credentials);
    }
    if (certificate) {
        CertFreeCertificateContext(certificate);
    }
    if (key) {
        if (retained) {
            NCryptFreeObject(key);
        } else {
            NCryptDeleteKey(key, 0);
        }
    }
    if (provider) {
        NCryptFreeObject(provider);
    }
}

std::vector<uint8_t> TlsIdentity::snapshot() const {
    if (!acquired || !certificate || key_name.empty()) {
        return {};
    }
    state::Writer out;
    out.word(1);
    out.word(static_cast<uint32_t>(key_name.size()));
    for (auto character : key_name) {
        out.word(static_cast<uint32_t>(character));
    }
    out.data(std::vector<uint8_t>(certificate->pbCertEncoded, certificate->pbCertEncoded + certificate->cbCertEncoded));
    return std::move(out.bytes);
}

bool TlsIdentity::restore(const std::vector<uint8_t>& bytes, std::string& error) {
    if (acquired || key || certificate) {
        error = "tls_restore_requires_empty_identity";
        return false;
    }
    std::wstring name;
    std::vector<uint8_t> encoded;
    if (!read_identity_snapshot(bytes, name, encoded, error)) {
        return false;
    }
    // Retained before the key is opened: the destructor deletes a key that is not.
    key_name = std::move(name);
    retained = true;
    auto status = NCryptOpenStorageProvider(&provider, MS_KEY_STORAGE_PROVIDER, 0);
    if (!status) {
        status = NCryptOpenKey(provider, &key, key_name.c_str(), 0, 0);
    }
    if (status) {
        error = status_error("tls_recovery_key_unavailable", status);
        return false;
    }
    if (!bind_recovered_certificate(*this, encoded, error)) {
        return false;
    }
    if (!certificate_sha256(certificate, pin)) {
        error = "tls_recovery_certificate_hash";
        return false;
    }
    status = acquire_server_credentials(*this);
    if (status) {
        error = status_error("tls_recovery_credentials", status);
        return false;
    }
    acquired = true;
    return true;
}

bool TlsIdentity::create(std::string& error) {
    if (!create_persisted_key(*this, error) || !create_self_signed_certificate(*this, error)) {
        return false;
    }
    if (!certificate_sha256(certificate, pin)) {
        error = "certificate_hash_failed";
        return false;
    }
    const auto status = acquire_server_credentials(*this);
    if (status) {
        error = status_error("AcquireCredentialsHandle(server)", status);
        return false;
    }
    acquired = true;
    return true;
}

TlsStream::~TlsStream() {
    if (context_valid_) {
        DeleteSecurityContext(&context_);
    }
    if (own_credentials_) {
        FreeCredentialsHandle(&credentials_);
    }
    if (socket_ != INVALID_SOCKET) {
        shutdown(socket_, SD_BOTH);
        closesocket(socket_);
    }
}

bool TlsStream::raw_send(const uint8_t* data, size_t size, std::string& error) {
    while (size) {
        int sent = ::send(socket_, reinterpret_cast<const char*>(data), static_cast<int>(size), 0);
        if (sent <= 0) {
            error = status_error("socket_send", WSAGetLastError());
            return false;
        }
        data += sent;
        size -= static_cast<size_t>(sent);
    }
    return true;
}

bool TlsStream::read_more(std::string& error) {
    uint8_t bytes[16384];
    int received = recv(socket_, reinterpret_cast<char*>(bytes), sizeof(bytes), 0);
    if (received <= 0) {
        error = received == 0 ? "peer_closed" : status_error("socket_receive", WSAGetLastError());
        return false;
    }
    if (encrypted_.size() + static_cast<size_t>(received) > 65536) {
        error = "tls_receive_capacity";
        return false;
    }
    encrypted_.insert(encrypted_.end(), bytes, bytes + received);
    return true;
}

// Keeps the last `unused` bytes of the received ciphertext, which the security package did not consume, and drops
// what comes before them.
void TlsStream::keep_unused(size_t unused) {
    if (unused && unused <= encrypted_.size()) {
        encrypted_.erase(encrypted_.begin(), encrypted_.end() - static_cast<ptrdiff_t>(unused));
    } else {
        encrypted_.clear();
    }
}

// One step of the handshake over the ciphertext received so far. `token` receives what has to be sent to the peer,
// in memory the security package allocates and the caller frees; `unused` receives how many trailing bytes of the
// ciphertext the step did not consume.
SECURITY_STATUS TlsStream::handshake_step(bool server, CredHandle* credential, bool first, std::wstring& target,
                                          SecBuffer& token, size_t& unused) {
    SecBuffer in[2]{{static_cast<unsigned long>(encrypted_.size()), SECBUFFER_TOKEN, encrypted_.data()},
                    {0, SECBUFFER_EMPTY, nullptr}};
    SecBufferDesc input{SECBUFFER_VERSION, 2, in};
    SecBufferDesc output{SECBUFFER_VERSION, 1, &token};
    ULONG attributes = 0;
    TimeStamp expires{};
    const ULONG flags =
        server ? (ASC_REQ_SEQUENCE_DETECT | ASC_REQ_REPLAY_DETECT | ASC_REQ_CONFIDENTIALITY | ASC_REQ_EXTENDED_ERROR |
                  ASC_REQ_ALLOCATE_MEMORY | ASC_REQ_STREAM)
               : (ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY | ISC_REQ_EXTENDED_ERROR |
                  ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM | ISC_REQ_MANUAL_CRED_VALIDATION);
    const SECURITY_STATUS status =
        server ? AcceptSecurityContext(credential, first ? nullptr : &context_, &input, flags, SECURITY_NATIVE_DREP,
                                       &context_, &output, &attributes, &expires)
               : InitializeSecurityContextW(credential, first ? nullptr : &context_, target.data(), flags, 0,
                                            SECURITY_NATIVE_DREP, first ? nullptr : &input, 0, &context_, &output,
                                            &attributes, &expires);
    unused = in[1].BufferType == SECBUFFER_EXTRA ? in[1].cbBuffer : 0;
    return status;
}

// Sends the token a handshake step produced, if it produced one, and frees it whether or not it was sent.
bool TlsStream::send_token(const SecBuffer& token, std::string& error) {
    if (!token.pvBuffer) {
        return true;
    }
    bool sent = raw_send(static_cast<uint8_t*>(token.pvBuffer), token.cbBuffer, error);
    FreeContextBuffer(token.pvBuffer);
    return sent;
}

// The client accepts the server by its certificate alone: its SHA-256 is the pinned one and it is within its validity
// period. The comparison reads every byte of the two digests whatever they hold.
bool TlsStream::verify_server_certificate(const Digest& pin, std::string& error) {
    PCCERT_CONTEXT remote = nullptr;
    const SECURITY_STATUS status =
        QueryContextAttributesW(&context_, SECPKG_ATTR_REMOTE_CERT_CONTEXT, static_cast<void*>(&remote));
    if (status || !remote) {
        error = "missing_server_certificate";
        return false;
    }
    Digest actual{};
    const bool hashed = certificate_sha256(remote, actual);
    const bool time_valid = CertVerifyTimeValidity(nullptr, remote->pCertInfo) == 0;
    CertFreeCertificateContext(remote);
    uint32_t difference = 0;
    for (size_t i = 0; i < pin.size(); ++i) {
        difference |= pin[i] ^ actual[i];
    }
    if (!hashed || !time_valid || difference) {
        error = "certificate_pin_or_validity_mismatch";
        return false;
    }
    return true;
}

// Reads the record sizes `send` needs and refuses a negotiated cipher weaker than 128 bits.
bool TlsStream::accept_connection(std::string& error) {
    SECURITY_STATUS status = QueryContextAttributesW(&context_, SECPKG_ATTR_STREAM_SIZES, &sizes_);
    if (status) {
        error = status_error("tls_stream_sizes", status);
        return false;
    }
    SecPkgContext_ConnectionInfo connection{};
    status = QueryContextAttributesW(&context_, SECPKG_ATTR_CONNECTION_INFO, &connection);
    if (status || connection.dwCipherStrength < 128) {
        error = "tls_cipher_not_accepted";
        return false;
    }
    return true;
}

bool TlsStream::handshake(bool server, TlsIdentity* identity, const Digest& pin, const std::string& host,
                          const std::atomic<bool>& stop, std::string& error) {
    socket_options(socket_);
    CredHandle* credential = nullptr;
    if (server) {
        credential = &identity->credentials;
    } else {
        const SECURITY_STATUS status = acquire_client_credentials(credentials_);
        if (status) {
            error = status_error("AcquireCredentialsHandle(client)", status);
            return false;
        }
        own_credentials_ = true;
        credential = &credentials_;
    }
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    bool first = true, need_read = server;
    std::wstring target(host.begin(), host.end());
    while (!stop && std::chrono::steady_clock::now() < deadline) {
        if (need_read) {
            if (!readable(socket_, 10000)) {
                continue;
            }
            if (!read_more(error)) {
                return false;
            }
        }
        SecBuffer token{0, SECBUFFER_TOKEN, nullptr};
        size_t unused = 0;
        const SECURITY_STATUS status = handshake_step(server, credential, first, target, token, unused);
        if (status == SEC_E_OK || status == SEC_I_CONTINUE_NEEDED) {
            context_valid_ = true;
            first = false;
        }
        if (!send_token(token, error)) {
            return false;
        }
        if (status == SEC_E_INCOMPLETE_MESSAGE) {
            need_read = true;
            continue;
        }
        if (status != SEC_E_OK && status != SEC_I_CONTINUE_NEEDED) {
            error = status_error("tls_handshake", status);
            return false;
        }
        keep_unused(unused);
        if (status == SEC_E_OK) {
            if (!server && !verify_server_certificate(pin, error)) {
                return false;
            }
            return accept_connection(error);
        }
        need_read = encrypted_.empty();
    }
    error = stop ? "stopped" : "tls_handshake_timeout";
    return false;
}

bool TlsStream::send(const uint8_t* data, size_t size, std::string& error) {
    while (size) {
        size_t take = (std::min)(size, static_cast<size_t>(sizes_.cbMaximumMessage));
        std::vector<uint8_t> block(sizes_.cbHeader + take + sizes_.cbTrailer);
        std::memcpy(block.data() + sizes_.cbHeader, data, take);
        SecBuffer buffers[4]{{sizes_.cbHeader, SECBUFFER_STREAM_HEADER, block.data()},
                             {static_cast<ULONG>(take), SECBUFFER_DATA, block.data() + sizes_.cbHeader},
                             {sizes_.cbTrailer, SECBUFFER_STREAM_TRAILER, block.data() + sizes_.cbHeader + take},
                             {0, SECBUFFER_EMPTY, nullptr}};
        SecBufferDesc message{SECBUFFER_VERSION, 4, buffers};
        auto status = EncryptMessage(&context_, 0, &message, 0);
        if (status) {
            error = status_error("EncryptMessage", status);
            return false;
        }
        if (!raw_send(block.data(), size_t(buffers[0].cbBuffer) + buffers[1].cbBuffer + buffers[2].cbBuffer, error)) {
            return false;
        }
        data += take;
        size -= take;
    }
    return true;
}

bool TlsStream::receive(std::vector<uint8_t>& plaintext, std::string& error) {
    if (encrypted_.empty()) {
        if (!readable(socket_, 1000)) {
            return true;
        }
        if (!read_more(error)) {
            return false;
        }
    }
    for (;;) {
        SecBuffer buffers[4]{{static_cast<ULONG>(encrypted_.size()), SECBUFFER_DATA, encrypted_.data()},
                             {0, SECBUFFER_EMPTY, nullptr},
                             {0, SECBUFFER_EMPTY, nullptr},
                             {0, SECBUFFER_EMPTY, nullptr}};
        SecBufferDesc message{SECBUFFER_VERSION, 4, buffers};
        auto status = DecryptMessage(&context_, &message, 0, nullptr);
        if (status == SEC_E_INCOMPLETE_MESSAGE) {
            if (readable(socket_, 0)) {
                return read_more(error);
            }
            return true;
        }
        if (status != SEC_E_OK) {
            error = status_error("DecryptMessage", status);
            return false;
        }
        size_t unused = 0;
        for (const auto& part : buffers) {
            if (part.BufferType == SECBUFFER_DATA) {
                if (plaintext.size() + part.cbBuffer > 65536) {
                    error = "plaintext_capacity";
                    return false;
                }
                auto decrypted = static_cast<uint8_t*>(part.pvBuffer);
                plaintext.insert(plaintext.end(), decrypted, decrypted + part.cbBuffer);
            } else if (part.BufferType == SECBUFFER_EXTRA) {
                unused = part.cbBuffer;
            }
        }
        keep_unused(unused);
        if (encrypted_.empty()) {
            return true;
        }
    }
}

SOCKET connect_socket(const std::string& host, uint16_t port, const std::atomic<bool>& stop, std::string& error) {
    if (!initialize_sockets(error)) {
        return INVALID_SOCKET;
    }
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfo* addresses = nullptr;
    auto service = std::to_string(port);
    const int lookup = getaddrinfo(host.c_str(), service.c_str(), &hints, &addresses);
    if (lookup) {
        error = status_error("getaddrinfo", lookup);
        return INVALID_SOCKET;
    }
    SOCKET connected = INVALID_SOCKET;
    for (auto address = addresses; address && !stop; address = address->ai_next) {
        connected = connect_address(*address, stop);
        if (connected != INVALID_SOCKET) {
            break;
        }
    }
    freeaddrinfo(addresses);
    if (connected == INVALID_SOCKET) {
        error = "tcp_connect_failed_or_timeout";
    }
    return connected;
}

SOCKET listen_socket(const std::string& host, uint16_t port, uint16_t& actual, std::string& error) {
    if (!initialize_sockets(error)) {
        return INVALID_SOCKET;
    }
    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) {
        error = "socket_create_failed";
        return listener;
    }
    BOOL yes = TRUE;
    setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&yes), sizeof(yes));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1 ||
        bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR ||
        listen(listener, 8) == SOCKET_ERROR) {
        error = status_error("listen_or_bind", WSAGetLastError());
        closesocket(listener);
        return INVALID_SOCKET;
    }
    int length = sizeof(address);
    getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length);
    actual = ntohs(address.sin_port);
    u_long nonblocking = 1;
    ioctlsocket(listener, FIONBIO, &nonblocking);
    return listener;
}
} // namespace sporemp::network::detail
