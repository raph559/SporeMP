#include "peer.h"
#include <windows.h>
#include <iostream>
#include <string>

using namespace sporemp::network;

namespace {
int digit(char character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    return -1;
}

bool parse(const std::string& encoded, ContentWire& out) {
    if (encoded.size() != out.size() * 2) {
        return false;
    }
    for (size_t n = 0; n < out.size(); ++n) {
        const int high = digit(encoded[n * 2]), low = digit(encoded[n * 2 + 1]);
        if (high < 0 || low < 0) {
            return false;
        }
        out[n] = uint8_t(high * 16 + low);
    }
    return true;
}

std::string text(const ContentWire& bytes) {
    constexpr char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (auto byte : bytes) {
        out += hex[byte >> 4];
        out += hex[byte & 15];
    }
    return out;
}

int fail(const char* code) {
    std::cout << "{\"event\":\"error\",\"code\":\"" << code << "\"}\n" << std::flush;
    return 2;
}

int stop_and_fail(Peer& peer, const char* code) {
    peer.stop();
    return fail(code);
}

void write_authenticated(const Packet& packet) {
    std::cout << "{\"event\":\"authenticated\",\"player\":" << packet.player << ",\"session\":" << packet.session
              << ",\"native_gameplay_client\":false}\n"
              << std::flush;
}

void write_content(const Packet& packet) {
    std::cout << "{\"event\":\"content\",\"payload\":\"" << text(packet.content) << "\"}\n" << std::flush;
}

void write_scene_metadata(const Packet& packet) {
    std::cout << "{\"event\":\"scene_metadata\",\"scene\":" << packet.scene << ",\"baseline\":" << packet.baseline
              << ",\"entities\":" << packet.count << ",\"native_baseline_applied\":false}\n"
              << std::flush;
}

// Writes a row for each event the network delivered, at most 4096 in one pass. Returns the code the client fails
// with, or nullptr while it goes on.
const char* report_network_events(Peer& peer, bool& welcomed) {
    Event incoming;
    size_t count = 0;
    while (count++ < 4096 && peer.poll(incoming)) {
        if (incoming.kind == EventKind::disconnected) {
            return "network_disconnected";
        }
        if (incoming.kind != EventKind::packet) {
            continue;
        }
        const auto& packet = incoming.packet;
        if (packet.kind == Kind::welcome) {
            welcomed = true;
            write_authenticated(packet);
        } else if (packet.kind == Kind::reject) {
            return "session_rejected";
        } else if (packet.kind == Kind::content_response) {
            write_content(packet);
        } else if (packet.kind == Kind::scene_end) {
            write_scene_metadata(packet);
        }
    }
    return nullptr;
}

// One line of standard input is one content request in hex, accepted only after the welcome. Returns the code the
// client fails with, or nullptr once the request is queued.
const char* send_content_request(Peer& peer, bool welcomed, const std::string& line) {
    ContentWire bytes;
    ContentFrame frame;
    if (!welcomed || !parse(line, bytes) || !decode_content(bytes, frame) || !frame.request ||
        !content_request_op(frame.op) ||
        (frame.failure != ContentFailure::none && frame.op != ContentOp::dependency_failure)) {
        return "invalid_or_premature_content_request";
    }
    Packet packet;
    packet.kind = Kind::content_request;
    packet.content = bytes;
    if (!peer.send(packet)) {
        return "output_queue_full";
    }
    return nullptr;
}

// Reads the bytes waiting on standard input, at most 4096 in one pass, and sends each line they complete. `line`
// keeps an unfinished line for the next pass. `done` is set by the line `quit` and by a read that fails. Returns the
// code the client fails with, or nullptr while it goes on.
const char* forward_requests(HANDLE input, DWORD available, Peer& peer, bool welcomed, std::string& line, bool& done) {
    const auto bounded = available < 4096 ? available : 4096;
    for (DWORD n = 0; n < bounded; ++n) {
        char character = 0;
        DWORD read = 0;
        if (!ReadFile(input, &character, 1, &read, nullptr) || read != 1) {
            done = true;
            break;
        }
        if (character == '\r') {
            continue;
        }
        if (character != '\n') {
            if (line.size() >= content_wire_bytes * 2) {
                return "stdin_frame_too_long";
            }
            line += character;
            continue;
        }
        if (line == "quit") {
            done = true;
            break;
        }
        if (const char* failure = send_content_request(peer, welcomed, line)) {
            return failure;
        }
        line.clear();
    }
    return nullptr;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    const bool fixture = argc == 4 && std::wstring(argv[1]) == L"--fixture-authority";
    const int config_arg = fixture ? 2 : 1;
    if ((argc != 3 && !fixture) || std::wstring(argv[config_arg]) != L"--config") {
        return fail("usage_config_path_required");
    }
    const auto input = GetStdHandle(STD_INPUT_HANDLE);
    if (input == INVALID_HANDLE_VALUE || GetFileType(input) != FILE_TYPE_PIPE) {
        return fail("private_stdin_pipe_required");
    }
    PeerConfig config;
    std::string error;
    if (!read_peer_config(argv[config_arg + 1], config, error) ||
        config.role != (fixture ? Role::authority : Role::player)) {
        return fail("invalid_role_configuration");
    }
    Peer peer;
    if (!peer.start(config, error)) {
        return fail("network_start_failed");
    }
    bool welcomed = false, done = false;
    std::string line;
    const auto deadline = GetTickCount64() + 600000;
    while (!done && GetTickCount64() < deadline) {
        if (const char* failure = report_network_events(peer, welcomed)) {
            return stop_and_fail(peer, failure);
        }
        DWORD available = 0;
        if (!PeekNamedPipe(input, nullptr, 0, nullptr, &available, nullptr)) {
            // A closed pipe ends the client cleanly, as `quit` does.
            if (GetLastError() == ERROR_BROKEN_PIPE) {
                done = true;
                break;
            }
            return stop_and_fail(peer, "stdin_unavailable");
        }
        if (const char* failure = forward_requests(input, available, peer, welcomed, line, done)) {
            return stop_and_fail(peer, failure);
        }
        Sleep(1);
    }
    peer.stop();
    if (!done) {
        return fail("bounded_client_deadline");
    }
    std::cout << "{\"event\":\"closed\",\"clean\":true}\n" << std::flush;
    return 0;
}
