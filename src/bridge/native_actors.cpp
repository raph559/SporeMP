#include "native_actors.h"
#include "actor_commands.h"
#include "native_actor_abi.h"
#include "native_pool_abi.h"
#include "native_player_context.h"
#include "native_award_context.h"
#include "native_pickup.h"
#include "native_replica.h"
#include "native_scene.h"
#include "native_network.h"
#include "native_replica_abi.h"
#include "native_persistence_abi.h"
#include "detour_transaction.h"
#include "diagnostics.h"
#include "build_identity.h"
#include <Spore/Simulator/SubSystem/GameModeManager.h>
#include <Spore/Simulator/SubSystem/GameNounManager.h>
#include <Spore/Simulator/cCreatureAbility.h>
#include <Spore/Simulator/cCreatureGameData.h>
#include <Spore/Simulator/cCollectableItems.h>
#include <Spore/Simulator/SubSystem/GamePersistenceManager.h>
#include <Spore/App/IGameModeManager.h>
#include <Spore/App/cCreatureModeStrategy.h>
#include <Spore/App/IMessageManager.h>
#include <Spore/App/ICheatManager.h>
#include <Spore/ArgScript/ICommand.h>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <intrin.h>

namespace sporemp {
namespace {
using Animal = Simulator::cCreatureAnimal;
using Creature = Simulator::cCreatureBase;
using Combatant = Simulator::cCombatant;
using Noun = Simulator::cGameData;
using Manager = Simulator::cGameNounManager;
using Vec = Math::Vector3;
using ActorAbi = NativeActorAbi<Creature,Animal,Combatant,Vec>;
ActorCommands commands;
std::atomic<bool> enabled{false};
DWORD engine_thread = 0;
HANDLE output = INVALID_HANDLE_VALUE;
LARGE_INTEGER started{}, frequency{};
std::array<char, 32768> buffer{};
size_t used = 0;
uint64_t sequence = 0, bytes_written = 0, native_call = 0, executing = 0;
// Three-process M07 death/reconnect evidence exceeded the earlier 32 MiB
// budget before the bounded 600-second run ended. Keep every existing sample
// and a finite file limit, with the same explicit failure on exhaustion.
constexpr uint64_t trace_limit_bytes = 64ull * 1024ull * 1024ull;
uint64_t target_callbacks = 0, target_sampled_out = 0;
uint64_t worker_ai_entries = 0;
uint64_t ability_scope = 0, strike_scope = 0, animal_damage_scope = 0;
uint64_t pickup_scope = 0;
bool pickup_award_observed = false;
struct PickupGrant { uint64_t epoch=0; uintptr_t corpse=0; uint32_t native_id=UINT32_MAX, owner=0; };
std::array<PickupGrant, network::max_entities> pickup_grants{};
struct PickupContext {
    Animal* actor=nullptr;
    uint64_t id=0;
    uint32_t native_id=UINT32_MAX,corpse_native_id=UINT32_MAX;
    uintptr_t corpse=0;
    const void* state=nullptr;
};
PickupContext pickup_context{};
ULONGLONG next_unbound_target_sample = 0;
std::atomic<uint64_t> foreign{0};
bool failed = false, attached = false, setup_pending = false, opponents_pending = false;
uint64_t npc_source = 0, npc_target = 0;
uint32_t duel_owner = 0;
int players_pending = 0;
bool rewards_pending = false;
ULONGLONG last_sample = 0;
uintptr_t executable_base = 0, executable_end = 0;
App::IMessageManager* messages = nullptr;
App::ICheatManager* cheats = nullptr;
struct Tracked {
    uint64_t id = 0, jump_command = 0, npc_ticks = 0, avatar_ticks = 0;
    uint64_t last_ability_call = 0;
    uint32_t last_ability_caller = 0;
    uint32_t native_id = UINT32_MAX;
    uint32_t decision_samples = 0;
    ActorCommand intention{};
    ULONGLONG expires = 0, next_step = 0, next_target_sample = 0;
};
std::array<Tracked, ActorCommands::capacity> tracked{};

bool on_thread() noexcept {
    if (GetCurrentThreadId() != engine_thread) { ++foreign; return false; }
    return enabled && !failed;
}
void flush() noexcept {
    if (!used || output == INVALID_HANDLE_VALUE || failed) return;
    DWORD written = 0;
    if (!WriteFile(output, buffer.data(), static_cast<DWORD>(used), &written, nullptr) || written != used) failed = true;
    bytes_written += written; used = 0;
}
// Constant format strings, bounded buffer/file; raw native addresses are never logged.
void record(const char* event, const char* format = "", ...) noexcept {
    if (output == INVALID_HANDLE_VALUE || GetCurrentThreadId() != engine_thread || failed) return;
    // Reserve one bounded terminal record so diagnostic exhaustion cannot look
    // like an unexplained native-progress regression or a healthy trace close.
    if (bytes_written + used >= trace_limit_bytes - 4096 && strcmp(event,"trace_limit") != 0) {
        record("trace_limit",",\"limit_bytes\":%llu,\"reason\":\"diagnostic_budget_exhausted\"",trace_limit_bytes);
        flush(); failed = true; return;
    }
    char extra[2048]{};
    va_list args; va_start(args, format); int count = vsprintf_s(extra, format, args); va_end(args);
    if (count < 0) { failed = true; return; }
    LARGE_INTEGER now{}; QueryPerformanceCounter(&now);
    char line[3072]{};
    int length = sprintf_s(line,
        "{\"schema_version\":1,\"evidence_class\":\"NATIVE_PROBE\",\"harness\":\"M03\",\"event\":\"%s\","
        "\"sequence\":%llu,\"pid\":%lu,\"thread_id\":%lu,\"qpc\":%lld,\"qpc_frequency\":%lld,"
        "\"epoch\":%llu,\"executing_command\":%llu,\"foreign_callbacks\":%llu,"
        "\"ability_scope\":%llu,\"strike_scope\":%llu,\"animal_damage_scope\":%llu,\"pickup_scope\":%llu%s}\n",
        event, ++sequence, GetCurrentProcessId(), engine_thread, now.QuadPart-started.QuadPart, frequency.QuadPart,
        commands.epoch(), executing, foreign.load(), ability_scope, strike_scope, animal_damage_scope, pickup_scope, extra);
    if (length < 0) { failed = true; return; }
    if (used + length > buffer.size()) flush();
    if (!failed) { memcpy(buffer.data()+used, line, length); used += length; }
}
void player_event(const char* event,const char* fields) {record(event,"%s",fields);}
uintptr_t key(Noun* noun) { return reinterpret_cast<uintptr_t>(noun); }
ActorBinding identity(Noun* noun) { return commands.find_key(key(noun)); }
Tracked* track(uint64_t id) {
    for (auto& state : tracked) if (id && state.id == id) return &state;
    return nullptr;
}
// Resolve through a fresh native noun enumeration. A stored address alone never permits a read/call.
Animal* resolve(uint64_t id) {
    auto binding = commands.find(id);
    if (!binding.live || binding.epoch != commands.epoch() || Simulator::GetGameModeID() != kGameCreature) return nullptr;
    auto manager = Manager::Get(); if (!manager) return nullptr;
    for (auto& noun : manager->mNouns) if (key(&noun) == binding.local_key) {
        if (noun.mbIsDestroyed || noun.GetNounID() != Animal::NOUN_ID) break;
        auto animal = object_cast<Animal>(&noun);
        auto detail=track(id);
        if (animal && detail && animal->mID==detail->native_id && !animal->mbMarkedForDeletion) return animal;
        break;
    }
    record("binding_missing", ",\"actor\":%llu", id);
    commands.invalidate(binding.local_key);
    return nullptr;
}
uint64_t combat_id(Combatant* combatant) {
    return combatant ? identity(combatant->ToGameData()).id : 0;
}
uint32_t caller_rva(void* address) {
    auto value=reinterpret_cast<uintptr_t>(address);
    return value>=executable_base && value<executable_end ? static_cast<uint32_t>(value-executable_base) : 0;
}
// Read-only, engine-thread snapshots of the pinned tree layout. Failed reads
// remain absent evidence, never permission to call or mutate native objects.
template<class T> bool native_read(uintptr_t address, size_t offset, T& value) {
    if(!address || offset>UINTPTR_MAX-address || sizeof(T)>UINTPTR_MAX-address-offset) return false;
    SIZE_T copied=0;
    const DWORD saved=GetLastError();
    const bool result=ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address+offset),
        &value,sizeof(T),&copied) && copied==sizeof(T);
    SetLastError(saved);
    return result;
}
struct BehaviorSnapshot {
    uint32_t decider_id=0,behavior_id=0,depth=0,root_id=0;
    uint32_t decider_rva=0,root_rva=0,decision_rva=0,tick_rva=0;
    bool valid=false;
};
BehaviorSnapshot behavior_snapshot(Creature* animal) {
    BehaviorSnapshot result{};
    if(!animal)return result;
    const auto tree=static_cast<uintptr_t>(animal->field_B4C);
    // C09B06 supplies tree+1D0 to the native executor. Header and decider
    // fields agree with pinned SDK AI/cBehaviorTreeData.h, not inferred types.
    uint32_t header[4]{},root=0,decision=0,behavior=0,tick=0;
    if(!native_read(tree,0x1d0,header) || !native_read(tree,0,root))return result;
    result.decider_id=header[0];result.behavior_id=header[2];result.depth=header[3];
    result.decider_rva=caller_rva(reinterpret_cast<void*>(header[1]));
    result.root_rva=caller_rva(reinterpret_cast<void*>(root));
    native_read(root,0,result.root_id);
    native_read(header[1],8,decision);
    if(native_read(header[1],0x70,behavior))native_read(behavior,8,tick);
    result.decision_rva=caller_rva(reinterpret_cast<void*>(decision));
    result.tick_rva=caller_rva(reinterpret_cast<void*>(tick));
    result.valid=true;
    return result;
}
void record_behavior(uint64_t actor,const char* event,const BehaviorSnapshot& value) {
    record(event,",\"actor\":%llu,\"valid\":%s,\"decider_id\":%u,\"behavior_id\":%u,\"depth\":%u,"
        "\"root_id\":%u,\"decider_rva\":%u,\"root_rva\":%u,\"decision_rva\":%u,\"tick_rva\":%u",
        actor,value.valid?"true":"false",value.decider_id,value.behavior_id,value.depth,value.root_id,
        value.decider_rva,value.root_rva,value.decision_rva,value.tick_rva);
}
// Engine-thread-only nesting. An asynchronous strike gets its own call identity;
// it is never attributed to whichever console command happened to run last.
struct CallScope {
    uint64_t& slot;
    uint64_t previous;
    CallScope(uint64_t& value, uint64_t current) : slot(value), previous(value) { slot=current; }
    ~CallScope() { slot=previous; }
};
void combat_context(Animal* animal, const char* event) {
    if(!animal) return;
    const auto binding=identity(animal);
    if(!binding.live) return;
    const auto spatial=static_cast<Simulator::cSpatialObject*>(animal);
    const auto vtable=*reinterpret_cast<uintptr_t**>(spatial);
    // Pinned Animal spatial vtable +0x58 is the pure IsPlayerOwned predicate.
    // Installed implementation reads general flags & 0x300, not the owner table.
    const bool predicate_valid=vtable[0x58/4]-executable_base==0x802ff0;
    const auto herd=animal->mHerd.get();
    const auto index=animal->mCurrentAttackIdx;
    const auto marker=animal->mpAnimatedCreature?animal->mpAnimatedCreature->field_170[0]:0;
    const int effect_gate=index<animal->mAbilityStates.size()?int(animal->mAbilityStates[index].field_01):-1;
    record(event,",\"actor\":%llu,\"owner\":%u,\"flags\":%d,\"player_owned_binding_valid\":%s,"
        "\"native_player_owned\":%s,\"current_attack_index\":%u,\"current_attack_animation\":%u,"
        "\"herd_native_id\":%u,\"herd_owned_by_avatar\":%s,\"herd_enabled\":%s,"
        "\"herd_damage_multiplier\":%.9g,\"herd_personality\":%d,\"animation_marker\":%d,\"effect_gate\":%d",
        binding.id,binding.owner,animal->mGeneralFlags,predicate_valid?"true":"false",
        predicate_valid ? (spatial->IsPlayerOwned()?"true":"false") : "null",
        animal->mCurrentAttackIdx,animal->mCurrentAttackAnimId,herd?herd->mID:UINT32_MAX,
        herd && herd->mOwnedByAvatar?"true":"false",herd && herd->mbEnabled?"true":"false",
        herd?double(herd->mDamageMultiplier):0.0,herd?static_cast<int>(herd->mCreaturePersonality):-1,marker,effect_gate);
}
void state(Animal* animal, const char* event) {
    if (!animal) return;
    auto binding = identity(animal); if (!binding.live) return;
    auto p = animal->GetPosition();
    uint32_t active[3]{}, recharge[3]{};
    for (size_t i = 0; i < 88; ++i) {
        if (animal->mInUseAbilityBits.test(i)) active[i/32] |= 1u << (i%32);
        if (animal->mRechargingAbilityBits.test(i)) recharge[i/32] |= 1u << (i%32);
    }
    const auto detail = track(binding.id);
    // Sampled native values. Campaign mEnergy semantics are not assumed.
    record(event, ",\"actor\":%llu,\"owner\":%u,\"native_id\":%u,\"political_id\":%u,"
        "\"avatar\":%s,\"health\":%.9g,\"raw_max_health_field\":%.9g,\"hunger\":%.9g,\"energy\":%.9g,"
        "\"dead\":%s,\"has_been_eaten\":%s,\"food_value\":%.9g,\"position\":[%.9g,%.9g,%.9g],\"target\":%llu,\"ability_target\":%llu,"
        "\"last_attacker\":%llu,\"last_attacker_political\":%u,\"default_attack\":%d,"
        "\"active_bits\":[%u,%u,%u],\"recharge_bits\":[%u,%u,%u],\"inventory_count\":%u,"
        "\"species\":[%u,%u,%u],\"flags\":%d,\"intention\":%d,\"npc_ticks\":%llu,\"avatar_ticks\":%llu",
        binding.id, binding.owner, animal->mID, animal->mPoliticalID,
        Manager::Get()->GetAvatar() == animal ? "true":"false", double(animal->mHealthPoints), double(animal->mMaxHealthPoints),
        double(animal->mHunger), double(animal->mEnergy), animal->mbDead ? "true":"false", animal->mbHasBeenEaten ? "true":"false",
        double(animal->mFoodValue), double(p.x),double(p.y),double(p.z),
        combat_id(animal->GetTarget()), combat_id(animal->mpCombatantTarget), combat_id(animal->mpLastAttacker.get()),
        animal->mLastAttacker, animal->mDefaultAttackAbilityIndex,
        active[0],active[1],active[2],recharge[0],recharge[1],recharge[2],static_cast<unsigned>(animal->mItemInventory.size()),
        animal->mSpeciesKey.instanceID,animal->mSpeciesKey.typeID,animal->mSpeciesKey.groupID,animal->mGeneralFlags,animal->mIntentionTowardsTarget,
        detail ? detail->npc_ticks : 0, detail ? detail->avatar_ticks : 0);
}
ActorBinding bind(Animal* animal, uint32_t owner, bool corpse_target=false) {
    // A freshly selected corpse may never have been a bridge combat target.
    // Only a validated ownerless pickup target can acquire a dead binding;
    // controlled actors still require the normal living admission.
    if (!animal || (animal->mbDead && (!corpse_target || owner)) || animal->mbIsDestroyed) return {};
    auto binding = commands.bind(key(animal), owner);
    if (!binding.live) return {};
    if (!track(binding.id)) for (auto& slot : tracked) if (!commands.find(slot.id).live) { slot = {binding.id}; break; }
    if(auto detail=track(binding.id)) detail->native_id=animal->mID;
    record("bound", ",\"actor\":%llu,\"owner\":%u,\"native_id\":%u,\"political_id\":%u",binding.id,owner,animal->mID,animal->mPoliticalID);
    // Virtual dispatch slots are recorded as executable-relative RVAs, never pointers.
    auto combat = static_cast<Combatant*>(animal);
    auto vtable = *reinterpret_cast<uintptr_t**>(combat);
    auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    auto creature_vtable = *reinterpret_cast<uintptr_t**>(animal);
    record("creature_target_abi", ",\"actor\":%llu,\"set_creature_target_rva\":%u",
        binding.id,static_cast<unsigned>(creature_vtable[0x84/4]-base));
    record("binding_abi", ",\"actor\":%llu,\"combatant_offset\":%u,\"damage_rva\":%u,\"set_target_rva\":%u,\"get_target_rva\":%u",
        binding.id,static_cast<unsigned>(reinterpret_cast<uintptr_t>(combat)-key(animal)),
        static_cast<unsigned>(vtable[0x18/4]-base),static_cast<unsigned>(vtable[0x50/4]-base),static_cast<unsigned>(vtable[0x54/4]-base));
    state(animal, "bound_state");
    combat_context(animal,"bound_combat_context");
    record("campaign_context",",\"actor\":%llu,\"archetype\":%d,\"native_archetype_data\":%s,\"herd_native_id\":%u",
        binding.id,animal->mArchetype,animal->field_E84?"true":"false",animal->mHerd?animal->mHerd->mID:UINT32_MAX);
    int ability_count = animal->GetAbilitiesCount();
    if (ability_count >= 0 && ability_count <= 88) for (int i=0; i<ability_count; ++i) {
        auto ability = animal->GetAbility(i); if (!ability) continue;
        record("ability", ",\"actor\":%llu,\"index\":%d,\"type\":%d,\"category\":%d,\"damage\":%.9g,\"range\":%.9g,\"energy_cost\":%.9g,\"adventure_energy_cost\":%.9g,\"recharge\":%.9g",
            binding.id,i,int(ability->mType),ability->mCategory,double(ability->mDamage),double(ability->mRange),
            double(ability->mEnergyCost),double(ability->mAdventurerEnergyCost),double(ability->mRecharge));
    }
    App::ConsolePrintF("M03 actor %llu, owner %u, native %u, health %.1f",binding.id,owner,animal->mID,animal->mHealthPoints);
    flush();
    return binding;
}
void sample() {
    if (Simulator::GetGameModeID()!=kGameCreature || !Manager::Get()) return;
    for (const auto& slot : tracked) if (auto animal = resolve(slot.id)) {
        state(animal,"actor_state");
        if(animal->mCurrentAttackIdx!=UINT32_MAX) combat_context(animal,"sampled_attack_context");
    }
    auto avatar = Manager::Get()->GetAvatar();
    record("player_context", ",\"avatar\":%llu,\"avatar_native_id\":%u,\"global_dna\":%.9g",
        avatar ? identity(avatar).id : 0,avatar ? avatar->mID : UINT32_MAX,double(Simulator::cCreatureGameData::GetEvolutionPoints()));
    sample_native_player_context(commands.epoch());
    sample_native_awards();
    sample_native_replica();
}
Vec offset(Animal* animal, int direction, float distance) {
    auto p = animal->GetPosition();
    auto forward = animal->GetDirection().Normalized();
    auto right = forward.Cross(p.Normalized()).Normalized();
    auto delta = direction==0 ? forward : direction==1 ? right : direction==2 ? forward*-1.0f : right*-1.0f;
    return p+delta*distance;
}
uint64_t owned(uint32_t owner) {
    for (const auto& slot : tracked) { auto b=commands.find(slot.id); if(b.live && b.owner==owner) return b.id; }
    return 0;
}
Animal* create_in_herd(const Vec& position, Animal* source) {
    if(!source || !source->mpSpeciesProfile || !source->mHerd || !source->field_E84) return nullptr;
    auto species=source->mpSpeciesProfile;
    auto herd=Manager::Get()->CreateHerd(position,species,1,false,
        static_cast<int>(Simulator::CreaturePersonality::Guard),false);
    if(!herd) return nullptr;
    // Creature factory resolves native campaign archetype data using the herd's
    // archetype and generation (0xC09C26..0xC09C43). Preserve a live template's
    // identifiers so target UI and native reward rules receive valid data.
    herd->mArchetype=source->mHerd->mArchetype;
    herd->mArchetypeGroup=source->mHerd->mArchetypeGroup;
    herd->mGeneration=source->mHerd->mGeneration;
    herd->mOwnerSpeciesKey=source->mHerd->mOwnerSpeciesKey;
    record("native_herd_created",",\"native_id\":%u,\"members\":%u,\"enabled\":%s",
        herd->mID,static_cast<unsigned>(herd->mHerd.size()),herd->mbEnabled?"true":"false");
    // CreateHerd initializes the herd; it does not populate it synchronously in
    // this campaign. Animal::Create retains the herd and inserts the member
    // through its native factory (installed 0xC09BD4..0xC09D76).
    auto animal=herd->mHerd.empty() ? Animal::Create(position,species,1,herd,false,false) : herd->mHerd.front().get();
    if(animal && !animal->field_E84) {
        record("archetype_initialization_failed",",\"native_id\":%u,\"template\":%u,\"herd_archetype\":%u,\"generation\":%d",
            animal->mID,source->mID,herd->mArchetype,herd->mGeneration);
        Manager::Get()->DestroyInstance(animal);return nullptr;
    }
    return animal;
}
bool set_creature_target(Animal* animal, Combatant* target) {
    auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    auto vtable=*reinterpret_cast<uintptr_t**>(animal);
    // Pinned SDK virtual slot, independently checked against the installed
    // animal vtable. Native implementation retains/releases mpCombatantTarget.
    // Native campaign callers use (target,false,0), e.g. 0xD3599B..0xD359B0.
    if(vtable[0x84/4]-base!=0x803DF0) {record("creature_target_binding_rejected");return false;}
    animal->SetCreatureTarget(target,false,0);
    return true;
}
void setup() {
    auto manager = Manager::Get();
    auto avatar = manager ? manager->GetAvatar() : nullptr;
    if (!avatar || avatar->mbDead || !avatar->mpSpeciesProfile || owned(2)) {
        record("setup_rejected"); App::ConsolePrintF("M03 setup requires a live Creature avatar and no existing actor pair."); return;
    }
    if(!owned(1)) bind(avatar,1);
    // Native factory performs model/physics/ability setup; neither actor is teleported by movement commands.
    // The optional null-herd factory argument is unsafe in Creature campaign AI:
    // probe-01 reaches an unchecked mHerd->mCreaturePersonality read at 0xD41AB7.
    // Supply the native herd to the native animal factory.
    auto second=create_in_herd(offset(avatar,1,5.0f),avatar);
    if (!bind(second,2).live) record("create_failed");
    sample();flush();
}
Animal* foreign_template(Animal* a) {
    Animal* source=nullptr; float best=1e20f;
    for(auto& noun : Manager::Get()->mNouns) if(noun.GetNounID()==Animal::NOUN_ID && !noun.mbIsDestroyed) {
        auto candidate=object_cast<Animal>(&noun);
        if(candidate && !candidate->mbDead && candidate->field_E84 && candidate->mHerd && candidate->mpSpeciesProfile && candidate->mpSpeciesProfile!=a->mpSpeciesProfile && !identity(candidate).live) {
            float d=(candidate->GetPosition()-a->GetPosition()).SquaredLength();
            if(d<best) {source=candidate;best=d;}
        }
    }
    if(!source) {record("no_foreign_species"); App::ConsolePrintF("M03: no live foreign species template in the scene.");return nullptr;}
    // Both template and native factory arguments are live in this single engine callback.
    record("opponent_template", ",\"native_id\":%u,\"political_id\":%u,\"distance\":%.9g",source->mID,source->mPoliticalID,double(std::sqrt(best)));
    return source;
}
void duel(uint32_t owner) {
    auto actor=resolve(owned(owner));
    if(!actor || actor->mbDead) {record("duel_rejected");return;}
    auto source=foreign_template(actor);if(!source)return;
    auto opponent=bind(create_in_herd(offset(actor,0,1.5f),source),0);
    if(!opponent.live)return;
    ActorCommand command{};command.actor=identity(actor).id;command.owner=owner;
    command.verb=ActorVerb::attack;command.target=opponent.id;
    auto decision=commands.enqueue(command);
    record("command_queued",",\"command\":%llu,\"actor\":%llu,\"owner\":%u,\"verb\":\"attack\",\"target\":%llu,\"decision\":\"%s\",\"fixture\":\"duel\"",
        command.sequence,command.actor,owner,command.target,actor_decision(decision));
    App::ConsolePrintF("M03 duel: actor %llu, native NPC %llu, %s",command.actor,command.target,actor_decision(decision));
}
void opponents() {
    auto a=resolve(owned(1)); auto b=resolve(owned(2));
    if (!a || !b) {record("opponents_rejected"); return;}
    for(const auto& slot:tracked) {auto binding=commands.find(slot.id);if(binding.live && !binding.owner) {record("opponents_already_present");return;}}
    auto source=foreign_template(a);if(!source)return;
    bind(create_in_herd(offset(a,0,1.5f),source),0);
    b=resolve(owned(2));
    if(b) {
        bind(create_in_herd(offset(b,0,1.5f),source),0);
    }
}

