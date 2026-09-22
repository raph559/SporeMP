#pragma once
#include "../network/peer.h"
#include <filesystem>

namespace sporemp {
using NativeContentSend=bool(*)(network::Packet);
// Explicit disposable-worker experiment: inactive unless a bounded local
// m08-content-allowlist.txt exists in the already-qualified diagnostics folder.
// Only a local operator can replace that allowlist; it is rechecked immediately
// before each import. Peers cannot add entries or choose native paths.
void initialize_native_content_network(const network::PeerConfig&,const std::filesystem::path&,NativeContentSend);
void receive_native_content_network(const network::Packet&);
void update_native_content_network();
void fence_native_content_network();
void dispose_native_content_network();
}
