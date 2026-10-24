#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>

namespace emuorbit::bridge {

/** Optional signed Libretro axes: independent left/right, x/y, port zero.
 * Input producers may update an axis while the owner runs a frame. Pausing,
 * disconnect and teardown must clear all axes to avoid stuck controls. */
class AnalogInput {
public:
    bool set(unsigned stick, unsigned axis, int value) {
        if (stick >= 2 || axis >= 2) return false;
        axes_[stick * 2 + axis].store(
                static_cast<int16_t>(std::clamp(value, -32768, 32767)), std::memory_order_relaxed);
        return true;
    }
    int16_t get(unsigned stick, unsigned axis) const {
        if (stick >= 2 || axis >= 2) return 0;
        return axes_[stick * 2 + axis].load(std::memory_order_relaxed);
    }
    void clear() {
        for (auto& axis : axes_) axis.store(0, std::memory_order_relaxed);
    }
private:
    std::array<std::atomic<int16_t>, 4> axes_{};
};

}  // namespace emuorbit::bridge