using DestroyFn=void(__thiscall*)(Manager*,Noun*);
using PoolReturnFn=NativePoolAbi<void,Noun>::Return;
using LandFn=void(__thiscall*)(Creature*);
using AiFn=void(__thiscall*)(Animal*,float);
using DamageFn=ActorAbi::Damage;
using AbilityFn=void(__thiscall*)(Creature*,int,Anim::AnimIndex*);
using EnergyFn=void(__thiscall*)(Creature*,float);
using DnaFn=void(__cdecl*)(float);
using TargetFn=void(__thiscall*)(Animal*,Combatant*,bool,int);
// Signature of the internal selector is established by PlayAbility's three
// pushes at C1E5CE..C1E5D5, its stack reads and ret 0xC at C19800..C198F9.
// Observer only: no standalone call is made to this internal function.
using SelectAbilityFn=ActorAbi::SelectAbility;
// SDK cCreatureBase.h virtual slot B8; installed Animal table 146A080+B8.
// This function APPLIES native effects. It is not a read-only eligibility query.
using StrikeFn=ActorAbi::Strike;
DestroyFn destroy_original=nullptr;
PoolReturnFn pool_return_original=nullptr;
LandFn land_original=nullptr;
AiFn npc_original=nullptr, avatar_original=nullptr;
DamageFn damage_original=nullptr;
AbilityFn ability_original=nullptr;
EnergyFn energy_original=nullptr;
DnaFn dna_original=nullptr;
TargetFn target_original=nullptr;
SelectAbilityFn select_ability_original=nullptr;
StrikeFn strike_original=nullptr;
DamageFn animal_damage_original=nullptr;
ActorAbi::SetAttack set_attack_original=nullptr;
ActorAbi::AnimationDone animation_done_original=nullptr;
ActorAbi::AttackStop attack_stop_original=nullptr;
ActorAbi::Decide nest_decide_original=nullptr;
ActorAbi::MemoryCreature memory_creature_original=nullptr;
struct NestDecisionScope {
    NestDecisionScope* previous;
    uint64_t target=0;
    uint32_t owner=0;
    bool native_avatar=false;
    static NestDecisionScope* current;
    NestDecisionScope():previous(current) {current=this;}
    ~NestDecisionScope() {current=previous;}
};
NestDecisionScope* NestDecisionScope::current=nullptr;
Creature* __cdecl memory_creature_hook(void* memory) {
    const auto caller=caller_rva(_ReturnAddress());
    auto result=memory_creature_original(memory);
    // Capture the object from the ORIGINAL lookup, without another lookup or
    // pointer substitution. Only D7D900's exact player-exemption branch qualifies.
    if(on_thread() && caller==0x97d9aa && NestDecisionScope::current && result) {
        auto binding=identity(result);
        if(binding.live && binding.owner && !result->mbDead) {
            auto& scope=*NestDecisionScope::current;
            scope.target=binding.id;scope.owner=binding.owner;
            scope.native_avatar=(result->mGeneralFlags&0x200)!=0;
        }
    }
    return result;
}
float __cdecl nest_decide_hook(Creature* actor,double time,uint32_t flags,void* scratch,bool* dirty,void* decider,void* context) {
    if(!on_thread() || Simulator::GetGameModeID()!=kGameCreature)
        return nest_decide_original(actor,time,flags,scratch,dirty,decider,context);
    NestDecisionScope scope;
    const float native_score=nest_decide_original(actor,time,flags,scratch,dirty,decider,context);
    // The native home-herd rule exempts a remembered target with avatar flag 200h.
    // Extend only that classification to a live, explicitly owned M03 actor.
    // Native decide/lookup still execute once; scratch ownership and all other
    // callbacks stay native. No avatar, flags, target, damage or reward is changed.
    if(native_score>0.0f && scope.owner && !scope.native_avatar) {
        record("native_nest_player_exemption",",\"actor\":%llu,\"target\":%llu,\"target_owner\":%u,"
            "\"native_score\":%.9g,\"adapted_score\":0,\"decision_flags\":%u",
            identity(actor).id,scope.target,scope.owner,double(native_score),flags);
        return 0.0f;
    }
    return native_score;
}
bool __cdecl attack_stop_hook(void* context,Creature* self,uint32_t flags,Combatant* target,Animal* target_animal) {
    const auto binding=on_thread()?identity(self):ActorBinding{};
    auto detail=binding.live?track(binding.id):nullptr;
    if(!detail || detail->decision_samples>=90) return attack_stop_original(context,self,flags,target,target_animal);
    ++detail->decision_samples;
    const auto call=++native_call;
    const auto target_id=combat_id(target);
    const auto spatial=target?target->ToSpatialObject():nullptr;
    const auto target_data=target?target->ToGameData():nullptr;
    uint32_t context_kind=0,animation=0,other_animation=0,tree_context=0,order_kind=0;
    native_read(reinterpret_cast<uintptr_t>(context),0x18,context_kind);
    native_read(reinterpret_cast<uintptr_t>(context),0x30,animation);
    native_read(reinterpret_cast<uintptr_t>(context),0x34,other_animation);
    if(native_read(static_cast<uintptr_t>(self->field_B4C),0x600,tree_context))native_read(tree_context,8,order_kind);
    record("native_attack_stop_enter",",\"call\":%llu,\"actor\":%llu,\"target\":%llu,\"caller_rva\":%u,"
        "\"decision_flags\":%u,\"actor_flags\":%d,\"actor_political\":%u,\"target_political\":%u,"
        "\"target_spatial_enabled\":%s,\"target_destroyed\":%s,\"target_combat_state\":%d,\"target_stealthed\":%s,"
        "\"context_kind\":%u,\"context_animation\":%u,\"context_other_animation\":%u,\"order_kind\":%u",
        call,binding.id,target_id,caller_rva(_ReturnAddress()),flags,self->mGeneralFlags,self->mPoliticalID,
        target_data?target_data->mPoliticalID:UINT32_MAX,spatial?(spatial->mbEnabled?"true":"false"):"null",
        target_data?(target_data->mbIsDestroyed?"true":"false"):"null",target?target->field_34:-1,
        target_animal?(target_animal->mbStealthed?"true":"false"):"null",context_kind,animation,other_animation,order_kind);
    const bool result=attack_stop_original(context,self,flags,target,target_animal);
    record("native_attack_stop_return",",\"call\":%llu,\"actor\":%llu,\"target\":%llu,\"stop\":%s",
        call,binding.id,target_id,result?"true":"false");
    return result;
}
void __fastcall set_attack_hook(Creature* self,void*,int index) {
    const auto binding=on_thread()?identity(self):ActorBinding{};
    const auto caller=caller_rva(_ReturnAddress());
    if(binding.live && (self->mCurrentAttackIdx!=UINT32_MAX || index!=-1))
        record("native_attack_index_set",",\"actor\":%llu,\"before_index\":%u,\"requested_index\":%d,\"caller_rva\":%u",
            binding.id,self->mCurrentAttackIdx,index,caller);
    set_attack_original(self,index);
}
bool __fastcall animation_done_hook(Creature* self,void*,uint32_t animation) {
    const auto binding=on_thread()?identity(self):ActorBinding{};
    const bool relevant=binding.live && self->mCurrentAttackIdx!=UINT32_MAX && animation==self->mCurrentAttackAnimId;
    const auto caller=caller_rva(_ReturnAddress());
    const auto index=relevant?self->mCurrentAttackIdx:UINT32_MAX;
    const bool result=animation_done_original(self,animation);
    if(relevant && result) {
        record("native_attack_animation_done",",\"actor\":%llu,\"index\":%u,\"animation\":%u,\"caller_rva\":%u,\"result\":true",
            binding.id,index,animation,caller);
        if(auto animal=resolve(binding.id))combat_context(animal,"animation_done_context");
    }
    return result;
}
int __fastcall select_ability_hook(Creature* self,void*,int index,bool recharge,bool extra) {
    // Native PlayAbility handles the original selector's -1 result itself,
    // preserving its output/return convention and native no-action path.
    if(!native_replica_allows(replica::Mutation::ability)) return -1;
    if(!on_thread()) return select_ability_original(self,index,recharge,extra);
    const auto binding=identity(self);
    if(!binding.live) return select_ability_original(self,index,recharge,extra);
    const auto call=++native_call;
    record("native_ability_select_enter",",\"call\":%llu,\"actor\":%llu,\"owner\":%u,\"index\":%d,"
        "\"recharge_flag\":%s,\"extra_flag\":%s,\"current_attack_index\":%u",
        call,binding.id,binding.owner,index,recharge?"true":"false",extra?"true":"false",self->mCurrentAttackIdx);
    const int result=select_ability_original(self,index,recharge,extra);
    record("native_ability_select_return",",\"call\":%llu,\"actor\":%llu,\"result_index\":%d",call,binding.id,result);
    return result;
}
bool __fastcall strike_hook(Animal* self,void*,Combatant* target,int index,Vec* position) {
    if(!native_replica_allows(replica::Mutation::damage)) return false;
    if(!on_thread()) return strike_original(self,target,index,position);
    const auto actor=identity(self);
    const auto target_id=combat_id(target);
    if(!actor.live && !target_id) return strike_original(self,target,index,position);
    const auto call=++native_call;
    CallScope scope(strike_scope,call);
    record("native_strike_enter",",\"call\":%llu,\"actor\":%llu,\"owner\":%u,\"target\":%llu,"
        "\"index\":%d,\"position_supplied\":%s,\"caller_rva\":%u",
        call,actor.id,actor.owner,target_id,index,position?"true":"false",caller_rva(_ReturnAddress()));
    if(auto animal=resolve(actor.id)) combat_context(animal,"strike_actor_context");
    if(auto animal=resolve(target_id)) combat_context(animal,"strike_target_context");
    const bool result=strike_original(self,target,index,position);
    // Raw native result; a true/false result alone is not hit acceptance.
    record("native_strike_return",",\"call\":%llu,\"actor\":%llu,\"target\":%llu,\"result\":%s",
        call,actor.id,target_id,result?"true":"false");
    return result;
}
int __fastcall animal_damage_hook(Combatant* self,void*,float amount,uint32_t political,int type,const Vec& direction,Combatant* attacker) {
    if(!native_replica_allows(replica::Mutation::damage)) return 0;
    if(!on_thread()) return animal_damage_original(self,amount,political,type,direction,attacker);
    // This extra layer records actor-to-actor combat. The existing base hook
    // retains environmental/starvation evidence without tripling every tick.
    if(!attacker) return animal_damage_original(self,amount,political,type,direction,attacker);
    const auto receiver=identity(self->ToGameData());
    const auto source=attacker?identity(attacker->ToGameData()):ActorBinding{};
    if(!receiver.live && !source.live) return animal_damage_original(self,amount,political,type,direction,attacker);
    const auto call=++native_call;
    CallScope scope(animal_damage_scope,call);
    NativeDamageOwnerScope award_scope(resolve(source.id),source.owner,source.id);
    record("native_animal_damage_enter",",\"call\":%llu,\"receiver\":%llu,\"receiver_owner\":%u,"
        "\"attacker\":%llu,\"attacker_owner\":%u,\"damage\":%.9g,\"type\":%d,\"caller_rva\":%u",
        call,receiver.id,receiver.owner,source.id,source.owner,double(amount),type,caller_rva(_ReturnAddress()));
    const int result=animal_damage_original(self,amount,political,type,direction,attacker);
    if(auto animal=resolve(receiver.id)) state(animal,"animal_damage_state");
    record("native_animal_damage_return",",\"call\":%llu,\"receiver\":%llu,\"attacker\":%llu,\"result\":%d",
        call,receiver.id,source.id,result);
    return result;
}
bool checkpoint_idle();
void __fastcall target_hook(Animal* self,void*,Combatant* target,bool flag,int intention) {
    auto caller=caller_rva(_ReturnAddress());
    bool selection_view=false;
    if(on_thread() && caller && !executing && native_replica_is_client() &&
        !native_replica_bootstrap_open() && self==resolve(owned(1)) &&
        self==Manager::Get()->GetAvatar() && !self->mbDead && !self->mbMarkedForDeletion &&
        self->mCurrentAttackIdx==UINT32_MAX && checkpoint_idle()) {
        // C03DF0's inspected body retains/releases the selected combatant,
        // updates local intention/selection timeout and refreshes native UI.
        // It does not run AI, an ability, damage, inventory or a reward. These
        // remain independently denied, including callbacks from the UI refresh.
        // Limit this reversible view to the native main avatar and a target
        // found in the current native census. No pointer or target is published.
        auto manager=Manager::Get();
        bool live_target=target==nullptr;
        for(auto& noun:manager->mNouns) {
            if(live_target) break;
            if(noun.mbIsDestroyed || noun.GetNounID()!=Animal::NOUN_ID) continue;
            auto animal=object_cast<Animal>(&noun);
            live_target=animal && !animal->mbMarkedForDeletion && static_cast<Combatant*>(animal)==target;
        }
        selection_view=live_target && native_replica_allows(replica::Mutation::presentation);
    }
    if(!selection_view && !native_replica_allows(replica::Mutation::targeting)) {
        static unsigned denied_samples=0;
        if(on_thread() && denied_samples++<16)
            record("replica_target_denied",",\"actor\":%llu,\"caller_rva\":%u,\"intention\":%d",identity(self).id,caller,intention);
        return;
    }
    const bool record_it=on_thread();
    auto actor=record_it ? identity(self) : ActorBinding{};
    auto next=record_it ? combat_id(target) : 0;
    if(actor.live && caller==0x9672f6 && self->mCurrentAttackIdx!=UINT32_MAX) {
        // D672A0 has four saved registers and D672F4 has three stack arguments.
        // At this exact, guarded callsite the callback's return slot is +32
        // from this hook's incoming return slot. No general stack unwind is inferred.
        const auto return_slot=reinterpret_cast<uintptr_t>(_AddressOfReturnAddress());
        uintptr_t callback_return=0,cleanup_return=0,decision_node=0;
        uint32_t selected_index=UINT32_MAX,selected_id=0,selected_rva=0;
        const bool frame_read=native_read(return_slot,32,callback_return);
        const auto callback_caller=frame_read?caller_rva(reinterpret_cast<void*>(callback_return)):0;
        bool selection_read=false;
        // Qualified only for BC8720's leaf cleanup call at BC87CE. Its three
        // saved registers plus six outgoing stack words put its return at +72.
        if(callback_caller==0x7c87d0 && native_read(return_slot,72,cleanup_return) &&
            caller_rva(reinterpret_cast<void*>(cleanup_return))==0x7c957d) {
            // BC9578 runs when a different child won. The caller's ESI is saved
            // at +64; BC957D reloads the winning index from caller ESP+54h,
            // which is +160 from this incoming slot. Never infer a general unwind.
            uint32_t count=0,children=0;
            if(native_read(return_slot,64,decision_node) && native_read(return_slot,160,selected_index) &&
                native_read(decision_node,0x7c,count) && count<=256 && selected_index<count &&
                native_read(decision_node,0x80,children)) {
                const auto selected=static_cast<uintptr_t>(children)+selected_index*0x88;
                selected_rva=caller_rva(reinterpret_cast<void*>(selected));
                selection_read=selected_rva && native_read(selected,0,selected_id);
            }
        }
        record("native_attack_deactivation",",\"actor\":%llu,\"attack_index\":%u,\"frame_read\":%s,\"callback_caller_rva\":%u,"
            "\"cleanup_caller_rva\":%u,\"selection_read\":%s,\"selected_index\":%u,\"selected_decider_id\":%u,\"selected_decider_rva\":%u",
            actor.id,self->mCurrentAttackIdx,frame_read?"true":"false",
            callback_caller,caller_rva(reinterpret_cast<void*>(cleanup_return)),selection_read?"true":"false",
            selected_index,selected_id,selected_rva);
        record_behavior(actor.id,"native_deactivation_behavior",behavior_snapshot(self));
    }
    uint64_t call=0;
    bool trace_target=actor.live || next;
    if(trace_target) {
        ++target_callbacks;
        // NPC decision evaluation repeatedly assigns and clears temporary
        // targets every frame. Sample those calls once per second per actor;
        // keep owned-actor changes and explicit command dispatch unthrottled.
        if(!actor.owner && !executing && (!actor.live || self->mCurrentAttackIdx==UINT32_MAX)) {
            auto detail=track(actor.id);
            auto& next_sample=detail ? detail->next_target_sample : next_unbound_target_sample;
            auto now=GetTickCount64();
            if(now<next_sample) {trace_target=false;++target_sampled_out;}
            else next_sample=now+1000;
        }
    }
    if(trace_target) {
        call=++native_call;
        record("native_target_enter",",\"call\":%llu,\"actor\":%llu,\"native_id\":%u,\"owner\":%u,\"before\":%llu,\"target\":%llu,\"target_native_id\":%u,\"flag\":%s,\"intention\":%d,\"previous_intention\":%d,\"caller_rva\":%u,\"attack_index\":%u",
            call,actor.id,self->mID,actor.owner,combat_id(self->mpCombatantTarget),next,target?target->ToGameData()->mID:UINT32_MAX,
            flag?"true":"false",intention,self->mIntentionTowardsTarget,caller,self->mCurrentAttackIdx);
    }
    target_original(self,target,flag,intention);
    if(selection_view)
        record("replica_selection_view",",\"actor\":%llu,\"target\":%llu,\"target_native_id\":%u,"
            "\"caller_rva\":%u,\"intention\":%d,\"gameplay_authority\":false",
            actor.id,combat_id(target),target?target->ToGameData()->mID:UINT32_MAX,caller,intention);
    if(call) record("native_target_return",",\"call\":%llu,\"actor\":%llu,\"attack_index\":%u",call,actor.id,self->mCurrentAttackIdx);
}
void retire_native_incarnation(Noun* noun,const char* reason) {
        // Native IDs and allocation addresses can both be reused. Retire the
        // observed beneficiary before destruction so a later corpse cannot
        // inherit an earlier incarnation's first-feed result.
        for(auto& grant:pickup_grants)if(grant.epoch==commands.epoch() && grant.native_id==noun->mID) {
            record("native_pickup_grant_retired",",\"corpse_native_id\":%u,\"owner\":%u,\"reason\":\"%s\"",grant.native_id,grant.owner,reason);
            grant={};
        }
        native_scene_invalidated(noun->mID);
        auto binding=commands.invalidate(key(noun));
        native_replica_invalidated(binding.id);
        if(binding.live) record("invalidated",",\"actor\":%llu,\"native_id\":%u,\"reason\":\"%s\"",binding.id,noun->mID,reason);
}
void __fastcall destroy_hook(Manager* self,void*,Noun* noun) {
    if(on_thread() && noun) retire_native_incarnation(noun,"destroy");
    destroy_original(self,noun); // No dereference after the native destroy.
}
Animal* current_pool_animal(Noun* candidate,uint32_t expected=UINT32_MAX) {
    auto manager=Manager::Get();
    if(!candidate || !manager || Simulator::GetGameModeID()!=kGameCreature)return nullptr;
    for(auto& noun:manager->mNouns)if(&noun==candidate) {
        if(noun.mbIsDestroyed || noun.field_20 || noun.GetNounID()!=Animal::NOUN_ID ||
            (expected!=UINT32_MAX && noun.mID!=expected))return nullptr;
        auto animal=object_cast<Animal>(&noun);
        if(!animal || *reinterpret_cast<const uintptr_t*>(animal)!=executable_base+0x106a080 ||
            reinterpret_cast<uintptr_t>(&animal->field_E54)-key(animal)!=0xe54)return nullptr;
        return animal;
    }
    return nullptr;
}
bool __fastcall pool_return_hook(void* self,void*,Noun* noun) {
    // ACCEC0 retains its noun in the original pool, increments Creature+E54,
    // and clears the animal's herd. It does not call DestroyInstance. Resolve
    // again after return: no saved pointer alone permits a native dereference.
    auto before=on_thread()?current_pool_animal(noun):nullptr;
    const auto native_id=before?before->mID:UINT32_MAX;
    const auto counter=before?static_cast<uint32_t>(before->field_E54):0;
    const bool result=pool_return_original(self,noun);
    if(result && before)if(auto after=current_pool_animal(noun,native_id)) {
        const auto next=static_cast<uint32_t>(after->field_E54);
        if(next!=counter) {
            record("native_pool_return_observed",",\"native_id\":%u,\"counter_before\":%u,\"counter_after\":%u,\"enabled\":%s,\"herd_present\":%s",
                native_id,counter,next,after->mbEnabled?"true":"false",after->mHerd?"true":"false");
            retire_native_incarnation(after,"native_pool_return");
        }
    }
    return result;
}
void __fastcall land_hook(Creature* self,void*) {
    native_replica_allows(replica::Mutation::presentation);
    const bool record_it=on_thread(); const auto b=record_it ? identity(self) : ActorBinding{};
    if(b.live) {
        auto t=track(b.id);
        record("native_landing",",\"actor\":%llu,\"owner\":%u,\"jump_command\":%llu",b.id,b.owner,t?t->jump_command:0);
        if(t) t->jump_command=0;
    }
    land_original(self);
}
void __fastcall npc_hook(Animal* self,void*,float delta) {
    if(!native_replica_allows(replica::Mutation::ai)) return;
    if (GetCurrentThreadId() == engine_thread) ++worker_ai_entries;
    const auto binding=on_thread()?identity(self):ActorBinding{};
    auto detail=binding.live?track(binding.id):nullptr;
    const auto before_index=detail?self->mCurrentAttackIdx:UINT32_MAX;
    const auto before_call=detail?detail->last_ability_call:0;
    const auto before_behavior=detail?behavior_snapshot(self):BehaviorSnapshot{};
    if(detail) ++detail->npc_ticks;
    npc_original(self,delta);
    // Observe the original AI boundary, including an ability started and cleared
    // within the same tick. No attack state or native decision is changed.
    if(detail) if(auto animal=resolve(binding.id)) {
        if(detail->last_ability_call!=before_call || before_index!=animal->mCurrentAttackIdx) {
            record("native_npc_attack_transition",",\"actor\":%llu,\"before_index\":%u,\"after_index\":%u,\"last_ability_call\":%llu,\"last_ability_caller_rva\":%u,\"target\":%llu",
                binding.id,before_index,animal->mCurrentAttackIdx,detail->last_ability_call,detail->last_ability_caller,combat_id(animal->mpCombatantTarget));
            record_behavior(binding.id,"native_ai_behavior_before",before_behavior);
            record_behavior(binding.id,"native_ai_behavior_after",behavior_snapshot(animal));
        }
    }
}
void __fastcall avatar_hook(Animal* self,void*,float delta) {
    if(!native_replica_allows(replica::Mutation::ai)) return;
    if (GetCurrentThreadId() == engine_thread) ++worker_ai_entries;
    if(on_thread()) if(auto t=track(identity(self).id)) ++t->avatar_ticks;
    avatar_original(self,delta);
}
int __fastcall damage_hook(Combatant* self,void*,float damage,uint32_t attacker_id,int type,const Vec& direction,Combatant* attacker) {
    if(!native_replica_allows(replica::Mutation::damage)) return 0;
    const bool record_it=on_thread();
    const auto receiver=record_it ? identity(self->ToGameData()) : ActorBinding{};
    const auto source=record_it && attacker ? identity(attacker->ToGameData()) : ActorBinding{};
    uint64_t call=0;
    if(receiver.live || source.live) {
        call=++native_call;
        record("native_damage_enter",",\"call\":%llu,\"receiver\":%llu,\"attacker\":%llu,\"attacker_owner\":%u,\"damage\":%.9g,\"attacker_political\":%u,\"type\":%d,\"receiver_health\":%.9g",
            call,receiver.id,source.id,source.owner,double(damage),attacker_id,type,double(self->mHealthPoints));
    }
    const int result=damage_original(self,damage,attacker_id,type,direction,attacker);
    if(call) {
        record("native_damage_return",",\"call\":%llu,\"receiver\":%llu,\"attacker\":%llu,\"result\":%d",call,receiver.id,source.id,result);
        if(auto animal=resolve(receiver.id)) state(animal,"damage_state");
    }
    return result;
}
void __fastcall ability_hook(Creature* self,void*,int index,Anim::AnimIndex* destination) {
    const auto caller=caller_rva(_ReturnAddress());
    const bool record_it=on_thread(); const auto b=record_it ? identity(self) : ActorBinding{};
    uint64_t call=0;
    if(b.live) call=++native_call;
    // Foreign callbacks never touch engine-thread scope state.
    if(!record_it) {ability_original(self,index,destination);return;}
    CallScope scope(ability_scope,call);
    if(call) {
        if(auto detail=track(b.id)) {detail->last_ability_call=call;detail->last_ability_caller=caller;}
        record("native_ability_enter",",\"call\":%llu,\"actor\":%llu,\"owner\":%u,\"index\":%d,\"target\":%llu,\"caller_rva\":%u",call,b.id,b.owner,index,combat_id(self->mpCombatantTarget),caller);
    }
    ability_original(self,index,destination);
    if(call) {
        if(auto animal=resolve(b.id)) combat_context(animal,"ability_return_context");
        record("native_ability_return",",\"call\":%llu,\"actor\":%llu",call,b.id);
    }
}
void __fastcall energy_hook(Creature* self,void*,float amount) {
    if(!native_replica_allows(replica::Mutation::energy)) return;
    const bool record_it=on_thread(); const auto b=record_it ? identity(self) : ActorBinding{};
    if(b.live) record("native_energy",",\"actor\":%llu,\"owner\":%u,\"amount\":%.9g,\"before\":%.9g",b.id,b.owner,double(amount),double(self->mEnergy));
    energy_original(self,amount);
}
void __cdecl dna_hook(float amount) {
    if(!native_replica_allows(replica::Mutation::reward)) return;
    auto caller=caller_rva(_ReturnAddress());
    if(dispatch_native_owned_award(amount,dna_original,caller)) {
        if(pickup_scope && native_pickup_award_scope_granted())pickup_award_observed=true;
        return;
    }
    const bool record_it=on_thread() && Simulator::GetGameModeID()==kGameCreature;
    auto avatar=record_it && Manager::Get() ? Manager::Get()->GetAvatar() : nullptr;
    const auto avatar_id=avatar?identity(avatar).id:0;
    const auto call=record_it?++native_call:0;
    if(record_it) record("native_global_dna_enter",",\"call\":%llu,\"amount\":%.9g,\"avatar\":%llu,\"before\":%.9g,\"caller_rva\":%u",call,double(amount),avatar_id,double(Simulator::cCreatureGameData::GetEvolutionPoints()),caller);
    const auto before=record_it?Simulator::cCreatureGameData::GetEvolutionPoints():0;
    dna_original(amount);
    const auto after=record_it?Simulator::cCreatureGameData::GetEvolutionPoints():0;
    // Original first feeding legitimately awards zero DNA in this fixture.
    // Observe its returned call without inventing a positive bonus; actual
    // resource benefit additionally requires original food/nutrition evidence.
    if(pickup_scope && caller==native_pickup_reward_caller_rva && record_it &&
       std::isfinite(amount) && amount>=0 && std::isfinite(before) && std::isfinite(after) &&
       (amount==0 ? after==before : after>=before && after<=before+amount))pickup_award_observed=true;
    if(record_it) record("native_global_dna_return",",\"call\":%llu,\"avatar\":%llu,\"after\":%.9g",call,avatar_id,double(after));
}
Animal* current_pickup_animal(uint32_t native_id, uintptr_t expected=0) {
    if(native_id==UINT32_MAX || !Manager::Get() || Simulator::GetGameModeID()!=kGameCreature)return nullptr;
    Animal* result=nullptr;
    for(auto& noun:Manager::Get()->mNouns)if(noun.mID==native_id) {
        if(result || noun.mbIsDestroyed || noun.field_20 || noun.GetNounID()!=Animal::NOUN_ID)return nullptr;
        result=object_cast<Animal>(&noun);
        if(!result || result->mbMarkedForDeletion || (expected && key(result)!=expected))return nullptr;
    }
    return result;
}
bool pickup_scope_snapshot(NativePickupSnapshot& state, ActorBinding& binding) {
    if(!pickup_scope || !on_thread() || native_replica_is_client() || !pickup_context.id)return false;
    binding=commands.find(pickup_context.id);
    if(!binding.live || binding.epoch!=commands.epoch() || (binding.owner!=1&&binding.owner!=2) ||
       resolve(binding.id)!=pickup_context.actor)return false;
    return native_pickup_snapshot(pickup_context.actor,pickup_context.native_id,pickup_context.state,state) &&
        state.corpse_native_id==pickup_context.corpse_native_id &&
        current_pickup_animal(state.corpse_native_id,pickup_context.corpse);
}
bool pickup_first_feed_owner() {
    NativePickupSnapshot state;ActorBinding binding;
    const bool qualified=pickup_scope_snapshot(state,binding) && binding.owner==2 && !state.eaten &&
        state.claimed_by_actor && native_awards_ready(binding.id,binding.epoch) && native_pickup_award_scope_ready();
    if(qualified)record("native_pickup_owner_classified",",\"actor\":%llu,\"owner\":2,\"corpse_native_id\":%u",binding.id,state.corpse_native_id);
    return qualified;
}
bool pickup_honor_owned_claim() {
    NativePickupSnapshot state;ActorBinding binding;
    if(!pickup_scope_snapshot(state,binding) || !state.claimant_present || state.claimed_by_actor)return false;
    auto claimant=current_pickup_animal(state.claimant_native_id);
    if(!claimant || claimant->mbDead || !claimant->mbEnabled ||
       !std::isfinite(claimant->mHealthPoints) || claimant->mHealthPoints<=0)return false;
    const auto other=claimant?identity(claimant):ActorBinding{};
    if(!other.live || other.epoch!=commands.epoch() || (other.owner!=1&&other.owner!=2) ||
       other.id==binding.id || resolve(other.id)!=claimant)return false;
    record("native_pickup_existing_claim_honored",",\"actor\":%llu,\"owner\":%u,\"claimant_actor\":%llu,\"claimant_owner\":%u,\"corpse_native_id\":%u",
        binding.id,binding.owner,other.id,other.owner,state.corpse_native_id);
    return true;
}
void pickup_sample(const char* name,const ActorBinding& binding,const NativePickupSnapshot& state) {
    record(name,",\"actor\":%llu,\"owner\":%u,\"actor_native_id\":%u,\"corpse_native_id\":%u,\"phase\":%u,"
        "\"claimant_native_id\":%u,\"claimant_present\":%s,\"claimed_by_actor\":%s,\"fed_on\":%s,"
        "\"food\":%.9g,\"hunger\":%.9g,\"health\":%.9g,\"avatar_bit\":%s,\"first_feed_state\":%s",
        binding.id,binding.owner,state.actor_native_id,state.corpse_native_id,state.phase,state.claimant_native_id,
        state.claimant_present?"true":"false",state.claimed_by_actor?"true":"false",state.eaten?"true":"false",
        double(state.food),double(state.hunger),double(state.health),state.avatar_classified?"true":"false",state.first_feed?"true":"false");
}
class PickupCallbackScope {
    PickupContext previous_context;
    uint64_t previous_scope;
    bool previous_award;
    NativePickupOwnerScope unowned;
public:
    PickupCallbackScope():previous_context(pickup_context),previous_scope(pickup_scope),
        previous_award(pickup_award_observed),unowned(nullptr,0,0,nullptr) {
        pickup_context={};pickup_scope=0;pickup_award_observed=false;
    }
    ~PickupCallbackScope() {
        pickup_context=previous_context;pickup_scope=previous_scope;pickup_award_observed=previous_award;
    }
    PickupCallbackScope(const PickupCallbackScope&)=delete;
    PickupCallbackScope& operator=(const PickupCallbackScope&)=delete;
};
bool pickup_tick_dispatch(NativePickupTick original,Animal* actor,double clock,uint32_t flags,
    uintptr_t parameter,void* state,void* activation,float delta) {
    if(!native_replica_allows(replica::Mutation::pickup))return false;
    if(!on_thread())return original(actor,clock,flags,parameter,state,activation,delta);
    // Even an untracked or unqualified nested original tick must not inherit
    // the outer actor's interior-branch predicates or reward/grant observation.
    // Every engine-thread return path restores both contexts through RAII.
    PickupCallbackScope callback_scope;
    const auto binding=identity(actor);
    if(!binding.live || (binding.owner!=1&&binding.owner!=2) || resolve(binding.id)!=actor)
        return original(actor,clock,flags,parameter,state,activation,delta);
    NativePickupSnapshot before;
    if(!native_pickup_snapshot(actor,actor->mID,state,before))
        return original(actor,clock,flags,parameter,state,activation,delta);
    auto corpse=current_pickup_animal(before.corpse_native_id);
    if(!corpse)return original(actor,clock,flags,parameter,state,activation,delta);
    // Only this original callback gets an owner identity. Individual audited
    // reward/action calls enter their own short StageScope; AI never does.
    pickup_context={actor,binding.id,actor->mID,before.corpse_native_id,key(corpse),state};pickup_scope=++native_call;
    pickup_award_observed=false;
    pickup_sample("native_pickup_tick_enter",binding,before);
    bool result=false;
    {
        NativePickupOwnerScope owner(actor,binding.owner,binding.id,corpse);
        result=original(actor,clock,flags,parameter,state,activation,delta);
    }
    NativePickupSnapshot after;
    const bool observed=resolve(binding.id)==actor && native_pickup_snapshot(actor,before.actor_native_id,state,after);
    if(observed) {
        pickup_sample("native_pickup_tick_return_state",binding,after);
        if(!before.eaten && after.eaten && before.corpse_native_id==after.corpse_native_id && pickup_award_observed) {
            auto current=current_pickup_animal(after.corpse_native_id,key(corpse));
            PickupGrant* grant=nullptr;
            for(auto& entry:pickup_grants)if(entry.epoch==commands.epoch() && entry.native_id==after.corpse_native_id && entry.corpse==key(corpse)) {grant=&entry;break;}
            if(!grant)for(auto& entry:pickup_grants)if(!entry.epoch) {grant=&entry;break;}
            if(current && grant) {
                *grant={commands.epoch(),key(corpse),after.corpse_native_id,binding.owner};
                record("native_pickup_first_feed_observed",",\"actor\":%llu,\"owner\":%u,\"corpse_native_id\":%u,\"food_before\":%.9g,\"food_after\":%.9g,\"remaining_food_is_separate\":true",
                    binding.id,binding.owner,after.corpse_native_id,double(before.food),double(after.food));
            } else record("native_pickup_observation_rejected",",\"reason\":\"corpse_lifetime_or_capacity\"");
        }
    }
    record("native_pickup_tick_return",",\"actor\":%llu,\"owner\":%u,\"result\":%s,\"state_observed\":%s,\"original_reward_return_observed\":%s",binding.id,binding.owner,result?"true":"false",observed?"true":"false",pickup_award_observed?"true":"false");
    return result;
}
const NativeHook hooks[]={
    {reinterpret_cast<void**>(&destroy_original),reinterpret_cast<void*>(destroy_hook)},
    {reinterpret_cast<void**>(&pool_return_original),reinterpret_cast<void*>(pool_return_hook)},
    {reinterpret_cast<void**>(&land_original),reinterpret_cast<void*>(land_hook)},
    {reinterpret_cast<void**>(&npc_original),reinterpret_cast<void*>(npc_hook)},
    {reinterpret_cast<void**>(&avatar_original),reinterpret_cast<void*>(avatar_hook)},
    {reinterpret_cast<void**>(&damage_original),reinterpret_cast<void*>(damage_hook)},
    {reinterpret_cast<void**>(&ability_original),reinterpret_cast<void*>(ability_hook)},
    {reinterpret_cast<void**>(&energy_original),reinterpret_cast<void*>(energy_hook)},
    {reinterpret_cast<void**>(&dna_original),reinterpret_cast<void*>(dna_hook)},
    {reinterpret_cast<void**>(&target_original),reinterpret_cast<void*>(target_hook)},
    {reinterpret_cast<void**>(&select_ability_original),reinterpret_cast<void*>(select_ability_hook)},
    {reinterpret_cast<void**>(&strike_original),reinterpret_cast<void*>(strike_hook)},
    {reinterpret_cast<void**>(&animal_damage_original),reinterpret_cast<void*>(animal_damage_hook)},
    {reinterpret_cast<void**>(&set_attack_original),reinterpret_cast<void*>(set_attack_hook)},
    {reinterpret_cast<void**>(&animation_done_original),reinterpret_cast<void*>(animation_done_hook)},
    {reinterpret_cast<void**>(&attack_stop_original),reinterpret_cast<void*>(attack_stop_hook)},
    {reinterpret_cast<void**>(&nest_decide_original),reinterpret_cast<void*>(nest_decide_hook)},
    {reinterpret_cast<void**>(&memory_creature_original),reinterpret_cast<void*>(memory_creature_hook)},
    native_player_message_binding(),
    native_player_remove_binding(),
    native_award_owned_binding(),native_award_amount_binding(),
    native_award_player_binding(),native_award_avatar_binding(),
    native_award_display_binding(),native_award_action_binding(),
    native_award_update_binding(),native_award_goal_binding(),
    native_pickup_tick_binding(),native_pickup_first_feed_binding(),native_pickup_claim_binding()
};

