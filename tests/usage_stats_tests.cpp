#include "core/usage_stats.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string_view>

using namespace std::chrono_literals;
using lookaway::UsageStats;

namespace {

void expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

}  // namespace

int main() {
    UsageStats stats;
    stats.add_active(100, 35min);
    stats.add_active(100, 25s);
    stats.add_active(102, 2h);
    stats.add_hourly_active(1000, 12min);
    stats.add_hourly_active(1000, 30s);
    stats.add_hourly_active(1002, 45min);

    expect(stats.active_on(100) == 35min + 25s, "merges usage on the same day");
    expect(stats.active_on(101) == 0min, "missing days are zero");
    expect(stats.total_for_period(102, 3) == 2h + 35min + 25s,
           "totals the requested period");
    expect(stats.active_days(102, 3) == 2, "counts active days");

    const auto recent = stats.recent(102, 3);
    expect(recent.size() == 3 && recent[0].day_index == 100 &&
               recent[1].day_index == 101 && recent[2].day_index == 102,
           "returns chronological daily buckets");

    expect(stats.hourly_active_on(1000) == 12min + 30s,
           "merges usage in the same hour");
    expect(stats.hourly_active_on(1001) == 0min, "missing hours are zero");
    expect(stats.total_for_hours(1002, 3) == 57min + 30s,
           "totals the requested hourly period");
    expect(stats.active_hours(1002, 3) == 2, "counts active hours");

    const auto recent_hours = stats.recent_hours(1002, 3);
    expect(recent_hours.size() == 3 && recent_hours[0].hour_index == 1000 &&
               recent_hours[1].hour_index == 1001 &&
               recent_hours[2].hour_index == 1002,
           "returns chronological hourly buckets");

    UsageStats restored;
    expect(restored.deserialize(stats.serialize()), "restores serialized history");
    expect(restored.active_on(102) == 2h, "serialized usage keeps values");
    expect(restored.hourly_active_on(1002) == 45min,
           "serialized hourly usage keeps values");
    expect(!restored.deserialize("invalid"), "rejects invalid history");
    expect(restored.active_on(102) == 2h, "invalid history does not erase data");
    expect(restored.hourly_active_on(1002) == 45min,
           "invalid history does not erase hourly data");

    UsageStats legacy;
    expect(legacy.deserialize("1\n77,60000\n"), "loads version 1 daily history");
    expect(legacy.active_on(77) == 1min, "keeps daily values from version 1");
    expect(legacy.hourly_active_on(77) == 0min,
           "version 1 history starts without fabricated hourly values");
    expect(legacy.serialize().rfind("2\n", 0) == 0,
           "migrates version 1 history to the current format");

    UsageStats retained;
    retained.add_active(0, 1min);
    retained.add_active(1, 2min);
    retained.add_active(14, 1min);
    expect(retained.active_on(0) == 0min, "drops entries outside retention window");
    expect(retained.active_on(1) == 2min, "keeps the oldest day inside retention window");
    expect(retained.active_on(14) == 1min, "keeps the newest retained entry");

    UsageStats hourly_retained;
    hourly_retained.add_hourly_active(0, 1min);
    hourly_retained.add_hourly_active(1, 2min);
    hourly_retained.add_hourly_active(24, 1min);
    expect(hourly_retained.hourly_active_on(0) == 0min,
           "drops hours outside retention window");
    expect(hourly_retained.hourly_active_on(1) == 2min,
           "keeps the oldest hour inside retention window");
    expect(hourly_retained.hourly_active_on(24) == 1min,
           "keeps the newest retained hour");

    std::cout << "All UsageStats tests passed.\n";
    return 0;
}
