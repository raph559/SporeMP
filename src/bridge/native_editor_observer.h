#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>

namespace sporemp {
using NativeEditorObserverEvent = void(*)(const char*, const char*);
// Passive, pinned original-editor messages only. Caller qualifies executable,
// loader/content identity and supplies the SDK lifecycle/app-update thread.
// A request observation is not editor landing; an accepted result is not a
// durable save, authority commit, dependency validation or readiness approval.
bool initialize_native_editor_observer(uintptr_t base, size_t image_size,
    DWORD engine_thread, NativeEditorObserverEvent event_sink);
void dispose_native_editor_observer();
}
