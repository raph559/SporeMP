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
    ~Handle() { if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : h(other.h) { other.h = INVALID_HANDLE_VALUE; }
};
std::ofstream log_stream;
std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (!size) throw std::runtime_error("Invalid Unicode");
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}
std::string quote(const std::string& s) {
    std::string result = "\"";
    for (unsigned char c : s) {
        if (c == '\\' || c == '"') { result += '\\'; result += static_cast<char>(c); }
        else if (c < 32) { char b[7]; sprintf_s(b, "\\u%04x", c); result += b; }
        else result += static_cast<char>(c);
    }
    return result + '"';
}
void event(const char* name, const std::string& fields = "") {
    SYSTEMTIME t{}; GetSystemTime(&t); char utc[40];
    sprintf_s(utc, "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds);
    log_stream << "{\"event\":" << quote(name) << ",\"utc\":" << quote(utc) << ",\"host_pid\":" << GetCurrentProcessId() << fields << "}\n";
    log_stream.flush();
}
void check(bool ok, const char* operation) {
    if (!ok) throw std::runtime_error(std::string(operation) + ": win32=" + std::to_string(GetLastError()));
}
fs::path safe_path(const fs::path& path) {
    if (!path.is_absolute() || !path.has_root_name() || path.native().rfind(L"\\\\",0) == 0)
        throw std::runtime_error("Local absolute path required");
    fs::path at;
    for (const auto& part : path) {
        if (part == L".." || part == L".") throw std::runtime_error("Relative path component refused");
        at /= part;
        DWORD attrs = GetFileAttributesW(at.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) throw std::runtime_error("Reparse path refused");
    }
    return path;
}
std::wstring key(const fs::path& path) {
    auto value = path.generic_wstring();
    std::transform(value.begin(),value.end(),value.begin(),[](wchar_t c){return static_cast<wchar_t>(towlower(c));});
    return value;
}
void deny_write(const fs::path& path) {
    for (DWORD access : {DWORD(GENERIC_WRITE), DWORD(DELETE)}) {
        Handle h(CreateFileW(path.c_str(), access, 7, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
        DWORD error = h.h == INVALID_HANDLE_VALUE ? GetLastError() : 0;
        event("access_probe", ",\"path\":" + quote(utf8(path.wstring())) + ",\"requested_access\":" + std::to_string(access) + ",\"error\":" + std::to_string(error));
        if (error != ERROR_ACCESS_DENIED) throw std::runtime_error("Protected path is not denied by OS");
    }
}
std::wstring identity(const std::wstring& expected_sid, const fs::path& personal, bool worker = false) {
    HANDLE token_raw = nullptr; check(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token_raw)!=0,"OpenProcessToken"); Handle token(token_raw);
    DWORD size=0; GetTokenInformation(token.h,TokenUser,nullptr,0,&size); std::vector<BYTE> bytes(size);
    check(GetTokenInformation(token.h,TokenUser,bytes.data(),size,&size)!=0,"TokenUser");
    LPWSTR sid=nullptr; check(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid,&sid)!=0,"Token SID");
    std::wstring actual_sid(sid); LocalFree(sid);
    wchar_t user[256]{}; DWORD user_size=256; check(GetUserNameW(user,&user_size)!=0,"GetUserName");
    const bool named_worker = worker && std::wstring(user).rfind(L"SporeMP-M04-", 0) == 0;
    if (actual_sid != expected_sid || (!named_worker && wcscmp(user,L"SporeMP-M01") != 0) || IsUserAnAdmin()) throw std::runtime_error("Dedicated standard account required");
    wchar_t profile[32768]{}; size=32768; check(GetUserProfileDirectoryW(token.h,profile,&size)!=0,"Profile directory");
    auto profile_key = key(safe_path(profile));
    if (profile_key == key(personal)) throw std::runtime_error("Personal profile refused");
    for (const KNOWNFOLDERID* folder : {&FOLDERID_Profile,&FOLDERID_RoamingAppData,&FOLDERID_LocalAppData,&FOLDERID_Documents}) {
        PWSTR value=nullptr; HRESULT hr=SHGetKnownFolderPath(*folder,KF_FLAG_CREATE,nullptr,&value);
        if (FAILED(hr)) throw std::runtime_error("Known folder unavailable");
        fs::path path(value); CoTaskMemFree(value); safe_path(path);
        auto path_key=key(path);
        if (path_key != profile_key && path_key.rfind(profile_key + L"/",0) != 0) throw std::runtime_error("Known folder escaped disposable profile");
        event("shell_folder", ",\"path\":"+quote(utf8(path.wstring())));
    }
    for (auto relative : {L"",L"AppData",L"AppData/Roaming",L"AppData/Roaming/Spore",L"Documents",L"Documents/My Spore Creations"}) deny_write(personal/relative);
    if (worker) for (auto peer_name : {L"SporeMP-M04-01",L"SporeMP-M04-02",L"SporeMP-M04-03"}) {
        const auto peer = fs::path(L"C:/Users")/peer_name;
        if (!fs::exists(peer)) continue;
        if (key(peer) == profile_key) continue;
        for (auto relative : {L"",L"AppData/Roaming/Spore",L"Documents/My Spore Creations"}) {
            const auto path = safe_path(peer/relative);
            deny_write(path);
        }
    }
    event("isolated_identity", ",\"sid\":"+quote(utf8(actual_sid))+",\"profile\":"+quote(utf8(profile)));
    return profile;
}
void guard_files(const fs::path& root, const GuardFile* begin, const GuardFile* end, std::vector<Handle>& locks) {
    for (auto entry=begin;entry!=end;++entry) {
        auto path=safe_path(root/entry->path);
        Handle hold(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr));
        check(hold.h != INVALID_HANDLE_VALUE,"Lock artifact against writes/deletion");
        sporemp::Sha256 hash{};
        if (!sporemp::sha256_file(path.c_str(),hash) || strcmp(hash.data(),entry->sha)!=0) throw std::runtime_error("Fingerprint mismatch: "+utf8(path.wstring()));
        locks.push_back(std::move(hold));
    }
}
void guard_content(const fs::path& root,std::vector<Handle>& locks,bool isolated) {
    // Reject even a same-size executable mutation before touching other content.
    for (auto& file : game_files)
        if (key(file.path) == L"sporebinep1/sporeapp.exe") guard_files(root,&file,&file+1,locks);
    std::set<std::wstring> expected_files,expected_dirs,actual_files,actual_dirs;
    for(auto& f:game_files) expected_files.insert(key(f.path));
    for(auto& d:game_directories) expected_dirs.insert(key(d));
    for(auto folder:{L"Data",L"DataEP1",L"bp1content",L"SporebinEP1"}) {
        auto base=safe_path(root/folder); actual_dirs.insert(key(folder));
        for(auto& entry:fs::recursive_directory_iterator(base)) {
            safe_path(entry.path());
            auto relative=key(entry.path().lexically_relative(root));
            if(entry.is_directory()) actual_dirs.insert(relative);
            else if(entry.is_regular_file()) {
                // The pinned SDK emits this diagnostic log during normal Play.
                // safe_path already rejects reparse points; no other extra file
                // is exempted from exact immutable-content validation.
                if(relative == L"sporebinep1/spore_log.txt")
                    event("runtime_output_present", ",\"path\":"+quote(utf8(entry.path().wstring())));
                else actual_files.insert(relative);
            }
            else throw std::runtime_error("Non-regular content");
        }
    }
    if(actual_files!=expected_files || actual_dirs!=expected_dirs) throw std::runtime_error("Unknown or missing content entry");
    guard_files(root,std::begin(game_files),std::end(game_files),locks);
    if (isolated) { deny_write(root); deny_write(root/L"SporebinEP1/SporeApp.exe"); }
    event("game_validated",",\"files\":"+std::to_string(expected_files.size()));
}
uintptr_t remote_module(DWORD pid,const std::wstring& basename) {
    for(int attempt=0;attempt<20;++attempt) {
        Handle snap(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,pid));
        if(snap.h==INVALID_HANDLE_VALUE) {Sleep(50);continue;}
        MODULEENTRY32W entry{}; entry.dwSize=sizeof(entry);
        if(Module32FirstW(snap.h,&entry)) do {
            if(_wcsicmp(entry.szModule,basename.c_str())==0) return reinterpret_cast<uintptr_t>(entry.modBaseAddr);
        } while(Module32NextW(snap.h,&entry));
        Sleep(50);
    }
    throw std::runtime_error("Remote module unavailable: "+utf8(basename));
}
DWORD remote_call(HANDLE process,uintptr_t function,const void* data,SIZE_T length) {
    void* memory=VirtualAllocEx(process,nullptr,length,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    check(memory!=nullptr,"VirtualAllocEx");
    SIZE_T written=0;
    if(!WriteProcessMemory(process,memory,data,length,&written) || written!=length) {VirtualFreeEx(process,memory,0,MEM_RELEASE); throw std::runtime_error("WriteProcessMemory failed");}
    Handle thread(CreateRemoteThread(process,nullptr,0,reinterpret_cast<LPTHREAD_START_ROUTINE>(function),memory,0,nullptr));
    if(!thread.h) {VirtualFreeEx(process,memory,0,MEM_RELEASE);throw std::runtime_error("CreateRemoteThread failed");}
    // Do not free the argument under a still-running remote thread on timeout.
    if(WaitForSingleObject(thread.h,15000)!=WAIT_OBJECT_0) throw std::runtime_error("Remote call timeout; suspended game will be terminated");
    DWORD code=0; check(GetExitCodeThread(thread.h,&code)!=0,"Remote thread result");
    VirtualFreeEx(process,memory,0,MEM_RELEASE); return code;
}
void inject(PROCESS_INFORMATION& process,const fs::path& payload) {
    auto load=GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"LoadLibraryW"); check(load!=nullptr,"LoadLibraryW");
    HMODULE owner=nullptr;
    check(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(load),&owner)!=0,"LoadLibrary owner");
    wchar_t owner_path[32768]{}; check(GetModuleFileNameW(owner,owner_path,32768)!=0,"Owner path");
    // CREATE_SUSPENDED precedes user-mode loader initialization, so kernel32 may
    // not yet be enumerable. Use the pinned upstream Injector.cs bootstrap:
    // a same-bitness remote LoadLibrary thread initializes the Windows loader.
    // Verify the actual system-module mapping before configuring/resuming game code.
    uintptr_t address=reinterpret_cast<uintptr_t>(load);
    auto injector=(payload/L"ModAPI.DLLInjector.dll").wstring();
    DWORD remote_base=remote_call(process.hProcess,address,injector.c_str(),(injector.size()+1)*sizeof(wchar_t));
    check(remote_base!=0,"Injector LoadLibraryW result");
    uintptr_t actual_owner=remote_module(process.dwProcessId,fs::path(owner_path).filename().wstring());
    if(actual_owner!=reinterpret_cast<uintptr_t>(owner)) throw std::runtime_error("System DLL mapping differs from upstream bootstrap assumption");
    if(remote_module(process.dwProcessId,L"ModAPI.DLLInjector.dll")!=remote_base) throw std::runtime_error("Injected module identity mismatch");
    event("bootstrap_mapping_verified",",\"system_module\":"+quote(utf8(owner_path)));
    HMODULE local=LoadLibraryExW(injector.c_str(),nullptr,DONT_RESOLVE_DLL_REFERENCES); check(local!=nullptr,"Map injector exports without executing");
    auto setter=GetProcAddress(local,"SetInjectionData");
    if(!setter) {FreeLibrary(local);throw std::runtime_error("SetInjectionData export missing");}
    uintptr_t setter_offset=reinterpret_cast<uintptr_t>(setter)-reinterpret_cast<uintptr_t>(local); FreeLibrary(local);
    std::vector<BYTE> data{0}; // upstream ABI: non-disc/March2017, then uint32 count and UTF-16 strings.
    auto append_u32=[&](uint32_t v){for(unsigned i=0;i<4;++i)data.push_back(static_cast<BYTE>(v>>(i*8)));};
    append_u32(2);
    for(auto dll:{L"mLibs/SporeModAPI.dll",L"mLibs/SporeMP.Bridge.dll"}) {
        auto path=(payload/dll).wstring(); append_u32(static_cast<uint32_t>(path.size()));
        auto start=reinterpret_cast<const BYTE*>(path.data()); data.insert(data.end(),start,start+path.size()*sizeof(wchar_t));
    }
    remote_call(process.hProcess,remote_base+setter_offset,data.data(),data.size());
    event("injection_configured",",\"game_pid\":"+std::to_string(process.dwProcessId)+",\"dll_count\":2,\"legacy_mods\":false");
}
void modules(DWORD pid) {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,pid)); if(snapshot.h==INVALID_HANDLE_VALUE)return;
    MODULEENTRY32W entry{}; entry.dwSize=sizeof(entry);
    if(Module32FirstW(snapshot.h,&entry)) do {
        sporemp::Sha256 hash{}; sporemp::sha256_file(entry.szExePath,hash);
        event("module",",\"game_pid\":"+std::to_string(pid)+",\"path\":"+quote(utf8(entry.szExePath))+",\"sha256\":"+quote(hash.data()));
    }while(Module32NextW(snapshot.h,&entry));
}
}
int wmain(int argc,wchar_t** argv) {
    // Player: --play game-root payload-dir run-dir (normal Windows account).
    // Developer: --probe|--launch|--observe|--actors game-root payload-dir run-dir expected-sid personal-profile.
    if(argc < 5) return 2;
    PROCESS_INFORMATION pi{}; bool created=false;
    HDESK worker_desktop=nullptr;
    std::unique_ptr<sporemp::worker::Supervisor> supervisor;
    try {
        std::wstring mode=argv[1];
        const bool join = mode == L"--join";
        const bool network_worker = mode == L"--network-authority" || mode == L"--network-replica";
        const bool network_mode = join || network_worker;
        const bool replica_probe = mode == L"--replica-probe" || mode == L"--network-replica" || join;
        const bool authority_probe = mode == L"--authority-probe" || mode == L"--network-authority";
        const bool worker = mode == L"--worker" || (!join && (replica_probe || authority_probe));
        bool isolated=mode!=L"--play" && !join;
        const int base_count = network_worker ? 10 : worker ? 9 : join ? 6 : isolated ? 7 : 5;
        if (argc < base_count || (isolated && !worker && mode!=L"--probe" && mode!=L"--launch" && mode!=L"--observe" && mode!=L"--actors")) return 2;
        sporemp::worker::Generation generation{};
        std::wstring desktop_name;
        if (worker) {
            if (!sporemp::worker::generation_from_hex(argv[7], generation)) return 2;
            if (std::wstring(argv[8]) != L"private" && std::wstring(argv[8]) != L"current") return 2;
            desktop_name = L"SporeMP-M04-" + sporemp::worker::generation_hex(generation);
        }
        const auto display = sporemp::display_arguments(argc - base_count, argv + base_count);
        auto game=safe_path(argv[2]),payload=safe_path(argv[3]),run=safe_path(argv[4]);
        auto log_path=run/L"native-host.jsonl";
        Handle unique(CreateFileW(log_path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
        check(unique.h!=INVALID_HANDLE_VALUE,"Exclusive evidence file"); CloseHandle(unique.h); unique.h=INVALID_HANDLE_VALUE;
        log_stream.open(log_path,std::ios::app); if(!log_stream)return 2;
        event("guard_started",",\"mode\":"+quote(utf8(mode))+",\"launched_processes\":0");
        if (isolated) identity(argv[5],safe_path(argv[6]),worker);
        else {
            wchar_t user[256]{}; DWORD size=256;
            check(GetUserNameW(user,&size)!=0,"GetUserName");
            event("player_account",",\"user\":"+quote(utf8(user))+",\"profile_redirection\":false");
        }
        // One guarded process per actual OS profile, across launch modes/sessions.
        const auto mutex_name = L"Global\\SporeMP-Native-" + sporemp::worker::current_sid();
        Handle singleton(CreateMutexW(nullptr,TRUE,mutex_name.c_str()));
        if(!singleton.h || GetLastError()==ERROR_ALREADY_EXISTS)throw std::runtime_error("Native qualification already running");
        std::vector<Handle> locks;
        guard_content(game,locks,isolated);
        guard_files(payload,std::begin(payload_files),std::end(payload_files),locks);
        event("payload_validated");
        fs::path network_config;
        if (network_mode) {
            network_config = safe_path(argv[network_worker ? 9 : 5]);
            Handle session_lock(CreateFileW(network_config.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
            check(session_lock.h != INVALID_HANDLE_VALUE, "Lock multiplayer session configuration");
            locks.push_back(std::move(session_lock));
            sporemp::network::PeerConfig config;
            std::string config_error;
            if (!sporemp::network::read_peer_config(network_config.wstring(), config, config_error))
                throw std::runtime_error("Invalid multiplayer session configuration: " + config_error);
            sporemp::network::Identity expected;
            PWSTR roaming=nullptr;
            check(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData,0,nullptr,&roaming)), "Resolve current-account shared scene");
            const auto fixture=safe_path(fs::path(roaming)/L"Spore/Games/Game0/Satiria.spo");
            const auto world_root=safe_path(fs::path(roaming)/L"Spore");
            CoTaskMemFree(roaming);
            sporemp::Sha256 fixture_hash{};
            if(!sporemp::sha256_file(fixture.c_str(),fixture_hash) ||
               !sporemp::network::parse_hex(fixture_hash.data(),expected.fixture))
                throw std::runtime_error("The shared Satiria Creature scene is missing from this account.");
            std::string world_error;
            if(!sporemp::network::read_world_identity(world_root.wstring(),expected.world,world_error))
                throw std::runtime_error(world_error);
            for(size_t index=0;index<sporemp::network::world_file_count;++index)
                if(config.identity.world[index]!=expected.world[index])
                    throw std::runtime_error(std::string("canonical_world_mismatch:")+sporemp::network::world_files[index].path);
            if (!sporemp::network::parse_hex(payload_files[2].sha, expected.build) ||
                !sporemp::network::parse_hex(network_executable_sha256, expected.executable) ||
                !sporemp::network::parse_hex(network_content_sha256, expected.content) || !(config.identity == expected))
                throw std::runtime_error("Multiplayer configuration does not match guarded build/content.");
            if (config.role != (authority_probe ? sporemp::network::Role::authority : sporemp::network::Role::player))
                throw std::runtime_error("Multiplayer role/configuration mismatch.");
            event("network_config_validated", ",\"role\":\"" + std::string(authority_probe ? "authority" : "player") + "\"");
            event("canonical_world_prelaunch_validated", ",\"bundle_version\":1,\"files\":6,\"runtime_terrain_qualified\":false");
        }
        if(mode==L"--probe") {event("guard_passed",",\"launched_processes\":0");return 0;}
        sporemp::worker::reject_process_in_profile(L"SporeApp.exe");
        check(SetEnvironmentVariableW(L"SPOREMP_DIAGNOSTICS_DIR",run.c_str())!=0,"Diagnostics environment");
        // Never inherit an experimental gameplay-hook switch into normal Play or the reference run.
        check(SetEnvironmentVariableW(L"SPOREMP_M02_TRACE",mode==L"--observe" ? L"observe" : nullptr)!=0,"Observation environment");
        check(SetEnvironmentVariableW(L"SPOREMP_M03_ACTORS",mode==L"--actors" || worker || join ? L"harness" : nullptr)!=0,"Actor harness environment");
        check(SetEnvironmentVariableW(L"SPOREMP_M05_ROLE",replica_probe ? L"replica" : authority_probe ? L"authority" : nullptr)!=0,"Replica role environment");
        std::wstring local_generation;
        if (join) {
            check(BCryptGenRandom(nullptr, generation.data(), static_cast<ULONG>(generation.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0, "Session generation");
            local_generation = sporemp::worker::generation_hex(generation);
        }
        check(SetEnvironmentVariableW(L"SPOREMP_M04_GENERATION",worker ? argv[7] : join ? local_generation.c_str() : nullptr)!=0,"Worker generation environment");
        check(SetEnvironmentVariableW(L"SPOREMP_M06_CONFIG",network_mode ? network_config.c_str() : nullptr)!=0,"Network configuration environment");
        const auto supervisor_pid = std::to_wstring(GetCurrentProcessId());
        check(SetEnvironmentVariableW(L"SPOREMP_M04_SUPERVISOR",worker ? supervisor_pid.c_str() : nullptr)!=0,"Worker supervisor environment");
        auto exe=(game/L"SporebinEP1/SporeApp.exe").wstring(); std::wstring command=L"\""+exe+L"\""+display;
        // Pinned EAMain 0xF48810 calls CommandLine::FindSwitch("multipleInstances")
        // before the original Global\SporeEditorOrGame mutex. Worker profiles are
        // still fenced separately; this is not a profile-isolation mechanism.
        if (worker || join) command += L" -multipleInstances";
        event("display_options",",\"arguments\":"+quote(utf8(display)));
        STARTUPINFOW si{};si.cb=sizeof(si);
        std::wstring desktop_path;
        if (worker) {
            // The network role/configuration has already passed guarded identity
            // validation. Replicas suppress original AI by design; their
            // readiness uses live app progress and an applied network baseline.
            const auto progress_role = mode == L"--network-replica"
                ? sporemp::worker::ProgressRole::network_replica
                : sporemp::worker::ProgressRole::native_simulation;
            supervisor = std::make_unique<sporemp::worker::Supervisor>(generation,run,event,
                [] { return GetTickCount64(); },progress_role);
            if (std::wstring(argv[8]) == L"private") {
                worker_desktop=CreateDesktopW(desktop_name.c_str(),nullptr,nullptr,0,DESKTOP_CREATEWINDOW|DESKTOP_READOBJECTS|DESKTOP_WRITEOBJECTS|DESKTOP_ENUMERATE,nullptr);
                check(worker_desktop!=nullptr,"Create private rendered worker desktop");
                desktop_path=L"WinSta0\\"+desktop_name; si.lpDesktop=desktop_path.data();
                event("worker_desktop_created",",\"switched_input_desktop\":false,\"desktop\":"+quote(utf8(desktop_name)));
            } else {
                event("worker_current_desktop",",\"rendered\":true,\"multiple_instances\":true");
            }
        }
        // Explicit executable and working directory; only allowlisted documented display flags.
        check(CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED,nullptr,(game/L"SporebinEP1").c_str(),&si,&pi)!=0,"CreateProcessW suspended");
        created=true; event("created_suspended",",\"game_pid\":"+std::to_string(pi.dwProcessId));
        if (supervisor) supervisor->attach(pi.hProcess,pi.dwProcessId);
        inject(pi,payload);
        check(ResumeThread(pi.hThread)==1,"ResumeThread");
        event("resumed",",\"game_pid\":"+std::to_string(pi.dwProcessId));
        bool captured=false; DWORD waited=0;
        while(WaitForSingleObject(pi.hProcess,worker?20:1000)==WAIT_TIMEOUT) {
            if (supervisor) supervisor->tick();
            ++waited;
            if(!captured && fs::exists(run/(L"bridge-"+std::to_wstring(pi.dwProcessId)+L".jsonl"))) {modules(pi.dwProcessId);captured=true;}
            if(waited%(worker?1500:30)==0) event("waiting_for_normal_exit",",\"game_pid\":"+std::to_string(pi.dwProcessId));
        }
        DWORD exit_code=0;check(GetExitCodeProcess(pi.hProcess,&exit_code)!=0,"GetExitCodeProcess");
        event("game_exited",",\"game_pid\":"+std::to_string(pi.dwProcessId)+",\"exit_code\":"+std::to_string(exit_code));
        if (supervisor) supervisor->exited(exit_code);
        CloseHandle(pi.hThread);CloseHandle(pi.hProcess);created=false;
        supervisor.reset();
        if(worker_desktop) {CloseDesktop(worker_desktop);worker_desktop=nullptr;}
        return exit_code==0 ? 0 : 31;
    } catch(const std::exception& error) {
        event("rejected",",\"reason\":"+quote(error.what())+",\"launched_processes\":"+std::string(created?"1":"0"));
        if(created) {TerminateProcess(pi.hProcess,32);WaitForSingleObject(pi.hProcess,5000);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);}
        supervisor.reset();
        if(worker_desktop) CloseDesktop(worker_desktop);
        return created?32:20;
    }
}
