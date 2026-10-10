// Windows-only host: no SDK import and no game code executes before the guard.
// Injector ABI/order: pinned ModAPI-Launcher-Kit Injector.cs and DLLInjector/dllmain.cpp.
#include "diagnostics.h"
#include "native_identity.h"
#include "../network/peer.h"
#include <bcrypt.h>
#include "native_display.h"
#include "../worker/supervisor.h"
#include "../worker/process_guard.h"
#include <windows.h>
#include <shlobj.h>
#include <sddl.h>
#include <tlhelp32.h>
#include <userenv.h>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>
#include <stdexcept>
#include <algorithm>
#include <memory>

namespace fs = std::filesystem;

namespace {
struct Handle {
    HANDLE h = INVALID_HANDLE_VALUE;

    explicit Handle(HANDLE value = INVALID_HANDLE_VALUE) : h(value) {}

    ~Handle() {
        if (h && h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
        }
    }

    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

    Handle(Handle&& other) noexcept : h(other.h) {
        other.h = INVALID_HANDLE_VALUE;
    }
};

std::ofstream log_stream;

std::string utf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }
    int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr,
                                   0, nullptr, nullptr);
    if (!size) {
        throw std::runtime_error("Invalid Unicode");
    }
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(),
                        size, nullptr, nullptr);
    return result;
}

std::string quote(const std::string& text) {
    std::string result = "\"";
    for (unsigned char character : text) {
        if (character == '\\' || character == '"') {
            result += '\\';
            result += static_cast<char>(character);
        } else if (character < 32) {
            char escaped[7];
            sprintf_s(escaped, "\\u%04x", character);
            result += escaped;
        } else {
            result += static_cast<char>(character);
        }
    }
    return result + '"';
}

void event(const char* name, const std::string& fields = "") {
    SYSTEMTIME now{};
    GetSystemTime(&now);
    char utc[40];
    sprintf_s(utc, "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
              now.wSecond, now.wMilliseconds);
    log_stream << "{\"event\":" << quote(name) << ",\"utc\":" << quote(utc) << ",\"host_pid\":" << GetCurrentProcessId()
               << fields << "}\n";
    log_stream.flush();
}

void check(bool ok, const char* operation) {
    if (!ok) {
        throw std::runtime_error(std::string(operation) + ": win32=" + std::to_string(GetLastError()));
    }
}

fs::path safe_path(const fs::path& path) {
    if (!path.is_absolute() || !path.has_root_name() || path.native().rfind(L"\\\\", 0) == 0) {
        throw std::runtime_error("Local absolute path required");
    }
    fs::path at;
    for (const auto& part : path) {
        if (part == L".." || part == L".") {
            throw std::runtime_error("Relative path component refused");
        }
        at /= part;
        DWORD attributes = GetFileAttributesW(at.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            throw std::runtime_error("Reparse path refused");
        }
    }
    return path;
}

std::wstring key(const fs::path& path) {
    auto value = path.generic_wstring();
    std::transform(value.begin(), value.end(), value.begin(),
                   [](wchar_t character) { return static_cast<wchar_t>(towlower(character)); });
    return value;
}

