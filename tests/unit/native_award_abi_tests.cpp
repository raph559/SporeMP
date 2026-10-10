#include "native_award_abi.h"
#include "stack_pointer.h"
#include "native_inventory_abi.h"
#include "native_social_observation.h"
#include "native_tutorial_pending.h"
#include "native_social_objective.h"
#include "detour_transaction.h"
#include <cstdio>
#include <cstring>
#include <iterator>
#include <limits>

namespace {
struct Object {
    int amount_calls = 0, update_calls = 0, action_calls = 0;
    bool social = false;
    float dt = 0, elapsed = 0;
    uint32_t action_id = 0;
    void* payload = nullptr;
    uint32_t tutorial_key[3]{};
    unsigned tutorial_records = 0;

    __declspec(noinline) void record_tutorial(const uint32_t* key) {
        std::memcpy(tutorial_key, key, sizeof(tutorial_key));
        ++tutorial_records;
    }

    __declspec(noinline) bool has_tutorial(const uint32_t* key) {
        return tutorial_records && !std::memcmp(key, tutorial_key, sizeof(tutorial_key));
    }

    __declspec(noinline) float amount(bool value) {
        ++amount_calls;
        social = value;
        return value ? 1.25f : -0.375f;
    }

    __declspec(noinline) void update(float first, float second) {
        ++update_calls;
        dt = first;
        elapsed = second;
    }

    __declspec(noinline) void action(uint32_t id, void* data) {
        ++action_calls;
        action_id = id;
        payload = data;
    }
};

struct PartNotificationFixture {
    unsigned calls = 0;
    sporemp::NativePartKey key{};

    __declspec(noinline) void notify(sporemp::NativePartKey value) {
        key = value;
        ++calls;
    }
};

struct CinematicFixture {
    const char* name = nullptr;
    const uint32_t* option = nullptr;
    uint32_t a = 0, b = 0, c = 0, d = 0, caption = 0, request = 0, key[3]{};
    unsigned plays = 0, captions = 0;

    __declspec(noinline) uint32_t play(const char* n, uint32_t x, uint32_t y, const uint32_t* p, uint32_t z,
                                       uint32_t w) {
        name = n;
        a = x;
        b = y;
        option = p;
        c = z;
        d = w;
        ++plays;
        return 0xfedc9876;
    }

    __declspec(noinline) void attach(uint32_t id, const uint32_t* value, uint32_t native_request) {
        caption = id;
        request = native_request;
        std::memcpy(key, value, sizeof(key));
        ++captions;
    }
};

struct Manager {
    unsigned char padding[0x74]{};
    Object* player = nullptr;

    // Intentionally four bytes + alignment padding in optimized x86, matching
    // the game's folded getter shape; Detours must handle the short function.
    __declspec(noinline) Object* get() {
        return player;
    }
};

struct SpeciesFixture {
    unsigned calls = 0;
    void* profile = nullptr;

    __declspec(noinline) void* get() {
        ++calls;
        return profile;
    }
};

struct RelationshipFixture {
    uint32_t key[3]{};
    unsigned calls = 0;

    __declspec(noinline) uint32_t* find(uint32_t* output, const uint32_t* input) {
        std::memcpy(key, input, sizeof(key));
        ++calls;
        output[0] = 0xfedc9876;
        output[1] = 0x98765432;
        return output;
    }
};

struct PropertyFixture {
    unsigned calls = 0;
    uint32_t id = 0;
    void* target = nullptr;

    __declspec(noinline) uint32_t action(uint32_t value, void* data) {
        ++calls;
        id = value;
        target = data;
        return 0xfedc9876;
    }

