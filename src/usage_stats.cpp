#include "usage_stats.hpp"

#include <algorithm>
#include <charconv>
#include <limits>

namespace lookaway {

namespace {

using Rep = UsageStats::Duration::rep;

bool parse_integer(std::string_view text, std::int64_t& value) {
    if (text.empty()) {
        return false;
    }
    const char* first = text.data();
    const char* last = first + text.size();
    const auto result = std::from_chars(first, last, value);
    return result.ec == std::errc{} && result.ptr == last;
}

bool add_without_overflow(Rep& target, Rep amount) {
    constexpr Rep max_value = std::numeric_limits<Rep>::max();
    if (amount < 0 || target > max_value - amount) {
        return false;
    }
    target += amount;
    return true;
}

}  // namespace

void UsageStats::add_active(std::int64_t day_index, Duration duration) {
    if (duration <= Duration{0}) {
        return;
    }

    const auto existing = std::find_if(
        days_.begin(), days_.end(),
        [day_index](const UsageDay& day) { return day.day_index == day_index; });
    if (existing != days_.end()) {
        Rep total = existing->active.count();
        if (!add_without_overflow(total, duration.count())) {
            existing->active = Duration{std::numeric_limits<Rep>::max()};
        } else {
            existing->active = Duration{total};
        }
    } else {
        days_.push_back(UsageDay{day_index, duration});
        std::sort(days_.begin(), days_.end(),
                  [](const UsageDay& left, const UsageDay& right) {
                      return left.day_index < right.day_index;
                  });
    }
    trim_to_retention(std::max(day_index, days_.back().day_index));
}

UsageStats::Duration UsageStats::active_on(std::int64_t day_index) const noexcept {
    const auto existing = std::find_if(
        days_.begin(), days_.end(),
        [day_index](const UsageDay& day) { return day.day_index == day_index; });
    return existing == days_.end() ? Duration{0} : existing->active;
}

std::vector<UsageDay> UsageStats::recent(std::int64_t last_day_index,
                                         std::size_t day_count) const {
    std::vector<UsageDay> result;
    result.reserve(day_count);
    if (day_count == 0) {
        return result;
    }

    const auto first_day = last_day_index - static_cast<std::int64_t>(day_count - 1);
    for (std::size_t offset = 0; offset < day_count; ++offset) {
        const auto day_index = first_day + static_cast<std::int64_t>(offset);
        result.push_back(UsageDay{day_index, active_on(day_index)});
    }
    return result;
}

UsageStats::Duration UsageStats::total_for_period(std::int64_t last_day_index,
                                                  std::size_t day_count) const {
    Rep total = 0;
    for (const UsageDay& day : recent(last_day_index, day_count)) {
        if (total > std::numeric_limits<Rep>::max() - day.active.count()) {
            return Duration{std::numeric_limits<Rep>::max()};
        }
        total += day.active.count();
    }
    return Duration{total};
}

std::size_t UsageStats::active_days(std::int64_t last_day_index,
                                    std::size_t day_count) const {
    std::size_t count = 0;
    for (const UsageDay& day : recent(last_day_index, day_count)) {
        if (day.active > Duration{0}) {
            ++count;
        }
    }
    return count;
}

std::string UsageStats::serialize() const {
    std::string result = "1\n";
    for (const UsageDay& day : days_) {
        result += std::to_string(day.day_index);
        result.push_back(',');
        result += std::to_string(day.active.count());
        result.push_back('\n');
    }
    return result;
}

bool UsageStats::deserialize(std::string_view data) {
    if (data.empty()) {
        return false;
    }

    std::vector<UsageDay> parsed;
    std::size_t line_start = 0;
    bool first_line = true;
    while (line_start <= data.size()) {
        const std::size_t line_end = data.find('\n', line_start);
        std::string_view line = data.substr(
            line_start, line_end == std::string_view::npos ? data.size() - line_start
                                                            : line_end - line_start);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }

        if (first_line) {
            if (line != "1") {
                return false;
            }
            first_line = false;
        } else if (!line.empty()) {
            const std::size_t comma = line.find(',');
            if (comma == std::string_view::npos) {
                return false;
            }
            std::int64_t day_index = 0;
            std::int64_t milliseconds = 0;
            if (!parse_integer(line.substr(0, comma), day_index) ||
                !parse_integer(line.substr(comma + 1), milliseconds) || milliseconds < 0) {
                return false;
            }

            const auto existing = std::find_if(
                parsed.begin(), parsed.end(),
                [day_index](const UsageDay& day) { return day.day_index == day_index; });
            if (existing != parsed.end()) {
                Rep total = existing->active.count();
                if (!add_without_overflow(total, milliseconds)) {
                    existing->active = Duration{std::numeric_limits<Rep>::max()};
                } else {
                    existing->active = Duration{total};
                }
            } else {
                parsed.push_back(UsageDay{day_index, Duration{milliseconds}});
            }
        }

        if (line_end == std::string_view::npos) {
            break;
        }
        line_start = line_end + 1;
    }

    if (first_line) {
        return false;
    }

    std::sort(parsed.begin(), parsed.end(),
              [](const UsageDay& left, const UsageDay& right) {
                  return left.day_index < right.day_index;
              });
    days_ = std::move(parsed);
    if (!days_.empty()) {
        trim_to_retention(days_.back().day_index);
    }
    return true;
}

void UsageStats::trim_to_retention(std::int64_t newest_day_index) {
    const auto oldest_day_index = newest_day_index -
                                  static_cast<std::int64_t>(kRetentionDays - 1);
    days_.erase(std::remove_if(
                    days_.begin(), days_.end(),
                    [oldest_day_index](const UsageDay& day) {
                        return day.day_index < oldest_day_index;
                    }),
                days_.end());
}

}  // namespace lookaway