void deny_write(const fs::path& path) {
    for (DWORD access : {DWORD(GENERIC_WRITE), DWORD(DELETE)}) {
        Handle probe(CreateFileW(path.c_str(), access, 7, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
        DWORD error = probe.h == INVALID_HANDLE_VALUE ? GetLastError() : 0;
        event("access_probe", ",\"path\":" + quote(utf8(path.wstring())) + ",\"requested_access\":" +
                                  std::to_string(access) + ",\"error\":" + std::to_string(error));
        if (error != ERROR_ACCESS_DENIED) {
            throw std::runtime_error("Protected path is not denied by OS");
        }
    }
}

// The string form of the SID of the user a token belongs to.
std::wstring token_user_sid(HANDLE token) {
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<BYTE> bytes(size);
    check(GetTokenInformation(token, TokenUser, bytes.data(), size, &size) != 0, "TokenUser");
    LPWSTR sid = nullptr;
    check(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid, &sid) != 0, "Token SID");
    std::wstring actual_sid(sid);
    LocalFree(sid);
    return actual_sid;
}

// The account must have the expected SID, be named SporeMP-M01 (or SporeMP-M04-... for a worker) and not be an
// administrator.
void require_dedicated_account(const std::wstring& actual_sid, const std::wstring& expected_sid, bool worker) {
    wchar_t user[256]{};
    DWORD user_size = 256;
    check(GetUserNameW(user, &user_size) != 0, "GetUserName");
    const bool named_worker = worker && std::wstring(user).rfind(L"SporeMP-M04-", 0) == 0;
    if (actual_sid != expected_sid || (!named_worker && wcscmp(user, L"SporeMP-M01") != 0) || IsUserAnAdmin()) {
        throw std::runtime_error("Dedicated standard account required");
    }
}

std::wstring token_profile_directory(HANDLE token) {
    wchar_t profile[32768]{};
    DWORD size = 32768;
    check(GetUserProfileDirectoryW(token, profile, &size) != 0, "Profile directory");
    return profile;
}

// The profile, roaming, local and documents folders of this account must resolve inside its disposable profile.
void require_shell_folders_inside(const std::wstring& profile_key) {
    for (const KNOWNFOLDERID* folder :
         {&FOLDERID_Profile, &FOLDERID_RoamingAppData, &FOLDERID_LocalAppData, &FOLDERID_Documents}) {
        PWSTR value = nullptr;
        HRESULT status = SHGetKnownFolderPath(*folder, KF_FLAG_CREATE, nullptr, &value);
        if (FAILED(status)) {
            throw std::runtime_error("Known folder unavailable");
        }
        fs::path path(value);
        CoTaskMemFree(value);
        safe_path(path);
        auto path_key = key(path);
        if (path_key != profile_key && path_key.rfind(profile_key + L"/", 0) != 0) {
            throw std::runtime_error("Known folder escaped disposable profile");
        }
        event("shell_folder", ",\"path\":" + quote(utf8(path.wstring())));
    }
}

// The operating system must refuse this account any write to the personal profile and its SPORE folders.
void deny_personal_writes(const fs::path& personal) {
    for (auto relative : {L"", L"AppData", L"AppData/Roaming", L"AppData/Roaming/Spore", L"Documents",
                          L"Documents/My Spore Creations"}) {
        deny_write(personal / relative);
    }
}

// A worker must also be refused the profiles of the other worker accounts present under C:/Users.
void deny_peer_worker_writes(const std::wstring& profile_key) {
    for (auto peer_name : {L"SporeMP-M04-01", L"SporeMP-M04-02", L"SporeMP-M04-03"}) {
        const auto peer = fs::path(L"C:/Users") / peer_name;
        if (!fs::exists(peer)) {
            continue;
        }
        if (key(peer) == profile_key) {
            continue;
        }
        for (auto relative : {L"", L"AppData/Roaming/Spore", L"Documents/My Spore Creations"}) {
            const auto path = safe_path(peer / relative);
            deny_write(path);
        }
    }
}

// The isolation gate of a developer launch: the dedicated account, its disposable profile, and the write denials
// that protect the personal files. The token stays open until the gate has passed.
std::wstring identity(const std::wstring& expected_sid, const fs::path& personal, bool worker = false) {
    HANDLE token_raw = nullptr;
    check(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token_raw) != 0, "OpenProcessToken");
    Handle token(token_raw);
    const std::wstring actual_sid = token_user_sid(token.h);
    require_dedicated_account(actual_sid, expected_sid, worker);
    const std::wstring profile = token_profile_directory(token.h);
    auto profile_key = key(safe_path(profile));
    if (profile_key == key(personal)) {
        throw std::runtime_error("Personal profile refused");
    }
    require_shell_folders_inside(profile_key);
    deny_personal_writes(personal);
    if (worker) {
        deny_peer_worker_writes(profile_key);
    }
    event("isolated_identity", ",\"sid\":" + quote(utf8(actual_sid)) + ",\"profile\":" + quote(utf8(profile)));
    return profile;
}

