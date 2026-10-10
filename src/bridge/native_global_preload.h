#pragma once
#include "../network/peer.h"

namespace sporemp {
bool initialize_native_global_preload(const network::PeerConfig& config, std::string& error);
bool update_native_global_preload(std::string& error);
bool native_global_preload_ready();
void fence_native_global_preload();
void dispose_native_global_preload();
} // namespace sporemp
