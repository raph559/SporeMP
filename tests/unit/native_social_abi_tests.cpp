#include "native_social_abi.h"
#include "stack_pointer.h"
#include "native_death_abi.h"
#include "detour_transaction.h"
#include <cstdio>
#include <iterator>
#include <cstring>

namespace {
struct Creature {
    unsigned calls = 0;
    int index = 0;
};

using Input = sporemp::NativeSocialAbi<Creature>::Input;
using Deactivate = sporemp::NativeSocialAbi<Creature>::Deactivate;
using SelectRoot = sporemp::NativeSocialAbi<Creature>::SelectRoot;
using Tick = sporemp::NativeSocialAbi<Creature>::Tick;
using Result = sporemp::NativeSocialAbi<Creature>::Result;
using Pause = sporemp::NativeSocialAbi<Creature>::Pause;
using MeterVisible = sporemp::NativeSocialAbi<Creature>::MeterVisible;
using MeterRelation = sporemp::NativeSocialAbi<Creature>::MeterRelation;

struct Meter {
    unsigned calls = 0;
    bool visible = false;
    uint32_t relation = 0;
};

__declspec(noinline) bool __fastcall meter_fixture(Meter* self, void*, bool visible) {
    ++self->calls;
    self->visible = visible;
    return visible;
}

MeterRelation relation_original = nullptr;
unsigned relation_hooks = 0;

__declspec(noinline) void __fastcall relation_fixture(Meter* self, void*, uint32_t relation) {
    ++self->calls;
    self->relation = relation;
}

void __fastcall relation_hook(void* self, void*, uint32_t relation) {
    ++relation_hooks;
    relation_original(self, relation);
}

struct Tree {
    unsigned calls = 0;
    const void* root = nullptr;
};

Input original = nullptr;
unsigned hook_calls = 0, original_calls = 0;
Creature* received = nullptr;
int received_index = 0;

__declspec(noinline) void __cdecl input_fixture(Creature* actor, int index) {
    ++original_calls;
    received = actor;
    received_index = index;
    if (actor) {
        ++actor->calls;
        actor->index = index;
    }
}

void __cdecl input_hook(Creature* actor, int index) {
    ++hook_calls;
    original(actor, index);
}

__declspec(noinline) void __cdecl deactivate_fixture(Creature* actor) {
    received = actor;
    if (actor) {
        ++actor->calls;
        actor->index = -1;
    }
}

__declspec(noinline) void __fastcall select_root_fixture(Tree* self, void*, const void* root) {
    ++self->calls;
    self->root = root;
}

struct TickArguments {
    Creature* actor = nullptr;
    double time = 0;
    uint32_t flags = 0, data = 0;
    void* state = nullptr;
    void* context = nullptr;
    float delta = 0;
} tick_received;

Tick tick_original = nullptr;
Result result_original = nullptr;
unsigned tick_calls = 0, tick_hooks = 0, result_calls = 0, result_hooks = 0;
void* result_receiver = nullptr;
Creature* result_npc = nullptr;
Creature* result_actor = nullptr;

__declspec(noinline) uint32_t __cdecl tick_fixture(Creature* actor, double time, uint32_t flags, uint32_t data,
                                                   void* state, void* context, float delta) {
    ++tick_calls;
    tick_received = {actor, time, flags, data, state, context, delta};
    return 0xabcdef01;
}

uint32_t __cdecl tick_hook(Creature* actor, double time, uint32_t flags, uint32_t data, void* state, void* context,
                           float delta) {
    ++tick_hooks;
    return tick_original(actor, time, flags, data, state, context, delta);
}

__declspec(noinline) uint32_t __fastcall result_fixture(void* self, void*, Creature* npc, Creature* actor) {
    ++result_calls;
    result_receiver = self;
    result_npc = npc;
    result_actor = actor;
    return 0x81234567;
}

uint32_t __fastcall result_hook(void* self, void*, Creature* npc, Creature* actor) {
    ++result_hooks;
    return result_original(self, npc, actor);
}

Pause pause_original = nullptr;
void* pause_receiver = nullptr;
uint32_t pause_type = 0;
unsigned pause_calls = 0, pause_hooks = 0;

__declspec(noinline) int __fastcall pause_fixture(void* self, void*, uint32_t type) {
    ++pause_calls;
    pause_receiver = self;
    pause_type = type;
    return -7;
}

int __fastcall pause_hook(void* self, void*, uint32_t type) {
    ++pause_hooks;
    return pause_original(self, type);
}

sporemp::NativeDeathAbi::Begin death_begin_original = nullptr;
sporemp::NativeDeathAbi::Message death_message_original = nullptr;
unsigned death_begin_calls = 0, death_begin_hooks = 0, death_message_calls = 0, death_message_hooks = 0;
void* death_receiver = nullptr;
void* death_payload = nullptr;
uint32_t death_reason = 0, death_message = 0;

__declspec(noinline) void __fastcall death_begin_fixture(void* self, void*, uint32_t reason) {
    ++death_begin_calls;
    death_receiver = self;
    death_reason = reason;
}

void __fastcall death_begin_hook(void* self, void*, uint32_t reason) {
    ++death_begin_hooks;
    death_begin_original(self, reason);
}

__declspec(noinline) bool __fastcall death_message_fixture(void* self, void*, uint32_t message, void* payload) {
    ++death_message_calls;
    death_receiver = self;
    death_message = message;
    death_payload = payload;
    return payload != nullptr;
}

bool __fastcall death_message_hook(void* self, void*, uint32_t message, void* payload) {
    ++death_message_hooks;
    return death_message_original(self, message, payload);
}

using DeathAbi = sporemp::NativeDeathAbi;

struct DeathLeaf {
    unsigned ticks = 0, resets = 0, parameters = 0, activations = 0, deactivations = 0;
    float delta = 0;
    uint32_t key = 0, request = 0;
    const void* value = nullptr;
};

DeathAbi::CinematicParameter parameter_original = nullptr;
DeathAbi::UiActivate activate_original = nullptr;
DeathAbi::UiDeactivate deactivate_original = nullptr;
bool suppress_death_ui = false;
unsigned parameter_hooks = 0, activate_hooks = 0, deactivate_hooks = 0;

__declspec(noinline) bool __fastcall death_tick_fixture(DeathLeaf* self, void*, float delta) {
    ++self->ticks;
    self->delta = delta;
    return delta > 0;
}

__declspec(noinline) bool __fastcall death_reset_fixture(DeathLeaf* self, void*) {
    ++self->resets;
    return true;
}

__declspec(noinline) void* __fastcall death_destroy_fixture(DeathLeaf* self, void*, uint32_t flags) {
    self->request = flags;
    return self;
}

__declspec(noinline) void __fastcall parameter_fixture(DeathLeaf* self, void*, uint32_t key, const void* value,
                                                       uint32_t request) {
    ++self->parameters;
    self->key = key;
    self->value = value;
    self->request = request;
}

void __fastcall parameter_hook(void* self, void*, uint32_t key, const void* value, uint32_t request) {
    ++parameter_hooks;
    if (!suppress_death_ui) {
        parameter_original(self, key, value, request);
    }
}

__declspec(noinline) void __fastcall activate_fixture(DeathLeaf* self, void*, uint32_t mode, uint32_t flags) {
    ++self->activations;
    self->key = mode;
    self->request = flags;
}

void __fastcall activate_hook(void* self, void*, uint32_t mode, uint32_t flags) {
    ++activate_hooks;
    if (!suppress_death_ui) {
        activate_original(self, mode, flags);
    }
}

__declspec(noinline) void __fastcall death_deactivate_fixture(DeathLeaf* self, void*) {
    ++self->deactivations;
}

void __fastcall deactivate_hook(void* self, void*) {
    ++deactivate_hooks;
    if (!suppress_death_ui) {
        deactivate_original(self);
    }
}
} // namespace

