#include "world_identity.h"
#include <windows.h>
#include <bcrypt.h>
#include <filesystem>

namespace sporemp::network {
namespace {
constexpr uint64_t max_file_bytes = 128ull * 1024 * 1024;
constexpr uint64_t max_bundle_bytes = 512ull * 1024 * 1024;
struct Files {
    std::array<HANDLE, world_file_count> handles{};
    ~Files() { for (auto h : handles) if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h); }
};
bool local_regular_path(const std::filesystem::path& path) {
    if (!path.is_absolute() || !path.has_root_name() || path.native().rfind(L"\\\\", 0) == 0) return false;
    std::filesystem::path at;
    for (const auto& part : path) {
        if (part == L"." || part == L".." || (part != path.root_name() && part.native().find(L':') != std::wstring::npos)) return false;
        at /= part;
        const auto attrs = GetFileAttributesW(at.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
    }
    return !(GetFileAttributesW(path.c_str()) & FILE_ATTRIBUTE_DIRECTORY);
}
bool hash_file(HANDLE file, Digest& result) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
    if (ok) ok = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    std::array<unsigned char, 65536> bytes{};
    DWORD count = 0;
    while (ok) {
        if (!ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr)) { ok = false; break; }
        if (!count) break;
        ok = BCryptHashData(hash, bytes.data(), count, 0) >= 0;
    }
    if (ok) ok = BCryptFinishHash(hash, result.data(), static_cast<ULONG>(result.size()), 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    return ok;
}
}
bool valid_world_identity(const WorldIdentity& world) noexcept {
    for (const auto& digest : world) if (digest == Digest{}) return false;
    return true;
}
bool read_world_identity(const std::wstring& root, WorldIdentity& result, std::string& error) {
    Files files;
    WorldIdentity actual{};
    uint64_t total = 0;
    for (size_t i = 0; i < world_file_count; ++i) {
        const auto path = std::filesystem::path(root) / world_files[i].path;
        const auto fail = [&](const char* reason) { error = std::string(reason) + ":" + world_files[i].path; return false; };
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return fail("canonical_world_missing_or_unreadable");
        if (!local_regular_path(path)) return fail("canonical_world_unsafe_path");
        files.handles[i] = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (files.handles[i] == INVALID_HANDLE_VALUE) return fail("canonical_world_not_closed_or_readable");
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(files.handles[i], &info) || info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) return fail("canonical_world_unsafe_file");
        const uint64_t size = (uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
        if (size > max_file_bytes || size > max_bundle_bytes - total) return fail("canonical_world_size_limit");
        total += size;
    }
    for (size_t i = 0; i < world_file_count; ++i) {
        if (!hash_file(files.handles[i], actual[i])) { error = std::string("canonical_world_read_failed:") + world_files[i].path; return false; }
    }
    result = actual;
    error.clear();
    return true;
}
}
