#pragma once

namespace sporemp {
// Only from the already guarded SDK post-init / dispose callbacks.
void initialize_native_observation(const wchar_t* directory);
void dispose_native_observation();
// Engine-thread, full pre-detour SetAvatar pin plus current original/trampoline check.
bool native_avatar_assignment_ready(const char** failure = nullptr);
} // namespace sporemp
