#include "protocol.h"
#include "pipe.h"
#include "supervisor.h"
#include "process_guard.h"
#include <filesystem>
#include <fstream>
#include <memory>
#include <iostream>
#include <regex>
#include <stdexcept>
#include <thread>

using namespace sporemp::worker;
namespace {
int count = 0;
void check(bool condition, const char* name) {
    if (!condition) throw std::runtime_error(name);
    ++count;
}
int fixture_engine(const Generation& generation, DWORD parent, bool acknowledge_shutdown = true, bool report_failure = false) {
    Pipe channel;
    if (!channel.client(pipe_name(generation, false), parent)) return 10;
    uint64_t sequence = 0, received = 0, ticks = 0;
    const auto end = GetTickCount64() + 10000;
    bool stopping = false;
    while (GetTickCount64() < end && !channel.failed()) {
        if (channel.writable()) {
            if (stopping) return 0;
            Message value; value.generation = generation; value.sequence = ++sequence;
            value.epoch = 3; value.values[0] = static_cast<uint64_t>(Phase::scene);
            value.values[1] = ++ticks; value.values[2] = ticks * 10; // explicitly synthetic HOST progress
            if (report_failure && ticks >= 3) {
                value.values[0] = static_cast<uint64_t>(Phase::failed);
                value.values[2] = 0; // Reproduce the older failed observation status.
            }
            Message request;
            if (channel.receive(request)) {
                if (!accept_sequence(request, generation, received)) return 11;
                if (request.op != Op::shutdown || acknowledge_shutdown) {
                    value.kind = Kind::reply; value.op = request.op;
                    value.values[6] = request.sequence; value.values[7] = static_cast<uint64_t>(Result::accepted);
                    stopping = request.op == Op::shutdown;
                }
            }
            channel.send(value);
        }
        Sleep(10);
    }
    return 12;
}
struct Child {
    PROCESS_INFORMATION info{};
    ~Child() {
        if (info.hProcess) { if (WaitForSingleObject(info.hProcess, 0) == WAIT_TIMEOUT) { TerminateProcess(info.hProcess, 99); WaitForSingleObject(info.hProcess, 1000); } CloseHandle(info.hProcess); }
        if (info.hThread) CloseHandle(info.hThread);
    }
};
void spawn_fixture(Child& child, Supervisor& supervisor, const Generation& generation, bool resume = true,
    bool acknowledge_shutdown = true, bool report_failure = false) {
    wchar_t exe[32768]{}; GetModuleFileNameW(nullptr, exe, _countof(exe));
    auto command = L"\"" + std::wstring(exe) + (report_failure ? L"\" --engine-failure " :
        acknowledge_shutdown ? L"\" --engine " : L"\" --engine-no-shutdown-ack ") +
        generation_hex(generation) + L" " + std::to_wstring(GetCurrentProcessId());
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    check(CreateProcessW(exe, command.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child.info) != 0, "HOST process created suspended");
    supervisor.attach(child.info.hProcess, child.info.dwProcessId);
    if (resume) check(ResumeThread(child.info.hThread) == 1, "contained HOST process resumed");
}
Message request(Supervisor& first, Supervisor& second, const Generation& generation, Op op) {
    Pipe controller;
    check(controller.client(pipe_name(generation, true), GetCurrentProcessId()), "separate operator connection authenticated");
    Message message; message.kind = Kind::command; message.op = op; message.generation = generation; message.sequence = 1; message.epoch = 3;
    check(controller.send(message), "operator request sent");
    const auto until = GetTickCount64() + 2000; Message reply;
    do {
        first.tick(); second.tick();
        if (controller.receive(reply)) return reply;
        Sleep(5);
    } while (GetTickCount64() < until);
    throw std::runtime_error("Supervisor did not return bounded operator response");
}
void supervision() {
    const auto nonce = std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    const auto root = std::filesystem::temp_directory_path() / (L"SporeMP-M04-host-" + nonce);
    check(std::filesystem::create_directory(root), "fresh HOST evidence directory");
    std::filesystem::create_directory(root / L"one"); std::filesystem::create_directory(root / L"two");
    std::filesystem::create_directory(root / L"orphan");
    std::filesystem::create_directory(root / L"forced");
    std::filesystem::create_directory(root / L"shutdown-timeout");
    std::filesystem::create_directory(root / L"engine-failure");
    Generation one{}, two{};
    generation_from_hex(L"10101010101010101010101010101010", one);
    generation_from_hex(L"20202020202020202020202020202020", two);
    // Names are per-test PID to permit independent test invocations.
    for (size_t i = 0; i < 4; ++i) { one[i] = two[i] = static_cast<uint8_t>(GetCurrentProcessId() >> (i * 8)); }
    std::vector<std::string> events;
    auto log = [&](const char* event, const std::string&) { events.emplace_back(event); };
    {
        Supervisor first(one, root / L"one", log), second(two, root / L"two", log);
        Child child1, child2;
        spawn_fixture(child1, first, one); spawn_fixture(child2, second, two);
        wchar_t own_exe[32768]{}; GetModuleFileNameW(nullptr, own_exe, _countof(own_exe));
        bool refused = false;
        try { reject_process_in_profile(std::filesystem::path(own_exe).filename().wstring(), GetCurrentProcessId()); }
        catch (const std::runtime_error&) { refused = true; }
        check(refused, "actual process sharing OS profile is rejected before launch");
        const auto until = GetTickCount64() + 150;
        while (GetTickCount64() < until) { first.tick(); second.tick(); Sleep(2); }
        auto status = request(first, second, one, Op::status);
        check(status.values[2] > 0 && status.epoch == 3, "supervisor receives HOST progress");
        for (unsigned i = 0; i < 10; ++i) { first.tick(); second.tick(); Sleep(2); }
        check(WaitForSingleObject(child1.info.hProcess, 0) == WAIT_TIMEOUT && WaitForSingleObject(child2.info.hProcess, 0) == WAIT_TIMEOUT, "closing controller leaves both HOST workers alive");
        TerminateProcess(child1.info.hProcess, 77); WaitForSingleObject(child1.info.hProcess, 1000); first.exited(77);
        std::ifstream first_status(root / L"one/worker-status.json");
        std::string content((std::istreambuf_iterator<char>(first_status)), {});
        check(content.find("\"state\":\"crashed\"") != std::string::npos, "actual child crash recorded");
        // Do not tick an exited supervisor; only its independent surviving peer.
        Pipe controller;
        check(controller.client(pipe_name(two, true), GetCurrentProcessId()), "peer controller remains reachable");
        Message shutdown; shutdown.kind = Kind::command; shutdown.op = Op::shutdown; shutdown.generation = two; shutdown.sequence = 1; shutdown.epoch = 3;
        check(controller.send(shutdown), "peer shutdown sent");
        const auto shutdown_end = GetTickCount64() + 2000;
        Message reply; bool acknowledged = false;
        while (WaitForSingleObject(child2.info.hProcess, 0) == WAIT_TIMEOUT && GetTickCount64() < shutdown_end) {
            second.tick(); if (controller.receive(reply)) acknowledged = true; Sleep(2);
        }
        DWORD code = 999; GetExitCodeProcess(child2.info.hProcess, &code); second.exited(code);
        if (controller.receive(reply)) acknowledged = true;
        check(code == 0 && acknowledged && reply.op == Op::shutdown, "other HOST worker retains IPC and exits on acknowledged shutdown");
    }
    {
        Generation fourth = one; fourth.back() = 4; Child stalled;
        Supervisor frozen(fourth, root / L"forced", log);
        spawn_fixture(stalled, frozen, fourth, false);
        const auto reply = request(frozen, frozen, fourth, Op::shutdown);
        check(reply.values[7] == static_cast<uint64_t>(Result::accepted), "uninitialized HOST stop acknowledged");
        check(WaitForSingleObject(stalled.info.hProcess, 1000) == WAIT_OBJECT_0, "operator stop terminates only contained stalled child");
        DWORD code = 0; GetExitCodeProcess(stalled.info.hProcess, &code); frozen.exited(code);
        std::ifstream status(root / L"forced/worker-status.json");
        std::string content((std::istreambuf_iterator<char>(status)), {});
        check(code == 35 && content.find("\"state\":\"stopped_forced\"") != std::string::npos, "forced stop is distinct from native clean exit");
    }
    {
        Generation fifth = one; fifth.back() = 5; Child unacknowledged;
        uint64_t now = GetTickCount64();
        bool sent_shutdown = false, acknowledgement_timed_out = false;
        auto timeout_log = [&](const char* event, const std::string& detail) {
            if (std::string(event) == "worker_command_sent" && detail.find("\"op\":\"shutdown\"") != std::string::npos)
                sent_shutdown = true;
            if (std::string(event) == "worker_failed" && detail.find("command_ack_timeout") != std::string::npos)
                acknowledgement_timed_out = true;
        };
        Supervisor waiting(fifth, root / L"shutdown-timeout", timeout_log, [&] { return now; });
        spawn_fixture(unacknowledged, waiting, fifth, true, false);
        const auto initialization_end = GetTickCount64() + 150;
        while (GetTickCount64() < initialization_end) { waiting.tick(); Sleep(2); }
        const auto status = request(waiting, waiting, fifth, Op::status);
        check(status.values[2] > 0, "shutdown-timeout HOST engine has live progress before request");
        for (unsigned i = 0; i < 10; ++i) { waiting.tick(); Sleep(2); }
        Pipe controller;
        check(controller.client(pipe_name(fifth, true), GetCurrentProcessId()), "shutdown-timeout controller connected");
        Message shutdown; shutdown.kind = Kind::command; shutdown.op = Op::shutdown;
        shutdown.generation = fifth; shutdown.sequence = 1; shutdown.epoch = 3;
        check(controller.send(shutdown), "shutdown-timeout request sent");
        const auto forwarding_end = GetTickCount64() + 2000;
        while (!sent_shutdown && GetTickCount64() < forwarding_end) { waiting.tick(); Sleep(2); }
        check(sent_shutdown && WaitForSingleObject(unacknowledged.info.hProcess, 0) == WAIT_TIMEOUT,
            "shutdown forwarded to independently live HOST child");
        // Only the supervisor deadline advances: the real child and pipe do not
        // synthesize an acknowledgement. This keeps the 15-second bound test fast.
        now += 15001;
        waiting.tick();
        check(acknowledgement_timed_out, "pending clean shutdown escalates after missing acknowledgement");
        check(WaitForSingleObject(unacknowledged.info.hProcess, 1000) == WAIT_OBJECT_0,
            "missing shutdown acknowledgement cannot leave the owned child running indefinitely");
        DWORD code = 0; GetExitCodeProcess(unacknowledged.info.hProcess, &code); waiting.exited(code);
        std::ifstream status_file(root / L"shutdown-timeout/worker-status.json");
        std::string content((std::istreambuf_iterator<char>(status_file)), {});
        check(code == 35 && content.find("\"state\":\"stopped_forced\"") != std::string::npos,
            "unacknowledged shutdown retains forced-stop evidence without a save claim");
    }
    {
        Generation sixth = one; sixth.back() = 6; Child failing;
        bool reported_failure=false, reported_regression=false;
        Supervisor observing(sixth,root / L"engine-failure",[&](const char* event,const std::string& detail) {
            if (std::string(event)=="worker_failed") {
                reported_failure=detail.find("engine_reported_failure")!=std::string::npos;
                reported_regression=detail.find("regressed_native_progress")!=std::string::npos;
            }
        });
        spawn_fixture(failing,observing,sixth,true,true,true);
        const auto deadline=GetTickCount64()+2000;
        while (WaitForSingleObject(failing.info.hProcess,0)==WAIT_TIMEOUT && GetTickCount64()<deadline) {
            observing.tick(); Sleep(2);
        }
        check(reported_failure && !reported_regression,"explicit engine failure is not mislabeled as regressed progress");
        DWORD code=0; GetExitCodeProcess(failing.info.hProcess,&code); observing.exited(code);
        check(code==35,"failed HOST observation remains a bounded failed job, not a clean native exit");
    }
    {
        Generation third = one; third.back() = 3; Child orphan;
        {
            Supervisor dying(third, root / L"orphan", log);
            spawn_fixture(orphan, dying, third);
        }
        check(WaitForSingleObject(orphan.info.hProcess, 1000) == WAIT_OBJECT_0, "closing supervisor job terminates its owned HOST child");
    }
    // This path was exclusively created above under the OS temp directory. It
    // contains only the HOST status files created by this test.
    for (auto name : {L"one", L"two", L"orphan", L"forced", L"shutdown-timeout", L"engine-failure"}) { std::filesystem::remove(root / name / L"worker-status.json"); std::filesystem::remove(root / name); }
    std::filesystem::remove(root);
}
void status_publication() {
    const auto root = std::filesystem::temp_directory_path() /
        (L"SporeMP-status-host-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    check(std::filesystem::create_directory(root), "fresh publication HOST directory");
    Generation generation{};
    generation_from_hex(L"50505050505050505050505050505050", generation);
    for (size_t i = 0; i < 4; ++i) generation[i] = static_cast<uint8_t>(GetCurrentProcessId() >> (i * 8));
    const auto status = root / L"worker-status.json";
    const auto read = [&] {
        std::ifstream stream(status);
        return std::string((std::istreambuf_iterator<char>(stream)), {});
    };
    // This flat-object JSON subset accepts only quoted strings and uint values;
    // full matching catches truncation, concatenation and malformed separators.
    const std::regex json(R"json(\{"[a-z_]+":("[^"\\\x00-\x1f]*"|[0-9]+)(,"[a-z_]+":("[^"\\\x00-\x1f]*"|[0-9]+))*\}\n)json");
    uint64_t now = 1000;
    unsigned retries = 0, recoveries = 0;
    std::string retry_detail;
    {
        Supervisor supervisor(generation, root, [&](const char* event, const std::string& detail) {
            if (std::string(event) == "worker_status_publication_retry") { ++retries; retry_detail = detail; }
            if (std::string(event) == "worker_status_publication_recovered") ++recoveries;
        }, [&] { return now; });
        const auto original = read();
        check(std::regex_match(original,json), "initial status is one complete valid flat JSON object");
        auto held = CreateFileW(status.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        check(held != INVALID_HANDLE_VALUE, "actual reader opens status without FILE_SHARE_DELETE");
        now += 1000;
        std::thread release([held] { Sleep(80); CloseHandle(held); });
        std::string unexpected;
        try { supervisor.tick(); } catch (const std::exception& error) { unexpected = error.what(); }
        release.join();
        check(unexpected.empty(), "transient status reader cannot abort the worker supervisor");
        auto content = read();
        check(std::regex_match(content,json) && content.find("\"uptime_ms\":1000") != std::string::npos,
            "released reader permits a complete atomic updated JSON publication");
        check(retries == 1 && recoveries == 1 && retry_detail.find("\"win32_error\":") != std::string::npos,
            "transient replacement conflict and recovery retain rare diagnostic events");

        held = CreateFileW(status.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        check(held != INVALID_HANDLE_VALUE, "persistent reader lock held for bounded failure test");
        now += 1000;
        const auto started = GetTickCount64();
        std::string failure;
        try { supervisor.tick(); } catch (const std::exception& error) { failure = error.what(); }
        const auto elapsed = GetTickCount64() - started;
        CloseHandle(held);
        check(!failure.empty() && failure.find("win32=") != std::string::npos,
            "persistent replacement conflict fails with the exact Win32 diagnostic");
        check(elapsed >= 100 && elapsed < 2000, "publication contention has a finite real-time retry bound");
        check(read() == content, "failed replacement preserves the previous atomic status bytes");
        check(retries == 2 && recoveries == 1, "permanent failure does not masquerade as recovered publication");
        // Do not advance the synthetic clock: failure must not update last_publish.
        supervisor.tick();
        content = read();
        check(std::regex_match(content,json) && content.find("\"uptime_ms\":2000") != std::string::npos,
            "failed publication leaves its cadence eligible for immediate recovery");
    }
    std::filesystem::remove(root / L"worker-status.tmp");
    std::filesystem::remove(status);
    std::filesystem::remove(root);
}
void role_readiness() {
    // Real authenticated local pipes, with an injected clock. Projection state
    // comes from status frames, never an old connected-status file.
    // No original game or native AI is executed by this fixture.
    const auto root = std::filesystem::temp_directory_path() /
        (L"SporeMP-readiness-host-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    check(std::filesystem::create_directory(root), "fresh readiness HOST directory");
    for (auto role : {ProgressRole::native_simulation, ProgressRole::network_replica}) {
        const auto run = root / (role == ProgressRole::network_replica ? L"replica" : L"authority");
        std::filesystem::create_directory(run);
        Generation generation{};
        generation_from_hex(L"60606060606060606060606060606060", generation);
        for (size_t i = 0; i < 4; ++i) generation[i] = static_cast<uint8_t>(GetCurrentProcessId() >> (i * 8));
        generation.back() = role == ProgressRole::network_replica ? 61 : 60;
        uint64_t now = 1000, sequence = 0;
        {
            Supervisor supervisor(generation, run, [](const char*, const std::string&) {}, [&] { return now; }, role);
            Pipe engine;
            check(engine.client(pipe_name(generation, false), GetCurrentProcessId()), "readiness engine verifies supervisor PID");
            const auto pump = [&] {
                for (int i = 0; i < 25; ++i) { supervisor.tick(); Sleep(2); }
            };
            pump();
            const auto state = [&] {
                std::ifstream input(run / L"worker-status.json");
                const std::string text((std::istreambuf_iterator<char>(input)), {});
                const auto begin = text.find("\"state\":\"");
                if (begin == std::string::npos) return std::string{};
                const auto value = begin + 9;
                return text.substr(value, text.find('"', value) - value);
            };
            const auto send = [&](Message value, uint64_t advance) {
                now += advance;
                value.generation = generation; value.sequence = ++sequence;
                check(engine.writable() && engine.send(value), "readiness HOST sends monotonic engine observation");
                pump(); now += 1000; supervisor.tick();
                return state();
            };
            const auto feed = [&](uint64_t app, uint64_t ai, ProjectionState projection = ProjectionState::connected,
                uint64_t baseline = 7, uint64_t advance = 1000) {
                Message value;
                value.epoch = 3; value.values[0] = static_cast<uint64_t>(Phase::scene);
                value.values[1] = app; value.values[2] = ai;
                value.values[6] = static_cast<uint64_t>(projection); value.values[7] = baseline;
                return send(value, advance);
            };
            const auto reply = [&](uint64_t app, uint64_t advance) {
                // A real operator command produces a correlated engine reply.
                // Request 2 / accepted 1 deliberately resembles connected / 1.
                Pipe controller;
                check(controller.client(pipe_name(generation, true), GetCurrentProcessId()),
                    "readiness operator verifies supervisor PID");
                Message request; request.kind = Kind::command; request.op = Op::move;
                request.generation = generation; request.sequence = 1; request.epoch = 3;
                check(controller.send(request), "readiness operator sends bounded HOST command");
                Message forwarded; bool received = false;
                const auto until = GetTickCount64() + 1000;
                do { supervisor.tick(); received = engine.receive(forwarded); if (!received) Sleep(2); }
                while (!received && GetTickCount64() < until);
                check(received && forwarded.op == Op::move, "readiness command is actually forwarded to engine fixture");
                Message value; value.kind = Kind::reply; value.op = forwarded.op; value.epoch = 3;
                value.values[0] = static_cast<uint64_t>(Phase::scene); value.values[1] = app; value.values[2] = 163;
                value.values[6] = forwarded.sequence; value.values[7] = static_cast<uint64_t>(Result::accepted);
                const auto observed = send(value, advance);
                Message acknowledged;
                check(controller.receive(acknowledged) && acknowledged.kind == Kind::reply &&
                    acknowledged.values[6] == forwarded.sequence &&
                    acknowledged.values[7] == static_cast<uint64_t>(Result::accepted),
                    "readiness HOST command reply remains normally correlated and accepted");
                controller.close(); pump();
                return observed;
            };
            check(feed(1, 163) == "ready", "fresh role-specific engine and projection evidence is ready");
            const auto local_status = request(supervisor, supervisor, generation, Op::status);
            check(local_status.values[6] == 0 && local_status.values[7] == static_cast<uint64_t>(Result::accepted),
                "local status reply clears projection word two instead of inventing an engine request ID");
            pump();
            if (role == ProgressRole::native_simulation) {
                check(feed(2, 163, ProjectionState::connected, 7, 4000) == "simulation_stalled",
                    "authority with app heartbeat but stopped native AI remains stalled despite projection annotation");
                check(feed(3, 164, static_cast<ProjectionState>(99), 999) == "ready",
                    "authority readiness depends on original AI and ignores replica annotation words");
            } else {
                check(feed(2, 163, ProjectionState::connected, 7, 4000) == "ready",
                    "verified replica with live app and established projection remains ready while native AI is suppressed");
                {
                    std::ofstream stale(run / L"network-status.json");
                    stale << "{\"state\":\"connected\",\"player_id\":2,\"baseline_sequence\":7,\"detail\":\"old\"}\n";
                }
                check(feed(3, 163, ProjectionState::waiting, 0) == "replica_waiting",
                    "new baseline waiting status clears connected readiness despite stale connected file");
                check(reply(4, 1000) == "replica_waiting", "first command reply cannot establish projection readiness");
                check(reply(5, 1000) == "replica_waiting",
                    "request two and accepted one reply cannot masquerade as connected projection baseline one");
                check(feed(6, 163, ProjectionState::connected, 8) == "ready", "new applied baseline restores replica readiness");
                check(reply(7, 4000) == "replica_waiting",
                    "fresh command reply and app heartbeat cannot refresh stale projection observation");
                std::filesystem::remove(run / L"network-status.json");
                check(feed(8, 163, ProjectionState::connected, 8) == "ready", "authenticated status establishes readiness without any network status file");
                uint64_t app = 8;
                struct InvalidProjection { ProjectionState state; uint64_t baseline; };
                for (const auto& invalid : {
                    InvalidProjection{ProjectionState::connected, 0},
                    InvalidProjection{static_cast<ProjectionState>(3), 7},
                    InvalidProjection{static_cast<ProjectionState>(UINT64_MAX), 7},
                    InvalidProjection{ProjectionState::none, 7},
                    InvalidProjection{ProjectionState::waiting, 7}}) {
                    check(feed(++app, 163, invalid.state, invalid.baseline) == "replica_waiting",
                        "invalid projection enum or baseline immediately fails closed");
                }
                check(feed(++app, 163, ProjectionState::none, 0) == "replica_waiting",
                    "explicit absent projection cannot admit a network replica");
                check(feed(++app, 163, ProjectionState::connected, UINT64_MAX) == "ready",
                    "maximum nonzero applied baseline retains full scalar width");
                check(feed(app, 163, ProjectionState::connected, UINT64_MAX, 4000) == "unresponsive",
                    "fresh replica pipe messages cannot hide an app-update counter stall");
                check(feed(++app, 163) == "ready", "fresh app progress restores otherwise qualified replica readiness");
                now += 4000; supervisor.tick();
                check(state() == "unresponsive", "connected replica projection cannot hide stale engine heartbeat");
            }
        }
        std::filesystem::remove(run / L"worker-status.json");
        std::filesystem::remove(run / L"network-status.json");
        std::filesystem::remove(run);
    }
    std::filesystem::remove(root);
}
}
int main(int argc, char** argv) {
    try {
        if (argc == 4 && (std::string(argv[1]) == "--engine" || std::string(argv[1]) == "--engine-no-shutdown-ack" || std::string(argv[1]) == "--engine-failure")) {
            const std::string text = argv[2]; Generation generation{};
            if (!generation_from_hex(std::wstring(text.begin(), text.end()), generation)) return 2;
            return fixture_engine(generation, static_cast<DWORD>(std::stoul(argv[3])), std::string(argv[1]) != "--engine-no-shutdown-ack", std::string(argv[1]) == "--engine-failure");
        }
        Message original; original.kind = Kind::command; original.op = Op::jump;
        original.sequence = UINT64_MAX; original.epoch = UINT64_MAX - 1;
        check(generation_from_hex(L"0123456789abcdef0123456789abcdef", original.generation), "generation parse");
        original.values = {1, 0xfedcba9876543210, 2, 3, 4, 5, 6, 7, 8, UINT64_MAX};
        auto wire = encode(original); Message decoded;
        check(wire[0] == 'S' && wire[1] == 'M' && wire[2] == 'W' && wire[3] == '4' && wire[6] == 128, "fixed wire header");
        check(decode(wire.data(), wire.size(), decoded) && decoded.values == original.values && decoded.epoch == original.epoch && decoded.generation == original.generation, "scalar lossless roundtrip");
        auto replica_request = original; replica_request.op = Op::replica;
        const auto replica_wire = encode(replica_request);
        check(decode(replica_wire.data(), replica_wire.size(), decoded) && decoded.op == Op::replica && decoded.values == original.values,
            "M05 private probe opcode retains all ten scalar words");
        auto creation_request = original; creation_request.op = Op::inspect_creation;
        creation_request.values = {0x12345678, 0x2b978c46, 0x40626200};
        const auto creation_wire = encode(creation_request);
        check(decode(creation_wire.data(), creation_wire.size(), decoded) && valid_creation_inspection(decoded),
            "M08 private creature key roundtrip admitted without pointer or path");
        for (size_t i = 0; i < creation_request.values.size(); ++i) {
            auto invalid = creation_request; invalid.values[i] |= uint64_t(1) << 32;
            check(!valid_creation_inspection(invalid), "M08 oversized key or nonzero reserved payload rejected");
        }
        auto invalid_creation = creation_request; invalid_creation.values[0] = 0;
        check(!valid_creation_inspection(invalid_creation), "M08 zero creation identity rejected");
        invalid_creation.values[0] = UINT32_MAX;
        check(!valid_creation_inspection(invalid_creation), "M08 sentinel creation identity rejected");
        invalid_creation = creation_request; invalid_creation.values[2] = UINT32_MAX;
        check(!valid_creation_inspection(invalid_creation), "M08 sentinel creation group rejected");
        invalid_creation = creation_request; invalid_creation.values[1] = 0x00e6bce5;
        check(!valid_creation_inspection(invalid_creation), "M08 generated model is not a creature query");
        invalid_creation = creation_request; invalid_creation.kind = Kind::status;
        check(!valid_creation_inspection(invalid_creation), "M08 observation cannot masquerade as command");
        invalid_creation = creation_request; invalid_creation.op = Op::network_action;
        check(!valid_creation_inspection(invalid_creation), "M08 network action cannot become inspection");
        auto import_request = original; import_request.op = Op::import_creation;
        import_request.values = {0x12345678,UINT32_MAX,0,4,5,6,7,8,0,0};
        auto import_wire = encode(import_request);
        check(decode(import_wire.data(), import_wire.size(), decoded) && valid_creation_import(decoded), "M08 import sends an exact digest without a peer path");
        for (size_t i = 0; i < 10; ++i) {
            auto invalid = import_request; invalid.values[i] = uint64_t(1) << 32;
            check(!valid_creation_import(invalid), "M08 import rejects oversized digest words and every reserved slot");
        }
        auto invalid_import = import_request; invalid_import.values = {};
        check(!valid_creation_import(invalid_import), "M08 all-zero import digest rejected");
        invalid_import = import_request; invalid_import.kind = Kind::reply;
        check(!valid_creation_import(invalid_import), "M08 import requires command frame");
        invalid_import = import_request; invalid_import.op = Op::inspect_creation;
        check(!valid_creation_import(invalid_import), "M08 import cannot reuse an inspection opcode");
        for (size_t i = 0; i < frame_size; ++i) check(!decode(wire.data(), i, decoded), "every truncated frame rejected");
        check(!decode(wire.data(), frame_size + 1, decoded), "oversized frame rejected without read");
        auto bad = wire; bad[4] = 2; check(!decode(bad.data(), bad.size(), decoded), "unknown schema rejected");
        bad = wire; bad[12] = 255; check(!decode(bad.data(), bad.size(), decoded), "unknown opcode rejected");
        bad = wire; bad[8] = 0; check(!decode(bad.data(), bad.size(), decoded), "unknown kind rejected");
        bad = wire; for (size_t i = 16; i < 32; ++i) bad[i] = 0;
        check(!decode(bad.data(), bad.size(), decoded), "zero generation rejected");
        uint64_t last = 0; original.sequence = 1;
        check(accept_sequence(original, original.generation, last), "first sequence accepted");
        check(!accept_sequence(original, original.generation, last) && last == 1, "duplicate cannot mutate fence");
        original.sequence = 3; check(!accept_sequence(original, original.generation, last), "gap rejected");
        original.sequence = 2; auto other = original.generation; ++other[0];
        check(!accept_sequence(original, other, last), "previous worker generation rejected");
        last = UINT64_MAX; original.sequence = 0; check(!accept_sequence(original, original.generation, last), "wrap rejected");
        const auto name = L"\\\\.\\pipe\\SporeMP-HOST-test-" + std::to_wstring(GetCurrentProcessId());
        Pipe server, client;
        check(server.server(name, current_sid(), false), "real private pipe created");
        check(client.client(name, GetCurrentProcessId()), "actual server process verified");
        const auto started = GetTickCount64();
        while (!server.connected(GetCurrentProcessId()) && GetTickCount64() - started < 2000) Sleep(1);
        check(server.connected(GetCurrentProcessId()), "actual client process verified");
        original.sequence = 1; check(client.send(original), "overlapped request sent");
        bool got = false;
        while (!(got = server.receive(decoded)) && GetTickCount64() - started < 2000) Sleep(1);
        check(got && decoded.values == original.values, "real pipe transfer decoded");
        check(!server.receive(decoded) && !server.failed(), "empty pipe does not block");
        client.close();
        while (!server.failed() && GetTickCount64() - started < 2000) { server.receive(decoded); Sleep(1); }
        check(server.failed(), "peer disconnect observed");
        Pipe impersonation_server, wrong;
        const auto other_name = name + L"-identity";
        check(impersonation_server.server(other_name, current_sid(), false), "identity fixture pipe created");
        check(!wrong.client(other_name, GetCurrentProcessId() + 1) && wrong.error() == ERROR_ACCESS_DENIED, "wrong server PID rejected");
        supervision();
        status_publication();
        role_readiness();
        std::cout << count << " HOST/FIXTURE worker assertions passed. Native SPORE NOT RUN.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << " win32=" << GetLastError() << '\n'; return 1; }
}
