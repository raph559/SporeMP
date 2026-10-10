#include "detour_transaction.h"
#include "stack_pointer.h"
#include "../../src/bridge/native_scene_avatar_abi.h"
#include "../../src/bridge/native_scene_portrait_abi.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {
// Host-only ABI fixture. These values verify MSVC's secondary-this adjustment,
// reference arguments and stack cleanup; they do not simulate SPORE movement.
struct AvatarReceiver {
    uintptr_t sentinel = 0;
    void* received = nullptr;
    unsigned calls = 0;
};

__declspec(noinline) void __fastcall avatar_assignment_fixture(AvatarReceiver* receiver, void*, void* actor) {
    receiver->received = actor;
    ++receiver->calls;
}

struct PortraitKey {
    uint32_t instance, type, group;
};

struct PortraitWindow {
    uintptr_t sentinel;
};

struct PortraitLayout {
    PortraitWindow* result;
    uint32_t control = 0;
    bool recursive = false;
    unsigned calls = 0;
};

PortraitWindow* portrait_window = nullptr;
const PortraitKey* portrait_key = nullptr;
int portrait_index = 0;
unsigned portrait_calls = 0;
bool portrait_return = false;

__declspec(noinline) bool __cdecl portrait_setter_fixture(PortraitWindow* window, const PortraitKey& key, int index) {
    portrait_window = window;
    portrait_key = &key;
    portrait_index = index;
    ++portrait_calls;
    return portrait_return;
}

__declspec(noinline)
PortraitWindow* __fastcall portrait_lookup_fixture(PortraitLayout* layout, void*, uint32_t control, bool recursive) {
    layout->control = control;
    layout->recursive = recursive;
    ++layout->calls;
    return layout->result;
}

struct Vector {
    float x, y, z;
};

struct Quaternion {
    float x, y, z, w;
};

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

    virtual const Vector& get_position() {
        return position;
    }

    virtual const Quaternion& get_orientation() {
        return orientation;
    }

    virtual float get_scale() {
        return 1;
    }

    __declspec(noinline) virtual void set_position(const Vector& value) {
        received_this = this;
        received_position = &value;
        position = value;
        ++positions;
    }

    __declspec(noinline) virtual void set_orientation(const Quaternion& value) {
        received_this = this;
        received_orientation = &value;
        orientation = value;
        ++orientations;
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
    ++position_hooks;
    position_receiver = self;
    position_argument = &value;
    position_original(self, value);
}

void __fastcall orientation_hook(Spatial* self, void*, const Quaternion& value) {
    ++orientation_hooks;
    orientation_receiver = self;
    orientation_argument = &value;
    orientation_original(self, value);
}

__declspec(noinline) void project(Creature* receiver, const Vector& position, const Quaternion& orientation) {
    receiver->set_position(position);
    receiver->set_orientation(orientation);
}
} // namespace

int native_scene_abi_checks() {
    int checks = 0, failures = 0;
    const auto check = [&](bool ok, const char* message) {
        ++checks;
        std::printf("%s: %s (HOST SCENE ABI; SPORE NOT LOADED)\n", ok ? "PASS" : "FAIL", message);
        if (!ok) {
            ++failures;
        }
    };
    AvatarReceiver receiver1{0x51a20c39}, receiver2{0xa3b41d27};
    const auto assign =
        reinterpret_cast<sporemp::NativeAvatarAssignment<AvatarReceiver, void>>(avatar_assignment_fixture);
    uintptr_t before = 0, after = 0;
    SPOREMP_READ_STACK_POINTER(before);
    for (unsigned i = 0; i < 4096; ++i) {
        assign(&receiver1, &receiver2);
        assign(&receiver2, nullptr);
    }
    SPOREMP_READ_STACK_POINTER(after);
    check(before == after && receiver1.calls == 4096 && receiver2.calls == 4096 && receiver1.received == &receiver2 &&
              receiver2.received == nullptr && receiver1.sentinel == 0x51a20c39 && receiver2.sentinel == 0xa3b41d27,
          "SetAvatar ABI retains manager ECX, full pointer/null argument, one call and RET4 stack balance");
    PortraitWindow portrait{0xa541b08e};
    PortraitLayout layout{&portrait};
    const PortraitKey key{0xe18972ab, 0x2f7d0004, 0x40380000};
    const auto set_portrait =
        static_cast<sporemp::NativePortraitSetBackground<PortraitWindow, PortraitKey>>(portrait_setter_fixture);
    const auto find_portrait =
        reinterpret_cast<sporemp::NativePortraitFindWindow<PortraitLayout, PortraitWindow>>(portrait_lookup_fixture);
    unsigned original_true = 0, original_false = 0;
    SPOREMP_READ_STACK_POINTER(before);
    for (unsigned i = 0; i < 4096; ++i) {
        portrait_return = (i & 1) != 0;
        auto found = find_portrait(&layout, sporemp::native_portrait_control, true);
        if (set_portrait(found, key, -1)) {
            ++original_true;
        } else {
            ++original_false;
        }
    }
    SPOREMP_READ_STACK_POINTER(after);
    check(before == after && layout.calls == 4096 && layout.control == sporemp::native_portrait_control &&
              layout.recursive && portrait_calls == 4096 && portrait_window == &portrait && portrait_key == &key &&
              portrait_index == -1 && original_true == 2048 && original_false == 2048 &&
              portrait.sentinel == 0xa541b08e && key.instance == 0xe18972ab && key.type == 0x2f7d0004 &&
              key.group == 0x40380000,
          "portrait lookup RET8 and image setter cdecl retain key reference, signed index, original bool and stack "
          "balance");
    layout.result = nullptr;
    check(find_portrait(&layout, sporemp::native_portrait_control, false) == nullptr && !layout.recursive,
          "portrait lookup preserves an original null result without substituting another control");
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
        {reinterpret_cast<void**>(&orientation_original), reinterpret_cast<void*>(orientation_hook)}};
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
        SPOREMP_READ_STACK_POINTER(stack_before);
        for (size_t i = 0; i < 4096; ++i) {
            project(&creature, position, orientation);
        }
        SPOREMP_READ_STACK_POINTER(stack_after);
        check(stack_before == stack_after && position_hooks == 4097 && orientation_hooks == 4097,
              "4096 paired thiscall reference calls retain Win32 stack balance");
        bool intact = true;
        for (auto byte : creature.sentinel) {
            intact = intact && byte == 0xa5;
        }
        check(intact, "secondary pose writes preserve the complete primary-base sentinel");
        check(sporemp::change_hooks(hooks, std::size(hooks), false) == NO_ERROR, "both pose hooks detach cleanly");
        project(&creature, position, orientation);
        check(creature.positions == 4098 && creature.orientations == 4098 && position_hooks == 4097 &&
                  orientation_hooks == 4097,
              "ordinary virtual calls retain their original behavior after detach");
    }
    return failures ? -failures : checks;
}
