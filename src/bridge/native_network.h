#pragma once
#include "../worker/protocol.h"
namespace sporemp {
void initialize_native_network(const wchar_t* directory);
void dispose_native_network();
void native_network_scene_exit();
// Developer private-IPC probe enters the exact client network intention path.
worker::Result native_network_command(const worker::Message& request);
}
