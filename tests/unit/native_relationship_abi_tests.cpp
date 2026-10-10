#include "native_relationship_abi.h"
#include "stack_pointer.h"
#include "detour_transaction.h"
#include <array>
#include <cstdio>
#include <limits>

namespace {
struct Animal {
    uint32_t sentinel;
};

struct Manager {
    uint32_t sentinel;
};

using Abi = sporemp::NativeRelationshipAbi<Animal, Manager>;

struct Receiver {
    uint32_t sentinel = 0xa571b902;
    unsigned constructs = 0, deletes = 0, actions = 0, effectives = 0, inserts = 0, refreshes = 0, populates = 0;
    char populate_flag = 0;
    uint32_t flags = 0, action = 0;
    const uint32_t* key = nullptr;
    Animal* target = nullptr;
    Animal* source = nullptr;
};

Receiver* root = nullptr;
unsigned get_calls = 0, action_hooks = 0;
Abi::Get get_original = nullptr;
Abi::Action action_original = nullptr;
std::array<uintptr_t, 6> allocation{};
std::array<void*, 5> collection{};
Manager* collection_manager = nullptr;
uint32_t collection_limit = 0;

struct Stream {
    uint32_t sentinel = 0x9fe382a1;
};

void* serialized_receiver = nullptr;
Stream* serialized_stream = nullptr;
unsigned serialized_calls = 0;

__declspec(noinline) void __fastcall serialize_fixture(void* self, void*, Stream* stream) {
    serialized_receiver = self;
    serialized_stream = stream;
    ++serialized_calls;
}

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

__declspec(noinline) void* __fastcall delete_fixture(Receiver* self, void*, uint32_t flags) {
    ++self->deletes;
    self->flags = flags;
    return self;
}

__declspec(noinline) int __fastcall effective_fixture(Receiver* self, void*, const uint32_t* key, Animal* target,
                                                      Animal* source) {
    ++self->effectives;
    self->key = key;
    self->target = target;
    self->source = source;
    return source ? -7 : 3;
}

__declspec(noinline) void __fastcall action_fixture(Receiver* self, void*, uint32_t action, const uint32_t* key,
                                                    Animal* target) {
    ++self->actions;
    self->action = action;
    self->key = key;
    self->target = target;
}

std::array<uint32_t, 5> inserted{};
const uint32_t* inserted_key = nullptr;

__declspec(noinline) uint32_t* __fastcall insert_fixture(Receiver* self, void*, const uint32_t* key) {
    ++self->inserts;
    inserted_key = key;
    return inserted.data();
}

__declspec(noinline) void __fastcall refresh_fixture(Receiver* self, void*) {
    ++self->refreshes;
}

__declspec(noinline) void __fastcall populate_fixture(Receiver* self, void*, char flag) {
    ++self->populates;
    self->populate_flag = flag;
}

void __fastcall action_hook(void* self, void*, uint32_t action, const uint32_t* key, Animal* target) {
    ++action_hooks;
    action_original(self, action, key, target);
}

__declspec(noinline) void* __fastcall collection_fixture(Manager* self, void*, void* output, void* first, void* last,
                                                         void* capacity, uint32_t limit) {
    collection_manager = self;
    collection = {output, first, last, capacity, self};
    collection_limit = limit;
    return output;
}
} // namespace

