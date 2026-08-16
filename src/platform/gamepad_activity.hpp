#pragma once

#include <windows.h>
#include <xinput.h>

#include <array>
#include <cstdint>

namespace lookaway::platform {

class GamepadActivityMonitor {
public:
    GamepadActivityMonitor() = default;
    ~GamepadActivityMonitor();

    GamepadActivityMonitor(const GamepadActivityMonitor&) = delete;
    GamepadActivityMonitor& operator=(const GamepadActivityMonitor&) = delete;

    [[nodiscard]] bool poll();
    void reset() noexcept;

private:
    struct Snapshot {
        std::uint16_t buttons{};
        std::uint8_t left_trigger{};
        std::uint8_t right_trigger{};
        std::int16_t left_x{};
        std::int16_t left_y{};
        std::int16_t right_x{};
        std::int16_t right_y{};

        [[nodiscard]] bool equals(const Snapshot& other) const noexcept {
            return buttons == other.buttons &&
                   left_trigger == other.left_trigger &&
                   right_trigger == other.right_trigger &&
                   left_x == other.left_x && left_y == other.left_y &&
                   right_x == other.right_x && right_y == other.right_y;
        }
    };

    using GetStateFunction = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);

    bool ensure_xinput_loaded();

    HMODULE xinput_module_{};
    GetStateFunction get_state_{};
    bool loading_attempted_{false};
    std::array<Snapshot, XUSER_MAX_COUNT> previous_states_{};
    std::array<bool, XUSER_MAX_COUNT> previous_states_valid_{};
};

}  // namespace lookaway::platform