void guard_files(const fs::path& root, const GuardFile* begin, const GuardFile* end, std::vector<Handle>& locks) {
    for (auto entry = begin; entry != end; ++entry) {
        auto path = safe_path(root / entry->path);
        Handle hold(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
        check(hold.h != INVALID_HANDLE_VALUE, "Lock artifact against writes/deletion");
        sporemp::Sha256 hash{};
        if (!sporemp::sha256_file(path.c_str(), hash) || strcmp(hash.data(), entry->sha) != 0) {
            throw std::runtime_error("Fingerprint mismatch: " + utf8(path.wstring()));
        }
        locks.push_back(std::move(hold));
    }
}

void guard_content(const fs::path& root, std::vector<Handle>& locks, bool isolated) {
    // Reject even a same-size executable mutation before touching other content.
    for (auto& file : game_files) {
        if (key(file.path) == L"sporebinep1/sporeapp.exe") {
            guard_files(root, &file, &file + 1, locks);
        }
    }
    std::set<std::wstring> expected_files, expected_dirs, actual_files, actual_dirs;
    for (auto& file : game_files) {
        expected_files.insert(key(file.path));
    }
    for (auto& directory : game_directories) {
        expected_dirs.insert(key(directory));
    }
    for (auto folder : {L"Data", L"DataEP1", L"bp1content", L"SporebinEP1"}) {
        auto base = safe_path(root / folder);
        actual_dirs.insert(key(folder));
        for (auto& entry : fs::recursive_directory_iterator(base)) {
            safe_path(entry.path());
            auto relative = key(entry.path().lexically_relative(root));
            if (entry.is_directory()) {
                actual_dirs.insert(relative);
            } else if (entry.is_regular_file()) {
                // The pinned SDK emits this diagnostic log during normal Play.
                // safe_path already rejects reparse points; no other extra file
                // is exempted from exact immutable-content validation.
                if (relative == L"sporebinep1/spore_log.txt") {
                    event("runtime_output_present", ",\"path\":" + quote(utf8(entry.path().wstring())));
                } else {
                    actual_files.insert(relative);
                }
            } else {
                throw std::runtime_error("Non-regular content");
            }
        }
    }
    if (actual_files != expected_files || actual_dirs != expected_dirs) {
        throw std::runtime_error("Unknown or missing content entry");
    }
    guard_files(root, std::begin(game_files), std::end(game_files), locks);
    if (isolated) {
        deny_write(root);
        deny_write(root / L"SporebinEP1/SporeApp.exe");
    }
    event("game_validated", ",\"files\":" + std::to_string(expected_files.size()));
}

uintptr_t remote_module(DWORD pid, const std::wstring& basename) {
    for (int attempt = 0; attempt < 20; ++attempt) {
        Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid));
        if (snapshot.h == INVALID_HANDLE_VALUE) {
            Sleep(50);
            continue;
        }
        MODULEENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        if (Module32FirstW(snapshot.h, &entry)) {
            do {
                if (_wcsicmp(entry.szModule, basename.c_str()) == 0) {
                    return reinterpret_cast<uintptr_t>(entry.modBaseAddr);
                }
            } while (Module32NextW(snapshot.h, &entry));
        }
        Sleep(50);
    }
    throw std::runtime_error("Remote module unavailable: " + utf8(basename));
}

