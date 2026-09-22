#pragma once
#include "content_registry.h"

namespace sporemp::network {
constexpr size_t content_wire_bytes=512,content_chunk_bytes=420,max_observation_bytes=64*1024;
enum class ContentOp : uint32_t {
    begin=1,offer,chunk,seal,fetch,observation_begin,observation_chunk,observation_end,commit,cancel,current,
    validate_event,load_event,published_event,cancelled_event,dependencies,dependency_failure,dependency_event
};
struct ContentFrame {
    ContentOp op=ContentOp::current;
    ContentFailure failure=ContentFailure::none;
    uint64_t request=0,transaction=0;
    uint32_t offset=0,total=0,world_index=0;
    Digest identity{};
    ContentKey missing{};
    std::vector<uint8_t> data;
};
using ContentWire=std::array<uint8_t,content_wire_bytes>;
bool encode_content(const ContentFrame&,ContentWire&);
bool decode_content(const ContentWire&,ContentFrame&);
bool encode_observation(const ContentObservation&,std::vector<uint8_t>&);
bool decode_observation(const std::vector<uint8_t>&,ContentObservation&);
constexpr size_t max_dependency_bytes=304+512*12;
struct ContentDependencies {
    Digest png{},native_properties{},installed_profile{};
    WorldIdentity world{};
    uint32_t png_bytes=0;
    std::vector<ContentKey> parts;
};
bool encode_dependencies(const ContentManifest&,std::vector<uint8_t>&);
bool decode_dependencies(const std::vector<uint8_t>&,ContentDependencies&);
inline bool content_request_op(ContentOp op) {
    return (uint32_t(op)>=1&&uint32_t(op)<=uint32_t(ContentOp::current))||op==ContentOp::dependencies||op==ContentOp::dependency_failure;
}
}
