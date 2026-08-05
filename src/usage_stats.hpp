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

class UsageStats {
public:
    using Duration = std::chrono::milliseconds;

    static constexpr std::size_t kRetentionDays = 14;

    void add_active(std::int64_t day_index, Duration duration);

    [[nodiscard]] Duration active_on(std::int64_t day_index) const noexcept;
    [[nodiscard]] std::vector<UsageDay> recent(std::int64_t last_day_index,
                                                std::size_t day_count) const;
    [[nodiscard]] Duration total_for_period(std::int64_t last_day_index,
                                             std::size_t day_count) const;
    [[nodiscard]] std::size_t active_days(std::int64_t last_day_index,
                                          std::size_t day_count) const;

    [[nodiscard]] std::string serialize() const;
    bool deserialize(std::string_view data);

private:
    void trim_to_retention(std::int64_t newest_day_index);

    std::vector<UsageDay> days_;
};

}  // namespace lookaway