DWORD remote_call(HANDLE process, uintptr_t function, const void* data, SIZE_T length) {
    void* memory = VirtualAllocEx(process, nullptr, length, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    check(memory != nullptr, "VirtualAllocEx");
    SIZE_T written = 0;
    if (!WriteProcessMemory(process, memory, data, length, &written) || written != length) {
        VirtualFreeEx(process, memory, 0, MEM_RELEASE);
        throw std::runtime_error("WriteProcessMemory failed");
    }
    Handle thread(CreateRemoteThread(process, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(function), memory, 0,
                                     nullptr));
    if (!thread.h) {
        VirtualFreeEx(process, memory, 0, MEM_RELEASE);
        throw std::runtime_error("CreateRemoteThread failed");
    }
    // Do not free the argument under a still-running remote thread on timeout.
    if (WaitForSingleObject(thread.h, 15000) != WAIT_OBJECT_0) {
        throw std::runtime_error("Remote call timeout; suspended game will be terminated");
    }
    DWORD code = 0;
    check(GetExitCodeThread(thread.h, &code) != 0, "Remote thread result");
    VirtualFreeEx(process, memory, 0, MEM_RELEASE);
    return code;
}

// Where SetInjectionData lies inside the injector module. The local copy is mapped for its export table only and
// never executes.
uintptr_t injection_setter_offset(const std::wstring& injector) {
    HMODULE local = LoadLibraryExW(injector.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
    check(local != nullptr, "Map injector exports without executing");
    auto setter = GetProcAddress(local, "SetInjectionData");
    if (!setter) {
        FreeLibrary(local);
        throw std::runtime_error("SetInjectionData export missing");
    }
    uintptr_t setter_offset = reinterpret_cast<uintptr_t>(setter) - reinterpret_cast<uintptr_t>(local);
    FreeLibrary(local);
    return setter_offset;
}

// The argument block of SetInjectionData: the paths of the two libraries, in the upstream ABI.
std::vector<BYTE> injection_data(const fs::path& payload) {
    std::vector<BYTE> data{0}; // upstream ABI: non-disc/March2017, then uint32 count and UTF-16 strings.
    auto append_u32 = [&](uint32_t value) {
        for (unsigned i = 0; i < 4; ++i) {
            data.push_back(static_cast<BYTE>(value >> (i * 8)));
        }
    };
    append_u32(2);
    for (auto dll : {L"mLibs/SporeModAPI.dll", L"mLibs/SporeMP.Bridge.dll"}) {
        auto path = (payload / dll).wstring();
        append_u32(static_cast<uint32_t>(path.size()));
        auto start = reinterpret_cast<const BYTE*>(path.data());
        data.insert(data.end(), start, start + path.size() * sizeof(wchar_t));
    }
    return data;
}

void inject(PROCESS_INFORMATION& process, const fs::path& payload) {
    auto load = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    check(load != nullptr, "LoadLibraryW");
    HMODULE owner = nullptr;
    check(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                             reinterpret_cast<LPCWSTR>(load), &owner) != 0,
          "LoadLibrary owner");
    wchar_t owner_path[32768]{};
    check(GetModuleFileNameW(owner, owner_path, 32768) != 0, "Owner path");
    // CREATE_SUSPENDED precedes user-mode loader initialization, so kernel32 may
    // not yet be enumerable. Use the pinned upstream Injector.cs bootstrap:
    // a same-bitness remote LoadLibrary thread initializes the Windows loader.
    // Verify the actual system-module mapping before configuring/resuming game code.
    uintptr_t address = reinterpret_cast<uintptr_t>(load);
    auto injector = (payload / L"ModAPI.DLLInjector.dll").wstring();
    DWORD remote_base =
        remote_call(process.hProcess, address, injector.c_str(), (injector.size() + 1) * sizeof(wchar_t));
    check(remote_base != 0, "Injector LoadLibraryW result");
    uintptr_t actual_owner = remote_module(process.dwProcessId, fs::path(owner_path).filename().wstring());
    if (actual_owner != reinterpret_cast<uintptr_t>(owner)) {
        throw std::runtime_error("System DLL mapping differs from upstream bootstrap assumption");
    }
    if (remote_module(process.dwProcessId, L"ModAPI.DLLInjector.dll") != remote_base) {
        throw std::runtime_error("Injected module identity mismatch");
    }
    event("bootstrap_mapping_verified", ",\"system_module\":" + quote(utf8(owner_path)));
    const uintptr_t setter_offset = injection_setter_offset(injector);
    const std::vector<BYTE> data = injection_data(payload);
    remote_call(process.hProcess, remote_base + setter_offset, data.data(), data.size());
    event("injection_configured",
          ",\"game_pid\":" + std::to_string(process.dwProcessId) + ",\"dll_count\":2,\"legacy_mods\":false");
}

void modules(DWORD pid) {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid));
    if (snapshot.h == INVALID_HANDLE_VALUE) {
        return;
    }
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Module32FirstW(snapshot.h, &entry)) {
        do {
            sporemp::Sha256 hash{};
            sporemp::sha256_file(entry.szExePath, hash);
            event("module", ",\"game_pid\":" + std::to_string(pid) + ",\"path\":" + quote(utf8(entry.szExePath)) +
                                ",\"sha256\":" + quote(hash.data()));
        } while (Module32NextW(snapshot.h, &entry));
    }
}

// What the launch mode named by argv[1] implies.
struct LaunchMode {
    std::wstring name;
    bool join = false;
    bool network_worker = false;
    bool network_mode = false;
    bool replica_probe = false;
    bool authority_probe = false;
    bool worker = false;
    bool isolated = false;
    // The number of arguments ahead of the optional display arguments, the program name included.
    int base_count = 0;
};

LaunchMode launch_mode(const wchar_t* argument) {
    LaunchMode mode;
    mode.name = argument;
    mode.join = mode.name == L"--join";
    mode.network_worker = mode.name == L"--network-authority" || mode.name == L"--network-replica";
    mode.network_mode = mode.join || mode.network_worker;
    mode.replica_probe = mode.name == L"--replica-probe" || mode.name == L"--network-replica" || mode.join;
    mode.authority_probe = mode.name == L"--authority-probe" || mode.name == L"--network-authority";
    mode.worker = mode.name == L"--worker" || (!mode.join && (mode.replica_probe || mode.authority_probe));
    mode.isolated = mode.name != L"--play" && !mode.join;
    mode.base_count = mode.network_worker ? 10 : mode.worker ? 9 : mode.join ? 6 : mode.isolated ? 7 : 5;
    return mode;
}

// Too few arguments for the mode, or a developer mode that is none of the known ones.
bool arguments_refused(int argc, const LaunchMode& mode) {
    return argc < mode.base_count ||
           (mode.isolated && !mode.worker && mode.name != L"--probe" && mode.name != L"--launch" &&
            mode.name != L"--observe" && mode.name != L"--actors");
}

