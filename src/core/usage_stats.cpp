#include "core/usage_stats.hpp"

#include <algorithm>
#include <charconv>
#include <limits>

namespace lookaway {

namespace {

using Duration = UsageStats::Duration;
using Rep = Duration::rep;

std::int64_t bucket_index(const UsageDay& day) {
    return day.day_index;
}

std::int64_t bucket_index(const UsageHour& hour) {
    return hour.hour_index;
}

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

template <typename Bucket>
void trim_buckets(std::vector<Bucket>& buckets, std::int64_t newest_index,
                  std::size_t retention_count) {
    const auto oldest_index = newest_index -
                              static_cast<std::int64_t>(retention_count - 1);
    buckets.erase(std::remove_if(
                      buckets.begin(), buckets.end(),
                      [oldest_index](const Bucket& bucket) {
                          return bucket_index(bucket) < oldest_index;
                      }),
                  buckets.end());
}

template <typename Bucket>
void add_to_buckets(std::vector<Bucket>& buckets, std::int64_t index,
                    Duration duration, std::size_t retention_count) {
    if (duration <= Duration{0}) {
        return;
    }

    const auto existing = std::find_if(
        buckets.begin(), buckets.end(),
        [index](const Bucket& bucket) { return bucket_index(bucket) == index; });
    if (existing != buckets.end()) {
        Rep total = existing->active.count();
        if (!add_without_overflow(total, duration.count())) {
            existing->active = Duration{std::numeric_limits<Rep>::max()};
        } else {
            existing->active = Duration{total};
        }
    } else {
        buckets.push_back(Bucket{index, duration});
        std::sort(buckets.begin(), buckets.end(),
                  [](const Bucket& left, const Bucket& right) {
                      return bucket_index(left) < bucket_index(right);
                  });
    }
    trim_buckets(buckets, std::max(index, bucket_index(buckets.back())), retention_count);
}

template <typename Bucket>
Duration active_in_bucket(const std::vector<Bucket>& buckets,
                          std::int64_t index) noexcept {
    const auto existing = std::find_if(
        buckets.begin(), buckets.end(),
        [index](const Bucket& bucket) { return bucket_index(bucket) == index; });
    return existing == buckets.end() ? Duration{0} : existing->active;
}

template <typename Bucket>
std::vector<Bucket> recent_buckets(const std::vector<Bucket>& buckets,
                                   std::int64_t last_index,
                                   std::size_t bucket_count) {
    std::vector<Bucket> result;
    result.reserve(bucket_count);
    if (bucket_count == 0) {
        return result;
    }

    const auto first_index = last_index -
                             static_cast<std::int64_t>(bucket_count - 1);
    for (std::size_t offset = 0; offset < bucket_count; ++offset) {
        const auto index = first_index + static_cast<std::int64_t>(offset);
        result.push_back(Bucket{index, active_in_bucket(buckets, index)});
    }
    return result;
}

template <typename Bucket>
Duration total_buckets(const std::vector<Bucket>& buckets,
                       std::int64_t last_index, std::size_t bucket_count) {
    Rep total = 0;
    for (const Bucket& bucket : recent_buckets(buckets, last_index, bucket_count)) {
        if (total > std::numeric_limits<Rep>::max() - bucket.active.count()) {
            return Duration{std::numeric_limits<Rep>::max()};
        }
        total += bucket.active.count();
    }
    return Duration{total};
}

}  // namespace

void UsageStats::add_active(std::int64_t day_index, Duration duration) {
    add_to_buckets(days_, day_index, duration, kRetentionDays);
}

void UsageStats::add_hourly_active(std::int64_t hour_index, Duration duration) {
    add_to_buckets(hours_, hour_index, duration, kRetentionHours);
}

UsageStats::Duration UsageStats::active_on(std::int64_t day_index) const noexcept {
    return active_in_bucket(days_, day_index);
}

std::vector<UsageDay> UsageStats::recent(std::int64_t last_day_index,
                                         std::size_t day_count) const {
    return recent_buckets(days_, last_day_index, day_count);
}

UsageStats::Duration UsageStats::total_for_period(std::int64_t last_day_index,
                                                  std::size_t day_count) const {
    return total_buckets(days_, last_day_index, day_count);
}

std::size_t UsageStats::active_days(std::int64_t last_day_index,
                                    std::size_t day_count) const {
    const auto days = recent(last_day_index, day_count);
    return static_cast<std::size_t>(std::count_if(
        days.begin(), days.end(),
        [](const UsageDay& day) { return day.active > Duration{0}; }));
}

UsageStats::Duration UsageStats::hourly_active_on(std::int64_t hour_index) const noexcept {
    return active_in_bucket(hours_, hour_index);
}

std::vector<UsageHour> UsageStats::recent_hours(std::int64_t last_hour_index,
                                                std::size_t hour_count) const {
    return recent_buckets(hours_, last_hour_index, hour_count);
}

UsageStats::Duration UsageStats::total_for_hours(std::int64_t last_hour_index,
                                                 std::size_t hour_count) const {
    return total_buckets(hours_, last_hour_index, hour_count);
}

std::size_t UsageStats::active_hours(std::int64_t last_hour_index,
                                     std::size_t hour_count) const {
    const auto hours = recent_hours(last_hour_index, hour_count);
    return static_cast<std::size_t>(std::count_if(
        hours.begin(), hours.end(),
        [](const UsageHour& hour) { return hour.active > Duration{0}; }));
}

std::string UsageStats::serialize() const {
    std::string result = "2\n";
    for (const UsageDay& day : days_) {
        result += "D," + std::to_string(day.day_index) + "," +
                  std::to_string(day.active.count()) + "\n";
    }
    for (const UsageHour& hour : hours_) {
        result += "H," + std::to_string(hour.hour_index) + "," +
                  std::to_string(hour.active.count()) + "\n";
    }
    return result;
}

bool UsageStats::deserialize(std::string_view data) {
    if (data.empty()) {
        return false;
    }

    std::vector<UsageDay> parsed_days;
    std::vector<UsageHour> parsed_hours;
    std::size_t line_start = 0;
    int version = 0;
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
            if (line == "1") {
                version = 1;
            } else if (line == "2") {
                version = 2;
            } else {
                return false;
            }
            first_line = false;
        } else if (!line.empty()) {
            char bucket_type = 'D';
            if (version == 2) {
                if (line.size() < 3 || line[1] != ',' ||
                    (line[0] != 'D' && line[0] != 'H')) {
                    return false;
                }
                bucket_type = line[0];
                line.remove_prefix(2);
            }

            const std::size_t comma = line.find(',');
            if (comma == std::string_view::npos) {
                return false;
            }
            std::int64_t index = 0;
            std::int64_t milliseconds = 0;
            if (!parse_integer(line.substr(0, comma), index) ||
                !parse_integer(line.substr(comma + 1), milliseconds) ||
                milliseconds < 0) {
                return false;
            }

            if (bucket_type == 'D') {
                add_to_buckets(parsed_days, index, Duration{milliseconds}, kRetentionDays);
            } else {
                add_to_buckets(parsed_hours, index, Duration{milliseconds}, kRetentionHours);
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

    days_ = std::move(parsed_days);
    hours_ = std::move(parsed_hours);
    return true;
}

}  // namespace lookaway
