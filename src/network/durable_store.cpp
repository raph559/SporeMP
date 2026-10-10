#include "durable_store.h"
#include "content_registry.h"
#include "protocol.h"
#include "state_codec.h"
#include <windows.h>
#include <bcrypt.h>

namespace sporemp::network {
namespace {
constexpr uint32_t record_magic = 0x39524d53, head_magic = 0x39484d53, schema = 1;

struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;

    ~Handle() {
        if (value != INVALID_HANDLE_VALUE) {
            CloseHandle(value);
        }
    }
};

bool regular(HANDLE handle) {
    BY_HANDLE_FILE_INFORMATION info{};
    return GetFileInformationByHandle(handle, &info) &&
           !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
           info.nNumberOfLinks == 1;
}

bool safe_directory(const std::filesystem::path& path, std::string& error) {
    if (!path.is_absolute() || path.lexically_normal() != path) {
        error = "store_path_must_be_absolute_normal";
        return false;
    }
    wchar_t filesystem[64]{};
    if (GetDriveTypeW(path.root_path().c_str()) != DRIVE_FIXED ||
        !GetVolumeInformationW(path.root_path().c_str(), nullptr, 0, nullptr, nullptr, nullptr, filesystem, 64) ||
        wcscmp(filesystem, L"NTFS") != 0) {
        error = "store_requires_local_ntfs";
        return false;
    }
    for (auto ancestor = path; !ancestor.empty(); ancestor = ancestor.parent_path()) {
        auto attributes = GetFileAttributesW(ancestor.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            error = "store_directory_missing_or_reparse";
            return false;
        }
        if (ancestor == ancestor.parent_path()) {
            break;
        }
    }
    return true;
}

bool read(const std::filesystem::path& path, size_t limit, std::vector<uint8_t>& bytes, std::string& error) {
    Handle file{CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    LARGE_INTEGER size{};
    if (file.value == INVALID_HANDLE_VALUE || !regular(file.value) || !GetFileSizeEx(file.value, &size) ||
        size.QuadPart < 0 || uint64_t(size.QuadPart) > limit) {
        error = "store_file_missing_unsafe_or_oversized";
        return false;
    }
    bytes.resize(static_cast<size_t>(size.QuadPart));
    DWORD got = 0;
    if (!ReadFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &got, nullptr) || got != bytes.size()) {
        error = "store_read_failed";
        return false;
    }
    return true;
}

bool write_new(const std::filesystem::path& path, const std::vector<uint8_t>& bytes, std::string& error) {
    Handle file{CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                            FILE_FLAG_WRITE_THROUGH | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    DWORD written = 0;
    if (file.value == INVALID_HANDLE_VALUE || !regular(file.value) ||
        !WriteFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) ||
        written != bytes.size() || !FlushFileBuffers(file.value)) {
        error = "store_write_or_flush_failed";
        return false;
    }
    return true;
}

bool rename(const std::filesystem::path& from, const std::filesystem::path& to, bool replace, std::string& error) {
    if (!MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_WRITE_THROUGH | (replace ? MOVEFILE_REPLACE_EXISTING : 0))) {
        error = "store_atomic_publish_failed:" + std::to_string(GetLastError());
        return false;
    }
    return true;
}

// A sealed record is a file named by the digest of its bytes.
std::filesystem::path record_path(const std::filesystem::path& directory, const Digest& identity) {
    return directory / (L"state-" + std::filesystem::path(hex(identity)).wstring() + L".bin");
}

std::vector<uint8_t> encode_record(uint64_t revision, const std::vector<uint8_t>& payload) {
    state::Writer out;
    out.word(record_magic);
    out.word(schema);
    out.wide(revision);
    out.data(payload);
    return std::move(out.bytes);
}

std::vector<uint8_t> encode_head(uint64_t revision, const Digest& identity) {
    state::Writer out;
    out.word(head_magic);
    out.word(schema);
    out.wide(revision);
    out.digest(identity);
    return std::move(out.bytes);
}

// A missing head with sealed records is ambiguous: never reinterpret a
// damaged acknowledged universe as a fresh empty one.
bool no_sealed_records(const std::filesystem::path& directory, std::string& error) {
    std::error_code scan_error;
    std::filesystem::directory_iterator at(directory, scan_error), end;
    while (!scan_error && at != end) {
        if (at->path().filename().wstring().rfind(L"state-", 0) == 0) {
            error = "store_missing_head_with_records";
            return false;
        }
        at.increment(scan_error);
    }
    if (scan_error) {
        error = "store_directory_scan_failed";
        return false;
    }
    return true;
}

// The head names the current record: its revision and the digest it is sealed under.
bool read_head(const std::filesystem::path& path, uint64_t& revision, Digest& identity, std::string& error) {
    std::vector<uint8_t> marker;
    if (!read(path, 48, marker, error)) {
        return false;
    }
    state::Reader in{marker};
    if (in.word() != head_magic || in.word() != schema) {
        error = "store_head_schema";
        return false;
    }
    revision = in.wide();
    identity = in.digest();
    if (!in.done() || !revision || identity == Digest{}) {
        error = "store_head_invalid";
        return false;
    }
    return true;
}

