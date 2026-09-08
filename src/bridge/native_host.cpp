// Windows-only host: no SDK import and no game code executes before the guard.
// Injector ABI/order: pinned ModAPI-Launcher-Kit Injector.cs and DLLInjector/dllmain.cpp.
#include "diagnostics.h"
#include "native_identity.h"
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
std::wstring identity(const std::wstring& expected_sid, const fs::path& personal) {
    HANDLE token_raw = nullptr; check(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token_raw)!=0,"OpenProcessToken"); Handle token(token_raw);
    DWORD size=0; GetTokenInformation(token.h,TokenUser,nullptr,0,&size); std::vector<BYTE> bytes(size);
    check(GetTokenInformation(token.h,TokenUser,bytes.data(),size,&size)!=0,"TokenUser");
    LPWSTR sid=nullptr; check(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid,&sid)!=0,"Token SID");
    std::wstring actual_sid(sid); LocalFree(sid);
    wchar_t user[256]{}; DWORD user_size=256; check(GetUserNameW(user,&user_size)!=0,"GetUserName");
    if (actual_sid != expected_sid || wcscmp(user,L"SporeMP-M01") != 0 || IsUserAnAdmin()) throw std::runtime_error("Dedicated standard account required");
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
            else if(entry.is_regular_file()) actual_files.insert(relative);
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
    // Developer: --probe|--launch game-root payload-dir run-dir expected-sid personal-profile.
    if(argc!=5 && argc!=7) return 2;
    PROCESS_INFORMATION pi{}; bool created=false;
    try {
        std::wstring mode=argv[1];
        bool isolated=mode!=L"--play";
        if ((isolated && (argc!=7 || (mode!=L"--probe" && mode!=L"--launch"))) || (!isolated && argc!=5)) return 2;
        auto game=safe_path(argv[2]),payload=safe_path(argv[3]),run=safe_path(argv[4]);
        auto log_path=run/L"native-host.jsonl";
        Handle unique(CreateFileW(log_path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
        check(unique.h!=INVALID_HANDLE_VALUE,"Exclusive evidence file"); CloseHandle(unique.h); unique.h=INVALID_HANDLE_VALUE;
        log_stream.open(log_path,std::ios::app); if(!log_stream)return 2;
        event("guard_started",",\"mode\":"+quote(utf8(mode))+",\"launched_processes\":0");
        if (isolated) identity(argv[5],safe_path(argv[6]));
        else {
            wchar_t user[256]{}; DWORD size=256;
            check(GetUserNameW(user,&size)!=0,"GetUserName");
            event("player_account",",\"user\":"+quote(utf8(user))+",\"profile_redirection\":false");
        }
        Handle singleton(CreateMutexW(nullptr,TRUE,L"Local\\SporeMP-M01-Native"));
        if(!singleton.h || GetLastError()==ERROR_ALREADY_EXISTS)throw std::runtime_error("Native qualification already running");
        std::vector<Handle> locks;
        guard_content(game,locks,isolated);
        guard_files(payload,std::begin(payload_files),std::end(payload_files),locks);
        event("payload_validated");
        if(mode==L"--probe") {event("guard_passed",",\"launched_processes\":0");return 0;}
        check(SetEnvironmentVariableW(L"SPOREMP_DIAGNOSTICS_DIR",run.c_str())!=0,"Diagnostics environment");
        auto exe=(game/L"SporebinEP1/SporeApp.exe").wstring(); std::wstring command=L"\""+exe+L"\"";
        STARTUPINFOW si{};si.cb=sizeof(si);
        // Explicit application path, original documented working directory, no guessed game flags.
        check(CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED,nullptr,(game/L"SporebinEP1").c_str(),&si,&pi)!=0,"CreateProcessW suspended");
        created=true; event("created_suspended",",\"game_pid\":"+std::to_string(pi.dwProcessId));
        inject(pi,payload);
        check(ResumeThread(pi.hThread)==1,"ResumeThread");
        event("resumed",",\"game_pid\":"+std::to_string(pi.dwProcessId));
        bool captured=false; DWORD waited=0;
        while(WaitForSingleObject(pi.hProcess,1000)==WAIT_TIMEOUT) {
            ++waited;
            if(!captured && fs::exists(run/(L"bridge-"+std::to_wstring(pi.dwProcessId)+L".jsonl"))) {modules(pi.dwProcessId);captured=true;}
            if(waited%30==0) event("waiting_for_normal_exit",",\"game_pid\":"+std::to_string(pi.dwProcessId));
        }
        DWORD exit_code=0;check(GetExitCodeProcess(pi.hProcess,&exit_code)!=0,"GetExitCodeProcess");
        event("game_exited",",\"game_pid\":"+std::to_string(pi.dwProcessId)+",\"exit_code\":"+std::to_string(exit_code));
        CloseHandle(pi.hThread);CloseHandle(pi.hProcess);created=false;
        return exit_code==0 ? 0 : 31;
    } catch(const std::exception& error) {
        event("rejected",",\"reason\":"+quote(error.what())+",\"launched_processes\":"+std::string(created?"1":"0"));
        if(created) {TerminateProcess(pi.hProcess,32);WaitForSingleObject(pi.hProcess,5000);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);}
        return created?32:20;
    }
}
