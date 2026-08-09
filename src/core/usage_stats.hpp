#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lookaway {

struct UsageDay {
    std::int64_t day_index{};
    std::chrono::milliseconds active{};
};

struct UsageHour {
    std::int64_t hour_index{};
    std::chrono::milliseconds active{};
};

class UsageStats {
public:
    using Duration = std::chrono::milliseconds;

    static constexpr std::size_t kRetentionDays = 14;
    static constexpr std::size_t kRetentionHours = 24;

    void add_active(std::int64_t day_index, Duration duration);
    void add_hourly_active(std::int64_t hour_index, Duration duration);

    [[nodiscard]] Duration active_on(std::int64_t day_index) const noexcept;
    [[nodiscard]] std::vector<UsageDay> recent(std::int64_t last_day_index,
                                                std::size_t day_count) const;
    [[nodiscard]] Duration total_for_period(std::int64_t last_day_index,
                                             std::size_t day_count) const;
    [[nodiscard]] std::size_t active_days(std::int64_t last_day_index,
                                          std::size_t day_count) const;
    [[nodiscard]] Duration hourly_active_on(std::int64_t hour_index) const noexcept;
    [[nodiscard]] std::vector<UsageHour> recent_hours(std::int64_t last_hour_index,
                                                       std::size_t hour_count) const;
    [[nodiscard]] Duration total_for_hours(std::int64_t last_hour_index,
                                            std::size_t hour_count) const;
    [[nodiscard]] std::size_t active_hours(std::int64_t last_hour_index,
                                           std::size_t hour_count) const;

    [[nodiscard]] std::string serialize() const;
    bool deserialize(std::string_view data);

private:
    std::vector<UsageDay> days_;
    std::vector<UsageHour> hours_;
};

}  // namespace lookaway
