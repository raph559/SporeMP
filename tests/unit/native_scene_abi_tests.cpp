#include "detour_transaction.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {
// Host-only ABI fixture. These values verify MSVC's secondary-this adjustment,
// reference arguments and stack cleanup; they do not simulate SPORE movement.
struct Vector { float x, y, z; };
struct Quaternion { float x, y, z, w; };
struct Primary {
    virtual void primary() {}
    std::array<unsigned char, 0xbc> sentinel{};
};
static_assert(sizeof(Primary) == 0xc0, "Win32 Creature spatial-base offset fixture");
struct Spatial {
    virtual void slot00() {}
    virtual void slot04() {}
    virtual void slot08() {}
    virtual void slot0c() {}
    virtual void slot10() {}
    virtual void slot14() {}
    virtual void slot18() {}
    virtual void slot1c() {}
    virtual void slot20() {}
    virtual void slot24() {}
    virtual void slot28() {}
    virtual const Vector& get_position() { return position; }
    virtual const Quaternion& get_orientation() { return orientation; }
    virtual float get_scale() { return 1; }
    __declspec(noinline) virtual void set_position(const Vector& value) {
        received_this = this; received_position = &value; position = value; ++positions;
    }
    __declspec(noinline) virtual void set_orientation(const Quaternion& value) {
        received_this = this; received_orientation = &value; orientation = value; ++orientations;
    }
    Vector position{};
    Quaternion orientation{};
    Spatial* received_this = nullptr;
    const Vector* received_position = nullptr;
    const Quaternion* received_orientation = nullptr;
    uint32_t positions = 0, orientations = 0;
};
struct Creature : Primary, Spatial {};
using SetPosition = void(__thiscall*)(Spatial*, const Vector&);
using SetOrientation = void(__thiscall*)(Spatial*, const Quaternion&);
SetPosition position_original = nullptr;
SetOrientation orientation_original = nullptr;
Spatial* position_receiver = nullptr;
Spatial* orientation_receiver = nullptr;
const Vector* position_argument = nullptr;
const Quaternion* orientation_argument = nullptr;
uint32_t position_hooks = 0, orientation_hooks = 0;
void __fastcall position_hook(Spatial* self, void*, const Vector& value) {
    ++position_hooks; position_receiver = self; position_argument = &value;
    position_original(self, value);
}
void __fastcall orientation_hook(Spatial* self, void*, const Quaternion& value) {
    ++orientation_hooks; orientation_receiver = self; orientation_argument = &value;
    orientation_original(self, value);
}
__declspec(noinline) void project(Creature* receiver, const Vector& position, const Quaternion& orientation) {
    receiver->set_position(position);
    receiver->set_orientation(orientation);
}
}

int native_scene_abi_checks() {
    int checks = 0, failures = 0;
    const auto check = [&](bool ok, const char* message) {
        ++checks;
        std::printf("%s: %s (HOST SCENE ABI; SPORE NOT LOADED)\n", ok ? "PASS" : "FAIL", message);
        if (!ok) ++failures;
    };
    Creature creature;
    creature.sentinel.fill(0xa5);
    auto spatial = static_cast<Spatial*>(&creature);
    check(reinterpret_cast<uintptr_t>(spatial) - reinterpret_cast<uintptr_t>(&creature) == 0xc0,
          "MSVC adjusts the Creature receiver to the secondary Spatial base");
    const auto table = *reinterpret_cast<const uintptr_t* const*>(spatial);
    position_original = reinterpret_cast<SetPosition>(table[0x38 / 4]);
    orientation_original = reinterpret_cast<SetOrientation>(table[0x3c / 4]);
    const sporemp::NativeHook hooks[]{
        {reinterpret_cast<void**>(&position_original), reinterpret_cast<void*>(position_hook)},
        {reinterpret_cast<void**>(&orientation_original), reinterpret_cast<void*>(orientation_hook)}
    };
    const auto attached = sporemp::change_hooks(hooks, std::size(hooks), true);
    check(attached == NO_ERROR, "both virtual pose implementations attach with Detours");
    if (attached == NO_ERROR) {
        const Vector position{-128.25f, 0.0625f, 981.5f};
        const Quaternion orientation{0.5f, -0.5f, 0.5f, -0.5f};
        project(&creature, position, orientation);
        check(position_receiver == spatial && orientation_receiver == spatial && creature.received_this == spatial,
              "both hooks and originals receive Spatial this, not primary Creature this");
        check(position_argument == &position && orientation_argument == &orientation &&
              creature.received_position == &position && creature.received_orientation == &orientation,
              "vector and quaternion const references remain the original arguments");
        check(!std::memcmp(&creature.position, &position, sizeof(position)) &&
              !std::memcmp(&creature.orientation, &orientation, sizeof(orientation)),
              "all signed vector and quaternion components survive interception");
        check(position_hooks == 1 && orientation_hooks == 1 && creature.positions == 1 && creature.orientations == 1,
              "one call executes each virtual pose original exactly once");
        uintptr_t stack_before = 0, stack_after = 0;
        __asm mov stack_before, esp
        for (size_t i = 0; i < 4096; ++i) project(&creature, position, orientation);
        __asm mov stack_after, esp
        check(stack_before == stack_after && position_hooks == 4097 && orientation_hooks == 4097,
              "4096 paired thiscall reference calls retain Win32 stack balance");
        bool intact = true;
        for (auto byte : creature.sentinel) intact = intact && byte == 0xa5;
        check(intact, "secondary pose writes preserve the complete primary-base sentinel");
        check(sporemp::change_hooks(hooks, std::size(hooks), false) == NO_ERROR,
              "both pose hooks detach cleanly");
        project(&creature, position, orientation);
        check(creature.positions == 4098 && creature.orientations == 4098 && position_hooks == 4097 && orientation_hooks == 4097,
              "ordinary virtual calls retain their original behavior after detach");
    }
    return failures ? -failures : checks;
}
