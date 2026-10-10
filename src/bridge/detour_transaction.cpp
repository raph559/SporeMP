#include "detour_transaction.h"
#include <detours.h>
#include <tlhelp32.h>
#include <array>

namespace sporemp {
namespace {
// The most threads of this process one transaction enrolls; one more fails it with ERROR_TOO_MANY_TCBS.
using ThreadHandles = std::array<HANDLE, 1024>;

bool hooks_valid(const NativeHook* hooks, size_t count) noexcept {
    if (!hooks || count == 0) {
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        if (!hooks[i].original || !*hooks[i].original || !hooks[i].replacement) {
            return false;
        }
    }
    return true;
}

// Enrolls every other thread of this process in the open transaction. The handles it opened are left in
// threads for the caller to close once the transaction has ended, on success and on failure alike.
LONG enroll_other_threads(ThreadHandles& threads, size_t& opened) noexcept {
    LONG status = NO_ERROR;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        status = static_cast<LONG>(GetLastError());
    }
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    if (status == NO_ERROR && !Thread32First(snapshot, &entry)) {
        status = static_cast<LONG>(GetLastError());
    }
    if (status == NO_ERROR) {
        do {
            if (entry.th32OwnerProcessID != GetCurrentProcessId() || entry.th32ThreadID == GetCurrentThreadId()) {
                continue;
            }
            if (opened == threads.size()) {
                status = ERROR_TOO_MANY_TCBS;
                break;
            }
            HANDLE thread =
                OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_QUERY_INFORMATION,
                           FALSE, entry.th32ThreadID);
            if (!thread) {
                DWORD error = GetLastError();
                if (error == ERROR_INVALID_PARAMETER) {
                    continue; // Thread exited after the snapshot.
                }
                status = static_cast<LONG>(error);
                break;
            }
            threads[opened++] = thread;
            status = DetourUpdateThread(thread);
            if (status != NO_ERROR) {
                break;
            }
        } while (Thread32Next(snapshot, &entry));
    }
    if (snapshot != INVALID_HANDLE_VALUE) {
        CloseHandle(snapshot);
    }
    return status;
}
} // namespace

LONG change_hooks(const NativeHook* hooks, size_t count, bool attach) noexcept {
    if (!hooks_valid(hooks, count)) {
        return ERROR_INVALID_PARAMETER;
    }
    LONG status = DetourTransactionBegin();
    if (status != NO_ERROR) {
        return status;
    }
    ThreadHandles threads{};
    size_t opened = 0;
    status = enroll_other_threads(threads, opened);
    if (status == NO_ERROR) {
        status = DetourUpdateThread(GetCurrentThread());
    }
    for (size_t i = 0; status == NO_ERROR && i < count; ++i) {
        status = attach ? DetourAttach(hooks[i].original, hooks[i].replacement)
                        : DetourDetach(hooks[i].original, hooks[i].replacement);
    }
    if (status == NO_ERROR) {
        status = DetourTransactionCommit();
    } else {
        DetourTransactionAbort();
    }
    for (size_t i = 0; i < opened; ++i) {
        CloseHandle(threads[i]);
    }
    return status;
}
} // namespace sporemp
