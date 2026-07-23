// SPDX-License-Identifier: Apache-2.0

#include "focus_pen_haptics.hpp"

#include <systemd/sd-bus.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cctype>
#include <cstdlib>
#include <cstdint>
#include <deque>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace {

constexpr std::string_view kCommandUuid =
    "0000fe11-aa6c-462a-964a-7f2ed5b3e512";
constexpr std::array<std::uint8_t, 4> kDoublePressHaptic{
    0x5e, 0x02, 0x03, 0x80};
constexpr std::array<std::uint8_t, 4> kSlideHaptic{
    0x5e, 0x02, 0x04, 0x80};
constexpr auto kDoublePressDelay = std::chrono::milliseconds(150);

enum class HapticKind {
    DoublePress,
    Slide,
};

struct HapticRequest {
    HapticKind kind;
    std::chrono::steady_clock::time_point due;
    std::string address;
};

class BluezGattWriter {
public:
    ~BluezGattWriter() {
        resetBus();
    }

    void setDeviceAddress(std::string_view address) {
        if (address.empty()) {
            device_address_.clear();
            command_path_.clear();
            return;
        }
        std::string normalized;
        normalized.reserve(address.size());
        for (const char character : address) {
            normalized.push_back(static_cast<char>(
                std::toupper(static_cast<unsigned char>(character))));
        }
        if (normalized == device_address_)
            return;
        device_address_ = std::move(normalized);
        command_path_.clear();
    }

    bool trigger(std::span<const std::uint8_t> command) {
        if (writeCommand(command))
            return true;
        command_path_.clear();
        return findCommandPath() && writeCommand(command);
    }

private:
    sd_bus *bus_ = nullptr;
    std::string device_address_;
    std::string command_path_;

    bool findCommandPath() {
        if (device_address_.empty())
            return false;
        if (!bus_ && sd_bus_open_system(&bus_) < 0) {
            bus_ = nullptr;
            return false;
        }
        sd_bus_error error = SD_BUS_ERROR_NULL;
        sd_bus_message *reply = nullptr;
        const int result = sd_bus_call_method(
            bus_, "org.bluez", "/", "org.freedesktop.DBus.ObjectManager",
            "GetManagedObjects", &error, &reply, "");
        if (result < 0) {
            sd_bus_error_free(&error);
            sd_bus_message_unref(reply);
            resetBus();
            return false;
        }
        sd_bus_message_enter_container(reply, 'a', "{oa{sa{sv}}}");
        while (sd_bus_message_enter_container(
                   reply, 'e', "oa{sa{sv}}") > 0) {
            const char *path = nullptr;
            sd_bus_message_read_basic(reply, 'o', &path);
            bool uuid_match = false;
            sd_bus_message_enter_container(reply, 'a', "{sa{sv}}");
            while (sd_bus_message_enter_container(
                       reply, 'e', "sa{sv}") > 0) {
                const char *interface = nullptr;
                sd_bus_message_read_basic(reply, 's', &interface);
                const bool characteristic = interface &&
                    std::string_view(interface) ==
                        "org.bluez.GattCharacteristic1";
                sd_bus_message_enter_container(reply, 'a', "{sv}");
                while (sd_bus_message_enter_container(
                           reply, 'e', "sv") > 0) {
                    const char *property = nullptr;
                    sd_bus_message_read_basic(reply, 's', &property);
                    if (characteristic && property &&
                        std::string_view(property) == "UUID") {
                        const char *uuid = nullptr;
                        sd_bus_message_enter_container(reply, 'v', "s");
                        sd_bus_message_read_basic(reply, 's', &uuid);
                        sd_bus_message_exit_container(reply);
                        uuid_match = uuid &&
                            std::string_view(uuid) == kCommandUuid;
                    } else {
                        sd_bus_message_skip(reply, "v");
                    }
                    sd_bus_message_exit_container(reply);
                }
                sd_bus_message_exit_container(reply);
                sd_bus_message_exit_container(reply);
            }
            sd_bus_message_exit_container(reply);
            sd_bus_message_exit_container(reply);
            if (uuid_match && path && belongsToDevice(path)) {
                command_path_ = path;
                break;
            }
        }
        sd_bus_message_exit_container(reply);
        sd_bus_message_unref(reply);
        sd_bus_error_free(&error);
        return !command_path_.empty();
    }

    bool belongsToDevice(std::string_view characteristic_path) {
        const std::size_t service = characteristic_path.find("/service");
        if (service == std::string_view::npos)
            return false;
        const std::string device_path(characteristic_path.substr(0, service));
        sd_bus_error error = SD_BUS_ERROR_NULL;
        char *address = nullptr;
        const int result = sd_bus_get_property_string(
            bus_, "org.bluez", device_path.c_str(), "org.bluez.Device1",
            "Address", &error, &address);
        bool matches = false;
        if (result >= 0 && address) {
            std::string normalized(address);
            std::transform(
                normalized.begin(), normalized.end(), normalized.begin(),
                [](unsigned char character) {
                    return static_cast<char>(std::toupper(character));
                });
            matches = normalized == device_address_;
        }
        std::free(address);
        sd_bus_error_free(&error);
        return matches;
    }

