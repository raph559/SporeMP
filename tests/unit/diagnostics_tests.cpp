#include "diagnostics.h"
#include "native_display.h"
#include <cstdio>
#include <cstring>

// Runs only host OS code. Does not load ModAPI or SPORE or simulate SDK callbacks.
int main() {
    int failures = 0;
    auto check = [&](bool value, const char* name) {
        std::printf("%s: %s (HOST ONLY)\n", value ? "PASS" : "FAIL", name);
        if (!value) ++failures;
    };
    check(sporemp::display_arguments(0, nullptr).empty(), "no override preserves native preferences");
    const wchar_t* windowed[]{L"--display-mode", L"windowed", L"--resolution", L"1920x1080"};
    check(sporemp::display_arguments(4, windowed) == L" -w -r:1920x1080", "documented windowed startup options");
    const wchar_t* fullscreen[]{L"--display-mode", L"fullscreen", L"--resolution", L"2560x1440"};
    check(sporemp::display_arguments(4, fullscreen) == L" -f -r:2560x1440", "documented fullscreen startup options");
    for (const wchar_t* invalid : {L"1920x1080 -safe", L"1920x1080\n-w", L"8193x1080", L"640x479", L"01920x1080", L"1920x+1080", L"1920X1080"}) {
        const wchar_t* args[]{L"--display-mode", L"windowed", L"--resolution", invalid};
        bool rejected = false;
        try { sporemp::display_arguments(4, args); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected, "malformed display arguments rejected");
    }
    bool incomplete = false;
    try { sporemp::display_arguments(3, windowed); } catch (const std::invalid_argument&) { incomplete = true; }
    check(incomplete, "incomplete display options rejected");
    check(!sporemp::absolute_local_path(nullptr), "null path rejected");
    check(!sporemp::absolute_local_path(L"logs"), "relative log path rejected");
    check(!sporemp::absolute_local_path(L"C:logs"), "drive-relative path rejected");
    check(!sporemp::absolute_local_path(L"\\\\server\\logs"), "network log path rejected");
    check(sporemp::absolute_local_path(L"C:\\logs"), "absolute local path recognized");
    wchar_t temp[32768]{}, file[32768]{};
    if (!GetTempPathW(_countof(temp), temp) || !GetTempFileNameW(temp, L"smp", 0, file)) return 2;
    HANDLE handle = CreateFileW(file, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD count = 0;
    check(handle != INVALID_HANDLE_VALUE && WriteFile(handle, "abc", 3, &count, nullptr) && count == 3, "create SHA fixture");
    if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
    sporemp::Sha256 digest{};
    check(sporemp::sha256_file(file, digest), "hash actual fixture using Windows CNG");
    check(std::strcmp(digest.data(), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0,
          "SHA-256 known vector");
    check(!sporemp::matches_candidate(digest), "foreign executable digest rejected");
    sporemp::DiagnosticSession session;
    check(!session.initialize(file, temp, {2, 5, 0, 1}), "foreign image cannot initialize diagnostic session");
    session.dispose();
    session.dispose();
    DeleteFileW(file);
    check(!sporemp::sha256_file(file, digest) && digest[0] == 0, "missing file fails with cleared digest");
    std::puts("Native callbacks/load/disposal: NOT RUN; this executable never loads the SDK core.");
    return failures == 0 ? 0 : 1;
}