int native_social_abi_checks() {
    int failures = 0;
    const auto check = [&](bool result, const char* message) {
        std::printf("%s: %s (HOST ABI FIXTURE; SPORE NOT LOADED)\n", result ? "PASS" : "FAIL", message);
        if (!result) {
            ++failures;
        }
    };
    original = &input_fixture;
    sporemp::NativeHook hooks[] = {{reinterpret_cast<void**>(&original), reinterpret_cast<void*>(input_hook)}};
    const auto attached = sporemp::change_hooks(hooks, std::size(hooks), true);
    check(attached == NO_ERROR, "social original-input cdecl observer attaches");
    if (attached != NO_ERROR) {
        return failures;
    }
    Input volatile call = &input_fixture;
    Creature actor;
    call(&actor, 37);
    check(received == &actor && received_index == 37 && actor.index == 37 && actor.calls == 1 && hook_calls == 1 &&
              original_calls == 1,
          "social input observer preserves both stack slots and one original call");
    call(nullptr, -1);
    check(!received && received_index == -1 && hook_calls == 2 && original_calls == 2,
          "social input observer retains native null and negative-index path");
    uintptr_t before = 0, after = 0;
    SPOREMP_READ_STACK_POINTER(before);
    for (int i = 0; i < 4096; ++i) {
        call(&actor, i);
    }
    SPOREMP_READ_STACK_POINTER(after);
    check(before == after && actor.calls == 4097 && hook_calls == 4098 && original_calls == 4098 && actor.index == 4095,
          "4096 original social-input calls preserve caller stack and exact call count");
    check(sporemp::change_hooks(hooks, std::size(hooks), false) == NO_ERROR, "social input observer detaches");
    Deactivate volatile deactivate = &deactivate_fixture;
    SelectRoot volatile select_root = reinterpret_cast<SelectRoot>(&select_root_fixture);
    Creature reset_actor;
    Tree tree;
    unsigned native_root = 0x31bef048;
    SPOREMP_READ_STACK_POINTER(before);
    for (int i = 0; i < 4096; ++i) {
        deactivate(&reset_actor);
        select_root(&tree, &native_root);
    }
    SPOREMP_READ_STACK_POINTER(after);
    check(before == after && received == &reset_actor && reset_actor.calls == 4096 && reset_actor.index == -1 &&
              tree.calls == 4096 && tree.root == &native_root,
          "native deactivation cdecl and tree-selection thiscall preserve receiver, root, exact counts and stack");
    tick_original = &tick_fixture;
    result_original = reinterpret_cast<Result>(&result_fixture);
    sporemp::NativeHook social_hooks[] = {
        {reinterpret_cast<void**>(&tick_original), reinterpret_cast<void*>(tick_hook)},
        {reinterpret_cast<void**>(&result_original), reinterpret_cast<void*>(result_hook)}};
    const auto social_attached = sporemp::change_hooks(social_hooks, std::size(social_hooks), true);
    check(social_attached == NO_ERROR, "native social tick/result ABI fixtures attach");
    if (social_attached != NO_ERROR) {
        return failures;
    }
    Tick volatile tick = &tick_fixture;
    Result volatile result = reinterpret_cast<Result>(&result_fixture);
    Creature npc;
    const double native_time = 1.0000000000000002;
    const float delta = -0.125f;
    uint32_t tick_result = 0, result_value = 0;
    SPOREMP_READ_STACK_POINTER(before);
    for (unsigned i = 0; i < 4096; ++i) {
        tick_result = tick(&actor, native_time, 0x89abcdef, 0x12345678, &tree, &native_root, delta);
        result_value = result(&tree, &npc, &actor);
    }
    SPOREMP_READ_STACK_POINTER(after);
    check(before == after && tick_calls == 4096 && tick_hooks == 4096 && result_calls == 4096 && result_hooks == 4096,
          "native social tick/result exact-once forwarding preserves caller and callee stack cleanup");
    check(tick_received.actor == &actor && std::memcmp(&tick_received.time, &native_time, sizeof(double)) == 0 &&
              tick_received.flags == 0x89abcdef && tick_received.data == 0x12345678 && tick_received.state == &tree &&
              tick_received.context == &native_root && std::memcmp(&tick_received.delta, &delta, sizeof(float)) == 0 &&
              tick_result == 0xabcdef01,
          "social tick preserves all eight native argument words and full EAX result");
    check(result_receiver == &tree && result_npc == &npc && result_actor == &actor && result_value == 0x81234567,
          "social result preserves context ECX, both creature slots and native animation return");
    check(sporemp::change_hooks(social_hooks, std::size(social_hooks), false) == NO_ERROR,
          "native social tick/result fixtures detach");
    pause_original = reinterpret_cast<Pause>(&pause_fixture);
    sporemp::NativeHook pause_binding[] = {
        {reinterpret_cast<void**>(&pause_original), reinterpret_cast<void*>(pause_hook)}};
    const auto pause_attached = sporemp::change_hooks(pause_binding, std::size(pause_binding), true);
    check(pause_attached == NO_ERROR, "native time-manager pause/resume ABI fixture attaches");
    if (pause_attached != NO_ERROR) {
        return failures;
    }
    Pause volatile pause = reinterpret_cast<Pause>(&pause_fixture);
    int pause_result = 0;
    SPOREMP_READ_STACK_POINTER(before);
    for (unsigned i = 0; i < 4096; ++i) {
        pause_result = pause(&tree, 0x80000000u + i);
    }
    SPOREMP_READ_STACK_POINTER(after);
    check(before == after && pause_calls == 4096 && pause_hooks == 4096 && pause_receiver == &tree &&
              pause_type == 0x80000fff && pause_result == -7,
          "pause/resume observer ABI preserves ECX, all type bits, signed counter, exact calls and stack cleanup");
    check(sporemp::change_hooks(pause_binding, std::size(pause_binding), false) == NO_ERROR,
          "native pause/resume ABI fixture detaches");
    Meter meter;
    MeterVisible volatile show = reinterpret_cast<MeterVisible>(&meter_fixture);
    bool shown = false, hidden = true;
    SPOREMP_READ_STACK_POINTER(before);
    for (unsigned i = 0; i < 4096; ++i) {
        shown = show(&meter, true);
        hidden = show(&meter, false);
    }
    SPOREMP_READ_STACK_POINTER(after);
    check(before == after && meter.calls == 8192 && !meter.visible && shown && !hidden,
          "social meter thiscall preserves receiver, boolean stack word, AL result and RET 4 cleanup");
    relation_original = reinterpret_cast<MeterRelation>(&relation_fixture);
    sporemp::NativeHook relation_binding[] = {
        {reinterpret_cast<void**>(&relation_original), reinterpret_cast<void*>(relation_hook)}};
    const auto relation_attached = sporemp::change_hooks(relation_binding, std::size(relation_binding), true);
    check(relation_attached == NO_ERROR, "native social meter relationship observer attaches");
    if (relation_attached != NO_ERROR) {
        return failures;
    }
    MeterRelation volatile relation = reinterpret_cast<MeterRelation>(&relation_fixture);
    SPOREMP_READ_STACK_POINTER(before);
    for (unsigned i = 0; i < 4096; ++i) {
        relation(&meter, 0x80000000u + i);
    }
    SPOREMP_READ_STACK_POINTER(after);
    check(before == after && meter.calls == 12288 && relation_hooks == 4096 && meter.relation == 0x80000fff,
          "social icon observer preserves receiver, full relationship word, exact forwarding and RET 4 cleanup");
    check(sporemp::change_hooks(relation_binding, std::size(relation_binding), false) == NO_ERROR,
          "native social icon observer detaches");
    death_begin_original = reinterpret_cast<sporemp::NativeDeathAbi::Begin>(&death_begin_fixture);
    death_message_original = reinterpret_cast<sporemp::NativeDeathAbi::Message>(&death_message_fixture);
    sporemp::NativeHook death_bindings[] = {
        {reinterpret_cast<void**>(&death_begin_original), reinterpret_cast<void*>(death_begin_hook)},
        {reinterpret_cast<void**>(&death_message_original), reinterpret_cast<void*>(death_message_hook)}};
    const auto death_attached = sporemp::change_hooks(death_bindings, std::size(death_bindings), true);
    check(death_attached == NO_ERROR, "native death handler and restart listener ABI fixtures attach");
    if (death_attached != NO_ERROR) {
        return failures;
    }
    sporemp::NativeDeathAbi::Begin volatile begin_death =
        reinterpret_cast<sporemp::NativeDeathAbi::Begin>(&death_begin_fixture);
    sporemp::NativeDeathAbi::Message volatile message_death =
        reinterpret_cast<sporemp::NativeDeathAbi::Message>(&death_message_fixture);
    bool death_false = true, death_true = false;
    SPOREMP_READ_STACK_POINTER(before);
    for (unsigned i = 0; i < 4096; ++i) {
        begin_death(&tree, 0x80000000u + i);
        death_false = message_death(&tree, 0x04f60b92, nullptr);
        death_true = message_death(&tree, 0x80000000u + i, &native_root);
    }
    SPOREMP_READ_STACK_POINTER(after);
    check(before == after && death_begin_calls == 4096 && death_begin_hooks == 4096 && death_message_calls == 8192 &&
              death_message_hooks == 8192,
          "death observers preserve one original call and both RET 4 and RET 8 stack cleanup");
    check(death_receiver == &tree && death_reason == 0x80000fff && death_message == 0x80000fff &&
              death_payload == &native_root && !death_false && death_true,
          "death observers preserve ECX, full reason/message words, payload and AL result");
    check(sporemp::change_hooks(death_bindings, std::size(death_bindings), false) == NO_ERROR,
          "native death ABI fixtures detach");
    DeathLeaf leaf;
    DeathAbi::Tick volatile death_tick = reinterpret_cast<DeathAbi::Tick>(&death_tick_fixture);
    DeathAbi::Reset volatile death_reset = reinterpret_cast<DeathAbi::Reset>(&death_reset_fixture);
    DeathAbi::Destroy volatile death_destroy = reinterpret_cast<DeathAbi::Destroy>(&death_destroy_fixture);
    bool tick_false = true, tick_true = false, reset_result = false;
    void* destroyed = nullptr;
    SPOREMP_READ_STACK_POINTER(before);
    for (unsigned i = 0; i < 4096; ++i) {
        tick_false = death_tick(&leaf, -0.125f);
        tick_true = death_tick(&leaf, 0.03125f);
        reset_result = death_reset(&leaf);
        destroyed = death_destroy(&leaf, 0);
    }
    SPOREMP_READ_STACK_POINTER(after);
    check(before == after && leaf.ticks == 8192 && leaf.resets == 4096 && leaf.delta == 0.03125f && !tick_false &&
              tick_true && reset_result && destroyed == &leaf && !leaf.request,
          "secondary death leaves preserve float argument, ECX, AL/EAX results, destructor flag and stack cleanup");
    parameter_original = reinterpret_cast<DeathAbi::CinematicParameter>(&parameter_fixture);
    activate_original = reinterpret_cast<DeathAbi::UiActivate>(&activate_fixture);
    deactivate_original = reinterpret_cast<DeathAbi::UiDeactivate>(&death_deactivate_fixture);
    sporemp::NativeHook ui_bindings[] = {
        {reinterpret_cast<void**>(&parameter_original), reinterpret_cast<void*>(parameter_hook)},
        {reinterpret_cast<void**>(&activate_original), reinterpret_cast<void*>(activate_hook)},
        {reinterpret_cast<void**>(&deactivate_original), reinterpret_cast<void*>(deactivate_hook)}};
    const auto ui_attached = sporemp::change_hooks(ui_bindings, std::size(ui_bindings), true);
    check(ui_attached == NO_ERROR, "secondary death presentation ABI fixtures attach");
    if (ui_attached != NO_ERROR) {
        return failures;
    }
    DeathAbi::CinematicParameter volatile set_parameter =
        reinterpret_cast<DeathAbi::CinematicParameter>(&parameter_fixture);
    DeathAbi::UiActivate volatile activate = reinterpret_cast<DeathAbi::UiActivate>(&activate_fixture);
    DeathAbi::UiDeactivate volatile deactivate_ui = reinterpret_cast<DeathAbi::UiDeactivate>(&death_deactivate_fixture);
    SPOREMP_READ_STACK_POINTER(before);
    for (unsigned i = 0; i < 4096; ++i) {
        suppress_death_ui = false;
        deactivate_ui(&leaf);
        activate(&leaf, 4, 0);
        set_parameter(&leaf, 0x927b68d9, &tree, 0x80000000u + i);
        suppress_death_ui = true;
        deactivate_ui(&leaf);
        activate(&leaf, 3, 7);
        set_parameter(&leaf, 0, nullptr, 0);
    }
    SPOREMP_READ_STACK_POINTER(after);
    suppress_death_ui = false;
    check(before == after && leaf.parameters == 4096 && leaf.activations == 4096 && leaf.deactivations == 4096 &&
              parameter_hooks == 8192 && activate_hooks == 8192 && deactivate_hooks == 8192 && leaf.key == 0x927b68d9 &&
              leaf.value == &tree && leaf.request == 0x80000fff,
          "presentation routing preserves RET 0/8/12 in forwarding and suppression paths without leaking "
          "current-request parameters");
    check(sporemp::change_hooks(ui_bindings, std::size(ui_bindings), false) == NO_ERROR,
          "secondary death presentation ABI fixtures detach");
    return failures;
}