    bool writeCommand(std::span<const std::uint8_t> command) {
        if (command_path_.empty() && !findCommandPath())
            return false;
        sd_bus_error error = SD_BUS_ERROR_NULL;
        sd_bus_message *message = nullptr;
        sd_bus_message *reply = nullptr;
        int result = sd_bus_message_new_method_call(
            bus_, &message, "org.bluez", command_path_.c_str(),
            "org.bluez.GattCharacteristic1", "WriteValue");
        if (result >= 0)
            result = sd_bus_message_append_array(
                message, 'y', command.data(), command.size());
        if (result >= 0)
            result = sd_bus_message_open_container(message, 'a', "{sv}");
        if (result >= 0)
            result = sd_bus_message_close_container(message);
        if (result >= 0)
            result = sd_bus_call(bus_, message, 500000, &error, &reply);
        sd_bus_message_unref(reply);
        sd_bus_message_unref(message);
        sd_bus_error_free(&error);
        if (result >= 0)
            return true;
        resetBus();
        return false;
    }

    void resetBus() {
        bus_ = sd_bus_unref(bus_);
    }
};

class BluezPenHaptics final : public FocusPenHaptics {
public:
    BluezPenHaptics()
        : worker_([this](std::stop_token stop) { run(stop); }) {}

    ~BluezPenHaptics() override {
        worker_.request_stop();
        condition_.notify_all();
    }

    void setDeviceAddress(std::string_view address) noexcept override {
        try {
            std::lock_guard lock(mutex_);
            if (device_address_ == address)
                return;
            device_address_ = address;
            requests_.clear();
            condition_.notify_all();
        } catch (const std::exception &error) {
            std::cerr << "Focus Pen Pro haptic address update failed: "
                      << error.what() << '\n';
        }
    }

    void scheduleDoublePress() noexcept override {
        enqueue(HapticKind::DoublePress, kDoublePressDelay);
    }

    void triggerSlide() noexcept override {
        enqueue(HapticKind::Slide, std::chrono::milliseconds(0));
    }

    void reset() noexcept override {
        try {
            std::lock_guard lock(mutex_);
            requests_.clear();
            condition_.notify_all();
        } catch (const std::exception &error) {
            std::cerr << "Focus Pen Pro haptic reset failed: "
                      << error.what() << '\n';
        }
    }

private:
    std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<HapticRequest> requests_;
    std::string device_address_;
    BluezGattWriter writer_;
    std::jthread worker_;

    void enqueue(HapticKind kind, std::chrono::milliseconds delay) noexcept {
        try {
            std::lock_guard lock(mutex_);
            requests_.push_back(HapticRequest{
                kind, std::chrono::steady_clock::now() + delay,
                device_address_});
            condition_.notify_all();
        } catch (const std::exception &error) {
            std::cerr << "Focus Pen Pro haptic request dropped: "
                      << error.what() << '\n';
        }
    }

    void run(std::stop_token stop) {
        std::stop_callback wake_on_stop(
            stop, [this]() { condition_.notify_all(); });
        std::unique_lock lock(mutex_);
        while (!stop.stop_requested()) {
            if (requests_.empty()) {
                condition_.wait(lock);
                continue;
            }
            const auto next = std::min_element(
                requests_.begin(), requests_.end(),
                [](const HapticRequest &left, const HapticRequest &right) {
                    return left.due < right.due;
                });
            const auto now = std::chrono::steady_clock::now();
            if (next->due > now) {
                condition_.wait_until(lock, next->due);
                continue;
            }
            HapticRequest request = std::move(*next);
            requests_.erase(next);
            lock.unlock();
            try {
                send(request);
            } catch (const std::exception &error) {
                std::cerr << "Focus Pen Pro haptic worker failed: "
                          << error.what() << '\n';
            }
            lock.lock();
        }
    }

    void send(const HapticRequest &request) {
        const char *name = request.kind == HapticKind::DoublePress
                               ? "double-press"
                               : "slide";
        if (request.address.empty()) {
            std::cerr << "Focus Pen Pro " << name
                      << " haptic skipped: device address unavailable\n";
            return;
        }
        writer_.setDeviceAddress(request.address);
        const std::span<const std::uint8_t> command =
            request.kind == HapticKind::DoublePress
                ? std::span<const std::uint8_t>(kDoublePressHaptic)
                : std::span<const std::uint8_t>(kSlideHaptic);
        std::cerr << "Focus Pen Pro " << name << " haptic "
                  << (writer_.trigger(command) ? "sent" : "failed") << '\n';
    }
};

}  // namespace

std::unique_ptr<FocusPenHaptics> makeFocusPenHaptics() {
    try {
        return std::make_unique<BluezPenHaptics>();
    } catch (const std::exception &error) {
        std::cerr << "Focus Pen Pro haptics disabled: " << error.what()
                  << '\n';
        return nullptr;
    }
}
