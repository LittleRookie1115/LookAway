#pragma once

#include <cstddef>
#include <cstdint>

namespace lookaway::rewards {

struct CycleRewardResult {
    bool ticket_awarded{false};
    std::uint32_t daily_completed_cycles{0};
};

struct DrawResult {
    bool drawn{false};
    std::size_t card_index{0};
    std::uint16_t owned_count{0};
    bool first_copy{false};
};

class RewardCollection {
public:
    RewardCollection();
    RewardCollection(const RewardCollection&) = delete;
    RewardCollection& operator=(const RewardCollection&) = delete;
    ~RewardCollection();

    void refresh_day();
    CycleRewardResult record_completed_cycle();
    DrawResult draw_card();

    [[nodiscard]] std::size_t collected_count() const noexcept;
    [[nodiscard]] std::uint32_t daily_completed_cycles() const noexcept;
    [[nodiscard]] std::uint32_t draw_tickets() const noexcept;
    [[nodiscard]] std::uint32_t total_draws() const noexcept;
    [[nodiscard]] std::uint16_t card_count(std::size_t index) const noexcept;

private:
    void load();
    bool persist() const;
    bool normalize_day();

    bool dirty_{false};
    std::int64_t day_index_{0};
    std::uint32_t daily_completed_cycles_{0};
    bool daily_reward_granted_{false};
    std::uint32_t draw_tickets_{0};
    std::uint32_t total_draws_{0};
    std::uint16_t card_counts_[15]{};
    std::uint64_t random_seed_{0};
};

}  // namespace lookaway::rewards
