#include "diagnostics.h"
#include "build_identity.h"
#include <bcrypt.h>
#include <cstdio>
#include <cstring>
#include <cwchar>

namespace sporemp {
bool sha256_file(const wchar_t* path, Sha256& digest) noexcept {
    digest.fill(0);
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
    if (ok) ok = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    unsigned char buffer[65536];
    DWORD count = 0;
    while (ok) {
        if (!ReadFile(file, buffer, sizeof(buffer), &count, nullptr)) { ok = false; break; }
        if (count == 0) break;
        ok = BCryptHashData(hash, buffer, count, 0) >= 0;
    }
    unsigned char value[32]{};
    if (ok) ok = BCryptFinishHash(hash, value, sizeof(value), 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    if (ok) {
        constexpr char hex[] = "0123456789abcdef";
        for (size_t i = 0; i < sizeof(value); ++i) {
            digest[2 * i] = hex[value[i] >> 4];
            digest[2 * i + 1] = hex[value[i] & 15];
        }
    }
    return ok;
}

bool matches_candidate(const Sha256& digest) noexcept {
    return std::strcmp(digest.data(), candidate_exe_sha256) == 0;
}

bool absolute_local_path(const wchar_t* path) noexcept {
    if (!path || std::wcslen(path) < 3) return false;
    const bool drive = (path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z');
    return drive && path[1] == L':' && (path[2] == L'\\' || path[2] == L'/');
}

bool DiagnosticSession::initialize(const wchar_t* executable, const wchar_t* directory,
                                   RuntimeIdentity runtime) noexcept {
    if (log_ != INVALID_HANDLE_VALUE || !absolute_local_path(directory)) return false;
    if (!sha256_file(executable, digest_) || !matches_candidate(digest_)) return false;
    // Runtime values come from the real SDK API in the callback. They are not a compatibility proof.
    if (runtime.major != 2 || runtime.minor != 5 || runtime.build != 0 || runtime.game_type != 1) return false;
    const DWORD attributes = GetFileAttributesW(directory);
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
    wchar_t log_path[32768]{};
    if (swprintf_s(log_path, L"%ls\\bridge-%lu.jsonl", directory, GetCurrentProcessId()) < 0) return false;
    // CREATE_NEW preserves evidence from earlier runs. The preflight must supply an isolated directory.
    log_ = CreateFileW(log_path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (log_ == INVALID_HANDLE_VALUE) return false;
    engine_thread_ = GetCurrentThreadId();
    runtime_ = runtime;
    if (!event("initialize")) { CloseHandle(log_); log_ = INVALID_HANDLE_VALUE; return false; }
    return true;
}

bool DiagnosticSession::event(const char* name) noexcept {
    char line[1024]{};
    SYSTEMTIME now{};
    GetSystemTime(&now);
    const int length = sprintf_s(line,
        "{\"schema_version\":1,\"source\":\"sporemp.bridge\",\"mode\":\"diagnostic\","
        "\"event\":\"%s\",\"utc\":\"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ\","
        "\"pid\":%lu,\"thread_id\":%lu,\"initialization_thread_id\":%lu,"
        "\"executable_sha256\":\"%s\",\"sdk_commit\":\"%s\",\"bridge_version\":\"%s\","
        "\"sdk_runtime\":\"%d.%d.%d\",\"game_type\":%d,\"gameplay_hooks\":0}\n",
        name, now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds,
        GetCurrentProcessId(), GetCurrentThreadId(), engine_thread_, digest_.data(), sdk_revision,
        bridge_version, runtime_.major, runtime_.minor, runtime_.build, runtime_.game_type);
    DWORD written = 0;
    return length > 0 && WriteFile(log_, line, static_cast<DWORD>(length), &written, nullptr) &&
        written == static_cast<DWORD>(length) && FlushFileBuffers(log_);
}

void DiagnosticSession::dispose() noexcept {
    if (log_ == INVALID_HANDLE_VALUE) return;
    if (!event("dispose")) OutputDebugStringW(L"SporeMP: failed to persist disposal diagnostic.\n");
    CloseHandle(log_);
    log_ = INVALID_HANDLE_VALUE;
}

bool DiagnosticSession::native_path(unsigned int id, const wchar_t* value) noexcept {
    if (log_ == INVALID_HANDLE_VALUE) return false;
    char utf8[32768]{};
    if (value && !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, utf8, sizeof(utf8), nullptr, nullptr)) return false;
    char escaped[65536]{};
    size_t out = 0;
    for (size_t i = 0; utf8[i] && out + 2 < sizeof(escaped); ++i) {
        if (utf8[i] == '\\' || utf8[i] == '"') escaped[out++] = '\\';
        if (static_cast<unsigned char>(utf8[i]) < 32) return false;
        escaped[out++] = utf8[i];
    }
    char line[66000]{};
    int length = sprintf_s(line,
        "{\"schema_version\":1,\"source\":\"sporemp.bridge\",\"event\":\"native_path\",\"pid\":%lu,\"thread_id\":%lu,\"path_id\":%u,\"path\":\"%s\",\"was_null\":%s}\n",
        GetCurrentProcessId(), GetCurrentThreadId(), id, escaped, value ? "false" : "true");
    DWORD written = 0;
    return length > 0 && WriteFile(log_, line, static_cast<DWORD>(length), &written, nullptr) &&
        written == static_cast<DWORD>(length) && FlushFileBuffers(log_);
}
}
