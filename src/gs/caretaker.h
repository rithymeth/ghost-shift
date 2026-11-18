#pragma once
// The Caretaker (design doc: "recontaminates moved belongings"; counter: restore the room's layout). While it is active
// (Pranking or Hunting) it re-dirties spots the team already cleaned, a limited number of times per contract. Cleaning them
// again, stunning it, or containing it stops the damage. All numbers are PLACEHOLDERS.
#include "aether/ecs/world.h"
#include "gs/cleaning.h"
#include "gs/haunting.h"

namespace ghostshift {
using namespace aether;

struct CaretakerConfig {
    f64 interval = 12.0;      // seconds between recontaminations
    u32 max_per_contract = 3; // so a team can always win
};

class CaretakerSystem {
public:
    explicit CaretakerSystem(CaretakerConfig config = {}) : config_(config) {}
    // Call once per server tick, after CleaningSystem::Update.
    void Update(World& world, f64 now, const CleaningSystem& cleaning);
    u32 Recontaminated() const { return count_; }

private:
    CaretakerConfig config_;
    f64 next_ = -1.0;
    u32 count_ = 0;
};

} // namespace ghostshift
