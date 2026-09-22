#include "native_life_abi.h"
#include "native_life_presentation.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {
// HOST ABI only. Sentinels validate register/stack/output placement, not game
// animation behavior, resource lifetime, presentation or compatibility.
struct Animal { uint32_t sentinel = 0xa15293e4; };
struct Animated {
    std::array<unsigned char, 32> sentinel{};
    Animated* receiver = nullptr;
    int mode_index = 0, mode_calls = 0, clear_calls = 0, read_calls = 0;
    uint32_t* id_output = nullptr;
    float* time_output = nullptr;
    int* state_output = nullptr;
    int* index_output = nullptr;
    __declspec(noinline) bool mode(int index) {
        receiver = this; mode_index = index; ++mode_calls; return index == -1;
    }
    __declspec(noinline) void clear() { receiver = this; ++clear_calls; }
    __declspec(noinline) void read(uint32_t* id, float* time, int* state, int* index) {
        receiver = this; ++read_calls;
        id_output = id; time_output = time; state_output = state; index_output = index;
        if (id) *id = 0xfedcba98;
        if (time) *time = -17.625f;
        if (state) *state = -123;
        if (index) *index = 0x1122330f;
    }
};
using Abi = sporemp::NativeLifeAbi<Animal, Animated>;
struct MapArguments { Animal* receiver; uint32_t id; uint32_t* output; int calls; } map_args{};
__declspec(noinline) bool __cdecl map_fixture(Animal* receiver, uint32_t id, uint32_t* output) {
    map_args = {receiver, id, output, map_args.calls + 1};
    if (output) *output = id ^ 0x81a53bc7;
    return receiver && id == 0xfedcba98;
}
template<class Function, class Member> Function address(Member member) {
    Function function{};
    static_assert(sizeof(function) == sizeof(member), "Win32 member pointer representation");
    std::memcpy(&function, &member, sizeof(function));
    return function;
}
}

int main() {
    int failures = 0;
    const auto check = [&](bool ok, const char* message) {
        std::printf("%s: %s (HOST LIFE ABI; SPORE NOT LOADED)\n", ok ? "PASS" : "FAIL", message);
        if (!ok) ++failures;
    };
    struct LifeCase {
        bool dead, native_dead;
        float health;
        uint32_t combatant_state;
        bool admitted;
        const char* description;
    };
    const LifeCase life_cases[] = {
        {true, true, 0, 0, true, "native13 reused corpse preserves independent combatant state zero"},
        {true, true, 0, 2, true, "earlier native corpse with combatant state two remains admitted"},
        {false, false, 5, 0, true, "living revival requires positive native health and state zero"},
        {true, false, 0, 0, false, "zero-health pending death does not become a corpse animation"},
        {false, true, 5, 0, false, "living projection requires the native dead flag to be clear"},
        {true, true, 1, 0, false, "positive-health corpse is rejected"},
        {true, true, -1, 2, false, "negative-health corpse is rejected"},
        {false, false, 0, 0, false, "zero-health pending state does not start revival"},
        {false, false, -1, 0, false, "negative-health living state is rejected"},
        {false, false, 5, 2, false, "living revival with corpse combatant state is rejected"},
        {true, true, 0, 1, false, "unknown corpse combatant state one is rejected"},
        {false, false, 5, 1, false, "unknown living combatant state one is rejected"},
        {true, true, 0, UINT32_MAX, false, "unknown full-width combatant state is rejected"},
        {true, true, std::numeric_limits<float>::quiet_NaN(), 0, false, "NaN corpse health is rejected"},
        {false, false, std::numeric_limits<float>::infinity(), 0, false, "positive infinite living health is rejected"},
        {true, true, -std::numeric_limits<float>::infinity(), 2, false, "negative infinite corpse health is rejected"},
    };
    for (const auto& value : life_cases)
        check(sporemp::native_life_state_admitted(value.dead, value.native_dead, value.health,
            value.combatant_state) == value.admitted, value.description);
    static_assert(sizeof(void*) == 4, "Fixture requires Win32");
    Animal animal;
    Animated model;
    model.sentinel.fill(0xa5);
    Abi::Map volatile map = &map_fixture;
    Abi::HasCurrentMode volatile mode = address<Abi::HasCurrentMode>(&Animated::mode);
    Abi::Clear volatile clear = address<Abi::Clear>(&Animated::clear);
    Abi::Read volatile read = address<Abi::Read>(&Animated::read);
    uint32_t id = 0; float time = 0; int state = 0, index = 0;
    check(map(&animal, 0xfedcba98, &id) && map_args.receiver == &animal &&
        map_args.id == 0xfedcba98 && map_args.output == &id && id == (0xfedcba98 ^ 0x81a53bc7),
        "three-word cdecl preserves primary animal, unsigned ID, output pointer and true AL");
    check(!map(nullptr, 0x81234567, nullptr) && !map_args.receiver && map_args.id == 0x81234567 &&
        !map_args.output && map_args.calls == 2,
        "cdecl null receiver/output and full-width ID retain false AL");
    check(mode(&model, -1) && !mode(&model, 0) && model.receiver == &model &&
        model.mode_index == 0 && model.mode_calls == 2,
        "one-word thiscall preserves model ECX, signed index and both AL results");
    clear(&model);
    check(model.receiver == &model && model.clear_calls == 1,
        "zero-word void thiscall receives the model exactly once");
    read(&model, &id, &time, &state, &index);
    check(model.receiver == &model && model.id_output == &id && model.time_output == &time &&
        model.state_output == &state && model.index_output == &index && id == 0xfedcba98 &&
        time == -17.625f && state == -123 && index == 0x1122330f,
        "four-word void thiscall preserves output order, float and signed values");
    id = 0; index = 0;
    read(&model, &id, nullptr, nullptr, &index);
    check(id == 0xfedcba98 && index == 0x1122330f && !model.time_output && !model.state_output &&
        model.id_output == &id && model.index_output == &index,
        "production readback shape preserves both null middle outputs");
    read(&model, nullptr, nullptr, nullptr, nullptr);
    check(!model.id_output && !model.time_output && !model.state_output && !model.index_output &&
        model.read_calls == 3, "all-null optional readback remains one valid call");
    uintptr_t stack_before = 0, stack_after = 0;
    __asm mov stack_before, esp
    for (int i = 0; i < 4096; ++i) {
        map(&animal, 0xfedcba98, &id);
        mode(&model, -1);
        clear(&model);
        read(&model, &id, nullptr, nullptr, &index);
    }
    __asm mov stack_after, esp
    check(stack_before == stack_after && map_args.calls == 4098 && model.mode_calls == 4098 &&
        model.clear_calls == 4097 && model.read_calls == 4099,
        "4096 mixed cdecl/thiscall sequences balance the Win32 stack and call counts");
    bool intact = animal.sentinel == 0xa15293e4;
    for (auto value : model.sentinel) intact = intact && value == 0xa5;
    check(intact, "all receiver sentinels survive the mixed ABI sequence");
    return failures ? 1 : 0;
}