// A worker's generation (argv[7]) and its desktop choice (argv[8]). False when either is malformed.
bool worker_arguments(wchar_t** argv, sporemp::worker::Generation& generation, std::wstring& desktop_name) {
    if (!sporemp::worker::generation_from_hex(argv[7], generation)) {
        return false;
    }
    if (std::wstring(argv[8]) != L"private" && std::wstring(argv[8]) != L"current") {
        return false;
    }
    desktop_name = L"SporeMP-M04-" + sporemp::worker::generation_hex(generation);
    return true;
}

// The arguments that follow the ones the mode requires.
struct DisplayOptions {
    bool m10_save_probe = false;
    sporemp::WorkerPresentation presentation;
    // The allowlisted display switches, as they go on the game's command line.
    std::wstring display;
};

DisplayOptions parse_display_options(int display_count, wchar_t** display_values, bool worker) {
    DisplayOptions options;
    // Explicit developer-only observation switch. Never inherited by Play
    // or Join and never accepted as an original-game command-line flag.
    options.m10_save_probe = worker && display_count > 0 && std::wstring(display_values[0]) == L"--m10-save-probe";
    if (options.m10_save_probe) {
        ++display_values;
        --display_count;
    }
    // Developer-only worker presentation: where the bridge places the window, and the original noSound switch.
    options.presentation =
        worker ? sporemp::worker_presentation(display_count, display_values) : sporemp::WorkerPresentation{};
    display_values += options.presentation.consumed;
    display_count -= options.presentation.consumed;
    options.display = sporemp::display_arguments(display_count, display_values);
    return options;
}

// Everything one command line asks for.
struct Request {
    wchar_t** argv = nullptr;
    LaunchMode mode;
    // A worker's generation comes from argv[7]; a joining player's is drawn when the environment is set.
    sporemp::worker::Generation generation{};
    std::wstring desktop_name;
    DisplayOptions options;
    fs::path game, payload, run;
};

// The host's own record. It must be a new file: an existing one is the evidence of another run. False when the
// stream cannot be opened.
bool open_evidence_log(const fs::path& run) {
    auto log_path = run / L"native-host.jsonl";
    Handle unique(CreateFileW(log_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, nullptr));
    check(unique.h != INVALID_HANDLE_VALUE, "Exclusive evidence file");
    CloseHandle(unique.h);
    unique.h = INVALID_HANDLE_VALUE;
    log_stream.open(log_path, std::ios::app);
    return !log_stream.fail();
}

void record_player_account() {
    wchar_t user[256]{};
    DWORD size = 256;
    check(GetUserNameW(user, &size) != 0, "GetUserName");
    event("player_account", ",\"user\":" + quote(utf8(user)) + ",\"profile_redirection\":false");
}

// One guarded process per actual OS profile, across launch modes/sessions. The caller holds the mutex until the
// game has exited.
Handle hold_profile_singleton() {
    const auto mutex_name = L"Global\\SporeMP-Native-" + sporemp::worker::current_sid();
    Handle singleton(CreateMutexW(nullptr, TRUE, mutex_name.c_str()));
    if (!singleton.h || GetLastError() == ERROR_ALREADY_EXISTS) {
        throw std::runtime_error("Native qualification already running");
    }
    return singleton;
}

// Opens the session configuration, holds it against writes for as long as the locks live, and parses it.
sporemp::network::PeerConfig read_locked_peer_config(const fs::path& network_config, std::vector<Handle>& locks) {
    Handle session_lock(CreateFileW(network_config.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, nullptr));
    check(session_lock.h != INVALID_HANDLE_VALUE, "Lock multiplayer session configuration");
    locks.push_back(std::move(session_lock));
    sporemp::network::PeerConfig config;
    std::string config_error;
    if (!sporemp::network::read_peer_config(network_config.wstring(), config, config_error)) {
        throw std::runtime_error("Invalid multiplayer session configuration: " + config_error);
    }
    return config;
}

// The saves and the world folder of the current account that a multiplayer session is checked against.
struct SharedWorldPaths {
    fs::path fixture, secondary_save, world_root;
};

SharedWorldPaths shared_world_paths(const sporemp::network::PeerConfig& config) {
    SharedWorldPaths paths;
    PWSTR roaming = nullptr;
    check(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming)),
          "Resolve current-account shared scene");
    paths.fixture = safe_path(fs::path(roaming) / L"Spore/Games/Game0/Satiria.spo");
    paths.secondary_save =
        config.primary_owner ? safe_path(fs::path(roaming) / L"Spore/Games/Game0/Haagos.spo") : fs::path{};
    paths.world_root = safe_path(fs::path(roaming) / L"Spore");
    CoTaskMemFree(roaming);
    return paths;
}

