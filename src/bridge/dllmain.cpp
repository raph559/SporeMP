#include <Spore/ModAPI.h>
#include <Spore/Resource/Paths.h>
#include "diagnostics.h"
#include "native_observation.h"
#include "native_actors.h"
#include "native_worker.h"
#include "native_replica.h"
#include "native_network.h"

namespace {
sporemp::DiagnosticSession session;

void Initialize() {
    wchar_t executable[32768]{};
    wchar_t directory[32768]{};
    const DWORD exe_length = GetModuleFileNameW(nullptr, executable, _countof(executable));
    const DWORD dir_length = GetEnvironmentVariableW(L"SPOREMP_DIAGNOSTICS_DIR", directory, _countof(directory));
    if (exe_length == 0 || exe_length >= _countof(executable) ||
        dir_length == 0 || dir_length >= _countof(directory)) {
        OutputDebugStringW(L"SporeMP: diagnostics disabled; missing/invalid diagnostic directory.\n");
        return;
    }
    const sporemp::RuntimeIdentity runtime{ModAPI::GetMajorVersion(), ModAPI::GetMinorVersion(),
        ModAPI::GetBuildVersion(), static_cast<int>(ModAPI::GetGameType())};
    if (!session.initialize(executable, directory, runtime)) {
        OutputDebugStringW(L"SporeMP: diagnostic initialization rejected; inspect external preflight.\n");
        return;
    }
    // Read-only, after verified post-init. Pinned Resource/Paths.h and
    // AddressesResource.cpp bind GetDirFromID; no path redirection is attempted.
    for (auto id : {Resource::PathID::App, Resource::PathID::Data, Resource::PathID::Config,
                   Resource::PathID::AppData, Resource::PathID::Creations, Resource::PathID::Temp,
                   Resource::PathID::Locale, Resource::PathID::BaseData, Resource::PathID::BaseDataLocale}) {
        const char16_t* path = Resource::Paths::GetDirFromID(id);
        static_assert(sizeof(char16_t) == sizeof(wchar_t));
        session.native_path(static_cast<unsigned int>(id), reinterpret_cast<const wchar_t*>(path));
    }
    sporemp::initialize_native_observation(directory);
    sporemp::initialize_native_actors(directory);
    sporemp::initialize_native_worker();
    // Initializes a normal-account network client's verified loader before
    // replica detours patch the checked entry prefixes. No AppUpdate runs here.
    sporemp::initialize_native_network(directory);
    // M04 verifies the untouched Save entry before M05 attaches its guard.
    // Both finish synchronously inside the same verified post-init callback.
    sporemp::initialize_native_replica();
}

void Dispose() { sporemp::dispose_native_network(); sporemp::dispose_native_worker(); sporemp::dispose_native_replica(); sporemp::dispose_native_actors(); sporemp::dispose_native_observation(); session.dispose(); }
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        // Pinned SDK registration pattern; no SporeMP detours, I/O, networking or joins here.
        // The SDK core itself hooks during load. External preflight MUST precede its loading.
        ModAPI::AddPostInitFunction(Initialize);
        ModAPI::AddDisposeFunction(Dispose);
    }
    return TRUE;
}
