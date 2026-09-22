#pragma once
#include "../worker/protocol.h"
namespace sporemp {
void initialize_native_network(const wchar_t* directory);
void dispose_native_network();
void native_network_scene_exit();
struct NativeNetworkProjection {
    worker::ProjectionState state = worker::ProjectionState::none;
    uint64_t baseline = 0;
};
// Engine-thread snapshot for periodic authenticated worker status only.
// Non-network/authority/wrong-thread callers receive none and baseline zero;
// a player receives connected only after its native baseline was applied.
NativeNetworkProjection native_network_projection();
// Developer private-IPC probe enters the exact client network intention path.
worker::Result native_network_command(const worker::Message& request);
}
