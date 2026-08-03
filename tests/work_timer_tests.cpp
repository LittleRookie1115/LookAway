#include "work_timer.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string_view>

using namespace std::chrono_literals;
using lookaway::WorkTimer;

namespace {

void expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

}  // namespace

int main() {
    WorkTimer timer{45min, 1min, 5min};

    expect(timer.tick(44min, 0s) == WorkTimer::Event::None, "does not remind early");
    expect(timer.remaining() == 1min, "tracks active time");
    expect(timer.tick(2min, 2min) == WorkTimer::Event::None, "idle time is ignored");
    expect(timer.remaining() == 1min, "idle time does not reduce remaining time");
    expect(timer.tick(1min, 0s) == WorkTimer::Event::ReminderDue, "reminds at interval");
    expect(timer.tick(1s, 0s) == WorkTimer::Event::None, "reminder is emitted once");

    timer.snooze(5min);
    expect(timer.is_snoozing(), "reports snooze state");
    expect(timer.tick(4min, 3min) == WorkTimer::Event::None, "snooze waits five minutes");
    expect(timer.snooze_remaining() == 1min, "exposes snooze countdown");
    expect(timer.tick(1min, 3min) == WorkTimer::Event::ReminderDue, "snooze uses wall time");

    timer.start_rest();
    expect(timer.state() == WorkTimer::State::Resting, "starts rest");
    expect(timer.tick(4min, 0s) == WorkTimer::Event::None, "rest does not finish early");
    expect(timer.tick(1min, 0s) == WorkTimer::Event::RestFinished, "rest finishes on time");
    expect(timer.state() == WorkTimer::State::Working, "work resumes after rest");
    expect(timer.remaining() == 45min, "new cycle starts after rest");

    timer.tick(10min, 0s);
    timer.toggle_pause();
    timer.tick(10min, 0s);
    expect(timer.remaining() == 35min, "manual pause stops counting");
    timer.toggle_pause();
    timer.reset();
    expect(timer.remaining() == 45min, "reset clears current cycle");

    std::cout << "All WorkTimer tests passed.\n";
    return 0;
}
