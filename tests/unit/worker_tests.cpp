#include "protocol.h"
#include "ui_text.h"
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
    if (!condition) {
        throw std::runtime_error(name);
    }
    ++count;
}

int fixture_engine(const Generation& generation, DWORD parent, bool acknowledge_shutdown = true,
                   bool report_failure = false) {
    Pipe channel;
    if (!channel.client(pipe_name(generation, false), parent)) {
        return 10;
    }
    uint64_t sequence = 0, received = 0, ticks = 0;
    const auto end = GetTickCount64() + 10000;
    bool stopping = false;
    while (GetTickCount64() < end && !channel.failed()) {
        if (channel.writable()) {
            if (stopping) {
                return 0;
            }
            Message value;
            value.generation = generation;
            value.sequence = ++sequence;
            value.epoch = 3;
            value.values[0] = static_cast<uint64_t>(Phase::scene);
            value.values[1] = ++ticks;
            value.values[2] = ticks * 10; // explicitly synthetic HOST progress
            if (report_failure && ticks >= 3) {
                value.values[0] = static_cast<uint64_t>(Phase::failed);
                value.values[2] = 0; // Reproduce the older failed observation status.
            }
            Message request;
            if (channel.receive(request)) {
                if (!accept_sequence(request, generation, received)) {
                    return 11;
                }
                if (request.op != Op::shutdown || acknowledge_shutdown) {
                    value.kind = Kind::reply;
                    value.op = request.op;
                    value.values[6] = request.sequence;
                    value.values[7] = static_cast<uint64_t>(Result::accepted);
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
        if (info.hProcess) {
            if (WaitForSingleObject(info.hProcess, 0) == WAIT_TIMEOUT) {
                TerminateProcess(info.hProcess, 99);
                WaitForSingleObject(info.hProcess, 1000);
            }
            CloseHandle(info.hProcess);
        }
        if (info.hThread) {
            CloseHandle(info.hThread);
        }
    }
};

void spawn_fixture(Child& child, Supervisor& supervisor, const Generation& generation, bool resume = true,
                   bool acknowledge_shutdown = true, bool report_failure = false) {
    wchar_t exe[32768]{};
    GetModuleFileNameW(nullptr, exe, _countof(exe));
    auto command = L"\"" + std::wstring(exe) +
                   (report_failure         ? L"\" --engine-failure "
                    : acknowledge_shutdown ? L"\" --engine "
                                           : L"\" --engine-no-shutdown-ack ") +
                   generation_hex(generation) + L" " + std::to_wstring(GetCurrentProcessId());
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    check(CreateProcessW(exe, command.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr,
                         nullptr, &startup, &child.info) != 0,
          "HOST process created suspended");
    supervisor.attach(child.info.hProcess, child.info.dwProcessId);
    if (resume) {
        check(ResumeThread(child.info.hThread) == 1, "contained HOST process resumed");
    }
}

Message request(Supervisor& first, Supervisor& second, const Generation& generation, Op op) {
    Pipe controller;
    check(controller.client(pipe_name(generation, true), GetCurrentProcessId()),
          "separate operator connection authenticated");
    Message message;
    message.kind = Kind::command;
    message.op = op;
    message.generation = generation;
    message.sequence = 1;
    message.epoch = 3;
    check(controller.send(message), "operator request sent");
    const auto until = GetTickCount64() + 2000;
    Message reply;
    do {
        first.tick();
        second.tick();
        if (controller.receive(reply)) {
            return reply;
        }
        Sleep(5);
    } while (GetTickCount64() < until);
    throw std::runtime_error("Supervisor did not return bounded operator response");
}

void supervision() {
    const auto nonce = std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    const auto root = std::filesystem::temp_directory_path() / (L"SporeMP-M04-host-" + nonce);
    check(std::filesystem::create_directory(root), "fresh HOST evidence directory");
    std::filesystem::create_directory(root / L"one");
    std::filesystem::create_directory(root / L"two");
    std::filesystem::create_directory(root / L"orphan");
    std::filesystem::create_directory(root / L"forced");
    std::filesystem::create_directory(root / L"shutdown-timeout");
    std::filesystem::create_directory(root / L"engine-failure");
    Generation one{}, two{};
    generation_from_hex(L"10101010101010101010101010101010", one);
    generation_from_hex(L"20202020202020202020202020202020", two);
    // Names are per-test PID to permit independent test invocations.
    for (size_t i = 0; i < 4; ++i) {
        one[i] = two[i] = static_cast<uint8_t>(GetCurrentProcessId() >> (i * 8));
    }
    std::vector<std::string> events;
    auto log = [&](const char* event, const std::string&) {
        events.emplace_back(event);
    };
    {
        Supervisor first(one, root / L"one", log), second(two, root / L"two", log);
        Child child1, child2;
        spawn_fixture(child1, first, one);
        spawn_fixture(child2, second, two);
        wchar_t own_exe[32768]{};
        GetModuleFileNameW(nullptr, own_exe, _countof(own_exe));
        bool refused = false;
        try {
            reject_process_in_profile(std::filesystem::path(own_exe).filename().wstring(), GetCurrentProcessId());
        } catch (const std::runtime_error&) {
            refused = true;
        }
        check(refused, "actual process sharing OS profile is rejected before launch");
        const auto until = GetTickCount64() + 150;
        while (GetTickCount64() < until) {
            first.tick();
            second.tick();
            Sleep(2);
        }
        auto status = request(first, second, one, Op::status);
        check(status.values[2] > 0 && status.epoch == 3, "supervisor receives HOST progress");
        for (unsigned i = 0; i < 10; ++i) {
            first.tick();
            second.tick();
            Sleep(2);
        }
        check(WaitForSingleObject(child1.info.hProcess, 0) == WAIT_TIMEOUT &&
                  WaitForSingleObject(child2.info.hProcess, 0) == WAIT_TIMEOUT,
              "closing controller leaves both HOST workers alive");
        TerminateProcess(child1.info.hProcess, 77);
        WaitForSingleObject(child1.info.hProcess, 1000);
        first.exited(77);
        std::ifstream first_status(root / L"one/worker-status.json");
        std::string content((std::istreambuf_iterator<char>(first_status)), {});
        check(content.find("\"state\":\"crashed\"") != std::string::npos, "actual child crash recorded");
        // Do not tick an exited supervisor; only its independent surviving peer.
        Pipe controller;
        check(controller.client(pipe_name(two, true), GetCurrentProcessId()), "peer controller remains reachable");
        Message shutdown;
        shutdown.kind = Kind::command;
        shutdown.op = Op::shutdown;
        shutdown.generation = two;
        shutdown.sequence = 1;
        shutdown.epoch = 3;
        check(controller.send(shutdown), "peer shutdown sent");
        const auto shutdown_end = GetTickCount64() + 2000;
        Message reply;
        bool acknowledged = false;
        while (WaitForSingleObject(child2.info.hProcess, 0) == WAIT_TIMEOUT && GetTickCount64() < shutdown_end) {
            second.tick();
            if (controller.receive(reply)) {
                acknowledged = true;
            }
            Sleep(2);
        }
        DWORD code = 999;
        GetExitCodeProcess(child2.info.hProcess, &code);
        second.exited(code);
        if (controller.receive(reply)) {
            acknowledged = true;
        }
        check(code == 0 && acknowledged && reply.op == Op::shutdown,
              "other HOST worker retains IPC and exits on acknowledged shutdown");
    }
    {
        Generation fourth = one;
        fourth.back() = 4;
        Child stalled;
        Supervisor frozen(fourth, root / L"forced", log);
        spawn_fixture(stalled, frozen, fourth, false);
        const auto reply = request(frozen, frozen, fourth, Op::shutdown);
        check(reply.values[7] == static_cast<uint64_t>(Result::accepted), "uninitialized HOST stop acknowledged");
        check(WaitForSingleObject(stalled.info.hProcess, 1000) == WAIT_OBJECT_0,
              "operator stop terminates only contained stalled child");
        DWORD code = 0;
        GetExitCodeProcess(stalled.info.hProcess, &code);
        frozen.exited(code);
        std::ifstream status(root / L"forced/worker-status.json");
        std::string content((std::istreambuf_iterator<char>(status)), {});
        check(code == 35 && content.find("\"state\":\"stopped_forced\"") != std::string::npos,
              "forced stop is distinct from native clean exit");
    }
    {
        Generation fifth = one;
        fifth.back() = 5;
        Child unacknowledged;
        uint64_t now = GetTickCount64();
        bool sent_shutdown = false, acknowledgement_timed_out = false;
        auto timeout_log = [&](const char* event, const std::string& detail) {
            if (std::string(event) == "worker_command_sent" &&
                detail.find("\"op\":\"shutdown\"") != std::string::npos) {
                sent_shutdown = true;
            }
            if (std::string(event) == "worker_failed" && detail.find("command_ack_timeout") != std::string::npos) {
                acknowledgement_timed_out = true;
            }
        };
        Supervisor waiting(fifth, root / L"shutdown-timeout", timeout_log, [&] { return now; });
        spawn_fixture(unacknowledged, waiting, fifth, true, false);
        const auto initialization_end = GetTickCount64() + 150;
        while (GetTickCount64() < initialization_end) {
            waiting.tick();
            Sleep(2);
        }
        const auto status = request(waiting, waiting, fifth, Op::status);
        check(status.values[2] > 0, "shutdown-timeout HOST engine has live progress before request");
        for (unsigned i = 0; i < 10; ++i) {
            waiting.tick();
            Sleep(2);
        }
        Pipe controller;
        check(controller.client(pipe_name(fifth, true), GetCurrentProcessId()),
              "shutdown-timeout controller connected");
        Message shutdown;
        shutdown.kind = Kind::command;
        shutdown.op = Op::shutdown;
        shutdown.generation = fifth;
        shutdown.sequence = 1;
        shutdown.epoch = 3;
        check(controller.send(shutdown), "shutdown-timeout request sent");
        const auto forwarding_end = GetTickCount64() + 2000;
        while (!sent_shutdown && GetTickCount64() < forwarding_end) {
            waiting.tick();
            Sleep(2);
        }
        check(sent_shutdown && WaitForSingleObject(unacknowledged.info.hProcess, 0) == WAIT_TIMEOUT,
              "shutdown forwarded to independently live HOST child");
        // Only the supervisor deadline advances: the real child and pipe do not
        // synthesize an acknowledgement. This keeps the 15-second bound test fast.
        now += 15001;
        waiting.tick();
        check(acknowledgement_timed_out, "pending clean shutdown escalates after missing acknowledgement");
        check(WaitForSingleObject(unacknowledged.info.hProcess, 1000) == WAIT_OBJECT_0,
              "missing shutdown acknowledgement cannot leave the owned child running indefinitely");
        DWORD code = 0;
        GetExitCodeProcess(unacknowledged.info.hProcess, &code);
        waiting.exited(code);
        std::ifstream status_file(root / L"shutdown-timeout/worker-status.json");
        std::string content((std::istreambuf_iterator<char>(status_file)), {});
        check(code == 35 && content.find("\"state\":\"stopped_forced\"") != std::string::npos,
              "unacknowledged shutdown retains forced-stop evidence without a save claim");
    }
    {
        Generation sixth = one;
        sixth.back() = 6;
        Child failing;
        bool reported_failure = false, reported_regression = false;
        Supervisor observing(sixth, root / L"engine-failure", [&](const char* event, const std::string& detail) {
            if (std::string(event) == "worker_failed") {
                reported_failure = detail.find("engine_reported_failure") != std::string::npos;
                reported_regression = detail.find("regressed_native_progress") != std::string::npos;
            }
        });
        spawn_fixture(failing, observing, sixth, true, true, true);
        const auto deadline = GetTickCount64() + 2000;
        while (WaitForSingleObject(failing.info.hProcess, 0) == WAIT_TIMEOUT && GetTickCount64() < deadline) {
            observing.tick();
            Sleep(2);
        }
        check(reported_failure && !reported_regression,
              "explicit engine failure is not mislabeled as regressed progress");
        DWORD code = 0;
        GetExitCodeProcess(failing.info.hProcess, &code);
        observing.exited(code);
        check(code == 35, "failed HOST observation remains a bounded failed job, not a clean native exit");
    }
    {
        Generation third = one;
        third.back() = 3;
        Child orphan;
        {
            Supervisor dying(third, root / L"orphan", log);
            spawn_fixture(orphan, dying, third);
        }
        check(WaitForSingleObject(orphan.info.hProcess, 1000) == WAIT_OBJECT_0,
              "closing supervisor job terminates its owned HOST child");
    }
    // This path was exclusively created above under the OS temp directory. It
    // contains only the HOST status files created by this test.
    for (auto name : {L"one", L"two", L"orphan", L"forced", L"shutdown-timeout", L"engine-failure"}) {
        std::filesystem::remove(root / name / L"worker-status.json");
        std::filesystem::remove(root / name);
    }
    std::filesystem::remove(root);
}

void status_publication() {
    const auto root =
        std::filesystem::temp_directory_path() /
        (L"SporeMP-status-host-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    check(std::filesystem::create_directory(root), "fresh publication HOST directory");
    Generation generation{};
    generation_from_hex(L"50505050505050505050505050505050", generation);
    for (size_t i = 0; i < 4; ++i) {
        generation[i] = static_cast<uint8_t>(GetCurrentProcessId() >> (i * 8));
    }
    const auto status = root / L"worker-status.json";
    const auto read = [&] {
        std::ifstream stream(status);
        return std::string((std::istreambuf_iterator<char>(stream)), {});
    };
    // This flat-object JSON subset accepts only quoted strings and uint values;
    // full matching catches truncation, concatenation and malformed separators.
    const std::regex json(
        R"json(\{"[a-z_]+":("[^"\\\x00-\x1f]*"|[0-9]+)(,"[a-z_]+":("[^"\\\x00-\x1f]*"|[0-9]+))*\}\n)json");
    uint64_t now = 1000;
    unsigned retries = 0, recoveries = 0;
    std::string retry_detail;
    {
        Supervisor supervisor(
            generation, root,
            [&](const char* event, const std::string& detail) {
                if (std::string(event) == "worker_status_publication_retry") {
                    ++retries;
                    retry_detail = detail;
                }
                if (std::string(event) == "worker_status_publication_recovered") {
                    ++recoveries;
                }
            },
            [&] { return now; });
        const auto original = read();
        check(std::regex_match(original, json), "initial status is one complete valid flat JSON object");
        auto held = CreateFileW(status.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
        check(held != INVALID_HANDLE_VALUE, "actual reader opens status without FILE_SHARE_DELETE");
        now += 1000;
        std::thread release([held] {
            Sleep(80);
            CloseHandle(held);
        });
        std::string unexpected;
        try {
            supervisor.tick();
        } catch (const std::exception& error) {
            unexpected = error.what();
        }
        release.join();
        check(unexpected.empty(), "transient status reader cannot abort the worker supervisor");
        auto content = read();
        check(std::regex_match(content, json) && content.find("\"uptime_ms\":1000") != std::string::npos,
              "released reader permits a complete atomic updated JSON publication");
        check(retries == 1 && recoveries == 1 && retry_detail.find("\"win32_error\":") != std::string::npos,
              "transient replacement conflict and recovery retain rare diagnostic events");

        held = CreateFileW(status.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                           nullptr);
        check(held != INVALID_HANDLE_VALUE, "persistent reader lock held for bounded failure test");
        now += 1000;
        const auto started = GetTickCount64();
        std::string failure;
        try {
            supervisor.tick();
        } catch (const std::exception& error) {
            failure = error.what();
        }
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
        check(std::regex_match(content, json) && content.find("\"uptime_ms\":2000") != std::string::npos,
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
    const auto root =
        std::filesystem::temp_directory_path() / (L"SporeMP-readiness-host-" + std::to_wstring(GetCurrentProcessId()) +
                                                  L"-" + std::to_wstring(GetTickCount64()));
    check(std::filesystem::create_directory(root), "fresh readiness HOST directory");
    for (auto role : {ProgressRole::native_simulation, ProgressRole::network_replica}) {
        const auto run = root / (role == ProgressRole::network_replica ? L"replica" : L"authority");
        std::filesystem::create_directory(run);
        Generation generation{};
        generation_from_hex(L"60606060606060606060606060606060", generation);
        for (size_t i = 0; i < 4; ++i) {
            generation[i] = static_cast<uint8_t>(GetCurrentProcessId() >> (i * 8));
        }
        generation.back() = role == ProgressRole::network_replica ? 61 : 60;
        uint64_t now = 1000, sequence = 0;
        {
            Supervisor supervisor(generation, run, [](const char*, const std::string&) {}, [&] { return now; }, role);
            Pipe engine;
            check(engine.client(pipe_name(generation, false), GetCurrentProcessId()),
                  "readiness engine verifies supervisor PID");
            const auto pump = [&] {
                for (int i = 0; i < 25; ++i) {
                    supervisor.tick();
                    Sleep(2);
                }
            };
            pump();
            const auto state = [&] {
                std::ifstream input(run / L"worker-status.json");
                const std::string text((std::istreambuf_iterator<char>(input)), {});
                const auto begin = text.find("\"state\":\"");
                if (begin == std::string::npos) {
                    return std::string{};
                }
                const auto value = begin + 9;
                return text.substr(value, text.find('"', value) - value);
            };
            const auto send = [&](Message value, uint64_t advance) {
                now += advance;
                value.generation = generation;
                value.sequence = ++sequence;
                check(engine.writable() && engine.send(value), "readiness HOST sends monotonic engine observation");
                pump();
                now += 1000;
                supervisor.tick();
                return state();
            };
            const auto feed = [&](uint64_t app, uint64_t ai, ProjectionState projection = ProjectionState::connected,
                                  uint64_t baseline = 7, uint64_t advance = 1000) {
                Message value;
                value.epoch = 3;
                value.values[0] = static_cast<uint64_t>(Phase::scene);
                value.values[1] = app;
                value.values[2] = ai;
                value.values[6] = static_cast<uint64_t>(projection);
                value.values[7] = baseline;
                return send(value, advance);
            };
            const auto reply = [&](uint64_t app, uint64_t advance) {
                // A real operator command produces a correlated engine reply.
                // Request 2 / accepted 1 deliberately resembles connected / 1.
                Pipe controller;
                check(controller.client(pipe_name(generation, true), GetCurrentProcessId()),
                      "readiness operator verifies supervisor PID");
                Message request;
                request.kind = Kind::command;
                request.op = Op::move;
                request.generation = generation;
                request.sequence = 1;
                request.epoch = 3;
                check(controller.send(request), "readiness operator sends bounded HOST command");
                Message forwarded;
                bool received = false;
                const auto until = GetTickCount64() + 1000;
                do {
                    supervisor.tick();
                    received = engine.receive(forwarded);
                    if (!received) {
                        Sleep(2);
                    }
                } while (!received && GetTickCount64() < until);
                check(received && forwarded.op == Op::move,
                      "readiness command is actually forwarded to engine fixture");
                Message value;
                value.kind = Kind::reply;
                value.op = forwarded.op;
                value.epoch = 3;
                value.values[0] = static_cast<uint64_t>(Phase::scene);
                value.values[1] = app;
                value.values[2] = 163;
                value.values[6] = forwarded.sequence;
                value.values[7] = static_cast<uint64_t>(Result::accepted);
                const auto observed = send(value, advance);
                Message acknowledged;
                check(controller.receive(acknowledged) && acknowledged.kind == Kind::reply &&
                          acknowledged.values[6] == forwarded.sequence &&
                          acknowledged.values[7] == static_cast<uint64_t>(Result::accepted),
                      "readiness HOST command reply remains normally correlated and accepted");
                controller.close();
                pump();
                return observed;
            };
            check(feed(1, 163) == "ready", "fresh role-specific engine and projection evidence is ready");
            const auto local_status = request(supervisor, supervisor, generation, Op::status);
            check(local_status.values[6] == 0 && local_status.values[7] == static_cast<uint64_t>(Result::accepted),
                  "local status reply clears projection word two instead of inventing an engine request ID");
            pump();
            if (role == ProgressRole::native_simulation) {
                check(
                    feed(2, 163, ProjectionState::connected, 7, 4000) == "simulation_stalled",
                    "authority with app heartbeat but stopped native AI remains stalled despite projection annotation");
                check(feed(3, 164, static_cast<ProjectionState>(99), 999) == "ready",
                      "authority readiness depends on original AI and ignores replica annotation words");
                const auto deadline = now + 10000;
                check(feed(4, 165, ProjectionState::checkpoint_lease, deadline) == "ready",
                      "restored authority establishes a finite checkpoint lease through authenticated status");
                check(feed(5, 166, ProjectionState::none, 0) == "ready",
                      "disconnect annotations cannot reset a previously established rollback deadline");
                Pipe shutting_down;
                check(shutting_down.client(pipe_name(generation, true), GetCurrentProcessId()),
                      "checkpoint shutdown fixture authenticates its operator pipe");
                Message shutdown;
                shutdown.kind = Kind::command;
                shutdown.op = Op::shutdown;
                shutdown.generation = generation;
                shutdown.sequence = 1;
                shutdown.epoch = 3;
                check(shutting_down.send(shutdown), "checkpoint fixture requests native shutdown before its deadline");
                Message forwarded;
                bool received = false;
                const auto until = GetTickCount64() + 1000;
                do {
                    supervisor.tick();
                    received = engine.receive(forwarded);
                    if (!received) {
                        Sleep(2);
                    }
                } while (!received && GetTickCount64() < until);
                check(received && forwarded.op == Op::shutdown,
                      "checkpoint shutdown reaches the engine without a completion reply");
                now = deadline;
                supervisor.tick();
                check(state() == "checkpoint_required",
                      "pending clean shutdown cannot extend the checkpoint rollback deadline");
            } else {
                check(feed(2, 163, ProjectionState::connected, 7, 4000) == "ready",
                      "verified replica with live app and established projection remains ready while native AI is "
                      "suppressed");
                {
                    std::ofstream stale(run / L"network-status.json");
                    stale << "{\"state\":\"connected\",\"player_id\":2,\"baseline_sequence\":7,\"detail\":\"old\"}\n";
                }
                check(feed(3, 163, ProjectionState::waiting, 0) == "replica_waiting",
                      "new baseline waiting status clears connected readiness despite stale connected file");
                check(reply(4, 1000) == "replica_waiting", "first command reply cannot establish projection readiness");
                check(reply(5, 1000) == "replica_waiting",
                      "request two and accepted one reply cannot masquerade as connected projection baseline one");
                check(feed(6, 163, ProjectionState::connected, 8) == "ready",
                      "new applied baseline restores replica readiness");
                check(reply(7, 4000) == "replica_waiting",
                      "fresh command reply and app heartbeat cannot refresh stale projection observation");
                std::filesystem::remove(run / L"network-status.json");
                check(feed(8, 163, ProjectionState::connected, 8) == "ready",
                      "authenticated status establishes readiness without any network status file");
                uint64_t app = 8;

                struct InvalidProjection {
                    ProjectionState state;
                    uint64_t baseline;
                };

                for (const auto& invalid :
                     {InvalidProjection{ProjectionState::connected, 0},
                      InvalidProjection{static_cast<ProjectionState>(3), 7},
                      InvalidProjection{static_cast<ProjectionState>(UINT64_MAX), 7},
                      InvalidProjection{ProjectionState::none, 7}, InvalidProjection{ProjectionState::waiting, 7}}) {
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
                now += 4000;
                supervisor.tick();
                check(state() == "unresponsive", "connected replica projection cannot hide stale engine heartbeat");
            }
        }
        std::filesystem::remove(run / L"worker-status.json");
        std::filesystem::remove(run / L"network-status.json");
        std::filesystem::remove(run);
    }
    std::filesystem::remove(root);
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 4 && (std::string(argv[1]) == "--engine" || std::string(argv[1]) == "--engine-no-shutdown-ack" ||
                          std::string(argv[1]) == "--engine-failure")) {
            const std::string text = argv[2];
            Generation generation{};
            if (!generation_from_hex(std::wstring(text.begin(), text.end()), generation)) {
                return 2;
            }
            return fixture_engine(generation, static_cast<DWORD>(std::stoul(argv[3])),
                                  std::string(argv[1]) != "--engine-no-shutdown-ack",
                                  std::string(argv[1]) == "--engine-failure");
        }
        Message original;
        original.kind = Kind::command;
        original.op = Op::jump;
        original.sequence = UINT64_MAX;
        original.epoch = UINT64_MAX - 1;
        check(generation_from_hex(L"0123456789abcdef0123456789abcdef", original.generation), "generation parse");
        original.values = {1, 0xfedcba9876543210, 2, 3, 4, 5, 6, 7, 8, UINT64_MAX};
        auto wire = encode(original);
        Message decoded;
        check(wire[0] == 'S' && wire[1] == 'M' && wire[2] == 'W' && wire[3] == '4' && wire[6] == 128,
              "fixed wire header");
        check(decode(wire.data(), wire.size(), decoded) && decoded.values == original.values &&
                  decoded.epoch == original.epoch && decoded.generation == original.generation,
              "scalar lossless roundtrip");
        // Exercise the same builder used by initial network bootstrap and
        // the late second-owner replica bootstrap. A default status-kind
        // Message previously slipped through their direct internal dispatch.
        for (uint64_t sequence : {2ull, 3ull}) {
            auto request = make_setup_command(original.epoch, sequence);
            check(valid_setup(request) && request.kind == Kind::command && request.op == Op::setup &&
                      request.epoch == original.epoch && request.sequence == sequence &&
                      request.values == std::array<uint64_t, 10>{},
                  "both actual network setup builders satisfy command admission without enabling foreign mode");
            request.generation = original.generation;
            const auto setup_wire = encode(request);
            check(decode(setup_wire.data(), setup_wire.size(), decoded) && valid_setup(decoded),
                  "internal network setup retains valid command kind through worker serialization");
        }
        for (bool foreign : {false, true}) {
            auto request = make_setup_command(original.epoch, 4, foreign);
            auto expected = std::array<uint64_t, 10>{};
            expected[0] = foreign ? 1 : 0;
            check(valid_setup(request) && request.kind == Kind::command && request.op == Op::setup &&
                      request.epoch == original.epoch && request.sequence == 4 && request.values == expected,
                  "explicit setup builder selects only the requested foreign mode and preserves every reserved word");
            request.generation = original.generation;
            const auto setup_wire = encode(request);
            check(decode(setup_wire.data(), setup_wire.size(), decoded) && valid_setup(decoded) &&
                      decoded.values == expected,
                  "explicit foreign setup builder survives the worker command wire");
            check(!valid_setup(make_setup_command(0, 4, foreign)),
                  "setup builder does not invent an absent native epoch");
        }
        auto setup_request = original;
        setup_request.op = Op::setup;
        setup_request.values = {};
        for (uint64_t mode : {0ull, 1ull}) {
            setup_request.values[0] = mode;
            const auto setup_wire = encode(setup_request);
            check(decode(setup_wire.data(), setup_wire.size(), decoded) && valid_setup(decoded) &&
                      decoded.values[0] == mode,
                  "local setup preserves default and explicitly selected foreign initialization modes");
        }
        for (size_t i = 0; i < setup_request.values.size(); ++i) {
            auto invalid = setup_request;
            invalid.values[i] = i ? 1 : 2;
            check(!valid_setup(invalid), "local setup rejects unknown mode and every reserved word");
        }
        auto invalid_setup = setup_request;
        invalid_setup.epoch = 0;
        check(!valid_setup(invalid_setup), "local setup requires a scene epoch");
        invalid_setup = setup_request;
        invalid_setup.kind = Kind::reply;
        check(!valid_setup(invalid_setup), "local setup rejects non-command kind");
        invalid_setup = setup_request;
        invalid_setup.op = Op::players;
        check(!valid_setup(invalid_setup), "local setup cannot alias player creation");
        auto replica_request = original;
        replica_request.op = Op::replica;
        auto social_request = original;
        social_request.op = Op::social_probe;
        social_request.values = {2, 0xfedcba9876543210, 232, UINT32_MAX};
        const auto social_wire = encode(social_request);
        check(decode(social_wire.data(), social_wire.size(), decoded) && valid_social_probe(decoded),
              "M11 local social start retains owner, wide actor identity and native target");
        for (uint64_t index : {0ull, 4ull, 87ull}) {
            auto reply = social_request;
            reply.values[3] = index;
            check(valid_social_probe(reply), "M11 bounded response index admitted for native validation");
        }
        for (uint64_t index : {88ull, UINT64_MAX, uint64_t(UINT32_MAX) + 1}) {
            auto invalid = social_request;
            invalid.values[3] = index;
            check(!valid_social_probe(invalid), "M11 oversized social response index rejected");
        }
        for (size_t i = 0; i < social_request.values.size(); ++i) {
            auto invalid = social_request;
            invalid.values[i] = i == 0 ? 3 : i == 1 ? 0 : UINT64_MAX;
            check(!valid_social_probe(invalid),
                  "M11 wrong owner, absent actor, oversized target and reserved slots rejected");
        }
        auto invalid_social = social_request;
        invalid_social.epoch = 0;
        check(!valid_social_probe(invalid_social), "M11 social input requires a scene epoch");
        invalid_social = social_request;
        invalid_social.kind = Kind::status;
        check(!valid_social_probe(invalid_social), "M11 status is not an input request");
        invalid_social = social_request;
        invalid_social.op = Op::network_action;
        check(!valid_social_probe(invalid_social), "M11 local probe cannot reuse network opcode");
        auto social_tree = social_request;
        social_tree.op = Op::social_tree_probe;
        social_tree.values = {2, 0xfedcba9876543210, 1};
        const auto social_tree_wire = encode(social_tree);
        check(decode(social_tree_wire.data(), social_tree_wire.size(), decoded) && valid_social_tree_probe(decoded),
              "M11 local tree selection retains secondary owner and wide actor identity");
        social_tree.values[2] = 0;
        check(valid_social_tree_probe(social_tree), "M11 native tree restoration is admitted");
        social_tree.values[2] = 2;
        check(valid_social_tree_probe(social_tree), "M11 bounded social-owner probe requires native admission");
        social_tree.values[2] = 3;
        check(valid_social_tree_probe(social_tree),
              "M11 local presentation-deferral mode requires native ownership admission");
        social_tree.values[2] = 4;
        check(!valid_social_tree_probe(social_tree), "M11 social-owner probe refuses the first unknown mode");
        social_tree.values[2] = 0;
        for (size_t i = 0; i < social_tree.values.size(); ++i) {
            auto invalid = social_tree;
            invalid.values[i] = i == 0 ? 1 : i == 1 ? 0 : UINT64_MAX;
            check(!valid_social_tree_probe(invalid),
                  "M11 tree probe refuses primary owner, absent actor, invalid flag and reserved words");
        }
        auto invalid_tree = social_tree;
        invalid_tree.epoch = 0;
        check(!valid_social_tree_probe(invalid_tree), "M11 tree probe requires a scene epoch");
        invalid_tree = social_tree;
        invalid_tree.kind = Kind::status;
        check(!valid_social_tree_probe(invalid_tree), "M11 tree probe requires command kind");
        invalid_tree = social_tree;
        invalid_tree.op = Op::social_probe;
        check(!valid_social_tree_probe(invalid_tree), "M11 tree probe cannot alias a social input");
        auto nest_request = original;
        nest_request.op = Op::nest_probe;
        nest_request.values = {2, 0xfedcba9876543210};
        const auto nest_wire = encode(nest_request);
        check(decode(nest_wire.data(), nest_wire.size(), decoded) && valid_nest_probe(decoded) &&
                  decoded.values == nest_request.values,
              "M11 nest factory probe retains secondary owner and wide bound actor");
        for (size_t i = 0; i < nest_request.values.size(); ++i) {
            auto invalid = nest_request;
            invalid.values[i] = i == 0 ? 1 : i == 1 ? 0 : 1;
            check(!valid_nest_probe(invalid),
                  "M11 nest probe refuses primary owner, absent actor and every reserved word");
        }
        auto invalid_nest = nest_request;
        invalid_nest.epoch = 0;
        check(!valid_nest_probe(invalid_nest), "M11 nest probe requires an epoch");
        invalid_nest = nest_request;
        invalid_nest.kind = Kind::status;
        check(!valid_nest_probe(invalid_nest), "M11 nest probe requires command kind");
        invalid_nest = nest_request;
        invalid_nest.op = Op::social_tree_probe;
        check(!valid_nest_probe(invalid_nest), "M11 nest probe cannot alias another opcode");
        auto fossil = original;
        fossil.op = Op::fossil_approach;
        fossil.values = {1, 0xfedcba9876543210, 9777};
        const auto fossil_wire = encode(fossil);
        check(decode(fossil_wire.data(), fossil_wire.size(), decoded) && valid_fossil_approach(decoded) &&
                  decoded.values == fossil.values,
              "M11 fossil approach preserves scalar actor and native fossil IDs");
        fossil.values[0] = 2;
        check(valid_fossil_approach(fossil), "M11 second owner still requires native ownership validation");
        for (size_t i = 0; i < fossil.values.size(); ++i) {
            auto invalid = fossil;
            invalid.values[i] = i == 0 ? 3 : i == 1 ? 0 : i == 2 ? UINT32_MAX : 1;
            check(!valid_fossil_approach(invalid),
                  "M11 fossil approach rejects wrong owner, missing actor, sentinel target and reserved words");
        }
        auto key = original;
        key.op = Op::input;
        key.values = {1, 'W'};
        const auto key_wire = encode(key);
        check(decode(key_wire.data(), key_wire.size(), decoded) && valid_input(decoded) &&
                  decoded.values == key.values && std::string(op_name(Op::input)) == "input",
              "key input preserves its transition and virtual key");
        for (uint64_t allowed : {uint64_t('W'), uint64_t('A'), uint64_t('S'), uint64_t('D'), uint64_t(0x20),
                                 uint64_t('1'), uint64_t('9')}) {
            for (uint64_t transition : {uint64_t(1), uint64_t(2)}) {
                auto ok = key;
                ok.values = {transition, allowed};
                check(valid_input(ok), "movement, jump and ability keys are admitted for down and up");
            }
        }
        for (uint64_t refused : {uint64_t(0), uint64_t('0'), uint64_t('Q'), uint64_t(0x0d), uint64_t(0x1b),
                                 uint64_t(0x5b), uint64_t(0x73), uint64_t('w'), uint64_t(0x100000057)}) {
            auto bad = key;
            bad.values = {1, refused};
            check(!valid_input(bad), "any other virtual key is refused");
        }
        for (uint64_t transition : {uint64_t(0), uint64_t(3)}) {
            auto bad = key;
            bad.values = {transition, 'W'};
            check(!valid_input(bad), "only down and up transitions exist");
        }
        for (size_t i = 2; i < key.values.size(); ++i) {
            auto bad = key;
            bad.values[i] = 1;
            check(!valid_input(bad), "key input reserved words are refused");
        }
        {
            auto bad = key;
            bad.epoch = 0;
            check(!valid_input(bad), "key input requires the scene epoch");
            bad = key;
            bad.kind = Kind::status;
            check(!valid_input(bad), "key input requires command kind");
            bad = key;
            bad.op = Op::jump;
            check(!valid_input(bad), "key input cannot alias another opcode");
        }
        auto click = original;
        click.op = Op::click;
        click.values = {2, 12884911489ull, 50};
        const auto click_wire = encode(click);
        check(decode(click_wire.data(), click_wire.size(), decoded) && valid_click(decoded) &&
                  decoded.values == click.values && std::string(op_name(Op::click)) == "click",
              "a click preserves its action and entity");
        for (uint64_t action : {uint64_t(1), uint64_t(2), uint64_t(3)}) {
            auto ok = click;
            ok.values = {action, 7, 50};
            check(valid_click(ok), "pointing, a left click and a right click are admitted");
        }
        for (uint64_t action : {uint64_t(0), uint64_t(4)}) {
            auto bad = click;
            bad.values = {action, 7, 50};
            check(!valid_click(bad), "no other pointer action exists");
        }
        for (uint64_t height : {uint64_t(5), uint64_t(20), uint64_t(95)}) {
            auto ok = click;
            ok.values = {2, 7, height};
            check(valid_click(ok), "a click may aim low or high on the creature");
        }
        for (uint64_t height : {uint64_t(0), uint64_t(4), uint64_t(96), uint64_t(500)}) {
            auto bad = click;
            bad.values = {2, 7, height};
            check(!valid_click(bad), "an aim height outside the creature is refused");
        }
        {
            auto bad = click;
            bad.values = {2, 0, 50};
            check(!valid_click(bad), "a click needs an entity");
            bad = click;
            bad.epoch = 0;
            check(!valid_click(bad), "a click requires the scene epoch");
            bad = click;
            bad.kind = Kind::status;
            check(!valid_click(bad), "a click requires command kind");
            bad = click;
            bad.op = Op::input;
            check(!valid_click(bad), "a click cannot alias another opcode");
        }
        for (size_t i = 3; i < click.values.size(); ++i) {
            auto bad = click;
            bad.values[i] = 1;
            check(!valid_click(bad), "click reserved words are refused");
        }
        auto point = original;
        point.op = Op::point;
        point.values = {2, 1250, 688};
        const auto point_wire = encode(point);
        check(decode(point_wire.data(), point_wire.size(), decoded) && valid_point(decoded) &&
                  decoded.values == point.values && std::string(op_name(Op::point)) == "point",
              "a pointer action at a place preserves its action and place");
        for (uint64_t action : {uint64_t(1), uint64_t(2), uint64_t(3)}) {
            auto ok = point;
            ok.values = {action, 0, 0};
            check(valid_point(ok), "pointing, a left click and a right click at a place are admitted");
        }
        for (uint64_t action : {uint64_t(0), uint64_t(4)}) {
            auto bad = point;
            bad.values = {action, 5, 5};
            check(!valid_point(bad), "no other action at a place exists");
        }
        {
            auto bad = point;
            bad.values = {2, 8192, 5};
            check(!valid_point(bad), "a place beyond any window is refused");
            bad = point;
            bad.values = {2, 5, 8192};
            check(!valid_point(bad), "a place below any window is refused");
            bad = point;
            bad.epoch = 0;
            check(!valid_point(bad), "a place requires the scene epoch");
            bad = point;
            bad.op = Op::click;
            check(!valid_point(bad), "a place cannot alias another opcode");
        }
        for (size_t i = 3; i < point.values.size(); ++i) {
            auto bad = point;
            bad.values[i] = 1;
            check(!valid_point(bad), "reserved words at a place are refused");
        }
        auto ui = original;
        ui.op = Op::ui;
        ui.values = {1, 0, 0};
        const auto ui_wire = encode(ui);
        check(decode(ui_wire.data(), ui_wire.size(), decoded) && valid_ui(decoded) &&
                  std::string(op_name(Op::ui)) == "ui",
              "an interface reading request survives the wire");
        {
            auto at = ui;
            at.values = {2, 1250, 688};
            check(valid_ui(at), "asking what lies under a place is admitted");
            auto bad = ui;
            bad.values = {1, 5, 0};
            check(!valid_ui(bad), "a whole reading takes no place");
            bad = ui;
            bad.values = {3, 0, 0};
            check(!valid_ui(bad), "no third kind of reading exists");
            bad = ui;
            bad.values = {2, 8192, 0};
            check(!valid_ui(bad), "a place beyond any window is refused");
            bad = ui;
            bad.epoch = 0;
            check(!valid_ui(bad), "a reading requires the scene epoch");
            bad = ui;
            bad.values = {1, 0, 0, 1};
            check(!valid_ui(bad), "reading reserved words are refused");
        }
        using sporemp::worker::ui_text_json;
        check(ui_text_json(u"Stance", 40) == "Stance", "plain interface text is kept");
        check(ui_text_json(u"Who's \"this\"?\\", 40) == "Who's \\\"this\\\"?\\\\",
              "quotes and backslashes are escaped for the trace");
        check(ui_text_json(u"two\nlines\ttab", 40) == "two lines tab", "line breaks and tabs become spaces");
        check(ui_text_json(u"caf\u00e9 \u2122", 40) == "caf\\u00e9 \\u2122", "other characters are written as escapes");
        check(ui_text_json(u"abcdefghij", 4) == "abcd", "long text is cut at the limit");
        check(ui_text_json(nullptr, 40).empty() && ui_text_json(u"", 40).empty(), "no text gives no text");
        auto invalid_fossil = fossil;
        invalid_fossil.epoch = 0;
        check(!valid_fossil_approach(invalid_fossil), "M11 fossil approach requires scene epoch");
        invalid_fossil = fossil;
        invalid_fossil.kind = Kind::status;
        check(!valid_fossil_approach(invalid_fossil), "M11 fossil approach requires command kind");
        invalid_fossil = fossil;
        invalid_fossil.op = Op::nest_probe;
        check(!valid_fossil_approach(invalid_fossil), "M11 fossil approach cannot alias a factory probe");
        const auto replica_wire = encode(replica_request);
        check(decode(replica_wire.data(), replica_wire.size(), decoded) && decoded.op == Op::replica &&
                  decoded.values == original.values,
              "M05 private probe opcode retains all ten scalar words");
        auto creation_request = original;
        creation_request.op = Op::inspect_creation;
        creation_request.values = {0x12345678, 0x2b978c46, 0x40626200};
        const auto creation_wire = encode(creation_request);
        check(decode(creation_wire.data(), creation_wire.size(), decoded) && valid_creation_inspection(decoded),
              "M08 private creature key roundtrip admitted without pointer or path");
        for (size_t i = 0; i < creation_request.values.size(); ++i) {
            auto invalid = creation_request;
            invalid.values[i] |= uint64_t(1) << 32;
            check(!valid_creation_inspection(invalid), "M08 oversized key or nonzero reserved payload rejected");
        }
        auto invalid_creation = creation_request;
        invalid_creation.values[0] = 0;
        check(!valid_creation_inspection(invalid_creation), "M08 zero creation identity rejected");
        invalid_creation.values[0] = UINT32_MAX;
        check(!valid_creation_inspection(invalid_creation), "M08 sentinel creation identity rejected");
        invalid_creation = creation_request;
        invalid_creation.values[2] = UINT32_MAX;
        check(!valid_creation_inspection(invalid_creation), "M08 sentinel creation group rejected");
        invalid_creation = creation_request;
        invalid_creation.values[1] = 0x00e6bce5;
        check(!valid_creation_inspection(invalid_creation), "M08 generated model is not a creature query");
        invalid_creation = creation_request;
        invalid_creation.kind = Kind::status;
        check(!valid_creation_inspection(invalid_creation), "M08 observation cannot masquerade as command");
        invalid_creation = creation_request;
        invalid_creation.op = Op::network_action;
        check(!valid_creation_inspection(invalid_creation), "M08 network action cannot become inspection");
        auto import_request = original;
        import_request.op = Op::import_creation;
        import_request.values = {0x12345678, UINT32_MAX, 0, 4, 5, 6, 7, 8, 0, 0};
        auto import_wire = encode(import_request);
        check(decode(import_wire.data(), import_wire.size(), decoded) && valid_creation_import(decoded),
              "M08 import sends an exact digest without a peer path");
        for (size_t i = 0; i < 10; ++i) {
            auto invalid = import_request;
            invalid.values[i] = uint64_t(1) << 32;
            check(!valid_creation_import(invalid), "M08 import rejects oversized digest words and every reserved slot");
        }
        auto invalid_import = import_request;
        invalid_import.values = {};
        check(!valid_creation_import(invalid_import), "M08 all-zero import digest rejected");
        invalid_import = import_request;
        invalid_import.kind = Kind::reply;
        check(!valid_creation_import(invalid_import), "M08 import requires command frame");
        invalid_import = import_request;
        invalid_import.op = Op::inspect_creation;
        check(!valid_creation_import(invalid_import), "M08 import cannot reuse an inspection opcode");
        auto player_import = import_request;
        player_import.op = Op::import_player;
        const auto player_wire = encode(player_import);
        check(decode(player_wire.data(), player_wire.size(), decoded) && valid_player_import(decoded),
              "M10 player import accepts only a pinned sidecar digest command");
        check(!valid_creation_import(player_import) && !valid_player_import(import_request),
              "M10 player and creation import domains remain distinct");
        for (size_t i = 0; i < 10; ++i) {
            auto invalid = player_import;
            invalid.values[i] = uint64_t(1) << 32;
            check(!valid_player_import(invalid), "M10 player import rejects wide words and reserved slots");
        }
        player_import.values = {};
        check(!valid_player_import(player_import), "M10 player import rejects empty digest");
        player_import = import_request;
        player_import.op = Op::import_player;
        player_import.kind = Kind::reply;
        check(!valid_player_import(player_import), "M10 observation cannot invoke player import");
        for (size_t i = 0; i < frame_size; ++i) {
            check(!decode(wire.data(), i, decoded), "every truncated frame rejected");
        }
        check(!decode(wire.data(), frame_size + 1, decoded), "oversized frame rejected without read");
        auto bad = wire;
        bad[4] = 2;
        check(!decode(bad.data(), bad.size(), decoded), "unknown schema rejected");
        bad = wire;
        bad[12] = 255;
        check(!decode(bad.data(), bad.size(), decoded), "unknown opcode rejected");
        bad = wire;
        bad[8] = 0;
        check(!decode(bad.data(), bad.size(), decoded), "unknown kind rejected");
        bad = wire;
        for (size_t i = 16; i < 32; ++i) {
            bad[i] = 0;
        }
        check(!decode(bad.data(), bad.size(), decoded), "zero generation rejected");
        uint64_t last = 0;
        original.sequence = 1;
        check(accept_sequence(original, original.generation, last), "first sequence accepted");
        check(!accept_sequence(original, original.generation, last) && last == 1, "duplicate cannot mutate fence");
        original.sequence = 3;
        check(!accept_sequence(original, original.generation, last), "gap rejected");
        original.sequence = 2;
        auto other = original.generation;
        ++other[0];
        check(!accept_sequence(original, other, last), "previous worker generation rejected");
        last = UINT64_MAX;
        original.sequence = 0;
        check(!accept_sequence(original, original.generation, last), "wrap rejected");
        const auto name = L"\\\\.\\pipe\\SporeMP-HOST-test-" + std::to_wstring(GetCurrentProcessId());
        Pipe server, client;
        check(server.server(name, current_sid(), false), "real private pipe created");
        check(client.client(name, GetCurrentProcessId()), "actual server process verified");
        const auto started = GetTickCount64();
        while (!server.connected(GetCurrentProcessId()) && GetTickCount64() - started < 2000) {
            Sleep(1);
        }
        check(server.connected(GetCurrentProcessId()), "actual client process verified");
        original.sequence = 1;
        check(client.send(original), "overlapped request sent");
        bool got = false;
        while (!(got = server.receive(decoded)) && GetTickCount64() - started < 2000) {
            Sleep(1);
        }
        check(got && decoded.values == original.values, "real pipe transfer decoded");
        check(!server.receive(decoded) && !server.failed(), "empty pipe does not block");
        client.close();
        while (!server.failed() && GetTickCount64() - started < 2000) {
            server.receive(decoded);
            Sleep(1);
        }
        check(server.failed(), "peer disconnect observed");
        Pipe impersonation_server, wrong;
        const auto other_name = name + L"-identity";
        check(impersonation_server.server(other_name, current_sid(), false), "identity fixture pipe created");
        check(!wrong.client(other_name, GetCurrentProcessId() + 1) && wrong.error() == ERROR_ACCESS_DENIED,
              "wrong server PID rejected");
        supervision();
        status_publication();
        role_readiness();
        std::cout << count << " HOST/FIXTURE worker assertions passed. Native SPORE NOT RUN.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << " win32=" << GetLastError() << '\n';
        return 1;
    }
}
