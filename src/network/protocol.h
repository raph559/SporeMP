#pragma once
#include "world_identity.h"
#include "content_wire.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sporemp::network {
constexpr uint32_t protocol_version = 1, schema_version = 7;
constexpr size_t packet_bytes = 600, max_entities = 2048, queue_capacity = 8192;
struct Identity { Digest build{}, executable{}, content{}, fixture{}; WorldIdentity world{}; bool operator==(const Identity&) const noexcept; };
enum class Kind : uint32_t { hello=1, welcome, reject, scene_begin, entity, scene_end, baseline_ack, motion, despawn, action, action_result, ping, content_request, content_response };
enum class Role : uint32_t { player=1, authority=2 };
enum class Error : uint32_t { none, malformed, incompatible, authentication, capacity, stale, ownership, not_ready, authority_lost, timeout, queue_full, world_mismatch };
enum class Verb : uint32_t { move=0, jump=1, attack=2, stop=3, approach=5, engage=6, pickup=7 };
struct Entity {
    uint64_t id=0, generation=0, owner=0, tick=0;
    uint32_t native_id=0, herd_native_id=0, species_instance=0, species_type=0, species_group=0, archetype=0;
    float x=0,y=0,z=0,qx=0,qy=0,qz=0,qw=1,health=0,energy=0,hunger=0,dna=0,vx=0,vy=0,vz=0;
    uint32_t life_state=0; // 0 alive, 1 native mbDead; never a request to execute death.
    uint32_t age=1, alpha=0, combatant_state=0; // Pinned native scalar context; packed bytes on the wire.
    float scale=1; // Observed native scale; never recomputed with local random growth.
    uint32_t fed_on=0, pickup_owner=0; // Native first-feed flag and observed original grant beneficiary (0 unknown).
    float food=0; // Remaining original corpse nutrition; first-feed bonus is a separate one-time resource.
};
// Value object only: serialize with encode/decode, never sizeof/memcpy Packet.
struct Packet {
    Kind kind=Kind::ping;
    uint64_t sequence=0, session=0, scene=0, baseline=0, player=0;
    Role role=Role::player;
    Error error=Error::none;
    Identity identity{};
    Digest credential{};
    Entity entity{};
    uint64_t target=0;
    uint64_t target_generation=0;
    uint64_t request=0; // Action/result correlation; independent from each stream sequence.
    Verb verb=Verb::stop;
    int32_t direction=0;
    uint32_t count=0;
    uint32_t world_index=0; // Only reject/world_mismatch; fixed allowlist index, never a peer path.
    ContentWire content{}; // Schema 6 content kinds use their own bounded payload after the common 72-byte header.
};
using Wire = std::array<uint8_t,packet_bytes>;
Wire encode(const Packet&);
bool decode(const uint8_t*,size_t,Packet&,std::string&);
bool valid_entity(const Entity&) noexcept;
bool targeted(Verb) noexcept;
bool valid_action(const Packet&) noexcept;
std::string hex(const Digest&);
bool parse_hex(const std::string&,Digest&) noexcept;
const char* error_name(Error) noexcept;
std::string error_detail(const Packet&);
}
