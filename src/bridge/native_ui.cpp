#include <Spore/ModAPI.h>
#include <Spore/UTFWin/IWindow.h>
#include <Spore/UTFWin/IWindowManager.h>
#include <Spore/UTFWin/WindowIterators.h>
#include "native_ui.h"
#include "native_actors.h"
#include "../worker/ui_text.h"
#include <cstdio>
#include <string>

namespace sporemp {
namespace {
constexpr size_t reported_limit = 200, visited_limit = 8000, text_limit = 160;
constexpr int depth_limit = 28;
// A window this small with a control id is reported even without text: the original's buttons are icons.
constexpr float button_limit = 220.0f;

struct Walk {
    size_t visited = 0, reported = 0;
    bool truncated = false;
};

void report(UTFWin::IWindow* window, int depth, uint32_t parent) {
    const auto& area = window->GetRealArea();
    const auto text = worker::ui_text_json(window->GetCaption(), text_limit);
    // The area is relative to the parent (trial242: a tutorial's button reported [343,193,374,224] while drawn
    // at 811,315). The box is the same rectangle in window pixels, from the window's own origin (+C0h).
    const auto origin = window->ToGlobalCoordinates(Math::Point(0.0f, 0.0f));
    const float width = area.x2 - area.x1, height = area.y2 - area.y1;
    char head[360]{};
    std::snprintf(head, sizeof(head),
                  ",\"id\":%u,\"command\":%u,\"parent\":%u,\"depth\":%d,\"flags\":%u,\"area\":[%.0f,%.0f,%.0f,%.0f],"
                  "\"box\":[%.0f,%.0f,%.0f,%.0f],\"text\":\"",
                  window->GetControlID(), window->GetCommandID(), parent, depth,
                  static_cast<unsigned>(window->GetFlags()), double(area.x1), double(area.y1), double(area.x2),
                  double(area.y2), double(origin.x), double(origin.y), double(origin.x + width),
                  double(origin.y + height));
    const std::string fields = std::string(head) + text + "\"";
    native_actor_worker_event("native_ui_window", fields.c_str());
}

void walk(UTFWin::IWindow* window, int depth, uint32_t parent, Walk& state) {
    if (!window) {
        return;
    }
    if (depth > depth_limit || state.visited >= visited_limit) {
        state.truncated = true;
        return;
    }
    ++state.visited;
    // A hidden window hides everything under it.
    if (!(window->GetFlags() & UTFWin::kWinFlagVisible)) {
        return;
    }
    const auto id = window->GetControlID();
    const auto caption = window->GetCaption();
    const auto& area = window->GetRealArea();
    const float width = area.x2 - area.x1, height = area.y2 - area.y1;
    const bool texted = caption && caption[0];
    const bool button = id && width > 0 && height > 0 && width <= button_limit && height <= button_limit;
    if (texted || button) {
        if (state.reported < reported_limit) {
            report(window, depth, parent);
            ++state.reported;
        } else {
            state.truncated = true;
        }
    }
    for (UTFWin::IWindow* child : window->children()) {
        walk(child, depth + 1, id ? id : parent, state);
    }
}
} // namespace

worker::Result native_ui_read(const worker::Message& request) {
    if (!worker::valid_ui(request)) {
        return worker::Result::invalid;
    }
    auto manager = UTFWin::IWindowManager::Get();
    auto main = manager ? manager->GetMainWindow() : nullptr;
    if (!main) {
        return worker::Result::unavailable;
    }
    Walk state;
    const int x = static_cast<int>(request.values[1]), y = static_cast<int>(request.values[2]);
    if (request.values[0] == 1) {
        walk(main, 0, 0, state);
    } else {
        const Math::Point place(static_cast<float>(x), static_cast<float>(y));
        auto window = manager->GetWindowAtPosition(place);
        for (int depth = 0; window && depth < 16; ++depth, window = window->GetParent()) {
            report(window, depth, 0);
            ++state.reported;
        }
    }
    char fields[200]{};
    std::snprintf(fields, sizeof(fields),
                  ",\"mode\":%u,\"place\":[%d,%d],\"windows\":%zu,\"visited\":%zu,\"truncated\":%s,\"evidence_label\":"
                  "\"UI_READBACK\"",
                  static_cast<unsigned>(request.values[0]), x, y, state.reported, state.visited,
                  state.truncated ? "true" : "false");
    native_actor_worker_event("native_ui_snapshot", fields);
    return worker::Result::accepted;
}
} // namespace sporemp
