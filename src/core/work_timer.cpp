#include "core/work_timer.hpp"

#include <algorithm>

namespace lookaway {

WorkTimer::WorkTimer(Duration work_interval, Duration idle_threshold, Duration rest_duration,
                     Duration idle_reset_threshold)
    : work_interval_(std::max(work_interval, Duration{1})),
      idle_threshold_(std::max(idle_threshold, Duration{0})),
      rest_duration_(std::max(rest_duration, Duration{1})),
      idle_reset_threshold_(std::max(idle_reset_threshold, idle_threshold_)) {}

WorkTimer::Event WorkTimer::tick(Duration elapsed, Duration system_idle) {
    elapsed = std::max(elapsed, Duration{0});

    if (state_ == State::Resting) {
        rest_remaining_ = elapsed >= rest_remaining_ ? Duration{0} : rest_remaining_ - elapsed;
        if (rest_remaining_ == Duration{0}) {
            finish_rest();
            return Event::RestFinished;
        }
        return Event::None;
    }

    if (is_long_idle(system_idle)) {
        if (!idle_reset_applied_) {
            active_time_ = Duration{0};
            snooze_remaining_ = Duration{0};
            reminder_sent_ = false;
            idle_reset_applied_ = true;
            return Event::IdleReset;
        }
        return Event::None;
    }
    idle_reset_applied_ = false;

    if (state_ == State::Paused) {
        return Event::None;
    }

    if (snooze_remaining_ > Duration{0}) {
        snooze_remaining_ = elapsed >= snooze_remaining_ ? Duration{0} : snooze_remaining_ - elapsed;
        if (snooze_remaining_ == Duration{0}) {
            reminder_sent_ = true;
            return Event::ReminderDue;
        }
        return Event::None;
    }

    if (!is_system_idle(system_idle)) {
        active_time_ = std::min(active_time_ + elapsed, work_interval_);
    }

    if (active_time_ >= work_interval_ && !reminder_sent_) {
        reminder_sent_ = true;
        return Event::ReminderDue;
    }
    return Event::None;
}

void WorkTimer::toggle_pause() {
    if (state_ == State::Resting) {
        return;
    }
    state_ = state_ == State::Paused ? State::Working : State::Paused;
}

void WorkTimer::reset() {
    active_time_ = Duration{0};
    rest_remaining_ = Duration{0};
    snooze_remaining_ = Duration{0};
    reminder_sent_ = false;
    idle_reset_applied_ = false;
    state_ = State::Working;
}

void WorkTimer::snooze(Duration duration) {
    state_ = State::Working;
    snooze_remaining_ = std::max(duration, Duration{1});
    reminder_sent_ = false;
}

void WorkTimer::start_rest() {
    state_ = State::Resting;
    rest_remaining_ = rest_duration_;
    snooze_remaining_ = Duration{0};
    reminder_sent_ = false;
}

void WorkTimer::finish_rest() {
    reset();
}

WorkTimer::State WorkTimer::state() const noexcept {
    return state_;
}

WorkTimer::Duration WorkTimer::active_time() const noexcept {
    return active_time_;
}

WorkTimer::Duration WorkTimer::remaining() const noexcept {
    return active_time_ >= work_interval_ ? Duration{0} : work_interval_ - active_time_;
}

WorkTimer::Duration WorkTimer::rest_remaining() const noexcept {
    return rest_remaining_;
}

WorkTimer::Duration WorkTimer::snooze_remaining() const noexcept {
    return snooze_remaining_;
}

WorkTimer::Duration WorkTimer::work_interval() const noexcept {
    return work_interval_;
}

bool WorkTimer::is_snoozing() const noexcept {
    return snooze_remaining_ > Duration{0};
}

bool WorkTimer::is_usage_active(Duration system_idle) const noexcept {
    return state_ == State::Working && !is_system_idle(system_idle);
}

bool WorkTimer::is_system_idle(Duration system_idle) const noexcept {
    return system_idle >= idle_threshold_;
}

bool WorkTimer::is_long_idle(Duration system_idle) const noexcept {
    return system_idle >= idle_reset_threshold_;
}

double WorkTimer::progress() const noexcept {
    if (state_ == State::Resting) {
        return 1.0 - static_cast<double>(rest_remaining_.count()) /
                         static_cast<double>(rest_duration_.count());
    }
    return static_cast<double>(active_time_.count()) /
           static_cast<double>(work_interval_.count());
}

}  // namespace lookaway
