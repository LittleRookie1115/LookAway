#include "app/reward_collection.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <random>
#include <vector>

#include "app/app_config.hpp"

namespace lookaway::rewards {

namespace {

using namespace lookaway::app;

std::int64_t local_day_index() {
    SYSTEMTIME local{};
    GetLocalTime(&local);
    local.wHour = 0;
    local.wMinute = 0;
    local.wSecond = 0;
    local.wMilliseconds = 0;

    FILETIME file_time{};
    if (!SystemTimeToFileTime(&local, &file_time)) {
        return 0;
    }
    ULARGE_INTEGER value{};
    value.LowPart = file_time.dwLowDateTime;
    value.HighPart = file_time.dwHighDateTime;
    return static_cast<std::int64_t>(value.QuadPart / kFileTimeTicksPerDay);
}

}  // namespace

RewardCollection::RewardCollection() {
    random_seed_ = static_cast<std::uint64_t>(GetTickCount64()) ^
                   static_cast<std::uint64_t>(std::chrono::high_resolution_clock::now()
                                                  .time_since_epoch()
                                                  .count());
    load();
}

RewardCollection::~RewardCollection() {
    if (dirty_) {
        persist();
    }
}

void RewardCollection::load() {
    RewardStateDisk disk{};
    DWORD size = sizeof(disk);
    const LSTATUS status = RegGetValueW(
        HKEY_CURRENT_USER, kRegistryPath, kRewardStateValueName,
        RRF_RT_REG_BINARY, nullptr, &disk, &size);
    const bool is_legacy_state = disk.version == kLegacyRewardStateVersion;
    if (status == ERROR_SUCCESS && size == sizeof(disk) &&
        disk.magic == kRewardStateMagic &&
        (disk.version == kRewardStateVersion || is_legacy_state)) {
        day_index_ = disk.day_index;
        daily_reward_granted_ = disk.daily_reward_granted != 0;
        draw_tickets_ = disk.draw_tickets;
        total_draws_ = disk.total_draws;
        std::copy(std::begin(disk.card_counts), std::end(disk.card_counts), card_counts_);

        if (is_legacy_state) {
            // Version 1 did not retain enough context to prove that an in-progress
            // daily count came from a complete work/rest cycle on the same day.
            daily_completed_cycles_ = daily_reward_granted_
                                          ? std::max<std::uint32_t>(
                                                disk.daily_completed_cycles, 3)
                                          : 0;
            dirty_ = true;
        } else {
            daily_completed_cycles_ = disk.daily_completed_cycles;
        }
    }
    normalize_day();
    if (dirty_ && persist()) {
        dirty_ = false;
    }
}

bool RewardCollection::normalize_day() {
    bool changed = false;
    const std::int64_t today = local_day_index();
    if (day_index_ != today) {
        day_index_ = today;
        daily_completed_cycles_ = 0;
        daily_reward_granted_ = false;
        changed = true;
    }
    for (std::uint16_t& count : card_counts_) {
        const std::uint16_t clamped = std::min<std::uint16_t>(count, 999);
        if (count != clamped) {
            count = clamped;
            changed = true;
        }
    }
    dirty_ = dirty_ || changed;
    return changed;
}

bool RewardCollection::persist() const {
    HKEY key{};
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegistryPath, 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr,
                        &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }

    RewardStateDisk disk{};
    disk.magic = kRewardStateMagic;
    disk.version = kRewardStateVersion;
    disk.day_index = day_index_;
    disk.daily_completed_cycles = daily_completed_cycles_;
    disk.daily_reward_granted = daily_reward_granted_ ? 1 : 0;
    disk.draw_tickets = draw_tickets_;
    disk.total_draws = total_draws_;
    std::copy(std::begin(card_counts_), std::end(card_counts_), std::begin(disk.card_counts));
    const bool saved = RegSetValueExW(
                           key, kRewardStateValueName, 0, REG_BINARY,
                           reinterpret_cast<const BYTE*>(&disk), sizeof(disk)) ==
                       ERROR_SUCCESS;
    RegCloseKey(key);
    return saved;
}

void RewardCollection::refresh_day() {
    if (normalize_day() && persist()) {
        dirty_ = false;
    }
}

CycleRewardResult RewardCollection::record_completed_cycle() {
    normalize_day();
    if (daily_completed_cycles_ < std::numeric_limits<std::uint32_t>::max()) {
        ++daily_completed_cycles_;
    }

    CycleRewardResult result{false, daily_completed_cycles_};
    if (daily_completed_cycles_ >= 3 && !daily_reward_granted_ &&
        draw_tickets_ < std::numeric_limits<std::uint32_t>::max()) {
        ++draw_tickets_;
        daily_reward_granted_ = true;
        result.ticket_awarded = true;
    }

    dirty_ = true;
    if (persist()) {
        dirty_ = false;
    }
    return result;
}

DrawResult RewardCollection::draw_card() {
    if (draw_tickets_ == 0) {
        return {};
    }

    std::vector<std::size_t> candidates;
    candidates.reserve(kCardCount);
    for (std::size_t index = 0; index < kCardCount; ++index) {
        if (card_counts_[index] == 0) {
            candidates.push_back(index);
        }
    }
    if (candidates.empty()) {
        for (std::size_t index = 0; index < kCardCount; ++index) {
            candidates.push_back(index);
        }
    }

    std::mt19937_64 engine(random_seed_++);
    std::uniform_int_distribution<std::size_t> distribution(0, candidates.size() - 1);
    const std::size_t card_index = candidates[distribution(engine)];
    --draw_tickets_;
    if (total_draws_ < std::numeric_limits<std::uint32_t>::max()) {
        ++total_draws_;
    }
    if (card_counts_[card_index] < 999) {
        ++card_counts_[card_index];
    }

    DrawResult result{true, card_index, card_counts_[card_index],
                      card_counts_[card_index] == 1};
    dirty_ = true;
    if (persist()) {
        dirty_ = false;
    }
    return result;
}

std::size_t RewardCollection::collected_count() const noexcept {
    std::size_t count = 0;
    for (const std::uint16_t card_count : card_counts_) {
        if (card_count > 0) {
            ++count;
        }
    }
    return count;
}

std::uint32_t RewardCollection::daily_completed_cycles() const noexcept {
    return daily_completed_cycles_;
}

std::uint32_t RewardCollection::draw_tickets() const noexcept {
    return draw_tickets_;
}

std::uint32_t RewardCollection::total_draws() const noexcept {
    return total_draws_;
}

std::uint16_t RewardCollection::card_count(std::size_t index) const noexcept {
    return index < kCardCount ? card_counts_[index] : 0;
}

}  // namespace lookaway::rewards
