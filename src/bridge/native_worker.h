#pragma once
#include <cstdint>

namespace sporemp {
void initialize_native_worker();
void dispose_native_worker();
// Actual SDK AppUpdate counter for authenticated engine-thread transfer work.
uint64_t native_worker_app_updates();
} // namespace sporemp
