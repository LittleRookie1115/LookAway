#pragma once

#include <chrono>

namespace lookaway {

class WorkTimer {
public:
    using Duration = std::chrono::milliseconds;

    enum class State {
        Working,
        Paused,
        Resting,
    };

    enum class Event {
        None,
        ReminderDue,
        RestFinished,
        IdleReset,
    };

    explicit WorkTimer(
        Duration work_interval = std::chrono::minutes(45),
        Duration idle_threshold = std::chrono::minutes(1),
        Duration rest_duration = std::chrono::minutes(5),
        Duration idle_reset_threshold = std::chrono::minutes(5));

    Event tick(Duration elapsed, Duration system_idle);
    void toggle_pause();
    void reset();
    void snooze(Duration duration = std::chrono::minutes(5));
    void start_rest();
    void finish_rest();

    [[nodiscard]] State state() const noexcept;
    [[nodiscard]] Duration active_time() const noexcept;
    [[nodiscard]] Duration remaining() const noexcept;
    [[nodiscard]] Duration rest_remaining() const noexcept;
    [[nodiscard]] Duration snooze_remaining() const noexcept;
    [[nodiscard]] Duration work_interval() const noexcept;
    [[nodiscard]] bool is_snoozing() const noexcept;
    [[nodiscard]] bool is_system_idle(Duration system_idle) const noexcept;
    [[nodiscard]] bool is_long_idle(Duration system_idle) const noexcept;
    [[nodiscard]] double progress() const noexcept;

private:
    Duration work_interval_;
    Duration idle_threshold_;
    Duration rest_duration_;
    Duration idle_reset_threshold_;
    Duration active_time_{0};
    Duration rest_remaining_{0};
    Duration snooze_remaining_{0};
    State state_{State::Working};
    bool reminder_sent_{false};
    bool idle_reset_applied_{false};
};

}  // namespace lookaway
