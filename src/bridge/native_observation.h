#pragma once
namespace sporemp {
// Only from the already guarded SDK post-init / dispose callbacks.
void initialize_native_observation(const wchar_t* directory);
void dispose_native_observation();
}