// The payload of the record the head names, once its bytes hash to that digest and it carries that revision.
bool read_record(const std::filesystem::path& path, uint64_t revision, const Digest& identity,
                 std::vector<uint8_t>& payload, std::string& error) {
    std::vector<uint8_t> record;
    if (!read(path, DurableStore::max_bytes + 24, record, error)) {
        return false;
    }
    Digest observed{};
    if (!content_digest(record.data(), record.size(), observed) || observed != identity) {
        error = "store_record_hash";
        return false;
    }
    state::Reader in{record};
    if (in.word() != record_magic || in.word() != schema || in.wide() != revision) {
        error = "store_record_schema_or_revision";
        return false;
    }
    payload = in.data(DurableStore::max_bytes);
    if (!in.done()) {
        error = "store_record_length";
        return false;
    }
    return true;
}

// Moves the flushed pending record to the name its digest gives it.
bool seal_record(const std::filesystem::path& pending, const std::filesystem::path& target,
                 const std::vector<uint8_t>& record, std::string& error) {
    if (GetFileAttributesW(target.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return rename(pending, target, false, error);
    }
    // A prior unacknowledged attempt may have sealed the identical record.
    std::vector<uint8_t> previous;
    if (!read(target, DurableStore::max_bytes + 24, previous, error) || previous != record) {
        error = "store_existing_record_mismatch";
        return false;
    }
    if (!DeleteFileW(pending.c_str())) {
        error = "store_pending_cleanup";
        return false;
    }
    return true;
}

// Tells the test's crash injection that a commit has reached the named step.
void reach(const DurableStore::Boundary& boundary, const char* step) {
    if (boundary) {
        boundary(step);
    }
}
} // namespace

struct DurableStore::Impl {
    std::filesystem::path directory;
    Handle lock;
    uint64_t revision = 0;
    Digest identity{};
    bool open = false, failed = false;

    bool publish(const std::vector<uint8_t>& record, const Digest& record_identity, const Digest& nonce,
                 const Boundary& boundary, std::string& error) const;
};

// Seals the record of the next revision, then replaces the head with one that names it. Each file is written under
// a pending name, flushed, and only then moved into place. After a failure the directory may hold part of the
// commit, so the caller refuses further commits.
bool DurableStore::Impl::publish(const std::vector<uint8_t>& record, const Digest& record_identity, const Digest& nonce,
                                 const Boundary& boundary, std::string& error) const {
    const auto pending = directory / (L"pending-" + std::filesystem::path(hex(nonce)).wstring());
    const auto target = record_path(directory, record_identity);
    reach(boundary, "before_record_write");
    if (!write_new(pending, record, error)) {
        return false;
    }
    reach(boundary, "after_record_flush");
    if (!seal_record(pending, target, record, error)) {
        return false;
    }
    reach(boundary, "after_record_publish");
    if (!write_new(pending, encode_head(revision + 1, record_identity), error)) {
        return false;
    }
    reach(boundary, "after_head_flush");
    return rename(pending, directory / L"CURRENT", true, error);
}

DurableStore::DurableStore() : impl_(std::make_unique<Impl>()) {}

DurableStore::~DurableStore() = default;

uint64_t DurableStore::revision() const noexcept {
    return impl_->revision;
}

Digest DurableStore::identity() const noexcept {
    return impl_->identity;
}

bool DurableStore::open(const std::filesystem::path& directory, std::vector<uint8_t>& payload, std::string& error) {
    auto& impl = *impl_;
    payload.clear();
    if (impl.open || impl.lock.value != INVALID_HANDLE_VALUE) {
        error = "store_already_open";
        return false;
    }
    if (!safe_directory(directory, error)) {
        return false;
    }
    impl.directory = directory;
    impl.lock.value = CreateFileW((directory / L"writer.lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                  OPEN_ALWAYS, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (impl.lock.value == INVALID_HANDLE_VALUE || !regular(impl.lock.value)) {
        error = "store_locked_or_unsafe";
        return false;
    }
    const auto head = directory / L"CURRENT";
    if (GetFileAttributesW(head.c_str()) == INVALID_FILE_ATTRIBUTES) {
        if (GetLastError() != ERROR_FILE_NOT_FOUND) {
            error = "store_head_unavailable";
            return false;
        }
        if (!no_sealed_records(directory, error)) {
            return false;
        }
        impl.open = true;
        return true;
    }
    uint64_t revision = 0;
    Digest identity{};
    std::vector<uint8_t> candidate;
    if (!read_head(head, revision, identity, error) ||
        !read_record(record_path(directory, identity), revision, identity, candidate, error)) {
        return false;
    }
    impl.revision = revision;
    impl.identity = identity;
    impl.open = true;
    payload = std::move(candidate);
    return true;
}

bool DurableStore::commit(const std::vector<uint8_t>& payload, std::string& error, const Boundary& boundary) {
    auto& impl = *impl_;
    if (!impl.open || impl.failed || payload.size() > max_bytes || impl.revision == UINT64_MAX) {
        error = "store_unavailable_or_capacity";
        return false;
    }
    const std::vector<uint8_t> record = encode_record(impl.revision + 1, payload);
    Digest identity{}, nonce{};
    if (!content_digest(record.data(), record.size(), identity) ||
        BCryptGenRandom(nullptr, nonce.data(), static_cast<ULONG>(nonce.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
        error = "store_hash_or_nonce_failed";
        return false;
    }
    if (!impl.publish(record, identity, nonce, boundary, error)) {
        impl.failed = true;
        return false;
    }
    impl.identity = identity;
    ++impl.revision;
    reach(boundary, "after_head_publish");
    return true;
}
} // namespace sporemp::network
