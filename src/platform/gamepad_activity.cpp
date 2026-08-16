#include "platform/gamepad_activity.hpp"

#include <array>
#include <cstdlib>
#include <cstring>

namespace lookaway::platform {

namespace {

std::int16_t normalized_axis(SHORT value, SHORT deadzone) {
    return std::abs(static_cast<int>(value)) > deadzone ? value : 0;
}

std::uint8_t normalized_trigger(BYTE value) {
    return value > XINPUT_GAMEPAD_TRIGGER_THRESHOLD ? value : 0;
}

}  // namespace

bool GamepadActivityMonitor::ensure_xinput_loaded() {
    if (get_state_) {
        return true;
    }
    if (loading_attempted_) {
        return false;
    }
    loading_attempted_ = true;

    constexpr std::array dll_names{
        L"xinput1_4.dll",
        L"xinput1_3.dll",
        L"xinput9_1_0.dll",
    };
    for (const wchar_t* name : dll_names) {
        xinput_module_ = LoadLibraryW(name);
        if (xinput_module_) {
            break;
        }
    }
    if (xinput_module_) {
        const FARPROC procedure = GetProcAddress(xinput_module_, "XInputGetState");
        static_assert(sizeof(get_state_) == sizeof(procedure));
        std::memcpy(&get_state_, &procedure, sizeof(get_state_));
        if (!get_state_) {
            FreeLibrary(xinput_module_);
            xinput_module_ = nullptr;
        }
    }
    return get_state_ != nullptr;
}

GamepadActivityMonitor::~GamepadActivityMonitor() {
    if (xinput_module_) {
        FreeLibrary(xinput_module_);
    }
}

bool GamepadActivityMonitor::poll() {
    if (!ensure_xinput_loaded()) {
        return false;
    }

    bool activity_detected = false;
    for (DWORD index = 0; index < XUSER_MAX_COUNT; ++index) {
        XINPUT_STATE state{};
        if (get_state_(index, &state) != ERROR_SUCCESS) {
            previous_states_valid_[index] = false;
            continue;
        }

        const XINPUT_GAMEPAD& gamepad = state.Gamepad;
        const Snapshot snapshot{
            gamepad.wButtons,
            normalized_trigger(gamepad.bLeftTrigger),
            normalized_trigger(gamepad.bRightTrigger),
            normalized_axis(gamepad.sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE),
            normalized_axis(gamepad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE),
            normalized_axis(gamepad.sThumbRX, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE),
            normalized_axis(gamepad.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE),
        };
        const bool controls_active = !snapshot.equals(Snapshot{});
        const bool controls_changed =
            previous_states_valid_[index] &&
            !snapshot.equals(previous_states_[index]);
        activity_detected = activity_detected || controls_active || controls_changed;
        previous_states_[index] = snapshot;
        previous_states_valid_[index] = true;
    }
    return activity_detected;
}

void GamepadActivityMonitor::reset() noexcept {
    previous_states_.fill(Snapshot{});
    previous_states_valid_.fill(false);
}

}  // namespace lookaway::platform
