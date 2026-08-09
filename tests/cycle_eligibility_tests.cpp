#include "app/reward_collection.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>

using lookaway::rewards::CycleEligibility;

namespace {

void expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

}  // namespace

int main() {
    CycleEligibility eligibility;

    expect(!eligibility.finish_rest(100),
           "rest alone does not qualify as a completed cycle");

    eligibility.mark_work_completed(100);
    expect(eligibility.finish_rest(100),
           "work and rest completed on the same day qualify");
    expect(!eligibility.finish_rest(100),
           "a completed rest can only be counted once");

    eligibility.mark_work_completed(100);
    eligibility.mark_work_completed(101);
    expect(!eligibility.finish_rest(101),
           "a repeated reminder does not move a cycle to another day");

    eligibility.mark_work_completed(100);
    eligibility.cancel();
    expect(!eligibility.finish_rest(100),
           "a reset cancels the pending cycle");

    std::cout << "All cycle eligibility tests passed.\n";
    return 0;
}
