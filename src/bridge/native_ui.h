#pragma once
#include "../worker/protocol.h"

namespace sporemp {
// Read the original's interface in this process and change nothing: the visible windows that carry text
// or act as buttons, or the window under a place with its ancestors. Each becomes one `native_ui_window`
// trace row (control id, command id, real area in window pixels, caption), closed by `native_ui_snapshot`.
//
// Source: SDK cbf9206 UTFWin/IWindowManager.h and UTFWin/IWindow.h. IWindowManager::Get, GetMainWindow
// (virtual +04h) and GetWindowAtPosition (+44h); IWindow GetParent (+10h), GetControlID (+1Ch),
// GetCommandID (+20h), GetFlags (+28h), GetRealArea (+38h), GetCaption (+3Ch), ToGlobalCoordinates
// (+C0h) and the child list (+CCh, +D0h). All are const readers on objects reached from the manager. Engine thread
// only, from the worker dispatch. The walk is bounded in depth, visits and reported rows.
worker::Result native_ui_read(const worker::Message& request);
} // namespace sporemp
