#include "usage_stats.hpp"

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

    expect(stats.active_on(100) == 35min + 25s, "merges usage on the same day");
    expect(stats.active_on(101) == 0min, "missing days are zero");
    expect(stats.total_for_period(102, 3) == 2h + 35min + 25s,
           "totals the requested period");
    expect(stats.active_days(102, 3) == 2, "counts active days");

    const auto recent = stats.recent(102, 3);
    expect(recent.size() == 3 && recent[0].day_index == 100 &&
               recent[1].day_index == 101 && recent[2].day_index == 102,
           "returns chronological daily buckets");

    UsageStats restored;
    expect(restored.deserialize(stats.serialize()), "restores serialized history");
    expect(restored.active_on(102) == 2h, "serialized usage keeps values");
    expect(!restored.deserialize("invalid"), "rejects invalid history");
    expect(restored.active_on(102) == 2h, "invalid history does not erase data");

    UsageStats retained;
    retained.add_active(0, 1min);
    retained.add_active(1, 2min);
    retained.add_active(14, 1min);
    expect(retained.active_on(0) == 0min, "drops entries outside retention window");
    expect(retained.active_on(1) == 2min, "keeps the oldest day inside retention window");
    expect(retained.active_on(14) == 1min, "keeps the newest retained entry");

    std::cout << "All UsageStats tests passed.\n";
    return 0;
}
