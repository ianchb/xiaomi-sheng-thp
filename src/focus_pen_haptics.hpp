// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>
#include <string_view>

class FocusPenHaptics {
public:
    virtual ~FocusPenHaptics() = default;

    virtual void setDeviceAddress(std::string_view address) noexcept = 0;
    virtual void scheduleDoublePress() noexcept = 0;
    virtual void triggerSlide() noexcept = 0;
    virtual void reset() noexcept = 0;
};

std::unique_ptr<FocusPenHaptics> makeFocusPenHaptics();