    __declspec(noinline) void* lookup(uint32_t value, void* data) {
        ++calls;
        id = value;
        target = data;
        return data;
    }
};

unsigned property_apply_calls = 0;
void* property_config = nullptr;
void* property_target = nullptr;
Object* property_actor = nullptr;

__declspec(noinline) uint32_t __stdcall property_apply_fixture(void* config, void* target, Object* actor) {
    ++property_apply_calls;
    property_config = config;
    property_target = target;
    property_actor = actor;
    return 0xfedc9876;
}

using Abi = sporemp::NativeAwardAbi<Object, Object, Object, Manager, Object>;
Abi::Amount amount_original = nullptr;
Abi::Update update_original = nullptr;
Abi::Action action_original = nullptr;
Abi::PlayerGet get_original = nullptr;
Abi::SpeciesGet species_original = nullptr;
unsigned species_hooks = 0;

void* __fastcall species_hook(void* self, void*) {
    ++species_hooks;
    return species_original(self);
}

int amounts = 0, updates = 0, actions = 0, gets = 0;

float __fastcall amount_hook(Object* self, void*, bool social) {
    ++amounts;
    return amount_original(self, social);
}

void __fastcall update_hook(Object* self, void*, float dt, float elapsed) {
    ++updates;
    update_original(self, dt, elapsed);
}

void __fastcall action_hook(Object* self, void*, uint32_t id, void* payload) {
    ++actions;
    action_original(self, id, payload);
}

Object* __fastcall get_hook(Manager* self, void*) {
    ++gets;
    return get_original(self);
}

Abi::Tutorial tutorial_original = nullptr;
unsigned tutorial_calls = 0, tutorial_hooks = 0;
const char* tutorial_name = nullptr;
uint32_t tutorial_token = 0;

__declspec(noinline) uint32_t __stdcall tutorial_fixture(const char* name, uint32_t token) {
    ++tutorial_calls;
    tutorial_name = name;
    tutorial_token = token;
    return 0xfedc9876;
}

uint32_t __stdcall tutorial_hook(const char* name, uint32_t token) {
    ++tutorial_hooks;
    return tutorial_original(name, token);
}

template <class Function, class Member>
void address(Function& fn, Member member) {
    static_assert(sizeof(fn) == sizeof(member));
    memcpy(&fn, &member, sizeof(fn));
}
} // namespace