// The shared scene must exist, and the second save must be the one the session was configured with. Fills the
// fixture digest of the expected identity.
void require_shared_saves(const sporemp::network::PeerConfig& config, const SharedWorldPaths& paths,
                          sporemp::network::Identity& expected) {
    sporemp::Sha256 fixture_hash{};
    if (!sporemp::sha256_file(paths.fixture.c_str(), fixture_hash) ||
        !sporemp::network::parse_hex(fixture_hash.data(), expected.fixture)) {
        throw std::runtime_error("The shared Satiria Creature scene is missing from this account.");
    }
    if (config.primary_owner) {
        sporemp::Sha256 secondary_hash{};
        sporemp::network::Digest observed{};
        if (!sporemp::sha256_file(paths.secondary_save.c_str(), secondary_hash) ||
            !sporemp::network::parse_hex(secondary_hash.data(), observed) || observed != config.secondary_save) {
            throw std::runtime_error("The selected shared Creature world has a different second save.");
        }
    }
}

// Every canonical world file of this account must match the configured one. Fills the world digests of the
// expected identity.
void require_canonical_world(const sporemp::network::PeerConfig& config, const fs::path& world_root,
                             sporemp::network::Identity& expected) {
    std::string world_error;
    if (!sporemp::network::read_world_identity(world_root.wstring(), expected.world, world_error)) {
        throw std::runtime_error(world_error);
    }
    for (size_t index = 0; index < sporemp::network::world_file_count; ++index) {
        if (config.identity.world[index] != expected.world[index]) {
            throw std::runtime_error(std::string("canonical_world_mismatch:") +
                                     sporemp::network::world_files[index].path);
        }
    }
}

// A multiplayer session starts only when its configuration names the guarded build, executable, content, shared
// saves and world of this account, in the role of this launch mode. Returns the configuration's path.
fs::path validate_network_session(const wchar_t* argument, bool authority_probe, std::vector<Handle>& locks) {
    fs::path network_config = safe_path(argument);
    const sporemp::network::PeerConfig config = read_locked_peer_config(network_config, locks);
    sporemp::network::Identity expected;
    const SharedWorldPaths paths = shared_world_paths(config);
    require_shared_saves(config, paths, expected);
    require_canonical_world(config, paths.world_root, expected);
    if (!sporemp::network::parse_hex(payload_files[2].sha, expected.build) ||
        !sporemp::network::parse_hex(network_executable_sha256, expected.executable) ||
        !sporemp::network::parse_hex(network_content_sha256, expected.content) || !(config.identity == expected)) {
        throw std::runtime_error("Multiplayer configuration does not match guarded build/content.");
    }
    if (config.role != (authority_probe ? sporemp::network::Role::authority : sporemp::network::Role::player)) {
        throw std::runtime_error("Multiplayer role/configuration mismatch.");
    }
    event("network_config_validated", ",\"role\":\"" + std::string(authority_probe ? "authority" : "player") + "\"");
    event("canonical_world_prelaunch_validated",
          ",\"bundle_version\":1,\"files\":6,\"runtime_terrain_qualified\":false");
    return network_config;
}

