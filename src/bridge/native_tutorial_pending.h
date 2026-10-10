#pragma once
#include <cstdint>

namespace sporemp {
// One measured presentation request, owned by a live native player. It holds
// values only. A pending request is neither shown nor acknowledged by queuing.
struct NativeTutorialRequest {
    uint64_t epoch = 0, actor = 0;
    uint32_t player = UINT32_MAX, token = 0, caption = 0x061d8045;
    uint32_t owner = 2;
};
enum class TutorialOffer {
    invalid,
    queued,
    duplicate,
    conflict
};

class NativeTutorialPending {
    NativeTutorialRequest value_{};

public:
    bool pending() const {
        return value_.epoch != 0;
    }

    const NativeTutorialRequest& request() const {
        return value_;
    }

    TutorialOffer offer(NativeTutorialRequest value) {
        if (!value.epoch || !value.actor || !value.player || value.player == UINT32_MAX ||
            (value.owner != 1 && value.owner != 2) ||
            !((value.token == 0xea7666af &&
               (value.caption == 0x04599f0e || value.caption == 0x061d8186 || value.caption == 0x061d8045)) ||
              (value.token == 0xb721f66f && !value.caption))) {
            return TutorialOffer::invalid;
        }
        if (pending()) {
            return value.epoch == value_.epoch && value.actor == value_.actor && value.owner == value_.owner &&
                           value.player == value_.player && value.token == value_.token &&
                           value.caption == value_.caption
                       ? TutorialOffer::duplicate
                       : TutorialOffer::conflict;
        }
        value_ = value;
        return TutorialOffer::queued;
    }

    // Scene retirement only. This is cancellation, never successful delivery.
    void retire() {
        value_ = {};
    }
};
} // namespace sporemp
