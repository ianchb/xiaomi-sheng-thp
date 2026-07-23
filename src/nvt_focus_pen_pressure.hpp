// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>

namespace nvt {

class FocusPenPressureQueue {
public:
    static constexpr uint8_t kReportId = 5;
    static constexpr int kStandardMaximumPressure = 8191;
    static constexpr int kProMaximumPressure = 16384;
    static constexpr size_t kMaximumQueued = 10;

    static std::optional<int> decodeReport(std::span<const uint8_t> report) {
        if (report.size() < 3 || report[0] != kReportId)
            return std::nullopt;
        return report[1] | static_cast<int>(report[2]) << 8;
    }

    bool pushReport(std::span<const uint8_t> report) {
        const std::optional<int> pressure = decodeReport(report);
        if (!pressure)
            return false;
        queue_.push_back(std::min(maximum_pressure_, *pressure));
        if (queue_.size() > kMaximumQueued)
            queue_.pop_front();
        return true;
    }

    int consume() {
        if (!queue_.empty()) {
            last_pressure_ = queue_.front();
            queue_.pop_front();
        }
        return last_pressure_;
    }

    size_t size() const {
        return queue_.size();
    }

    void setMaximumPressure(int maximum_pressure) {
        maximum_pressure_ = maximum_pressure;
        last_pressure_ = std::min(last_pressure_, maximum_pressure_);
        for (int &pressure : queue_)
            pressure = std::min(pressure, maximum_pressure_);
    }

    void reset() {
        queue_.clear();
        last_pressure_ = 0;
    }

private:
    std::deque<int> queue_;
    int last_pressure_ = 0;
    int maximum_pressure_ = kStandardMaximumPressure;
};

}  // namespace nvt