void cancel_intention(Tracked& detail,const char* reason) {
    if(!detail.intention.sequence)return;
    record("intention_cancelled",",\"command\":%llu,\"actor\":%llu,\"owner\":%u,\"target\":%llu,\"reason\":\"%s\"",
        detail.intention.sequence,detail.id,detail.intention.owner,detail.intention.target,reason);
    detail.intention={};detail.expires=detail.next_step=0;
}
// Bounded owner input adapter, not a combat simulator. Original AI, WalkTo,
// PlayAbility, range checks, cooldowns, damage and rewards continue unchanged.
// Native pointers are resolved afresh each dispatch and never retained here.
void dispatch_intentions() {
    auto now=GetTickCount64();
    for(auto& detail:tracked) {
        const auto command=detail.intention;
        if(!command.sequence || now<detail.next_step)continue;
        detail.next_step=now+250;
        if(now>=detail.expires) {cancel_intention(detail,"expired");continue;}
        if(commands.authorize(command)!=ActorDecision::accepted) {cancel_intention(detail,"authority_or_target_invalid");continue;}
        auto actor=resolve(command.actor);auto target=resolve(command.target);
        if(!actor || !target || actor->mbDead || actor->mHealthPoints<=0 || target->mbDead || target->mHealthPoints<=0 || !target->field_E84) {
            cancel_intention(detail,"actor_or_target_unavailable");continue;
        }
        int index=actor->mDefaultAttackAbilityIndex;
        if(index<0 || index>=88 || index>=actor->GetAbilitiesCount()) {cancel_intention(detail,"unsupported_ability");continue;}
        auto ability=actor->GetAbility(index);
        // Use the pinned native ability and footprint values to choose when
        // to submit input. This is not a replacement for native hit checks.
        // A centre-distance constant can demand overlapping creature bodies.
        if(!ability || !std::isfinite(ability->mRange) || ability->mRange<=0 ||
            !std::isfinite(actor->mFootprintRadius) || actor->mFootprintRadius<0 ||
            !std::isfinite(target->mFootprintRadius) || target->mFootprintRadius<0) {
            cancel_intention(detail,"unsupported_native_range");continue;
        }
        float approach_range=ability->mRange+actor->mFootprintRadius+target->mFootprintRadius;
        auto delta=target->GetPosition()-actor->GetPosition();
        auto distance=std::sqrt(delta.SquaredLength());
        executing=command.sequence;
        if(distance>approach_range) {
            auto destination=target->GetPosition();
            record("intention_walk",",\"actor\":%llu,\"target\":%llu,\"distance\":%.9g,\"approach_range\":%.9g,\"destination\":[%.9g,%.9g,%.9g]",
                command.actor,command.target,double(distance),double(approach_range),double(destination.x),double(destination.y),double(destination.z));
            actor->WalkTo(Creature::kStandardSpeed,destination,delta.Normalized(),0.3f,0.5f);
        } else if(!actor->mInUseAbilityBits.test(index) && !actor->mRechargingAbilityBits.test(index)) {
            record("intention_attack",",\"actor\":%llu,\"owner\":%u,\"target\":%llu,\"distance\":%.9g,\"approach_range\":%.9g,\"ability_range\":%.9g,\"actor_footprint\":%.9g,\"target_footprint\":%.9g",
                command.actor,command.owner,command.target,double(distance),double(approach_range),double(ability->mRange),double(actor->mFootprintRadius),double(target->mFootprintRadius));
            sample();
            if(set_creature_target(actor,static_cast<Combatant*>(target))) {
                actor=resolve(command.actor);target=resolve(command.target);
                if(actor && target && !actor->mbDead && !target->mbDead)actor->PlayAbility(index);
            } else cancel_intention(detail,"binding_rejected");
            sample();
        }
        executing=0;
    }
}
void execute(ActorCommand command) {
    auto decision=commands.authorize(command);
    auto animal=decision==ActorDecision::accepted ? resolve(command.actor) : nullptr;
    if(!animal || animal->mbDead || animal->mHealthPoints<=0) {
        record("command_rejected",",\"command\":%llu,\"actor\":%llu,\"reason\":\"%s\"",command.sequence,command.actor,animal?"dead_or_zero_health":actor_decision(decision));return;
    }
    bool targeted=command.verb==ActorVerb::attack || command.verb==ActorVerb::approach || command.verb==ActorVerb::engage || command.verb==ActorVerb::pickup;
    auto target=targeted ? resolve(command.target) : nullptr;
    if(targeted && (!target || !target->field_E84 || (command.verb==ActorVerb::pickup ? !target->mbDead||target->mFoodValue<=0 : target->mbDead||target->mHealthPoints<=0))) {record("command_rejected",",\"command\":%llu,\"reason\":\"target_unavailable\"",command.sequence);return;}
    executing=command.sequence;
    if(auto detail=track(command.actor))cancel_intention(*detail,"superseded_by_command");
    sample();
    record("command_begin",",\"command\":%llu,\"actor\":%llu,\"owner\":%u,\"verb\":\"%s\",\"target\":%llu,\"direction\":%d",command.sequence,command.actor,command.owner,actor_verb(command.verb),command.target,command.direction);
    if(command.verb==ActorVerb::engage) {
        if(auto detail=track(command.actor)) {
            detail->intention=command;detail->expires=GetTickCount64()+30000;detail->next_step=0;
            record("intention_started",",\"actor\":%llu,\"owner\":%u,\"target\":%llu,\"maximum_ms\":30000",command.actor,command.owner,command.target);
        }
    } else if(command.verb==ActorVerb::move || command.verb==ActorVerb::approach) {
        auto destination=target ? target->GetPosition() : offset(animal,command.direction,6.0f);
        auto direction=(destination-animal->GetPosition()).Normalized();
        record("native_walk_request",",\"actor\":%llu,\"destination\":[%.9g,%.9g,%.9g]",command.actor,double(destination.x),double(destination.y),double(destination.z));
        animal->WalkTo(Creature::kStandardSpeed,destination,direction,0.5f,1.0f);
    } else if(command.verb==ActorVerb::jump) {
        auto t=track(command.actor); if(t) t->jump_command=command.sequence;
        bool accepted=animal->DoJump(0);
        record("native_jump_return",",\"actor\":%llu,\"accepted\":%s",command.actor,accepted?"true":"false");
        if(t && !accepted) t->jump_command=0;
    } else if(command.verb==ActorVerb::attack) {
        int index=animal->mDefaultAttackAbilityIndex;
        if(index>=0 && index<animal->GetAbilitiesCount()) {
            if(!set_creature_target(animal,static_cast<Combatant*>(target))) {executing=0;flush();return;}
            state(animal,"target_assigned");
            animal=resolve(command.actor); target=resolve(command.target);
            if(animal && target && !animal->mbDead && !target->mbDead) animal->PlayAbility(index);
        } else record("unsupported_ability",",\"actor\":%llu",command.actor);
    } else if(command.verb==ActorVerb::pickup) {
        const bool ordered=native_pickup_order(animal,animal->mID,target,target->mID);
        record("native_pickup_order_return",",\"actor\":%llu,\"owner\":%u,\"target\":%llu,\"ordered\":%s,\"accepted_is_queued\":true",command.actor,command.owner,command.target,ordered?"true":"false");
    } else if(command.verb==ActorVerb::stop) {
        set_creature_target(animal,nullptr);
    } else if(command.verb==ActorVerb::retire) {
        if(animal!=Manager::Get()->GetAvatar()) Manager::Get()->DestroyInstance(animal);
        else record("retire_rejected_avatar");
    }
    record("command_return",",\"command\":%llu,\"actor\":%llu",command.sequence,command.actor);
    sample(); executing=0; flush();
}
uint64_t integer(const char* value) {
    if(!value || !*value || strlen(value)>18) return 0;
    for(auto c=value;*c;++c) if(*c<'0'||*c>'9') return 0;
    return _strtoui64(value,nullptr,10);
}
class Command final : public ArgScript::ICommand {
public:
    void ParseLine(const ArgScript::Line& line) override {
        if(!on_thread()) return;
        if(native_replica_is_client()) {App::ConsolePrintF("M05 replica commands require the bounded worker control channel.");return;}
        size_t count=0;auto args=line.GetArgumentsRange(&count,1,4);
        if(Simulator::GetGameModeID()!=kGameCreature) {App::ConsolePrintF("M03 requires the original Creature campaign.");return;}
        if(count==1 && !strcmp(args[0],"setup")) setup_pending=true;
        else if(count==1 && !strcmp(args[0],"players")) players_pending=1;
        else if(count==2 && !strcmp(args[0],"players") && !strcmp(args[1],"off")) players_pending=-1;
        else if(count==1 && !strcmp(args[0],"rewards")) rewards_pending=true;
        else if(count==1 && !strcmp(args[0],"opponents")) opponents_pending=true;
        else if(count==2 && !strcmp(args[0],"duel") && (integer(args[1])==1 || integer(args[1])==2)) duel_owner=static_cast<uint32_t>(integer(args[1]));
        else if(count==1 && !strcmp(args[0],"status")) {sample();App::ConsolePrintF("M03: A=%llu (owner 1), B=%llu (owner 2), epoch %llu, trace %s",owned(1),owned(2),commands.epoch(),failed?"incomplete":"healthy");}
        else if(count==3 && !strcmp(args[0],"npc")) {npc_source=integer(args[1]);npc_target=integer(args[2]);}
        else if(count>=3) {
            ActorCommand command{};
            uint64_t owner=integer(args[1]); if(owner>UINT32_MAX) owner=0;
            command.owner=static_cast<uint32_t>(owner);command.actor=integer(args[2]);
            if(!strcmp(args[0],"move") && count==4) {
                command.verb=ActorVerb::move;
                if(!strcmp(args[3],"forward")) command.direction=0;
                else if(!strcmp(args[3],"right")) command.direction=1;
                else if(!strcmp(args[3],"back")) command.direction=2;
                else if(!strcmp(args[3],"left")) command.direction=3;
                else {App::ConsolePrintF("M03 move direction: forward/right/back/left");return;}
            } else if(!strcmp(args[0],"attack") && count==4) {command.verb=ActorVerb::attack;command.target=integer(args[3]);}
            else if(!strcmp(args[0],"approach") && count==4) {command.verb=ActorVerb::approach;command.target=integer(args[3]);}
            else if(!strcmp(args[0],"engage") && count==4) {command.verb=ActorVerb::engage;command.target=integer(args[3]);}
            else if(!strcmp(args[0],"jump") && count==3) command.verb=ActorVerb::jump;
            else if(!strcmp(args[0],"stop") && count==3) command.verb=ActorVerb::stop;
            else if(!strcmp(args[0],"retire") && count==3) command.verb=ActorVerb::retire;
            else {App::ConsolePrintF("M03 unknown command.");return;}
            auto decision=commands.enqueue(command);
            record("command_queued",",\"command\":%llu,\"actor\":%llu,\"owner\":%u,\"verb\":\"%s\",\"target\":%llu,\"decision\":\"%s\"",command.sequence,command.actor,command.owner,actor_verb(command.verb),command.target,actor_decision(decision));
            App::ConsolePrintF("M03 %s actor %llu: %s, command %llu",actor_verb(command.verb),command.actor,actor_decision(decision),command.sequence);
        } else App::ConsolePrintF("M03: setup | players [off] | opponents | duel OWNER | status | move OWNER ACTOR DIRECTION | jump OWNER ACTOR | attack/approach/engage OWNER ACTOR TARGET | npc NPC TARGET | stop OWNER ACTOR | retire OWNER ACTOR");
        flush();
    }
};
class Listener final : public App::IUnmanagedMessageListener {
public:
    bool HandleMessage(uint32_t id,void*) override {
        if(!on_thread()) return false;
        if(id==App::kMsgOnModeExit) {
            native_network_scene_exit();
            native_replica_scene_exit();
            for(auto& detail:tracked)cancel_intention(detail,"scene_exit");
            retire_native_player_context("scene_exit");players_pending=0;rewards_pending=false;
            record("scene_exit");commands.scene_exit();pickup_grants={};setup_pending=opponents_pending=false;npc_source=npc_target=0;duel_owner=0;flush();
        } else if(id==App::kMsgAppUpdate && Simulator::GetGameModeID()==kGameCreature) {
            if(setup_pending) {setup_pending=false;setup();}
            if(players_pending) {
                const int action=players_pending;players_pending=0;
                if(action<0)retire_native_player_context("explicit_stop");
                else {
                    auto b=resolve(owned(2));
                    const bool created=b && !b->mbDead && create_native_player_context(commands.epoch());
                    record("player_context_request",",\"created\":%s",created?"true":"false");
                    App::ConsolePrintF(created?"M03 native player B initialized; reward routing remains disabled.":"M03 native player creation rejected.");
                }
                sample();flush();
            }
            if(rewards_pending) {
                rewards_pending=false;
                const auto actor_id=owned(2);
                const bool reward_ready=enable_native_awards(resolve(actor_id),actor_id,commands.epoch());
                record("reward_context_request",",\"enabled\":%s",reward_ready?"true":"false");
                App::ConsolePrintF(reward_ready?"M03 native B reward context enabled.":"M03 reward context rejected; setup and players required.");
                flush();
            }
            update_native_awards(commands.epoch());
            if(opponents_pending) {opponents_pending=false;opponents();}
            if(duel_owner) {auto owner=duel_owner;duel_owner=0;duel(owner);}
            if(npc_source) {
                auto source=resolve(npc_source);auto target=resolve(npc_target);
                if(source && target && !commands.find(npc_source).owner && !source->mbDead && !target->mbDead) {
                    record("npc_target_request",",\"actor\":%llu,\"target\":%llu",npc_source,npc_target);
                    set_creature_target(source,static_cast<Combatant*>(target));state(source,"npc_target_assigned");
                } else record("npc_target_rejected");
                npc_source=npc_target=0;
            }
            ActorCommand command; if(commands.pop(command)) execute(command);
            dispatch_intentions();
            if(GetTickCount64()-last_sample>=250) {last_sample=GetTickCount64();sample();flush();}
        }
        return false;
    }
} listener;
constexpr uint32_t message_ids[]={App::kMsgAppUpdate,App::kMsgOnModeExit};
bool verify_context_bindings() {
    // Exact installed executable inspection, 2026-09-12. This supplements the
    // pre-injection file guard; it is not runtime gameplay qualification.
    struct Code { uint32_t rva; std::array<unsigned char,16> bytes; };
    const Code code[]={
        {0x6ccec0,{0x83,0xec,0x78,0x53,0x8b,0x9c,0x24,0x80,0x00,0x00,0x00,0x56,0x8b,0xf1,0x85,0xdb}},
        {0x6cd0d5,{0xff,0x86,0x54,0x0e,0x00,0x00,0x8b,0x55,0x00,0x8b,0x42,0x20,0x8b,0xcd,0xff,0xd0}},
        {0x6cd13e,{0x8b,0xce,0xff,0xd2,0x5f,0x5d,0x5e,0xb0,0x01,0x5b,0x83,0xc4,0x78,0xc2,0x04,0x00}},
        {0x819800,{0x53,0x55,0x56,0x57,0x8b,0xf9,0x83,0xcd,0xff,0x80,0x7c,0x24,0x18,0x00,0x8d,0x9f}},
        {0x802b40,{0x8b,0x44,0x24,0x0c,0x83,0xec,0x20,0x53,0x55,0x57,0x8b,0x7c,0x24,0x30,0x8b,0xe9}},
        {0x8075e0,{0x83,0xec,0x6c,0x53,0x8b,0xd9,0xe8,0x45,0x0c,0xce,0xff,0x83,0xf8,0x02,0x0f,0x84}},
        {0x802ff0,{0x8b,0x81,0x98,0x0a,0x00,0x00,0x25,0x00,0x03,0x00,0x00,0xf7,0xd8,0x1b,0xc0,0xf7}},
        {0x807500,{0x8b,0x44,0x24,0x04,0x57,0x8b,0xf9,0x89,0x87,0x8c,0x0e,0x00,0x00,0x83,0xf8,0xff}},
        {0x80e2d0,{0x56,0x8b,0xf1,0x8b,0x8e,0x54,0x0b,0x00,0x00,0x85,0xc9,0x75,0x06,0xb0,0x01,0x5e}},
        {0x965980,{0x57,0x8b,0x7c,0x24,0x14,0x85,0xff,0x0f,0x84,0x4a,0x02,0x00,0x00,0x8b,0x07,0x8b}},
        {0x9672a0,{0x53,0x55,0x56,0x8b,0x74,0x24,0x10,0x80,0xbe,0x5e,0x0b,0x00,0x00,0x00,0x57,0x75}},
        {0x7c8720,{0x53,0x56,0x8b,0x74,0x24,0x20,0x57,0x8b,0x7c,0x24,0x20,0x32,0xdb,0x8d,0x49,0x00}},
        {0x7c9578,{0xe8,0xa3,0xf1,0xff,0xff,0x8b,0x4c,0x24,0x54,0x83,0xc4,0x18,0x8b,0x44,0x24,0x2c}},
        {0x97d900,{0xf7,0x44,0x24,0x10,0x00,0x10,0x00,0x00,0x56,0x57,0x0f,0x85,0xd3,0x00,0x00,0x00}},
        {0x999930,{0x8b,0x44,0x24,0x04,0x56,0x85,0xc0,0x74,0x33,0x8b,0x48,0x2c,0x85,0xc9,0x74,0x2c}}
    };
    const auto image_size=executable_end-executable_base;
    for(const auto& entry:code) {
        if(entry.rva>image_size || entry.bytes.size()>image_size-entry.rva ||
            memcmp(reinterpret_cast<const void*>(executable_base+entry.rva),entry.bytes.data(),entry.bytes.size())) {
            record("context_binding_rejected",",\"rva\":%u",entry.rva);return false;
        }
    }
    const struct {uint32_t slot, expected;} slots[]={
        {0x106a080+0xb8,0x802b40}, // Animal's primary vtable, SDK funcB8h
        {0x1069e30+0x18,0x8075e0}, // Animal's Combatant virtual TakeDamage
        {0x1069ec0+0x58,0x802ff0}  // Animal's Spatial IsPlayerOwned
    };
    for(const auto& entry:slots) {
        if(entry.slot>image_size || sizeof(uintptr_t)>image_size-entry.slot ||
            *reinterpret_cast<const uintptr_t*>(executable_base+entry.slot)!=executable_base+entry.expected) {
            record("context_vtable_rejected",",\"slot_rva\":%u",entry.slot);return false;
        }
    }
    record("context_bindings_checked",",\"code_prefixes\":%u,\"virtual_slots\":3",unsigned(_countof(code)));
    return true;
}
bool checkpoint_idle() {
    if (commands.pending_count() || executing || setup_pending || opponents_pending ||
        players_pending || rewards_pending || duel_owner || npc_source || npc_target ||
        ability_scope || strike_scope || animal_damage_scope || pickup_scope) return false;
    for (const auto& detail : tracked) if (commands.find(detail.id).live &&
        (detail.intention.sequence || detail.jump_command)) return false;
    // Reward context requires this separate player. Also count native players
    // below: a saved but not yet adopted second player is outside this slice.
    return !native_second_player(commands.epoch());
}
void checkpoint_actor_event(const NativeActorFingerprint& actor, uint32_t owner, uint64_t request) {
    record("checkpoint_actor", ",\"request\":%llu,\"owner\":%u,\"native_id\":%u,\"herd_native_id\":%u,"
        "\"species_instance\":%u,\"species_type\":%u,\"species_group\":%u,\"archetype\":%u",
        request, owner, actor.native_id, actor.herd_native_id, actor.species_instance,
        actor.species_type, actor.species_group, actor.archetype);
}
bool fingerprint_equal(const NativeActorFingerprint& a, const NativeActorFingerprint& b) {
    return a.native_id == b.native_id && a.herd_native_id == b.herd_native_id &&
        a.species_instance == b.species_instance && a.species_type == b.species_type &&
        a.species_group == b.species_group && a.archetype == b.archetype;
}
// Every address is obtained from this enumeration or checked against it before
// dereferencing. Native IDs are checkpoint-local matching candidates only.
bool checkpoint_fingerprint(Animal* actor, NativeActorFingerprint& out) {
    auto manager = Manager::Get();
    if (!manager || !actor) return false;
    Noun* actor_noun = nullptr;
    for (auto& noun : manager->mNouns) if (&noun == static_cast<Noun*>(actor)) {
        if (noun.mbIsDestroyed || noun.field_20 || noun.GetNounID() != Animal::NOUN_ID) return false;
        actor_noun = &noun; break;
    }
    if (!actor_noun || object_cast<Animal>(actor_noun) != actor || actor->mbDead ||
        actor->mbMarkedForDeletion || !actor->field_E84 || !actor->mpSpeciesProfile || !actor->mHerd ||
        actor->mID == UINT32_MAX) return false;
    auto herd = actor->mHerd.get();
    bool herd_live = false;
    size_t actor_ids = 0, herd_ids = 0;
    for (auto& noun : manager->mNouns) {
        if (noun.mID == actor->mID) ++actor_ids;
        if (&noun == static_cast<Noun*>(herd)) {
            if (noun.mbIsDestroyed || noun.field_20 || noun.GetNounID() != Simulator::cHerd::NOUN_ID ||
                noun.mID == UINT32_MAX) return false;
            herd_live = true;
        }
    }
    if (!herd_live || actor_ids != 1) return false;
    for (auto& noun : manager->mNouns) if (noun.mID == herd->mID) ++herd_ids;
    if (herd_ids != 1) return false;
    size_t memberships = 0;
    for (const auto& member : herd->mHerd) if (member.get() == actor) ++memberships;
    if (memberships != 1) return false;
    out = {actor->mID, herd->mID, actor->mSpeciesKey.instanceID, actor->mSpeciesKey.typeID,
        actor->mSpeciesKey.groupID, static_cast<uint32_t>(actor->mArchetype)};
    return true;
}
bool checkpoint_census(const NativeActorCheckpoint* expected, std::array<Animal*, 2>& candidates,
                       uint64_t request) {
    auto manager = Manager::Get();
    size_t animal_count = 0, herd_count = 0, player_count = 0;
    std::array<size_t, 2> id_matches{};
    bool first_player_present = false;
    auto first_player = manager->GetPlayer();
    for (auto& noun : manager->mNouns) {
        const auto type = noun.GetNounID();
        if (!noun.mbIsDestroyed && !noun.field_20) {
            if (type == Animal::NOUN_ID) ++animal_count;
            if (type == Simulator::cHerd::NOUN_ID) ++herd_count;
            // Installed noun ID, independently qualified for M03. SDK cPlayer
            // TYPE differs from this executable and is deliberately unused.
            if (type == 0x02c21781) {
                ++player_count;
                if (&noun == static_cast<Noun*>(first_player)) first_player_present = true;
            }
        }
        if (expected) for (size_t i = 0; i < candidates.size(); ++i)
            if (noun.mID == expected->actors[i].native_id) {
                ++id_matches[i];
                const bool available = !noun.mbIsDestroyed && !noun.field_20 && type == Animal::NOUN_ID;
                if (available) candidates[i] = object_cast<Animal>(&noun);
                record("checkpoint_candidate", ",\"request\":%llu,\"owner\":%u,\"native_id\":%u,"
                    "\"noun_type\":%u,\"available\":%s", request, static_cast<unsigned>(i + 1),
                    noun.mID, type, available ? "true" : "false");
            }
    }
    record("checkpoint_census", ",\"request\":%llu,\"animals\":%u,\"herds\":%u,\"players\":%u,"
        "\"a_id_matches\":%u,\"b_id_matches\":%u,\"native_player_present\":%s",
        request, static_cast<unsigned>(animal_count), static_cast<unsigned>(herd_count),
        static_cast<unsigned>(player_count), static_cast<unsigned>(id_matches[0]),
        static_cast<unsigned>(id_matches[1]), first_player_present ? "true" : "false");
    return player_count == 1 && first_player_present &&
        (!expected || (id_matches[0] == 1 && id_matches[1] == 1 && candidates[0] && candidates[1]));
}
}
void native_actor_worker_event(const char* event, const char* json_fields) {
    if (!on_thread() || !event || !json_fields) return;
    record(event, "%s", json_fields); flush();
}
bool snapshot_native_actor_checkpoint(NativeActorCheckpoint& checkpoint, uint64_t request) {
    checkpoint = {};
    if (!on_thread()) return false;
    const char* reason = "scene_unavailable";
    NativeActorCheckpoint capture;
    bool valid = false;
    do {
        if (Simulator::GetGameModeID() != kGameCreature || !Manager::Get()) break;
        reason = "pending_actions_or_player_context";
        if (!checkpoint_idle()) break;
        reason = "actor_pair_unavailable";
        if (commands.live_count() != 2) break;
        auto a = resolve(owned(1)); auto b = resolve(owned(2));
        if (!a || !b || a == b || a != Manager::Get()->GetAvatar()) break;
        std::array<Animal*, 2> ignored{};
        reason = "additional_native_player";
        if (!checkpoint_census(nullptr, ignored, request)) break;
        reason = "native_fingerprint_unavailable";
        if (!checkpoint_fingerprint(a, capture.actors[0]) ||
            !checkpoint_fingerprint(b, capture.actors[1])) break;
        capture.epoch = commands.epoch();
        checkpoint_actor_event(capture.actors[0], 1, request);
        checkpoint_actor_event(capture.actors[1], 2, request);
        valid = true; reason = "captured";
    } while (false);
    record("checkpoint_snapshot", ",\"request\":%llu,\"valid\":%s,\"reason\":\"%s\"",
        request, valid ? "true" : "false", reason); flush();
    if (failed) return false;
    if (valid) checkpoint = capture;
    return valid;
}
bool restore_native_actor_checkpoint(const NativeActorCheckpoint& checkpoint, uint64_t request) {
    if (!on_thread()) return false;
    const char* reason = "scene_unavailable";
    bool valid = false;
    std::array<Animal*, 2> candidates{};
    do {
        if (Simulator::GetGameModeID() != kGameCreature || !Manager::Get()) break;
        reason = "stale_epoch";
        if (checkpoint.epoch != commands.epoch()) break;
        reason = "existing_bindings_or_pending_context";
        if (commands.live_count() || !checkpoint_idle()) break;
        reason = "invalid_native_ids";
        if (checkpoint.actors[0].native_id == UINT32_MAX || checkpoint.actors[1].native_id == UINT32_MAX ||
            checkpoint.actors[0].native_id == checkpoint.actors[1].native_id) break;
        reason = "native_census_mismatch";
        if (!checkpoint_census(&checkpoint, candidates, request)) break;
        reason = "native_avatar_mismatch";
        if (candidates[0] != Manager::Get()->GetAvatar() || candidates[0] == candidates[1]) break;
        reason = "native_fingerprint_mismatch";
        NativeActorFingerprint observed_a, observed_b;
        if (!checkpoint_fingerprint(candidates[0], observed_a) ||
            !checkpoint_fingerprint(candidates[1], observed_b) ||
            !fingerprint_equal(checkpoint.actors[0], observed_a) ||
            !fingerprint_equal(checkpoint.actors[1], observed_b)) break;
        // Both slots are free after the no-live-bindings check. These two pure
        // table operations contain no native callback: publish both identities
        // before the existing bind helper emits any native diagnostics.
        auto a = commands.bind(key(candidates[0]), 1);
        auto b = commands.bind(key(candidates[1]), 2);
        if (!a.live || !b.live) {
            commands.invalidate(key(candidates[0])); commands.invalidate(key(candidates[1]));
            reason = "binding_capacity"; break;
        }
        bind(candidates[0], 1); bind(candidates[1], 2);
        if (failed) {
            commands.invalidate(key(candidates[0])); commands.invalidate(key(candidates[1]));
            return false;
        }
        valid = true; reason = "adopted_existing_nouns";
    } while (false);
    record("checkpoint_restore", ",\"request\":%llu,\"valid\":%s,\"reason\":\"%s\","
        "\"actor_a\":%llu,\"actor_b\":%llu,\"created_nouns\":0", request, valid ? "true" : "false",
        reason, owned(1), owned(2)); flush();
    return valid && !failed;
}
worker::Message native_actor_worker_status() {
    worker::Message value;
    // A trace error disables unsafe native observation, not the progress already
    // counted on this engine thread. Preserve it in the explicit failed status.
    if (GetCurrentThreadId() == engine_thread) {
        value.epoch = commands.epoch(); value.values[2] = worker_ai_entries;
    }
    if (!on_thread()) { value.values[0] = static_cast<uint64_t>(worker::Phase::failed); return value; }
    const auto mode = Simulator::GetGameModeID();
    value.epoch = commands.epoch();
    value.values[0] = static_cast<uint64_t>(mode == kGameCreature && Manager::Get() && Manager::Get()->GetAvatar() ?
        worker::Phase::scene : Simulator::IsLoadingGameMode() ? worker::Phase::loading : worker::Phase::menu);
    value.values[2] = worker_ai_entries;
    value.values[3] = owned(1); value.values[4] = owned(2);
    value.values[5] = mode;
    return value;
}
bool native_actor_read_vitals(uint64_t local_id, replica::Vitals& value) {
    if(!on_thread()) return false;
    auto actor=resolve(local_id);
    if(!actor || actor!=Manager::Get()->GetAvatar() || actor->mbDead || actor->mbMarkedForDeletion ||
        !checkpoint_idle()) return false;
    value={actor->mHealthPoints,actor->mEnergy,actor->mHunger,Simulator::cCreatureGameData::GetEvolutionPoints()};
    return replica::valid(value);
}
bool native_actor_owner_native_id(uint32_t owner, uint32_t& native_id) {
    if (!on_thread() || (owner != 1 && owner != 2)) return false;
    auto actor = resolve(owned(owner));
    if (!actor) return false;
    native_id = actor->mID;
    return true;
}
bool native_actor_network_prepare_rewards() {
    if(!on_thread() || native_replica_is_client() || Simulator::GetGameModeID()!=kGameCreature)return false;
    const auto id=owned(2);
    auto actor=resolve(id);
    if(!actor || actor->mbDead)return false;
    if(native_awards_ready(id,commands.epoch()))return true;
    if(!native_second_player(commands.epoch()) && !create_native_player_context(commands.epoch()))return false;
    actor=resolve(id);
    if(!actor || actor->mbDead)return false;
    const bool ready=enable_native_awards(actor,id,commands.epoch());
    record("network_reward_context",",\"owner\":2,\"actor\":%llu,\"ready\":%s",id,ready?"true":"false");
    return ready;
}
bool native_actor_owner_dna(uint32_t owner,float& dna,bool* current_native) {
    if(current_native)*current_native=false;
    if(!on_thread() || Simulator::GetGameModeID()!=kGameCreature || (owner!=1 && owner!=2))return false;
    auto actor=resolve(owned(owner));
    if(!actor)return false;
    if(owner==2)return native_awards_read_dna(owned(2),commands.epoch(),dna,actor->mbDead,current_native);
    if(actor!=Manager::Get()->GetAvatar())return false;
    const float value=Simulator::cCreatureGameData::GetEvolutionPoints();
    if(!std::isfinite(value) || value<0)return false;
    dna=value;if(current_native)*current_native=true;return true;
}
worker::Result native_actor_network_command(uint64_t epoch,uint32_t owner,
    uint32_t actor_native_id,uint32_t target_native_id,ActorVerb verb,int direction,uint64_t request) {
    using Result=worker::Result;
    if(!on_thread() || native_replica_is_client() || Simulator::GetGameModeID()!=kGameCreature)return Result::unavailable;
    if(epoch!=commands.epoch())return Result::stale;
    if(!request || (owner!=1 && owner!=2) || actor_native_id==UINT32_MAX)return Result::invalid;
    const bool targeted=verb==ActorVerb::attack || verb==ActorVerb::approach || verb==ActorVerb::engage || verb==ActorVerb::pickup;
    if(!targeted && verb!=ActorVerb::move && verb!=ActorVerb::jump && verb!=ActorVerb::stop)return Result::invalid;
    if(verb==ActorVerb::move && (direction<0 || direction>3))return Result::invalid;
    if((targeted && target_native_id==UINT32_MAX) || (!targeted && target_native_id!=UINT32_MAX))return Result::invalid;
    const auto actor_id=owned(owner);
    auto actor=resolve(actor_id);
    if(!actor || actor->mbDead || actor->mHealthPoints<=0 || actor->mID!=actor_native_id)return Result::invalid;
    ActorCommand command;command.actor=actor_id;command.owner=owner;command.verb=verb;command.direction=direction;
    if(targeted) {
        Animal* target=nullptr;
        // A global/native mapping from a previous frame is insufficient. Resolve
        // one current noun and register its process-local lifetime afresh.
        for(auto& noun:Manager::Get()->mNouns)if(noun.mID==target_native_id) {
            if(target || noun.mbIsDestroyed || noun.field_20 || noun.GetNounID()!=Animal::NOUN_ID)return Result::invalid;
            target=object_cast<Animal>(&noun);
        }
        if(!target || target==actor || (verb==ActorVerb::pickup ? !target->mbDead||target->mFoodValue<=0 : target->mbDead||target->mHealthPoints<=0) || target->mbMarkedForDeletion ||
           !target->field_E84 || !target->mpSpeciesProfile || !target->mHerd)return Result::invalid;
        auto target_binding=identity(target);
        // M07's shared-NPC fixture does not enable PvP or player-owned targets.
        if(target_binding.live && target_binding.owner)return Result::invalid;
        if(!target_binding.live)target_binding=bind(target,0,verb==ActorVerb::pickup);
        if(!target_binding.live)return Result::busy;
        command.target=target_binding.id;
    }
    const auto decision=commands.enqueue(command);
    record("network_actor_command_queued",",\"request\":%llu,\"command\":%llu,\"actor\":%llu,\"actor_native_id\":%u,"
        "\"owner\":%u,\"verb\":\"%s\",\"target\":%llu,\"target_native_id\":%u,\"decision\":\"%s\"",
        request,command.sequence,command.actor,actor_native_id,owner,actor_verb(verb),command.target,target_native_id,actor_decision(decision));
    return decision==ActorDecision::accepted?Result::accepted:decision==ActorDecision::full?Result::busy:Result::invalid;
}
uint32_t native_actor_pickup_owner(uint32_t corpse_native_id) {
    if(!on_thread() || native_replica_is_client())return 0;
    for(const auto& grant:pickup_grants)if(grant.epoch==commands.epoch() && grant.native_id==corpse_native_id) {
        auto corpse=current_pickup_animal(corpse_native_id,grant.corpse);
        if(corpse && corpse->mbHasBeenEaten)return grant.owner;
    }
    return 0;
}
bool native_actor_apply_vitals(uint64_t local_id, const replica::Vitals& value) {
    replica::Vitals before;
    if(!native_replica_is_client() || !native_replica_allows(replica::Mutation::apply_state) ||
        !replica::valid(value) || !native_actor_read_vitals(local_id,before)) return false;
    auto actor=resolve(local_id);
    if(!actor) return false;
    const auto combatant=static_cast<Combatant*>(actor);
    const auto vtable=*reinterpret_cast<uintptr_t**>(combatant);
    if(vtable[0x58/4]!=executable_base+0x805d50) {
        record("replica_vitals_rejected",",\"reason\":\"effective_health_binding\"");return false;
    }
    // C05D50 reads base/species/stage modifiers. The raw Combatant maximum
    // is 1 in this real 10-HP fixture and is not the effective Creature limit.
    const float maximum=actor->GetMaxHitPoints();
    if(!std::isfinite(maximum) || maximum<=0 || value.health>maximum) {
        record("replica_vitals_rejected",",\"reason\":\"effective_health_limit\",\"maximum\":%.9g",double(maximum));return false;
    }
    record("replica_vitals_limits",",\"effective_max_health\":%.9g,\"raw_max_health\":%.9g,\"raw_max_energy\":%.9g",
        double(maximum),double(actor->mMaxHealthPoints),double(actor->mMaxEnergy));
    // Absolute presentation fields only. Never call TakeDamage, AddEvolutionPoints,
    // native reward handlers, death, or an irreversible action to replay a result.
    actor->mHealthPoints=value.health; actor->mEnergy=value.energy; actor->mHunger=value.hunger;
    Simulator::cCreatureGameData::SetEvolutionPoints(value.dna);
    replica::Vitals after;
    const bool matched=native_actor_read_vitals(local_id,after) && after.health==value.health &&
        after.energy==value.energy && after.hunger==value.hunger && after.dna==value.dna;
    record("replica_vitals_applied",",\"actor\":%llu,\"matched\":%s,\"before_health\":%.9g,"
        "\"health\":%.9g,\"energy\":%.9g,\"hunger\":%.9g,\"before_dna\":%.9g,\"dna\":%.9g",
        local_id,matched?"true":"false",double(before.health),double(value.health),double(value.energy),
        double(value.hunger),double(before.dna),double(value.dna));
    return matched;
}
bool native_actor_probe_replica_denials() {
    if (!on_thread() || !native_replica_is_client() || native_replica_bootstrap_open()) return false;
    replica::Vitals before;
    auto a=resolve(owned(1)), b=resolve(owned(2));
    auto manager=Manager::Get();
    auto player=manager ? manager->GetPlayer() : nullptr;
    auto items=player ? player->mpCRGItems.get() : nullptr;
    auto strategy=App::cCreatureModeStrategy::Get();
    if (!a || !b || a==b || !native_actor_read_vitals(owned(1),before) || !items ||
        items->mUnlockableItems.empty() || !a->mHerd || !a->mpSpeciesProfile || !strategy) return false;
    const auto key=items->mUnlockableItems.begin()->first;
    const auto inventory_digest=[&]() {
        uint64_t digest=0;
        // Read IDs/flags in native containers in this callback only; never transmit
        // addresses or the container representation. Commutative, so order-independent.
        for (const auto& item:items->mItemStatusInfos) {
            uint64_t id=0; static_assert(sizeof(item.first)==8); memcpy(&id,&item.first,8);
            digest ^= (id ^ (uint64_t(item.second)<<56)) * 0x9e3779b97f4a7c15ULL;
        }
        return digest;
    };
    const auto item_digest=inventory_digest();
    const auto flags_count=items->mItemStatusInfos.size(), unlocked_count=items->mUnlockedItems.size();
    const auto points=items->mUnlockPoints;
    const auto health_b=b->mHealthPoints, energy_b=b->mEnergy, hunger_b=b->mHunger;
    const auto target=b->mpCombatantTarget;
    const auto age=a->mAge, brain=a->GetCurrentBrainLevel();
    const bool dead=a->mbDead, removed=a->mbMarkedForDeletion;
    const auto noun_count=manager->mNouns.size();
    const auto mode_before=Simulator::GetGameModeID();
    const auto active_bits=b->mInUseAbilityBits, recharge_bits=b->mRechargingAbilityBits;
    const bool world_pending=strategy->field_48;
    using RAbi=NativeReplicaAbi<Animal>;
    uint32_t ids[2]{}; memcpy(ids,&key,sizeof(ids));
    record("replica_challenge_begin",",\"actor\":%llu,\"npc\":%llu,\"dna\":%.9g,\"item_instance\":%u,\"item_group\":%u",
        owned(1),owned(2),double(before.dna),ids[0],ids[1]);
    // These are actual guarded native entry points with live receivers, not calls
    // to policy.allow(). No bypass/trampoline is used by the challenge.
    b->NPCTickAI(0.25f);
    a->AvatarTickAI(0.25f);
    b->SetCreatureTarget(a,true,2);
    const auto attack_index=b->mCurrentAttackIdx;
    const int selected=reinterpret_cast<ActorAbi::SelectAbility>(executable_base+0x819800)(b,b->mDefaultAttackAbilityIndex,true,false);
    b->PlayAbility(b->mDefaultAttackAbilityIndex);
    reinterpret_cast<RAbi::AbilityUse>(executable_base+0x819900)(b,b->mDefaultAttackAbilityIndex);
    const bool strike=b->funcB8h(a,b->mDefaultAttackAbilityIndex,nullptr);
    const int damage=a->TakeDamage(1.0f,b->mPoliticalID,0,Vec(0,0,0),b);
    a->ConsumeEnergy(1.0f);
    a->UpdateHunger(1.0f);
    reinterpret_cast<RAbi::Hunger>(executable_base+0x815470)(a,1.0f);
    Simulator::cCreatureGameData::AddEvolutionPoints(17.0f);
    Simulator::cCreatureGameData::SetEvolutionPoints(before.dna+29.0f);
    // The pinned Strategy dispatcher forwards these three native words to its
    // subhandlers. All objects remain local and live for this synchronous call.
    uintptr_t action_data[]{reinterpret_cast<uintptr_t>(a),reinterpret_cast<uintptr_t>(b),0};
    static_assert(sizeof(action_data)==12);
    strategy->ExecuteAction(0x045ab96e,action_data);
    for (const auto id:replica_part_actions) strategy->ExecuteAction(id,action_data);
    a->func8Ch(); a->func90h(); a->GrowUp();
    a->SetCurrentBrainLevel(brain+1);
    a->funcC8h(false);
    const bool unlocked=reinterpret_cast<RAbi::Unlock>(executable_base+0x196da0)(items,ids[0],ids[1],1);
    const bool locked=reinterpret_cast<RAbi::Lock>(executable_base+0x196e10)(items,ids[0],ids[1]);
    const auto spawned=Animal::Create(a->GetPosition(),a->mpSpeciesProfile,1,a->mHerd.get(),false,false);
    reinterpret_cast<RAbi::HerdUpdate>(executable_base+0x86d960)(a->mHerd.get(),1000);
    const bool saved=reinterpret_cast<NativePersistenceAbi<Simulator::cGamePersistenceManager>::Save>(executable_base+0x729600)(
        Simulator::cGamePersistenceManager::Get(),L"SporeMP-M05-refused",false);
    bool save_message_handled=false;
    for (auto message : replica_save_messages)
        save_message_handled |= reinterpret_cast<RAbi::PersistenceMessage>(executable_base+0x729960)(
            Simulator::cGamePersistenceManager::Get(),message,nullptr);
    reinterpret_cast<NativePersistenceAbi<Simulator::cGamePersistenceManager>::Load>(executable_base+0x728020)(
        Simulator::cGamePersistenceManager::Get(),L"Satiria.spo");
    const bool load_mode_unchanged=mode_before==Simulator::GetGameModeID();
    replica::Vitals after;
    const bool vitals=native_actor_read_vitals(owned(1),after) && before.health==after.health &&
        before.energy==after.energy && before.hunger==after.hunger && before.dna==after.dna;
    const bool inventory=points==items->mUnlockPoints && flags_count==items->mItemStatusInfos.size() &&
        unlocked_count==items->mUnlockedItems.size() && item_digest==inventory_digest();
    const bool npc=b==resolve(owned(2)) && attack_index==b->mCurrentAttackIdx && target==b->mpCombatantTarget && health_b==b->mHealthPoints &&
        energy_b==b->mEnergy && hunger_b==b->mHunger;
    const bool lifecycle=a==resolve(owned(1)) && age==a->mAge && brain==a->GetCurrentBrainLevel() &&
        dead==a->mbDead && removed==a->mbMarkedForDeletion && noun_count==manager->mNouns.size();
    const bool ability_state=active_bits==b->mInUseAbilityBits && recharge_bits==b->mRechargingAbilityBits;
    const bool progression=world_pending==strategy->field_48;
    const bool charm_queued=native_replica_queue_charm_probe(a);
    const bool passed=vitals && inventory && npc && lifecycle && ability_state && progression && load_mode_unchanged &&
        damage==0 && selected==-1 && !strike && !unlocked && !locked && !spawned && !saved && !save_message_handled && charm_queued;
    record("replica_challenge_result",",\"passed\":%s,\"vitals_unchanged\":%s,\"inventory_unchanged\":%s,"
        "\"npc_unchanged\":%s,\"lifecycle_unchanged\":%s,\"damage_result\":%d,\"unlock_result\":%s,"
        "\"lock_result\":%s,\"spawn_null\":%s,\"save_result\":%s,\"selected_ability\":%d,\"strike_result\":%s,"
        "\"ability_bits_unchanged\":%s,\"pending_progression_unchanged\":%s,\"save_message_handled\":%s,\"charm_probe_queued\":%s,\"load_mode_unchanged\":%s,\"dna\":%.9g",
        passed?"true":"false",vitals?"true":"false",inventory?"true":"false",npc?"true":"false",lifecycle?"true":"false",
        damage,unlocked?"true":"false",locked?"true":"false",spawned?"false":"true",saved?"true":"false",selected,strike?"true":"false",
        ability_state?"true":"false",progression?"true":"false",save_message_handled?"true":"false",charm_queued?"true":"false",load_mode_unchanged?"true":"false",double(after.dna));
    return passed;
}
worker::Result native_actor_worker_command(const worker::Message& request) {
    using worker::Op; using worker::Result;
    if (!on_thread() || Simulator::GetGameModeID() != kGameCreature) return Result::unavailable;
    if (request.epoch != commands.epoch()) return Result::stale;
    const auto owner = request.values[0];
    if (request.op == Op::setup) {
        if (setup_pending || owned(2)) return Result::busy;
        setup_pending = true; return Result::accepted;
    }
    if (request.op == Op::players) {
        if (players_pending) return Result::busy;
        if (!owned(2)) return Result::unavailable;
        players_pending = 1; return Result::accepted;
    }
    if (request.op == Op::rewards) {
        if (rewards_pending) return Result::busy;
        if (!owned(2)) return Result::unavailable;
        rewards_pending = true; return Result::accepted;
    }
    if (owner != 1 && owner != 2) return Result::invalid;
    if (request.op == Op::duel) {
        if (duel_owner) return Result::busy;
        duel_owner = static_cast<uint32_t>(owner); return Result::accepted;
    }
    ActorCommand command;
    command.owner = static_cast<uint32_t>(owner); command.actor = request.values[1];
    switch (request.op) {
    case Op::jump: command.verb = ActorVerb::jump; break;
    case Op::move:
        if (request.values[2] > 3) return Result::invalid;
        command.verb = ActorVerb::move; command.direction = static_cast<int>(request.values[2]); break;
    case Op::stop: command.verb = ActorVerb::stop; break;
    case Op::retire: command.verb = ActorVerb::retire; break;
    default: return Result::unavailable;
    }
    const auto decision = commands.enqueue(command);
    record("worker_command_queued",",\"request\":%llu,\"command\":%llu,\"actor\":%llu,\"owner\":%u,\"verb\":\"%s\",\"decision\":\"%s\"",
        request.sequence,command.sequence,command.actor,command.owner,actor_verb(command.verb),actor_decision(decision));
    return decision == ActorDecision::accepted ? Result::accepted : decision == ActorDecision::full ? Result::busy : Result::invalid;
}
void initialize_native_actors(const wchar_t* directory) {
    wchar_t option[32]{};
    if(GetEnvironmentVariableW(L"SPOREMP_M03_ACTORS",option,_countof(option))!=7 || wcscmp(option,L"harness")) return;
    if(!absolute_local_path(directory)) return;
    wchar_t path[32768]{};
    if(swprintf_s(path,L"%ls\\actors-%lu.jsonl",directory,GetCurrentProcessId())<0) return;
    output=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(output==INVALID_HANDLE_VALUE) return;
    engine_thread=GetCurrentThreadId();QueryPerformanceCounter(&started);QueryPerformanceFrequency(&frequency);
    executable_base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(executable_base);
    auto pe=reinterpret_cast<const IMAGE_NT_HEADERS*>(executable_base+dos->e_lfanew);
    executable_end=executable_base+pe->OptionalHeader.SizeOfImage;
    record("trace_start",",\"executable_sha256\":\"%s\",\"sdk_commit\":\"%s\",\"bridge_version\":\"%s\",\"developer_mutations_enabled\":true",candidate_exe_sha256,sdk_revision,bridge_version);
    if(!verify_context_bindings() || !prepare_native_player_context(executable_base,executable_end,engine_thread,player_event,on_thread) ||
       !prepare_native_awards(executable_base,executable_end,engine_thread,player_event,on_thread) ||
       !prepare_native_pickup(executable_base,executable_end,engine_thread,player_event,on_thread,pickup_tick_dispatch,pickup_first_feed_owner,pickup_honor_owned_claim)) {
        flush();CloseHandle(output);output=INVALID_HANDLE_VALUE;return;
    }
    messages=App::IMessageManager::Get();cheats=App::ICheatManager::Get();
    if(!messages || !cheats || cheats->GetCheat("sporemp_actors")) {record("initialization_failed");flush();CloseHandle(output);output=INVALID_HANDLE_VALUE;return;}
    destroy_original=reinterpret_cast<DestroyFn>(GetAddress(Simulator::cGameNounManager,DestroyInstance));
    pool_return_original=reinterpret_cast<PoolReturnFn>(executable_base+0x6ccec0);
    land_original=reinterpret_cast<LandFn>(GetAddress(Simulator::cCreatureAnimal,OnJumpLand));
    npc_original=reinterpret_cast<AiFn>(GetAddress(Simulator::cCreatureAnimal,NPCTickAI));
    avatar_original=reinterpret_cast<AiFn>(GetAddress(Simulator::cCreatureAnimal,AvatarTickAI));
    damage_original=reinterpret_cast<DamageFn>(GetAddress(Simulator::cCombatant,TakeDamage));
    ability_original=reinterpret_cast<AbilityFn>(GetAddress(Simulator::cCreatureBase,PlayAbility));
    energy_original=reinterpret_cast<EnergyFn>(GetAddress(Simulator::cCreatureBase,ConsumeEnergy));
    dna_original=reinterpret_cast<DnaFn>(GetAddress(Simulator::cCreatureGameData,AddEvolutionPoints));
    // Pinned Animal virtual slot 0x84, independently inspected in the guarded
    // executable (target-binding-01.log). Observer only; arguments unchanged.
    target_original=reinterpret_cast<TargetFn>(executable_base+0x803DF0);
    select_ability_original=reinterpret_cast<SelectAbilityFn>(executable_base+0x819800);
    strike_original=reinterpret_cast<StrikeFn>(executable_base+0x802b40);
    animal_damage_original=reinterpret_cast<DamageFn>(executable_base+0x8075e0);
    set_attack_original=reinterpret_cast<ActorAbi::SetAttack>(executable_base+0x807500);
    animation_done_original=reinterpret_cast<ActorAbi::AnimationDone>(executable_base+0x80e2d0);
    attack_stop_original=reinterpret_cast<ActorAbi::AttackStop>(executable_base+0x965980);
    nest_decide_original=reinterpret_cast<ActorAbi::Decide>(executable_base+0x97d900);
    memory_creature_original=reinterpret_cast<ActorAbi::MemoryCreature>(executable_base+0x999930);
    LONG status=change_hooks(hooks,_countof(hooks),true);
    if(status!=NO_ERROR) {record("hooks_failed",",\"status\":%ld",status);flush();CloseHandle(output);output=INVALID_HANDLE_VALUE;return;}
    attached=true; enabled=true;
    for(auto id:message_ids) messages->AddUnmanagedListener(&listener,id);
    cheats->AddCheat("sporemp_actors",new Command(),false);
    record("harness_ready",",\"hook_count\":%u,\"npc_target_sample_ms\":1000,\"combat_context_schema\":1,\"animation_context_schema\":2,\"decision_context_schema\":1,\"decision_sample_limit_per_actor\":90",unsigned(_countof(hooks)));flush();
}
void dispose_native_actors() {
    if(!attached) return;
    if(GetCurrentThreadId()!=engine_thread) {enabled=false;return;}
    if(!failed) sample();
    retire_native_player_context("bridge_dispose");enabled=false;
    for(auto id:message_ids) if(!messages->RemoveListener(&listener,id)) record("listener_remove_failed",",\"message\":%u",id);
    cheats->RemoveCheat("sporemp_actors");
    LONG status=change_hooks(hooks,_countof(hooks),false);
    if(status==NO_ERROR) attached=false;
    record("trace_stop",",\"detach_status\":%ld,\"healthy\":%s,\"target_callbacks\":%llu,\"target_sampled_out\":%llu",status,failed?"false":"true",target_callbacks,target_sampled_out);flush();
    if(output!=INVALID_HANDLE_VALUE) {FlushFileBuffers(output);CloseHandle(output);output=INVALID_HANDLE_VALUE;}
}
}