int native_award_abi_checks() {
    int failed = 0;
    auto check = [&](bool value, const char* label) {
        std::printf("%s: %s (HOST ABI; SPORE NOT LOADED)\n", value ? "PASS" : "FAIL", label);
        if (!value) {
            ++failed;
        }
    };
    {
        SpeciesFixture first, second;
        first.profile = reinterpret_cast<void*>(uintptr_t(0xfedc9876));
        address(species_original, &SpeciesFixture::get);
        const sporemp::NativeHook hook{reinterpret_cast<void**>(&species_original),
                                       reinterpret_cast<void*>(species_hook)};
        const auto status = sporemp::change_hooks(&hook, 1, true);
        check(status == NO_ERROR, "species getter thiscall observer fixture attaches");
        if (status != NO_ERROR) {
            return failed;
        }
        void* (SpeciesFixture::* volatile invoke)() = &SpeciesFixture::get;
        uintptr_t before = 0, after = 0;
        void* value = nullptr;
        void* empty = reinterpret_cast<void*>(uintptr_t(1));
        SPOREMP_READ_STACK_POINTER(before);
        for (unsigned i = 0; i < 4096; ++i) {
            value = (first.*invoke)();
            empty = (second.*invoke)();
        }
        SPOREMP_READ_STACK_POINTER(after);
        check(before == after && value == first.profile && empty == nullptr && first.calls == 4096 &&
                  second.calls == 4096 && species_hooks == 8192,
              "species getter preserves ECX receiver, full EAX/null, RET stack and exactly one original per call");
        check(sporemp::change_hooks(&hook, 1, false) == NO_ERROR, "species getter observer fixture detaches");
    }
    {
        PartNotificationFixture fixture;
        sporemp::NativeInventoryAbi::Notify notify = nullptr;
        address(notify, &PartNotificationFixture::notify);
        auto volatile invoke = notify;
        uintptr_t before = 0, after = 0;
        SPOREMP_READ_STACK_POINTER(before);
        for (unsigned i = 0; i < 4096; ++i) {
            invoke(&fixture, {0xfedc9876, 0x00b1b104, 0x40626000});
        }
        SPOREMP_READ_STACK_POINTER(after);
        check(before == after && fixture.calls == 4096 && fixture.key.instance == 0xfedc9876 &&
                  fixture.key.type == 0x00b1b104 && fixture.key.group == 0x40626000,
              "part notification ECX receiver, three key words and RET 0Ch preserve stack");
    }
    {
        PropertyFixture handler;
        Object actor;
        uint32_t payload[3]{};
        Abi::PropertyAction action;
        Abi::PropertyLookup lookup;
        address(action, &PropertyFixture::action);
        address(lookup, &PropertyFixture::lookup);
        Abi::PropertyApply volatile apply = &property_apply_fixture;
        uintptr_t before = 0, after = 0;
        uint32_t result = 0;
        void* selected = nullptr;
        SPOREMP_READ_STACK_POINTER(before);
        for (unsigned i = 0; i < 4096; ++i) {
            result = action(&handler, 0xd3353638, payload);
            selected = lookup(&handler, 0xd3353638, payload);
            result = apply(selected, &handler, &actor);
        }
        SPOREMP_READ_STACK_POINTER(after);
        check(before == after && handler.calls == 8192 && handler.id == 0xd3353638 && handler.target == payload &&
                  selected == payload && result == 0xfedc9876 && property_apply_calls == 4096 &&
                  property_config == payload && property_target == &handler && property_actor == &actor,
              "property handler/lookup RET 8 and application RET 0Ch preserve stack, receivers, pointers and return "
              "bits");
    }
    {
        sporemp::NativeSocialObjectiveReceipt source{17, 22, 9, 2000, 12559, 2, 3, 4, 6, {101, 202, 303}, true};
        for (unsigned field = 0; field < 12; ++field) {
            auto receipt = source;
            auto key = source.species;
            uint64_t epoch = 17, actor = 22, now = 1500;
            uint32_t native = 12559, cached = 2, old_relation = 4, current = 3, relation = 6, required = 3;
            if (field == 0) {
                epoch++;
            }
            if (field == 1) {
                actor++;
            }
            if (field == 2) {
                native++;
            }
            if (field == 3) {
                key[0]++;
            }
            if (field == 4) {
                key[1]++;
            }
            if (field == 5) {
                key[2]++;
            }
            if (field == 6) {
                cached++;
            }
            if (field == 7) {
                old_relation++;
            }
            if (field == 8) {
                current++;
            }
            if (field == 9) {
                relation--;
            }
            if (field == 10) {
                required++;
            }
            if (field == 11) {
                now = 2000;
            }
            check(!receipt.claim(epoch, actor, native, key, cached, old_relation, current, relation, required, now) &&
                      receipt.pending,
                  "delayed objective refuses another owner, epoch, species, transition, threshold or expired receipt");
        }
        auto receipt = source;
        check(receipt.claim(17, 22, 12559, source.species, 2, 4, 3, 6, 3, 1500) &&
                  !receipt.claim(17, 22, 12559, source.species, 2, 4, 3, 6, 3, 1501),
              "one matching native transition assigns only one delayed objective callback");
        receipt = source;
        receipt.before_count = UINT32_MAX;
        receipt.after_count = 0;
        check(!receipt.claim(17, 22, 12559, source.species, UINT32_MAX, 4, 0, 6, 0, 1500),
              "wrapped relationship counter cannot authorize a reward");
        receipt = source;
        receipt.before_relation = 6;
        check(!receipt.claim(17, 22, 12559, source.species, 2, 6, 3, 6, 3, 1500),
              "an already allied relationship cannot authorize another completion");
        RelationshipFixture fixture;
        Abi::RelationshipFind find;
        address(find, &RelationshipFixture::find);
        uint32_t output[2]{}, key[] = {0xf1234567, 0xe2345678, 0xd3456789};
        uint32_t* result = nullptr;
        uintptr_t before = 0, after = 0;
        SPOREMP_READ_STACK_POINTER(before);
        for (unsigned i = 0; i < 4096; ++i) {
            result = find(&fixture, output, key);
        }
        SPOREMP_READ_STACK_POINTER(after);
        check(before == after && result == output && output[0] == 0xfedc9876 && output[1] == 0x98765432 &&
                  fixture.calls == 4096 && !std::memcmp(fixture.key, key, sizeof(key)),
              "non-inserting relationship lookup ABI preserves receiver, key, two-word iterator and RET 8");
    }
    {
        using namespace sporemp;
        NativeTutorialPending pending;
        check(!pending.pending(), "no tutorial is implied before a native request");
        const NativeTutorialRequest request{17, 0xfedcba9876543210, 123, 0xea7666af};
        for (unsigned field = 0; field < 8; ++field) {
            auto invalid = request;
            if (field == 0) {
                invalid.epoch = 0;
            }
            if (field == 1) {
                invalid.actor = 0;
            }
            if (field == 2) {
                invalid.player = UINT32_MAX;
            }
            if (field == 3) {
                invalid.player = 0;
            }
            if (field == 4) {
                invalid.token++;
            }
            if (field == 5) {
                invalid.caption++;
            }
            if (field == 6) {
                invalid.owner = 0;
            }
            if (field == 7) {
                invalid.owner = 3;
            }
            check(pending.offer(invalid) == TutorialOffer::invalid && !pending.pending(),
                  "unresolved native tutorial identity or unsupported token cannot become pending");
        }
        check(pending.offer(request) == TutorialOffer::queued && pending.pending(),
              "native presentation request remains pending until delivery or explicit retirement");
        check(pending.offer(request) == TutorialOffer::duplicate && pending.pending(),
              "repeated native request retains one pending presentation");
        for (unsigned field = 0; field < 5; ++field) {
            auto changed = request;
            if (field == 0) {
                changed.epoch++;
            }
            if (field == 1) {
                changed.actor++;
            }
            if (field == 2) {
                changed.player++;
            }
            if (field == 3) {
                changed.caption = 0x061d8186;
            }
            if (field == 4) {
                changed.owner = 1;
            }
            check(pending.offer(changed) == TutorialOffer::conflict && pending.request().epoch == request.epoch &&
                      pending.request().actor == request.actor && pending.request().player == request.player,
                  "changed owner generation cannot overwrite or acknowledge pending presentation");
        }
        pending.retire();
        NativeTutorialPending primary;
        auto primary_request = request;
        primary_request.owner = 1;
        primary_request.actor = 99;
        primary_request.player = 100;
        check(primary.offer(primary_request) == TutorialOffer::queued &&
                  pending.offer(request) == TutorialOffer::queued,
              "both native owners retain independent pending presentations");
        primary.retire();
        check(pending.pending() && pending.request().owner == 2,
              "completing primary presentation cannot acknowledge secondary presentation");
        pending.retire();
        auto next = request;
        next.epoch++;
        check(!pending.pending() && pending.offer(next) == TutorialOffer::queued,
              "explicit retirement clears old ownership before a fresh request");
        auto part = request;
        part.token = 0xb721f66f;
        part.caption = 0;
        check(pending.offer(part) == TutorialOffer::conflict,
              "part tutorial cannot discard a pending DNA presentation");
        pending.retire();
        check(pending.offer(part) == TutorialOffer::queued && pending.request().token == 0xb721f66f,
              "first part retains its own original tutorial identity");
        pending.retire();
        part.caption = 0x061d8045;
        check(pending.offer(part) == TutorialOffer::invalid && !pending.pending(),
              "part tutorial rejects a foreign caption override");
    }
    {
        using namespace sporemp;
        NativeSocialPayload decoded{}, source{0xf1234560, 0xe2345670, 0};
        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        auto allocation = static_cast<unsigned char*>(
            VirtualAlloc(nullptr, 2 * info.dwPageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        check(allocation != nullptr, "social payload guard-page fixture allocated");
        if (allocation) {
            DWORD previous = 0;
            const bool protected_page =
                VirtualProtect(allocation + info.dwPageSize, info.dwPageSize, PAGE_NOACCESS, &previous) != FALSE;
            check(protected_page, "social payload ends immediately before an unreadable page");
            auto payload = allocation + info.dwPageSize - sizeof(source);
            std::memcpy(payload, &source, sizeof(source));
            if (protected_page) {
                check(decode_native_social_payload(native_social_action, native_social_action_caller_rva, payload,
                                                   sizeof(source), decoded) &&
                          decoded.actor == source.actor && decoded.target == source.target && !decoded.reserved,
                      "social action reads exactly three words without reading the SDK-inferred tail");
            }
            check(std::memcmp(payload, &source, sizeof(source)) == 0,
                  "social observation preserves the original payload bytes");
            check(!decode_native_social_payload(native_social_action, native_social_action_caller_rva, payload, 11,
                                                decoded) &&
                      !decoded.actor,
                  "truncated social payload is refused and clears stale output");
            VirtualFree(allocation, 0, MEM_RELEASE);
        }
        check(!decode_native_social_payload(native_social_action + 1, native_social_action_caller_rva, &source,
                                            sizeof(source), decoded),
              "different native action is not a social observation");
        check(!decode_native_social_payload(native_social_action, native_social_action_caller_rva + 1, &source,
                                            sizeof(source), decoded),
              "different call site is not decoded using this layout");
        check(!decode_native_social_payload(native_social_action, native_social_action_caller_rva, nullptr,
                                            sizeof(source), decoded),
              "null social payload is refused");
        source.reserved = 1;
        check(!decode_native_social_payload(native_social_action, native_social_action_caller_rva, &source,
                                            sizeof(source), decoded),
              "changed native payload tail is refused");
        source.reserved = 0;
        source.target = source.actor;
        check(!decode_native_social_payload(native_social_action, native_social_action_caller_rva, &source,
                                            sizeof(source), decoded),
              "self-target social payload is refused");
        source.target = 0;
        check(!decode_native_social_payload(native_social_action, native_social_action_caller_rva, &source,
                                            sizeof(source), decoded),
              "unresolved target cannot supply social evidence");
    }
    {
        using namespace sporemp;
        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        auto allocation = static_cast<unsigned char*>(
            VirtualAlloc(nullptr, 2 * info.dwPageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        check(allocation != nullptr, "social meter guard-page fixture allocated");
        if (allocation) {
            DWORD previous = 0;
            const bool protected_page =
                VirtualProtect(allocation + info.dwPageSize, info.dwPageSize, PAGE_NOACCESS, &previous) != FALSE;
            check(protected_page, "social meter final scalar borders an unreadable page");
            auto bytes = allocation + info.dwPageSize - native_social_ui_read_size;
            const uint32_t vtable = 0x400000 + native_social_ui_vtable_rva;
            std::memcpy(bytes, &vtable, sizeof(vtable));
            bytes[0x24] = 1;
            const float last_value = -2.75f;
            std::memcpy(bytes + 0x1e0, &last_value, sizeof(last_value));
            std::array<unsigned char, native_social_ui_read_size> original{};
            std::memcpy(original.data(), bytes, original.size());
            NativeSocialMeter observed;
            if (protected_page) {
                check(decode_native_social_meter(bytes, native_social_ui_read_size, vtable, observed) &&
                          observed.flags[0] == 1 && observed.values.back() == last_value,
                      "meter reads bounded original fields through the final scalar");
            }
            check(!std::memcmp(bytes, original.data(), original.size()),
                  "meter observation leaves every source byte unchanged");
            check(!decode_native_social_meter(bytes, native_social_ui_read_size - 1, vtable, observed) &&
                      observed.values.back() == 0,
                  "short meter region is refused and clears previous observation");
            check(!decode_native_social_meter(bytes, native_social_ui_read_size, vtable + 4, observed),
                  "wrong native UI type is refused");
            bytes[0x25] = 2;
            check(!decode_native_social_meter(bytes, native_social_ui_read_size, vtable, observed),
                  "non-boolean native UI flag is refused");
            bytes[0x25] = 0;
            for (float invalid : {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
                std::memcpy(bytes + 0x1e0, &invalid, sizeof(invalid));
                check(!decode_native_social_meter(bytes, native_social_ui_read_size, vtable, observed),
                      "nonfinite UI scalar cannot enter the JSON trace");
            }
            check(!decode_native_social_meter(nullptr, native_social_ui_read_size, vtable, observed),
                  "absent social UI is refused");
            VirtualFree(allocation, 0, MEM_RELEASE);
        }
    }
    address(amount_original, &Object::amount);
    address(update_original, &Object::update);
    address(action_original, &Object::action);
    address(get_original, &Manager::get);
    const sporemp::NativeHook hooks[] = {
        {reinterpret_cast<void**>(&amount_original), reinterpret_cast<void*>(amount_hook)},
        {reinterpret_cast<void**>(&update_original), reinterpret_cast<void*>(update_hook)},
        {reinterpret_cast<void**>(&action_original), reinterpret_cast<void*>(action_hook)},
        {reinterpret_cast<void**>(&get_original), reinterpret_cast<void*>(get_hook)}};
    Object o;
    Manager m;
    m.player = &o;
    const auto status = sporemp::change_hooks(hooks, std::size(hooks), true);
    std::printf("award hook transaction status=%ld; getter prefix=%02x %02x %02x %02x %02x %02x %02x %02x\n", status,
                ((unsigned char*)get_original)[0], ((unsigned char*)get_original)[1], ((unsigned char*)get_original)[2],
                ((unsigned char*)get_original)[3], ((unsigned char*)get_original)[4], ((unsigned char*)get_original)[5],
                ((unsigned char*)get_original)[6], ((unsigned char*)get_original)[7]);
    check(status == NO_ERROR, "four award signature hooks attach, including the tiny getter");
    if (status != NO_ERROR) {
        for (size_t i = 0; i < std::size(hooks); ++i) {
            const auto single = sporemp::change_hooks(hooks + i, 1, true);
            std::printf("single award hook %u attach=%ld\n", unsigned(i), single);
            if (single == NO_ERROR) {
                std::printf("single detach=%ld\n", sporemp::change_hooks(hooks + i, 1, false));
            }
        }
        return failed;
    }
    float (Object::* volatile amount)(bool) = &Object::amount;
    void (Object::* volatile update)(float, float) = &Object::update;
    void (Object::* volatile action)(uint32_t, void*) = &Object::action;
    Object* (Manager::* volatile get)() = &Manager::get;
    check((o.*amount)(true) == 1.25f && o.social && amounts == 1 && o.amount_calls == 1,
          "thiscall bool argument and positive x87 award return");
    check((o.*amount)(false) == -0.375f && !o.social && amounts == 2 && o.amount_calls == 2,
          "false argument and negative x87 result preserve stack and count");
    (o.*update)(0.016125f, -123.5f);
    check(o.dt == 0.016125f && o.elapsed == -123.5f && o.update_calls == 1 && updates == 1,
          "mode update preserves both float slots and exactly one original");
    (o.*action)(0xf1234567, &m);
    check(o.action_id == 0xf1234567 && o.payload == &m && o.action_calls == 1 && actions == 1,
          "void action preserves full-width ID and payload");
    check((m.*get)() == &o && gets == 1, "tiny native-shaped manager getter retains its pointer result");
    m.player = nullptr;
    check((m.*get)() == nullptr && gets == 2, "tiny getter retains null and exact original count");
    check(sporemp::change_hooks(hooks, std::size(hooks), false) == NO_ERROR, "all award ABI hooks detach");
    tutorial_original = &tutorial_fixture;
    const sporemp::NativeHook tutorial_binding[] = {
        {reinterpret_cast<void**>(&tutorial_original), reinterpret_cast<void*>(tutorial_hook)}};
    const auto tutorial_status = sporemp::change_hooks(tutorial_binding, std::size(tutorial_binding), true);
    check(tutorial_status == NO_ERROR, "native tutorial stdcall observer fixture attaches");
    if (tutorial_status != NO_ERROR) {
        return failed;
    }
    Abi::Tutorial volatile tutorial = &tutorial_fixture;
    const char name[] = "original tutorial";
    uint32_t value = 0;
    uintptr_t before = 0, after = 0;
    SPOREMP_READ_STACK_POINTER(before);
    for (unsigned i = 0; i < 4096; ++i) {
        value = tutorial(name, 0x80000000u + i);
    }
    SPOREMP_READ_STACK_POINTER(after);
    check(before == after && tutorial_calls == 4096 && tutorial_hooks == 4096 && tutorial_name == name &&
              tutorial_token == 0x80000fff && value == 0xfedc9876,
          "tutorial observer preserves both stack slots, all return bits, exact forwarding and RET 8 cleanup");
    check(sporemp::change_hooks(tutorial_binding, std::size(tutorial_binding), false) == NO_ERROR,
          "native tutorial observer fixture detaches");
    {
        Abi::TutorialHas has;
        Abi::TutorialRecord record;
        Abi::CinematicPlay play;
        Abi::CinematicCaption attach;
        address(has, &Object::has_tutorial);
        address(record, &Object::record_tutorial);
        address(play, &CinematicFixture::play);
        address(attach, &CinematicFixture::attach);
        Object primary, secondary;
        CinematicFixture cinematic;
        const uint32_t key[] = {0xea7666af, 0, 0x68dd6929};
        const char caption_name[] = "CRG_FirstDNA";
        check(!has(&primary, key) && !has(&secondary, key),
              "native-shaped tutorial lookup begins absent for both receivers");
        uintptr_t stack_before = 0, stack_after = 0;
        uint32_t result = 0;
        SPOREMP_READ_STACK_POINTER(stack_before);
        for (unsigned i = 0; i < 4096; ++i) {
            record(&secondary, key);
            result = play(&cinematic, caption_name, 0x81234567, 0x92345678, key, 0xa3456789, 0xb456789a);
            attach(&cinematic, 0x7046e98b, key, result);
        }
        SPOREMP_READ_STACK_POINTER(stack_after);
        check(stack_before == stack_after && has(&secondary, key) && !has(&primary, key) &&
                  secondary.tutorial_records == 4096,
              "tutorial marker ABI retains receiver and three-word key without a primary mutation");
        check(cinematic.plays == 4096 && cinematic.captions == 4096 && cinematic.name == caption_name &&
                  cinematic.option == key && cinematic.a == 0x81234567 && cinematic.b == 0x92345678 &&
                  cinematic.c == 0xa3456789 && cinematic.d == 0xb456789a && result == 0xfedc9876 &&
                  cinematic.request == result && cinematic.caption == 0x7046e98b &&
                  !std::memcmp(key, cinematic.key, sizeof(key)),
              "native cinematic/caption ABI preserves six/three stack words, full request ID and exact counts");
    }
    return failed;
}
