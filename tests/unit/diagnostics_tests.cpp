#include "diagnostics.h"
#include <cstdio>
#include <cstring>

// Runs only host OS code. Does not load ModAPI or SPORE or simulate SDK callbacks.
int main() {
    int failures = 0;
    auto check = [&](bool value, const char* name) {
        std::printf("%s: %s (HOST ONLY)\n", value ? "PASS" : "FAIL", name);
        if (!value) ++failures;
    };
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
