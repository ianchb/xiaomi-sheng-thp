// SPDX-License-Identifier: Apache-2.0

#include "focus_pen_haptics.hpp"

namespace {

class NoopFocusPenHaptics final : public FocusPenHaptics {
public:
    void setDeviceAddress(std::string_view) noexcept override {}
    void scheduleDoublePress() noexcept override {}
    void triggerSlide() noexcept override {}
    void reset() noexcept override {}
};

}  // namespace

std::unique_ptr<FocusPenHaptics> makeFocusPenHaptics() {
    return std::make_unique<NoopFocusPenHaptics>();
}
