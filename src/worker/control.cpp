#include "pipe.h"
#include <iostream>
#include <memory>
#include <limits>

using namespace sporemp::worker;

namespace {
bool parse_number(const wchar_t* text, uint64_t& value) {
    if (!text || !*text) {
        return false;
    }
    value = 0;
    for (auto digit = text; *digit; ++digit) {
        if (*digit < L'0' || *digit > L'9' || value > (UINT64_MAX - (*digit - L'0')) / 10) {
            return false;
        }
        value = value * 10 + (*digit - L'0');
    }
    return true;
}

bool parse_operation(const wchar_t* text, Op& operation) {
    for (uint32_t i = 0; i <= static_cast<uint32_t>(Op::ui); ++i) {
        const auto candidate = static_cast<Op>(i);
        const std::string name = op_name(candidate);
        if (std::wstring(name.begin(), name.end()) == text) {
            operation = candidate;
            return true;
        }
    }
    return false;
}

// How many arguments a command line carries for each operation, the program's own name included: the
// generation, the supervisor's process, the operation, then its epoch and its values.
int expected_argument_count(Op operation) {
    return operation == Op::replica                                             ? 15
           : operation == Op::import_creation || operation == Op::import_player ? 13
           : operation == Op::network_action                                    ? 11
           : operation == Op::social_probe                                      ? 9
           : operation == Op::status                                            ? 4
           : operation == Op::restore                                           ? 11
           : operation == Op::move || operation == Op::inspect_creation || operation == Op::engage ||
                   operation == Op::pickup || operation == Op::social_tree_probe || operation == Op::fossil_approach ||
                   operation == Op::click || operation == Op::point || operation == Op::ui
               ? 8
           : operation == Op::jump || operation == Op::stop || operation == Op::retire || operation == Op::nest_probe ||
                   operation == Op::input
               ? 7
           : operation == Op::duel || operation == Op::save_fault || operation == Op::bind_primary ||
                   operation == Op::scene_probe || operation == Op::resume_world
               ? 6
               : 5;
}

// What each operation requires of its values before the command is sent at all.
bool valid_request(const Message& request) {
    if (request.op == Op::load && request.values[0] > 1) {
        return false;
    }
    if (request.op == Op::setup && !valid_setup(request)) {
        return false;
    }
    if (request.op == Op::resume_world && !request.values[0]) {
        return false;
    }
    if ((request.op == Op::bind_primary || request.op == Op::scene_probe) &&
        (request.values[0] < 1 || request.values[0] > 2)) {
        return false;
    }
    if (request.op == Op::inspect_creation && !valid_creation_inspection(request)) {
        return false;
    }
    if (request.op == Op::import_creation && !valid_creation_import(request)) {
        return false;
    }
    if (request.op == Op::import_player && !valid_player_import(request)) {
        return false;
    }
    if (request.op == Op::social_probe && !valid_social_probe(request)) {
        return false;
    }
    if (request.op == Op::social_tree_probe && !valid_social_tree_probe(request)) {
        return false;
    }
    if (request.op == Op::nest_probe && !valid_nest_probe(request)) {
        return false;
    }
    if (request.op == Op::fossil_approach && !valid_fossil_approach(request)) {
        return false;
    }
    if (request.op == Op::input && !valid_input(request)) {
        return false;
    }
    if (request.op == Op::click && !valid_click(request)) {
        return false;
    }
    if (request.op == Op::point && !valid_point(request)) {
        return false;
    }
    if (request.op == Op::ui && !valid_ui(request)) {
        return false;
    }
    return true;
}

// Builds the command from the arguments: generation, supervisor process, operation, epoch, values.
// No shell strings, generic console execution, file paths, or native pointers.
bool parse_request(int argc, wchar_t** argv, Message& request, uint64_t& supervisor_pid) {
    if (argc < 4 || argc > 15) {
        return false;
    }
    request.kind = Kind::command;
    request.sequence = 1;
    if (!generation_from_hex(argv[1], request.generation) || !parse_number(argv[2], supervisor_pid) ||
        !supervisor_pid || supervisor_pid > UINT32_MAX) {
        return false;
    }
    if (!parse_operation(argv[3], request.op)) {
        return false;
    }
    if (argc != expected_argument_count(request.op) &&
        !((request.op == Op::load || request.op == Op::setup) && argc == 6)) {
        return false;
    }
    if (argc >= 5 && !parse_number(argv[4], request.epoch)) {
        return false;
    }
    for (int i = 5; i < argc; ++i) {
        if (!parse_number(argv[i], request.values[i - 5])) {
            return false;
        }
    }
    return valid_request(request);
}

// Opens the supervisor's operator pipe, trying again for a while when it is busy or not yet there. The
// caller reads from the returned pipe whether that failed.
std::unique_ptr<Pipe> open_control(const Generation& generation, DWORD supervisor_pid, ULONGLONG started) {
    std::unique_ptr<Pipe> channel;
    do {
        channel = std::make_unique<Pipe>();
        if (channel->client(pipe_name(generation, true), supervisor_pid)) {
            break;
        }
        if (channel->error() != ERROR_PIPE_BUSY && channel->error() != ERROR_FILE_NOT_FOUND) {
            break;
        }
        Sleep(20);
    } while (GetTickCount64() - started < 2000);
    return channel;
}

void print_reply(const Message& reply) {
    std::cout << "{\"schema_version\":1,\"op\":\"" << op_name(reply.op) << "\",\"result\":\""
              << result_name(static_cast<Result>(reply.values[7])) << "\",\"epoch\":" << reply.epoch
              << ",\"phase\":" << reply.values[0] << ",\"app_updates\":" << reply.values[1]
              << ",\"native_ai_entries\":" << reply.values[2] << ",\"actor_a\":" << reply.values[3]
              << ",\"actor_b\":" << reply.values[4] << ",\"mode\":" << reply.values[5]
              << ",\"request\":" << reply.values[6] << ",\"persistence_request\":" << reply.values[8]
              << ",\"persistence_state\":" << reply.values[9] << "}\n";
}

// Waits for the supervisor's reply to the command and prints it; the result is the process exit code.
int await_reply(Pipe& channel, const Message& request, ULONGLONG started) {
    Message reply;
    while (GetTickCount64() - started < 10000 && !channel.failed()) {
        if (channel.receive(reply)) {
            if (reply.kind != Kind::reply || reply.generation != request.generation || reply.sequence != 1 ||
                reply.op != request.op || reply.values[7] > static_cast<uint64_t>(Result::failed)) {
                return 4;
            }
            print_reply(reply);
            return reply.values[7] == static_cast<uint64_t>(Result::accepted) ? 0 : 5;
        }
        channel.writable();
        Sleep(10);
    }
    std::cerr << "Worker reply unavailable; outcome unknown. Do not retry a mutation automatically.\n";
    return 6;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    Message request;
    uint64_t supervisor_pid = 0;
    if (!parse_request(argc, argv, request, supervisor_pid)) {
        return 2;
    }
    const auto started = GetTickCount64();
    const auto channel = open_control(request.generation, static_cast<DWORD>(supervisor_pid), started);
    if (channel->failed() || !channel->send(request)) {
        std::cerr << "Worker control unavailable; win32=" << channel->error() << '\n';
        return 3;
    }
    return await_reply(*channel, request, started);
}
