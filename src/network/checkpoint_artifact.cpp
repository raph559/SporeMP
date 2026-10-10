#include "checkpoint_artifact.h"
#include "content_registry.h"
#include "state_codec.h"
#include <windows.h>
#include <set>
#include <cctype>

namespace sporemp::network {
namespace {
bool fail(std::string& error, const char* message) {
    error = message;
    return false;
}

std::string lowercase(std::string text) {
    for (auto& character : text) {
        character = char(std::tolower(static_cast<unsigned char>(character)));
    }
    return text;
}

// One component of a member path: printable ASCII that Windows takes as a file name, and not the name of a device.
bool portable_component(const std::string& part) {
    if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ') {
        return false;
    }
    for (unsigned char character : part) {
        if (character < 32 || character > 126 || std::string("\\:*?\"<>|").find(char(character)) != std::string::npos) {
            return false;
        }
    }
    auto stem = part.substr(0, part.find('.'));
    for (auto& character : stem) {
        character = char(std::toupper(static_cast<unsigned char>(character)));
    }
    if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" ||
        (stem.size() == 4 && (stem.substr(0, 3) == "COM" || stem.substr(0, 3) == "LPT") && stem[3] >= '0' &&
         stem[3] <= '9')) {
        return false;
    }
    return true;
}

bool path_name(const std::string& name) {
    if (name.size() > 240 || (name.rfind("spore/", 0) != 0 && name.rfind("creations/", 0) != 0)) {
        return false;
    }
    size_t begin = 0;
    while (begin < name.size()) {
        const auto end = name.find('/', begin);
        if (!portable_component(name.substr(begin, end == std::string::npos ? end : end - begin))) {
            return false;
        }
        if (end == std::string::npos) {
            return true;
        }
        begin = end + 1;
    }
    return false;
}

bool safe_parent(const std::filesystem::path& path) {
    if (!path.is_absolute() || path.lexically_normal() != path ||
        GetDriveTypeW(path.root_path().c_str()) != DRIVE_FIXED) {
        return false;
    }
    for (auto ancestor = path.parent_path(); !ancestor.empty(); ancestor = ancestor.parent_path()) {
        const auto attributes = GetFileAttributesW(ancestor.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            return false;
        }
        if (ancestor == ancestor.parent_path()) {
            break;
        }
    }
    return true;
}

// The fixed part of an artifact, after its magic and schema words. Returns the number of members that follow it.
uint32_t read_metadata(state::Reader& in, CheckpointArtifact& value) {
    for (auto* digest : {&value.identity.build, &value.identity.executable, &value.identity.content}) {
        *digest = in.digest();
    }
    for (auto& digest : value.identity.world) {
        digest = in.digest();
    }
    value.identity.fixture = value.identity.world[0];
    value.provenance = in.digest();
    value.save_request = in.wide();
    value.rollback_ms = in.word();
    for (auto& id : value.native_ids) {
        id = in.word();
    }
    return in.word();
}

// What each schema requires of the metadata; `CheckpointArtifact::native_ids` says what the four numbers are.
bool check_metadata(const state::Reader& in, const CheckpointArtifact& value, uint32_t count, std::string& error) {
    if (!in.valid || value.identity.build == Digest{} || value.identity.executable == Digest{} ||
        value.identity.content == Digest{} || !valid_world_identity(value.identity.world) ||
        value.provenance == Digest{} || count < world_file_count + (value.schema == 2) || count > 512) {
        return fail(error, "checkpoint_metadata_invalid");
    }
    if (value.schema == 1) {
        if (!value.save_request || value.rollback_ms != 300000 || value.native_ids[0] == value.native_ids[1] ||
            value.native_ids[2] == value.native_ids[3]) {
            return fail(error, "checkpoint_metadata_invalid");
        }
        for (auto id : value.native_ids) {
            if (!id || id == UINT32_MAX) {
                return fail(error, "checkpoint_native_identity_invalid");
            }
        }
    } else if (value.save_request || value.rollback_ms || !value.native_ids[0] || !value.native_ids[1] ||
               value.native_ids[0] == value.native_ids[1] || value.native_ids[0] == UINT32_MAX ||
               value.native_ids[1] == UINT32_MAX || value.native_ids[2] || value.native_ids[3]) {
        return fail(error, "checkpoint_primary_identity_invalid");
    }
    return true;
}

