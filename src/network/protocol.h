#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sporemp::network {
constexpr uint32_t protocol_version = 1, schema_version = 1;
constexpr size_t packet_bytes = 384, max_entities = 2048, queue_capacity = 8192;
using Digest = std::array<uint8_t, 32>;
struct Identity { Digest build{}, executable{}, content{}, fixture{}; bool operator==(const Identity&) const noexcept; };
enum class Kind : uint32_t { hello=1, welcome, reject, scene_begin, entity, scene_end, baseline_ack, motion, despawn, action, action_result, ping };
enum class Role : uint32_t { player=1, authority=2 };
enum class Error : uint32_t { none, malformed, incompatible, authentication, capacity, stale, ownership, not_ready, authority_lost, timeout, queue_full };
enum class Verb : uint32_t { move=0, jump=1, attack=2, stop=3, approach=5, engage=6 };
struct Entity {
    uint64_t id=0, generation=0, owner=0, tick=0;
    uint32_t native_id=0, herd_native_id=0, species_instance=0, species_type=0, species_group=0, archetype=0;
    float x=0,y=0,z=0,qx=0,qy=0,qz=0,qw=1,health=0,energy=0,hunger=0,dna=0,vx=0,vy=0,vz=0;
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
    uint64_t request=0; // Action/result correlation; independent from each stream sequence.
    Verb verb=Verb::stop;
    int32_t direction=0;
    uint32_t count=0;
};
using Wire = std::array<uint8_t,packet_bytes>;
Wire encode(const Packet&);
bool decode(const uint8_t*,size_t,Packet&,std::string&);
bool valid_entity(const Entity&) noexcept;
std::string hex(const Digest&);
bool parse_hex(const std::string&,Digest&) noexcept;
const char* error_name(Error) noexcept;
}