// The switches the bridge reads from its environment. Each is set or cleared on every launch, so that nothing is
// inherited from the host's own environment.
void set_bridge_environment(Request& request, const fs::path& network_config) {
    const LaunchMode& mode = request.mode;
    check(SetEnvironmentVariableW(L"SPOREMP_DIAGNOSTICS_DIR", request.run.c_str()) != 0, "Diagnostics environment");
    // Never inherit an experimental gameplay-hook switch into normal Play or the reference run.
    check(SetEnvironmentVariableW(L"SPOREMP_M02_TRACE", mode.name == L"--observe" ? L"observe" : nullptr) != 0,
          "Observation environment");
    check(SetEnvironmentVariableW(L"SPOREMP_M03_ACTORS",
                                  mode.name == L"--actors" || mode.worker || mode.join ? L"harness" : nullptr) != 0,
          "Actor harness environment");
    check(SetEnvironmentVariableW(L"SPOREMP_M05_ROLE", mode.replica_probe     ? L"replica"
                                                       : mode.authority_probe ? L"authority"
                                                                              : nullptr) != 0,
          "Replica role environment");
    std::wstring local_generation;
    if (mode.join) {
        check(BCryptGenRandom(nullptr, request.generation.data(), static_cast<ULONG>(request.generation.size()),
                              BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0,
              "Session generation");
        local_generation = sporemp::worker::generation_hex(request.generation);
    }
    check(SetEnvironmentVariableW(L"SPOREMP_M04_GENERATION", mode.worker ? request.argv[7]
                                                             : mode.join ? local_generation.c_str()
                                                                         : nullptr) != 0,
          "Worker generation environment");
    check(SetEnvironmentVariableW(L"SPOREMP_M06_CONFIG", mode.network_mode ? network_config.c_str() : nullptr) != 0,
          "Network configuration environment");
    const auto supervisor_pid = std::to_wstring(GetCurrentProcessId());
    check(SetEnvironmentVariableW(L"SPOREMP_M04_SUPERVISOR", mode.worker ? supervisor_pid.c_str() : nullptr) != 0,
          "Worker supervisor environment");
    check(SetEnvironmentVariableW(L"SPOREMP_M10_SAVE_PROBE", request.options.m10_save_probe ? L"observe" : nullptr) !=
              0,
          "M10 save observation environment");
    // Never inherited by Play or Join: the bridge installs its placement hook only when this is set.
    const auto& window_position = request.options.presentation.window_position;
    check(SetEnvironmentVariableW(L"SPOREMP_WINDOW_POSITION",
                                  window_position.empty() ? nullptr : window_position.c_str()) != 0,
          "Window position environment");
}

std::wstring game_command_line(const std::wstring& exe, const Request& request) {
    std::wstring command = L"\"" + exe + L"\"" + request.options.display;
    if (request.options.presentation.no_sound) {
        command += L" -noSound";
    }
    // Pinned EAMain 0xF48810 calls CommandLine::FindSwitch("multipleInstances")
    // before the original Global\SporeEditorOrGame mutex. Worker profiles are
    // still fenced separately; this is not a profile-isolation mechanism.
    if (request.mode.worker || request.mode.join) {
        command += L" -multipleInstances";
    }
    return command;
}

// What the host holds from the moment it starts a game. wmain releases all of it on a rejection.
struct GameProcess {
    PROCESS_INFORMATION process{};
    bool created = false;
    HDESK worker_desktop = nullptr;
    std::unique_ptr<sporemp::worker::Supervisor> supervisor;
};

// A worker is supervised and, when argv[8] asks for it, rendered on a private desktop. desktop_path is the
// string the start-up information then points into: it must outlive the creation of the process.
void start_worker_supervision(const Request& request, GameProcess& child, STARTUPINFOW& startup,
                              std::wstring& desktop_path) {
    // The network role/configuration has already passed guarded identity
    // validation. Replicas suppress original AI by design; their
    // readiness uses live app progress and an applied network baseline.
    const auto progress_role = request.mode.name == L"--network-replica"
                                   ? sporemp::worker::ProgressRole::network_replica
                                   : sporemp::worker::ProgressRole::native_simulation;
    child.supervisor = std::make_unique<sporemp::worker::Supervisor>(
        request.generation, request.run, event, [] { return GetTickCount64(); }, progress_role);
    if (std::wstring(request.argv[8]) == L"private") {
        child.worker_desktop = CreateDesktopW(
            request.desktop_name.c_str(), nullptr, nullptr, 0,
            DESKTOP_CREATEWINDOW | DESKTOP_READOBJECTS | DESKTOP_WRITEOBJECTS | DESKTOP_ENUMERATE, nullptr);
        check(child.worker_desktop != nullptr, "Create private rendered worker desktop");
        desktop_path = L"WinSta0\\" + request.desktop_name;
        startup.lpDesktop = desktop_path.data();
        event("worker_desktop_created",
              ",\"switched_input_desktop\":false,\"desktop\":" + quote(utf8(request.desktop_name)));
    } else {
        event("worker_current_desktop", ",\"rendered\":true,\"multiple_instances\":true");
    }
}

// Waits for the game to end by itself. Meanwhile the supervisor is ticked, and the modules of the game are
// recorded once, when the bridge has written its first record.
DWORD wait_for_exit(GameProcess& child, bool worker, const fs::path& run) {
    bool captured = false;
    DWORD waited = 0;
    while (WaitForSingleObject(child.process.hProcess, worker ? 20 : 1000) == WAIT_TIMEOUT) {
        if (child.supervisor) {
            child.supervisor->tick();
        }
        ++waited;
        if (!captured && fs::exists(run / (L"bridge-" + std::to_wstring(child.process.dwProcessId) + L".jsonl"))) {
            modules(child.process.dwProcessId);
            captured = true;
        }
        if (waited % (worker ? 1500 : 30) == 0) {
            event("waiting_for_normal_exit", ",\"game_pid\":" + std::to_string(child.process.dwProcessId));
        }
    }
    DWORD exit_code = 0;
    check(GetExitCodeProcess(child.process.hProcess, &exit_code) != 0, "GetExitCodeProcess");
    return exit_code;
}

// Starts the game suspended, injects the bridge, resumes the game and waits for it to end. Returns the host's
// exit code.
int launch_game(const Request& request, GameProcess& child) {
    const LaunchMode& mode = request.mode;
    auto exe = (request.game / L"SporebinEP1/SporeApp.exe").wstring();
    std::wstring command = game_command_line(exe, request);
    event("display_options", ",\"arguments\":" + quote(utf8(request.options.display)));
    event("worker_presentation", ",\"window_position\":" + quote(utf8(request.options.presentation.window_position)) +
                                     ",\"no_sound\":" + (request.options.presentation.no_sound ? "true" : "false"));
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    std::wstring desktop_path;
    if (mode.worker) {
        start_worker_supervision(request, child, startup, desktop_path);
    }
    // Explicit executable and working directory; only allowlisted documented display flags.
    check(CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr,
                         (request.game / L"SporebinEP1").c_str(), &startup, &child.process) != 0,
          "CreateProcessW suspended");
    child.created = true;
    event("created_suspended", ",\"game_pid\":" + std::to_string(child.process.dwProcessId));
    if (child.supervisor) {
        child.supervisor->attach(child.process.hProcess, child.process.dwProcessId);
    }
    inject(child.process, request.payload);
    check(ResumeThread(child.process.hThread) == 1, "ResumeThread");
    event("resumed", ",\"game_pid\":" + std::to_string(child.process.dwProcessId));
    const DWORD exit_code = wait_for_exit(child, mode.worker, request.run);
    event("game_exited",
          ",\"game_pid\":" + std::to_string(child.process.dwProcessId) + ",\"exit_code\":" + std::to_string(exit_code));
    if (child.supervisor) {
        child.supervisor->exited(exit_code);
    }
    CloseHandle(child.process.hThread);
    CloseHandle(child.process.hProcess);
    child.created = false;
    child.supervisor.reset();
    if (child.worker_desktop) {
        CloseDesktop(child.worker_desktop);
        child.worker_desktop = nullptr;
    }
    return exit_code == 0 ? 0 : 31;
}