int native_relationship_abi_checks() {
    unsigned failures = 0;
    const auto check = [&](bool condition, const char* text) {
        if (!condition) {
            ++failures;
            std::fprintf(stderr, "relationship ABI: %s\n", text);
        }
    };
    Receiver receiver;
    root = &receiver;
    Stream stream;
    Abi::Serialize<Stream> volatile serialize = reinterpret_cast<Abi::Serialize<Stream>>(&serialize_fixture);
    Animal target{0xf124abcd}, source{0x9bcdd031};
    Manager manager{0xa1839d23};
    const uint32_t key[]{0xa6b173d9, 0xf1230045, 0xe03489ab};
    const char name[] = "native relationship fixture", file[] = "fixture";
    Abi::Allocate volatile allocate = &allocate_fixture;
    Abi::Construct volatile construct = reinterpret_cast<Abi::Construct>(&construct_fixture);
    Abi::Delete volatile destroy = reinterpret_cast<Abi::Delete>(&delete_fixture);
    Abi::Effective volatile effective = reinterpret_cast<Abi::Effective>(&effective_fixture);
    Abi::Collection volatile collect = reinterpret_cast<Abi::Collection>(&collection_fixture);
    Abi::Insert volatile insert = reinterpret_cast<Abi::Insert>(&insert_fixture);
    Abi::RefreshGoal volatile refresh = reinterpret_cast<Abi::RefreshGoal>(&refresh_fixture);
    Abi::PopulateCard volatile populate = reinterpret_cast<Abi::PopulateCard>(&populate_fixture);
    Abi::CardManager volatile card_manager = &get_fixture;
    get_original = &get_fixture;
    action_original = reinterpret_cast<Abi::Action>(&action_fixture);
    const sporemp::NativeHook hooks[]{
        {reinterpret_cast<void**>(&get_original), reinterpret_cast<void*>(get_hook)},
        {reinterpret_cast<void**>(&action_original), reinterpret_cast<void*>(action_hook)}};
    const auto attached = sporemp::change_hooks(hooks, std::size(hooks), true);
    check(attached == NO_ERROR, "cdecl getter and thiscall action detours attach");
    if (attached == NO_ERROR) {
        Abi::Get volatile get = &get_fixture;
        Abi::Action volatile action = reinterpret_cast<Abi::Action>(&action_fixture);
        uintptr_t before = 0, after = 0;
        bool outputs = true;
        SPOREMP_READ_STACK_POINTER(before);
        for (unsigned i = 0; i < 4096; ++i) {
            outputs = outputs && allocate(0x60, name, -13, 0xfebc9876, file, -29) == &receiver;
            outputs = outputs && construct(&receiver) == &receiver && get() == &receiver;
            outputs = outputs && effective(&receiver, key, &target, &source) == -7;
            action(&receiver, i, key, &target);
            serialize(reinterpret_cast<unsigned char*>(&receiver) + 4, &stream);
            outputs = outputs && collect(&manager, &receiver, &target, &source, nullptr, 0xf123ab89) == &receiver;
            outputs = outputs && insert(&receiver, key) == inserted.data();
            refresh(&receiver);
            populate(&receiver, 1);
            outputs = outputs && destroy(&receiver, 1) == &receiver;
        }
        SPOREMP_READ_STACK_POINTER(after);
        check(before == after, "six-argument cdecl and native thiscall calls retain the stack");
        check(outputs, "six-argument cdecl and native thiscall calls retain full return values");
        check(serialized_calls == 4096 && serialized_receiver == reinterpret_cast<unsigned char*>(&receiver) + 4 &&
                  serialized_stream == &stream && stream.sentinel == 0x9fe382a1,
              "relationship serializer preserves adjusted subsystem receiver, stream pointer and stack");
        check(receiver.constructs == 4096 && receiver.deletes == 4096 && get_calls == 4096 &&
                  receiver.actions == 4096 && action_hooks == 4096 && receiver.effectives == 4096,
              "all original lifecycle/action calls execute exactly once");
        check(receiver.flags == 1 && receiver.action == 4095 && receiver.key == key && receiver.target == &target &&
                  receiver.source == &source && receiver.sentinel == 0xa571b902,
              "action and effective lookup retain receiver, key reference and distinct animal arguments");
        check(allocation == std::array<uintptr_t, 6>{0x60, reinterpret_cast<uintptr_t>(name),
                                                     static_cast<uintptr_t>(-13), 0xfebc9876,
                                                     reinterpret_cast<uintptr_t>(file), static_cast<uintptr_t>(-29)},
              "allocator preserves all six argument positions including signed flags and line");
        check(collection_manager == &manager &&
                  collection == std::array<void*, 5>{&receiver, &target, &source, nullptr, &manager} &&
                  collection_limit == 0xf123ab89,
              "original collection ABI retains all five arguments and full count bits");
        check(receiver.populates == 4096 && receiver.populate_flag == 1 && card_manager() == &receiver,
              "E17BF0 one-argument card populate retains receiver, flag and RET 4; B3D500 returns the full pointer");
        check(receiver.inserts == 4096 && receiver.refreshes == 4096 && inserted_key == key,
              "BA3B40 one-argument insert and C2FBB0 receiver-only goal refresh retain stack, key and value pointer");
        root = nullptr;
        check(get() == nullptr && effective(&receiver, key, nullptr, nullptr) == 3,
              "null manager result and optional animal arguments remain original");
        check(sporemp::change_hooks(hooks, std::size(hooks), false) == NO_ERROR, "both relationship detours detach");
    }
    using sporemp::native_relationship_count_below_thresholds;
    using sporemp::native_relationship_first_feared_kill;
    using sporemp::native_relationship_damage_caller;
    const auto max = std::numeric_limits<uint32_t>::max();
    check(native_relationship_first_feared_kill(0, 1, 3) && native_relationship_first_feared_kill(0, 1, 2) &&
              !native_relationship_first_feared_kill(1, 1, 3) && !native_relationship_first_feared_kill(max, 1, 3) &&
              !native_relationship_first_feared_kill(0, 1, 1) && !native_relationship_first_feared_kill(0, 1, 0) &&
              !native_relationship_first_feared_kill(0, 0, 3) && !native_relationship_first_feared_kill(0, 2, 3),
          "first feared branch requires fresh count, exact first-kill fear threshold and no extinction");
    using sporemp::native_relationship_later_kill;
    // A species the owner has already hunted and that its next kill does not finish off (trial261: one kill, feared
    // from one, extinct at three).
    check(native_relationship_later_kill(1, 1, 3) && native_relationship_later_kill(1, 1, 4) &&
              native_relationship_later_kill(2, 1, 4),
          "a kill after the first that leaves the species short of extinction is its own branch");
    check(!native_relationship_later_kill(0, 1, 3) && !native_relationship_later_kill(2, 1, 3) &&
              !native_relationship_later_kill(3, 1, 3) && !native_relationship_later_kill(1, 2, 4) &&
              !native_relationship_later_kill(1, 0, 3) && !native_relationship_later_kill(max, 1, 3),
          "the first kill, the kill that makes extinct and other fear thresholds are not that branch");
    using sporemp::native_relationship_watch;
    using sporemp::RelationshipWatch;
    // Trial318: the second player's creature died in its second fight, and the watch on player 1's state took the
    // unreadable owner for a change and ended the session.
    check(native_relationship_watch(0, false) == RelationshipWatch::idle &&
              native_relationship_watch(0, true) == RelationshipWatch::idle &&
              native_relationship_watch(3, false) == RelationshipWatch::sample &&
              native_relationship_watch(3, true) == RelationshipWatch::end,
          "a fight is watched while samples remain, and the owner's death ends the watch without a failure");
    using sporemp::native_relationship_fight_finished;
    // Trial320 and before: once the second player had fought, player 1 could not attack for the rest of the session.
    check(native_relationship_fight_finished(true, 0, true) && !native_relationship_fight_finished(true, 1, true) &&
              !native_relationship_fight_finished(false, 0, false) &&
              !native_relationship_fight_finished(false, 5, false),
          "a fight is finished once its kill has been seen and watched; an unfinished one stays followed");
    // Trial338: wild creatures killed the creature the second player was fighting.
    check(native_relationship_fight_finished(false, 0, true) && !native_relationship_fight_finished(false, 2, true),
          "a fight whose creature another killed is finished too, once its samples are done");
    using sporemp::native_relationship_primary_change;
    using sporemp::PrimaryChange;
    // Trial342: player 1's attack on the creature the second player was fighting was cancelled, and the pack's
    // bites on player 1 changed its own relationship, which was taken for a leak and ended all combat.
    check(native_relationship_primary_change(false, false, false) == PrimaryChange::none &&
              native_relationship_primary_change(false, true, true) == PrimaryChange::none &&
              native_relationship_primary_change(true, false, true) == PrimaryChange::own,
          "between the second player's blows, a change to player 1 that has acted or been struck is its own");
    check(native_relationship_primary_change(true, false, false) == PrimaryChange::leak &&
              native_relationship_primary_change(true, true, false) == PrimaryChange::leak &&
              native_relationship_primary_change(true, true, true) == PrimaryChange::leak,
          "a change inside the second player's blow, or to an idle player 1, is a leak");
    using sporemp::native_relationship_target_replaceable;
    check(native_relationship_target_replaceable(0, 0, 0) && !native_relationship_target_replaceable(1, 0, 0) &&
              !native_relationship_target_replaceable(0, 1, 0) && !native_relationship_target_replaceable(0, 0, 1),
          "the owner may turn to another creature once nothing of its last fight is in flight or still being watched");
    check(sporemp::native_relationship_initialized_last_word(false, 0) == max &&
              sporemp::native_relationship_initialized_last_word(true, 0) == 0 &&
              sporemp::native_relationship_initialized_last_word(true, 0xf183a782) == 0xf183a782,
          "a missing entry expects native initialization; an existing zero or nonzero fifth word is retained");
    check(native_relationship_count_below_thresholds(0, 2, 2) &&
              native_relationship_count_below_thresholds(max - 2, max, max),
          "strict unsigned thresholds accept interior values");
    check(!native_relationship_count_below_thresholds(0, 1, 9) &&
              !native_relationship_count_below_thresholds(0, 9, 1) &&
              !native_relationship_count_below_thresholds(0, 0, 0) &&
              !native_relationship_count_below_thresholds(max, 9, 9) &&
              !native_relationship_count_below_thresholds(max - 1, max, max),
          "threshold equality, zero and count overflow are refused");
    check(native_relationship_damage_caller(0x807bba) && native_relationship_damage_caller(0x807c57) &&
              !native_relationship_damage_caller(0x807ae7) && !native_relationship_damage_caller(0x807c7b) &&
              !native_relationship_damage_caller(0x82ee8e) && !native_relationship_damage_caller(0x99bc41),
          "only two damage sites qualify; incoming, objective and social sites stay excluded");
    using sporemp::native_relationship_goal_values;
    using sporemp::native_relationship_goal_reader;
    const auto absent = native_relationship_goal_values(false, {9, 8, 7, 6, 5});
    const auto present = native_relationship_goal_values(true, {1, 0xf0000004, 2, 3, max});
    check(!absent.befriended && !absent.killed && !absent.relation,
          "an absent species presents the zero counts and relation C2EE80 reads after native insertion");
    check(present.befriended == 2 && present.killed == 3 && present.relation == 1,
          "goal display uses original befriended, killed and relation words, never the native count or fifth word");
    check(native_relationship_goal_reader(0x82ee8e) && !native_relationship_goal_reader(0x82ee89) &&
              !native_relationship_goal_reader(0x807bba) && !native_relationship_goal_reader(0x807c57) &&
              !native_relationship_goal_reader(0x7a58b3) && !native_relationship_goal_reader(0x82e98d),
          "only the C2EE80 goal getter return selects the display root; damage, history and card sites stay excluded");
    using sporemp::native_relationship_card_reader;
    check(native_relationship_card_reader(0x82e98d) && !native_relationship_card_reader(0x82ee8e) &&
              !native_relationship_card_reader(0x82e988) && !native_relationship_card_reader(0x807bba) &&
              !native_relationship_damage_caller(0x82e98d),
          "the C2E980 card relation getter is a separate display-only site, never a goal or damage site");
    return static_cast<int>(failures);
}