// One member: its path, the digest it is claimed to have, and its bytes. `names` holds the paths seen so far without
// their case, `world` which of the world files have been found; a world file has to hash to the artifact's identity.
bool read_member(state::Reader& in, CheckpointArtifact& value, std::set<std::string>& names,
                 std::array<bool, world_file_count>& world, std::string& error) {
    auto raw = in.data(240);
    std::string name(raw.begin(), raw.end());
    if (!in.valid || !path_name(name)) {
        return fail(error, "checkpoint_member_path");
    }
    if (!names.insert(lowercase(name)).second) {
        return fail(error, "checkpoint_duplicate_member");
    }
    const auto expected = in.digest();
    auto data = in.data(CheckpointArtifact::max_bytes);
    Digest actual{};
    if (!in.valid || !content_digest(data.data(), data.size(), actual) || actual != expected) {
        return fail(error, "checkpoint_member_hash");
    }
    for (size_t i = 0; i < world_file_count; ++i) {
        if (name == std::string("spore/") + world_files[i].path) {
            if (actual != value.identity.world[i]) {
                return fail(error, "checkpoint_world_hash");
            }
            world[i] = true;
        }
    }
    if (value.schema == 2 && name == "spore/Games/Game0/Haagos.spo") {
        value.secondary_save = actual;
    }
    return true;
}
} // namespace

bool validate_checkpoint_artifact(const std::vector<uint8_t>& bytes, CheckpointArtifact& output, std::string& error) {
    if (bytes.size() > CheckpointArtifact::max_bytes) {
        return fail(error, "checkpoint_size_limit");
    }
    CheckpointArtifact value;
    state::Reader in{bytes};
    if (in.word() != 0x39424d53) {
        return fail(error, "checkpoint_schema_migration_required");
    }
    value.schema = in.word();
    if (value.schema != 1 && value.schema != 2) {
        return fail(error, "checkpoint_schema_migration_required");
    }
    const auto count = read_metadata(in, value);
    if (!check_metadata(in, value, count, error)) {
        return false;
    }
    std::set<std::string> names;
    std::array<bool, world_file_count> world{};
    for (uint32_t n = 0; n < count; ++n) {
        if (!read_member(in, value, names, world, error)) {
            return false;
        }
    }
    if (!in.done()) {
        return fail(error, "checkpoint_truncated_or_trailing");
    }
    for (auto found : world) {
        if (!found) {
            return fail(error, "checkpoint_world_missing");
        }
    }
    if (value.schema == 2 && value.secondary_save == Digest{}) {
        return fail(error, "checkpoint_secondary_save_missing");
    }
    if (!content_digest(bytes.data(), bytes.size(), value.digest)) {
        return fail(error, "checkpoint_hash_failed");
    }
    output = value;
    error.clear();
    return true;
}

bool read_checkpoint_artifact(const std::filesystem::path& path, std::vector<uint8_t>& bytes, std::string& error) {
    if (!safe_parent(path)) {
        error = "checkpoint_unsafe_path";
        return false;
    }
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    BY_HANDLE_FILE_INFORMATION info{};
    LARGE_INTEGER size{};
    DWORD received = 0;
    bool ok = file != INVALID_HANDLE_VALUE && GetFileInformationByHandle(file, &info) && info.nNumberOfLinks == 1 &&
              !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
              GetFileSizeEx(file, &size) && size.QuadPart >= 0 &&
              uint64_t(size.QuadPart) <= CheckpointArtifact::max_bytes;
    if (ok) {
        bytes.resize(static_cast<size_t>(size.QuadPart));
        ok = ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &received, nullptr) &&
             received == bytes.size();
    }
    if (file != INVALID_HANDLE_VALUE) {
        CloseHandle(file);
    }
    if (!ok) {
        error = "checkpoint_file_missing_unsafe_or_changed";
    }
    return ok;
}

bool write_checkpoint_artifact(const std::filesystem::path& path, const std::vector<uint8_t>& bytes,
                               std::string& error) {
    if (bytes.empty() || bytes.size() > CheckpointArtifact::max_bytes || !safe_parent(path)) {
        error = "checkpoint_export_invalid";
        return false;
    }
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_FLAG_WRITE_THROUGH | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    DWORD written = 0;
    bool ok = file != INVALID_HANDLE_VALUE &&
              WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
              written == bytes.size() && FlushFileBuffers(file);
    if (file != INVALID_HANDLE_VALUE) {
        CloseHandle(file);
    }
    if (!ok) {
        error = "checkpoint_export_write_failed";
    }
    return ok;
}
} // namespace sporemp::network