// Parses the command line, passes every guard in order, then launches. Nothing of the game runs before the
// guards have passed. The singleton and the file locks are held until this returns or throws.
int guard_and_launch(int argc, wchar_t** argv, GameProcess& child) {
    Request request;
    request.argv = argv;
    request.mode = launch_mode(argv[1]);
    const LaunchMode& mode = request.mode;
    if (arguments_refused(argc, mode)) {
        return 2;
    }
    if (mode.worker && !worker_arguments(argv, request.generation, request.desktop_name)) {
        return 2;
    }
    request.options = parse_display_options(argc - mode.base_count, argv + mode.base_count, mode.worker);
    request.game = safe_path(argv[2]);
    request.payload = safe_path(argv[3]);
    request.run = safe_path(argv[4]);
    if (!open_evidence_log(request.run)) {
        return 2;
    }
    event("guard_started", ",\"mode\":" + quote(utf8(mode.name)) + ",\"launched_processes\":0");
    if (mode.isolated) {
        identity(argv[5], safe_path(argv[6]), mode.worker);
    } else {
        record_player_account();
    }
    Handle singleton(hold_profile_singleton());
    std::vector<Handle> locks;
    guard_content(request.game, locks, mode.isolated);
    guard_files(request.payload, std::begin(payload_files), std::end(payload_files), locks);
    event("payload_validated");
    fs::path network_config;
    if (mode.network_mode) {
        network_config = validate_network_session(argv[mode.network_worker ? 9 : 5], mode.authority_probe, locks);
    }
    if (mode.name == L"--probe") {
        event("guard_passed", ",\"launched_processes\":0");
        return 0;
    }
    sporemp::worker::reject_process_in_profile(L"SporeApp.exe");
    set_bridge_environment(request, network_config);
    return launch_game(request, child);
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    // Player: --play game-root payload-dir run-dir (normal Windows account).
    // Developer: --probe|--launch|--observe|--actors game-root payload-dir run-dir expected-sid personal-profile.
    if (argc < 5) {
        return 2;
    }
    GameProcess child;
    try {
        return guard_and_launch(argc, argv, child);
    } catch (const std::exception& error) {
        event("rejected", ",\"reason\":" + quote(error.what()) +
                              ",\"launched_processes\":" + std::string(child.created ? "1" : "0"));
        if (child.created) {
            TerminateProcess(child.process.hProcess, 32);
            WaitForSingleObject(child.process.hProcess, 5000);
            CloseHandle(child.process.hThread);
            CloseHandle(child.process.hProcess);
        }
        child.supervisor.reset();
        if (child.worker_desktop) {
            CloseDesktop(child.worker_desktop);
        }
        return child.created ? 32 : 20;
    }
}
