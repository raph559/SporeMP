#include "native_history_abi.h"
#include "stack_pointer.h"
#include "detour_transaction.h"
#include <array>
#include <cstdio>

namespace {
struct Animal {
    uint32_t sentinel = 0xab67e931;
};

using Abi = sporemp::NativeHistoryAbi<Animal>;

struct Receiver {
    uint32_t sentinel = 0xce134a97;
    unsigned constructs = 0, initializes = 0, deletes = 0;
    uint32_t flags = 0;
    bool initialize_result = true;
    unsigned creates = 0, resizes = 0;
    uint32_t category = 0, kind = 0, words = 0;
};

Receiver* root = nullptr;
void* event_result = nullptr;
Animal* event_target = nullptr;
uint32_t event_kind = 0;
unsigned get_calls = 0, event_calls = 0, event_hooks = 0;
unsigned read_calls = 0, read_hooks = 0;
void* read_output = nullptr;
uint32_t read_category = 0;

struct Stream {
    bool result = true;
};

void* serialized_receiver = nullptr;
Stream* serialized_stream = nullptr;
unsigned serialized_calls = 0;

__declspec(noinline) bool __fastcall serialize_fixture(void* self, void*, Stream* stream) {
    serialized_receiver = self;
    serialized_stream = stream;
    ++serialized_calls;
    return stream->result;
}

std::array<uintptr_t, 6> allocation{};
Abi::Get get_original = nullptr;
Abi::Event event_original = nullptr;
Abi::ReadCategory read_original = nullptr;

__declspec(noinline) void* __cdecl get_fixture() {
    ++get_calls;
    return root;
}

void* __cdecl get_hook() {
    return get_original();
}

__declspec(noinline) void* __cdecl allocate_fixture(uint32_t size, const char* name, int flags, unsigned debug,
                                                    const char* file, int line) {
    allocation = {size,  reinterpret_cast<uintptr_t>(name), static_cast<uintptr_t>(flags),
                  debug, reinterpret_cast<uintptr_t>(file), static_cast<uintptr_t>(line)};
    return root;
}

__declspec(noinline) void* __fastcall construct_fixture(Receiver* self, void*) {
    ++self->constructs;
    return self;
}

__declspec(noinline) bool __fastcall initialize_fixture(Receiver* self, void*) {
    ++self->initializes;
    return self->initialize_result;
}

__declspec(noinline) void* __fastcall delete_fixture(Receiver* self, void*, uint32_t flags) {
    ++self->deletes;
    self->flags = flags;
    return self;
}

__declspec(noinline) void* __fastcall create_entry_fixture(Receiver* self, void*, uint32_t category, uint32_t kind) {
    ++self->creates;
    self->category = category;
    self->kind = kind;
    return event_result;
}

__declspec(noinline) void __fastcall resize_payload_fixture(Receiver* self, void*, uint32_t words) {
    ++self->resizes;
    self->words = words;
}

__declspec(noinline) void* __cdecl event_fixture(Animal* target, uint32_t kind) {
    ++event_calls;
    event_target = target;
    event_kind = kind;
    return event_result;
}

void* __cdecl event_hook(Animal* target, uint32_t kind) {
    ++event_hooks;
    return event_original(target, kind);
}

__declspec(noinline) void __cdecl read_fixture(void* output, uint32_t category) {
    ++read_calls;
    read_output = output;
    read_category = category;
}

void __cdecl read_hook(void* output, uint32_t category) {
    ++read_hooks;
    read_original(output, category);
}
} // namespace

