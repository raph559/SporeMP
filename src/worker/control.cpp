#include "pipe.h"
#include <iostream>
#include <memory>
#include <limits>

using namespace sporemp::worker;
namespace {
bool number(const wchar_t* text, uint64_t& value) {
    if (!text || !*text) return false;
    value = 0;
    for (auto c = text; *c; ++c) {
        if (*c < L'0' || *c > L'9' || value > (UINT64_MAX - (*c - L'0')) / 10) return false;
        value = value * 10 + (*c - L'0');
    }
    return true;
}
}
int wmain(int argc, wchar_t** argv) {
    // No shell strings, generic console execution, file paths, or native pointers.
    if (argc < 4 || argc > 15) return 2;
    Message request; request.kind = Kind::command; request.sequence = 1;
    uint64_t pid = 0;
    if (!generation_from_hex(argv[1], request.generation) || !number(argv[2], pid) || !pid || pid > UINT32_MAX) return 2;
    bool found = false;
    for (uint32_t i = 0; i <= static_cast<uint32_t>(Op::replica); ++i) {
        const auto op = static_cast<Op>(i); const std::string name = op_name(op);
        if (std::wstring(name.begin(), name.end()) == argv[3]) { request.op = op; found = true; break; }
    }
    if (!found) return 2;
    const int expected = request.op == Op::replica ? 15 : request.op == Op::status ? 4 : request.op == Op::restore ? 11 :
        request.op == Op::move ? 8 : request.op == Op::jump || request.op == Op::stop || request.op == Op::retire ? 7 :
        request.op == Op::duel ? 6 : 5;
    if (argc != expected) return 2;
    if (argc >= 5 && !number(argv[4], request.epoch)) return 2;
    for (int i = 5; i < argc; ++i) if (!number(argv[i], request.values[i - 5])) return 2;
    std::unique_ptr<Pipe> channel;
    const auto started = GetTickCount64();
    do {
        channel = std::make_unique<Pipe>();
        if (channel->client(pipe_name(request.generation, true), static_cast<DWORD>(pid))) break;
        if (channel->error() != ERROR_PIPE_BUSY && channel->error() != ERROR_FILE_NOT_FOUND) break;
        Sleep(20);
    } while (GetTickCount64() - started < 2000);
    if (channel->failed() || !channel->send(request)) {
        std::cerr << "Worker control unavailable; win32=" << channel->error() << '\n'; return 3;
    }
    Message reply;
    while (GetTickCount64() - started < 10000 && !channel->failed()) {
        if (channel->receive(reply)) {
            if (reply.kind != Kind::reply || reply.generation != request.generation || reply.sequence != 1 || reply.op != request.op || reply.values[7] > static_cast<uint64_t>(Result::failed)) return 4;
            std::cout << "{\"schema_version\":1,\"op\":\"" << op_name(reply.op) << "\",\"result\":\"" << result_name(static_cast<Result>(reply.values[7]))
                << "\",\"epoch\":" << reply.epoch << ",\"phase\":" << reply.values[0] << ",\"app_updates\":" << reply.values[1]
                << ",\"native_ai_entries\":" << reply.values[2] << ",\"actor_a\":" << reply.values[3]
                << ",\"actor_b\":" << reply.values[4] << ",\"mode\":" << reply.values[5] << ",\"request\":" << reply.values[6]
                << ",\"persistence_request\":" << reply.values[8] << ",\"persistence_state\":" << reply.values[9] << "}\n";
            return reply.values[7] == static_cast<uint64_t>(Result::accepted) ? 0 : 5;
        }
        channel->writable(); Sleep(10);
    }
    std::cerr << "Worker reply unavailable; outcome unknown. Do not retry a mutation automatically.\n";
    return 6;
}
