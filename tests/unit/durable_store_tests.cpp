#include "../../src/network/durable_store.h"
#include "../../src/network/protocol.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace sporemp::network;

namespace {
unsigned checks = 0;

void check(bool ok, const char* message) {
    ++checks;
    if (!ok) {
        throw std::runtime_error(message);
    }
}

std::vector<uint8_t> before{1, 2, 3, 4}, after{7, 8, 9, 10, 11};

std::filesystem::path directory(const std::filesystem::path& root, const char* name) {
    auto path = root / name;
    check(std::filesystem::create_directory(path), "fresh test directory");
    return path;
}

void initialize(const std::filesystem::path& path) {
    DurableStore s;
    std::vector<uint8_t> bytes;
    std::string e;
    check(s.open(path, bytes, e) && bytes.empty() && s.revision() == 0,
          "empty store opens without fabricated durable state");
    check(s.commit(before, e) && s.revision() == 1, "first record flushed and acknowledged");
}

void crash(const std::filesystem::path& path, const char* boundary) {
    wchar_t executable[32768]{};
    check(GetModuleFileNameW(nullptr, executable, 32768) != 0, "test executable identity");
    std::wstring cmd = L"\"" + std::wstring(executable) + L"\" --crash \"" + path.wstring() + L"\" ";
    for (auto p = boundary; *p; ++p) {
        cmd += wchar_t(*p);
    }
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    check(CreateProcessW(executable, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si,
                         &pi) != FALSE,
          "spawn crash-injection process");
    CloseHandle(pi.hThread);
    DWORD wait = WaitForSingleObject(pi.hProcess, 15000), exit = 0;
    if (wait != WAIT_OBJECT_0) {
        TerminateProcess(pi.hProcess, 92);
    }
    GetExitCodeProcess(pi.hProcess, &exit);
    CloseHandle(pi.hProcess);
    check(wait == WAIT_OBJECT_0 && exit == 91, "child terminated abruptly at requested store boundary");
}

void tests(const std::filesystem::path& root) {
    for (const auto* boundary : {"before_record_write", "after_record_flush", "after_record_publish",
                                 "after_head_flush", "after_head_publish"}) {
        auto path = directory(root, boundary);
        initialize(path);
        crash(path, boundary);
        DurableStore recovered;
        std::vector<uint8_t> bytes;
        std::string e;
        check(recovered.open(path, bytes, e), "reopen store after abrupt process termination");
        const bool committed = std::string(boundary) == "after_head_publish";
        check(bytes == (committed ? after : before) && recovered.revision() == (committed ? 2u : 1u),
              "only atomic publication chooses recovery; pending files never override last good");
        // Also covers an identical sealed orphan from the terminated writer.
        check(recovered.commit(after, e), "recovery can commit after ignoring incomplete attempt");
    }
    auto locked = directory(root, "lock");
    initialize(locked);
    {
        DurableStore one, two;
        std::vector<uint8_t> bytes;
        std::string e;
        check(one.open(locked, bytes, e), "first writer obtains exclusive lock");
        check(!two.open(locked, bytes, e) && e == "store_locked_or_unsafe", "second writer cannot create split brain");
        check(!one.commit(std::vector<uint8_t>(DurableStore::max_bytes + 1), e) && one.revision() == 1,
              "oversized state cannot change acknowledged revision");
    }
    auto corrupt = directory(root, "corrupt");
    initialize(corrupt);
    Digest id{};
    {
        DurableStore s;
        std::vector<uint8_t> bytes;
        std::string e;
        check(s.open(corrupt, bytes, e), "corruption fixture opened");
        id = s.identity();
    }
    {
        std::ofstream file(corrupt / ("state-" + hex(id) + ".bin"), std::ios::binary | std::ios::app);
        file.put('x');
    }
    {
        DurableStore s;
        std::vector<uint8_t> bytes;
        std::string e;
        check(!s.open(corrupt, bytes, e) && e == "store_record_hash",
              "corrupt acknowledged record fails closed instead of silent rollback");
    }
    auto schema = directory(root, "schema");
    initialize(schema);
    {
        std::fstream file(schema / L"CURRENT", std::ios::binary | std::ios::in | std::ios::out);
        file.seekp(4);
        file.put(2);
    }
    {
        DurableStore s;
        std::vector<uint8_t> bytes;
        std::string e;
        check(!s.open(schema, bytes, e) && e == "store_head_schema", "unknown schema requires explicit migration");
    }
    auto linked = directory(root, "hardlink");
    initialize(linked);
    check(CreateHardLinkW((linked / L"head-alias").c_str(), (linked / L"CURRENT").c_str(), nullptr) != FALSE,
          "hardlink adversarial fixture");
    {
        DurableStore s;
        std::vector<uint8_t> bytes;
        std::string e;
        check(!s.open(linked, bytes, e), "multi-link head refused");
    }
    auto missing = directory(root, "missing-head");
    initialize(missing);
    check(DeleteFileW((missing / L"CURRENT").c_str()) != FALSE, "remove only the generated test marker");
    {
        DurableStore s;
        std::vector<uint8_t> bytes;
        std::string e;
        check(!s.open(missing, bytes, e) && e == "store_missing_head_with_records",
              "missing committed head must not silently reset universe");
    }
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc == 4 && std::wstring(argv[1]) == L"--crash") {
        DurableStore s;
        std::vector<uint8_t> bytes;
        std::string error;
        if (!s.open(argv[2], bytes, error)) {
            return 93;
        }
        std::wstring wanted = argv[3];
        bool ok = s.commit(after, error, [&](const char* name) {
            std::string step = name;
            if (std::wstring(step.begin(), step.end()) == wanted) {
                TerminateProcess(GetCurrentProcess(), 91);
            }
        });
        return ok ? 94 : 95;
    }
    try {
        auto root = std::filesystem::current_path() / (L"m09-store-tests-" + std::to_wstring(GetCurrentProcessId()) +
                                                       L"-" + std::to_wstring(GetTickCount64()));
        check(std::filesystem::create_directory(root), "fresh private build test workspace");
        tests(root);
        std::cout << "PASS " << checks
                  << " HOST durable-store checks, including 5 real abrupt child-process terminations; SPORE NOT RUN.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}
