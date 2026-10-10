#pragma once
#include <array>
#include <cstdint>

namespace sporemp {
// Correlates an observed original relationship transition with one delayed
// objective evaluation. It never computes a score, relationship or DNA amount.
struct NativeSocialObjectiveReceipt {
    uint64_t epoch = 0, actor = 0, result = 0, expires = 0;
    uint32_t native_actor = 0, before_count = 0, after_count = 0, before_relation = 0, after_relation = 0;
    std::array<uint32_t, 3> species{};
    bool pending = false;

    bool claim(uint64_t current_epoch, uint64_t current_actor, uint32_t current_native,
               const std::array<uint32_t, 3>& key, uint32_t cached_count, uint32_t cached_relation,
               uint32_t current_count, uint32_t current_relation, uint32_t required, uint64_t now) {
        if (!pending || !epoch || !actor || !result || now >= expires || current_epoch != epoch ||
            current_actor != actor || current_native != native_actor || key != species || before_count == UINT32_MAX ||
            after_count != before_count + 1 || before_relation == 6 || after_relation != 6 ||
            cached_count != before_count || cached_relation != before_relation || current_count != after_count ||
            current_relation != after_relation || required != after_count) {
            return false;
        }
        pending = false;
        return true;
    }
};
} // namespace sporemp