int native_history_abi_checks() {
    unsigned failures = 0;
    const auto check = [&](bool value, const char* label) {
        if (!value) {
            ++failures;
            std::fprintf(stderr, "history ABI: %s\n", label);
        }
    };
    Receiver receiver;
    Animal target;
    root = &receiver;
    Stream stream;
    Abi::Serialize<Stream> volatile serialize = reinterpret_cast<Abi::Serialize<Stream>>(&serialize_fixture);
    // A borrowed entry result is forwarded as a full pointer; no release or
    // conversion to boolean belongs in the cdecl event adapter.
    std::array<uint32_t, 8> entry{};
    event_result = entry.data();
    const char name[] = "history fixture", file[] = "fixture";
    Abi::Allocate volatile allocate = &allocate_fixture;
    Abi::Construct volatile construct = reinterpret_cast<Abi::Construct>(&construct_fixture);
    Abi::Initialize volatile initialize = reinterpret_cast<Abi::Initialize>(&initialize_fixture);
    Abi::Delete volatile destroy = reinterpret_cast<Abi::Delete>(&delete_fixture);
    Abi::CreateEntry volatile create_entry = reinterpret_cast<Abi::CreateEntry>(&create_entry_fixture);
    Abi::ResizePayload volatile resize_payload = reinterpret_cast<Abi::ResizePayload>(&resize_payload_fixture);
    get_original = &get_fixture;
    event_original = &event_fixture;
    read_original = &read_fixture;
    const sporemp::NativeHook hooks[]{{reinterpret_cast<void**>(&get_original), reinterpret_cast<void*>(get_hook)},
                                      {reinterpret_cast<void**>(&event_original), reinterpret_cast<void*>(event_hook)},
                                      {reinterpret_cast<void**>(&read_original), reinterpret_cast<void*>(read_hook)}};
    const auto attached = sporemp::change_hooks(hooks, std::size(hooks), true);
    check(attached == NO_ERROR, "original cdecl getter and event detours attach");
    if (attached == NO_ERROR) {
        Abi::Get volatile get = &get_fixture;
        Abi::Event volatile record = &event_fixture;
        Abi::ReadCategory volatile read = &read_fixture;
        uintptr_t before = 0, after = 0;
        bool outputs = true;
        SPOREMP_READ_STACK_POINTER(before);
        for (unsigned i = 0; i < 4096; ++i) {
            outputs = outputs && allocate(0x40, name, -13, 0xfac71293, file, -29) == &receiver;
            outputs = outputs && construct(&receiver) == &receiver && initialize(&receiver);
            outputs = outputs && get() == &receiver && record(&target, 0xf1720000 + i) == entry.data();
            outputs = outputs && create_entry(&receiver, 8, 0) == entry.data();
            resize_payload(&receiver, 12);
            read(entry.data(), 8);
            outputs = outputs && serialize(reinterpret_cast<unsigned char*>(&receiver) + 8, &stream);
            outputs = outputs && destroy(&receiver, 1) == &receiver;
        }
        SPOREMP_READ_STACK_POINTER(after);
        check(before == after && outputs, "all lifecycle and two-argument cdecl calls preserve stack and results");
        check(serialized_calls == 4096 && serialized_receiver == reinterpret_cast<unsigned char*>(&receiver) + 8 &&
                  serialized_stream == &stream,
              "history serializer preserves adjusted native receiver and original stream pointer");
        stream.result = false;
        check(!serialize(reinterpret_cast<unsigned char*>(&receiver) + 8, &stream),
              "history serializer failure is not converted to success");
        check(receiver.constructs == 4096 && receiver.initializes == 4096 && receiver.deletes == 4096 &&
                  get_calls == 4096 && event_calls == 4096 && event_hooks == 4096,
              "original operations execute exactly once");
        check(
            receiver.creates == 4096 && receiver.resizes == 4096 && receiver.category == 8 && receiver.kind == 0 &&
                receiver.words == 12,
            "original entry creation and payload resize preserve receiver, arguments and stack without reward replay");
        check(read_calls == 4096 && read_hooks == 4096 && read_output == entry.data() && read_category == 8,
              "original category query forwards output storage and category once through cdecl detour");
        check(receiver.sentinel == 0xce134a97 && receiver.flags == 1 && target.sentinel == 0xab67e931 &&
                  event_target == &target && event_kind == 0xf1720fff,
              "receivers, target and complete event kind survive adapters");
        check(allocation == std::array<uintptr_t, 6>{0x40, reinterpret_cast<uintptr_t>(name),
                                                     static_cast<uintptr_t>(-13), 0xfac71293,
                                                     reinterpret_cast<uintptr_t>(file), static_cast<uintptr_t>(-29)},
              "native allocation preserves all six arguments and exact size64");
        receiver.initialize_result = false;
        root = nullptr;
        event_result = nullptr;
        check(!initialize(&receiver) && !get() && !record(nullptr, 5) && !event_target && event_kind == 5,
              "native initialization failure, null receiver result and null borrowed event are preserved");
        check(!create_entry(&receiver, 8, 0), "null borrowed entry is preserved for caller failure handling");
        check(sporemp::change_hooks(hooks, std::size(hooks), false) == NO_ERROR, "history detours detach");
    }
    using sporemp::native_history_first_kill;
    using sporemp::native_history_manager_caller;
    check(native_history_manager_caller(0x7a57ff) && native_history_manager_caller(0x7a5813),
          "ordinary native species history lookup and create are admitted");
    check(native_history_manager_caller(0x7a57c4) && native_history_manager_caller(0x7a57db) &&
              !native_history_first_kill(0, 7, 0x7a4d4d) && !native_history_first_kill(0, 8, 0x7a4d4d),
          "known exceptional native kinds remain contained but fail gameplay acceptance");
    check(!native_history_manager_caller(0x7a5919) && !native_history_manager_caller(0xa396d0) &&
              !native_history_manager_caller(0x7a57fa) && !native_history_manager_caller(0x7a5812),
          "unrelated history writers, call instruction addresses and adjacent bytes stay excluded");
    check(native_history_first_kill(0, 5, 0x7a4d4d) && !native_history_first_kill(2, 5, 0x7a4d4d) &&
              !native_history_first_kill(0, 6, 0x7a4d4d) && !native_history_first_kill(0, 5, 0x7a4d48),
          "only original first-kill event5 is accepted; damage reset, extinction and wrong caller are refused");
    using sporemp::native_history_one_event_added;
    // Trial315: player 2's first kill of a second species took its history from one event to two.
    check(native_history_one_event_added(0, 0, 1, 1) && native_history_one_event_added(1, 1, 2, 1) &&
              native_history_one_event_added(1, 1, 2, 2),
          "one event more, in a category that was empty or already held events, is the original's first-kill result");
    check(!native_history_one_event_added(1, 1, 1, 1) && !native_history_one_event_added(1, 1, 3, 1) &&
              !native_history_one_event_added(1, 1, 2, 3) && !native_history_one_event_added(2, 2, 3, 1) &&
              !native_history_one_event_added(0, 0, 1, 0),
          "no event, two events, two new categories or a lost category are refused");
    check(sporemp::native_history_relationship_caller(0x7a58b3) &&
              !sporemp::native_history_relationship_caller(0x82ee8e) &&
              !sporemp::native_history_relationship_caller(0x7a58ae),
          "history relation read is exact and does not admit the shared objective");
    check(sporemp::native_history_ui_reader(0x269318, 8) && !sporemp::native_history_ui_reader(0x269318, 12) &&
              !sporemp::native_history_ui_reader(0x269313, 8) && !sporemp::native_history_ui_reader(0x269319, 8),
          "only exact original Creature history query is selected; Space and adjacent sites remain primary");
    return static_cast<int>(failures);
}
