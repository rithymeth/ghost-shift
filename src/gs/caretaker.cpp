#include "gs/caretaker.h"

#include "aether/core/log.h"
#include "aether/net/replication.h"
#include "aether/scene/gameplay.h"

namespace ghostshift {

void CaretakerSystem::Update(World& world, f64 now, const CleaningSystem& cleaning) {
    bool active = false;
    world.ForEach<GhostBrain>([&](GhostBrain& g) {
        const GhostState s = static_cast<GhostState>(g.state);
        if (g.type == static_cast<i32>(GhostType::Caretaker) && (s == GhostState::Pranking || s == GhostState::Hunting)) active = true;
    });
    if (!active) { next_ = -1.0; return; } // stunned, contained or calm: the clock stops and restarts when it is active again
    if (next_ < 0.0) next_ = now + config_.interval;
    if (now < next_ || count_ >= config_.max_per_contract || cleaning.CleanedPositions().empty()) return;

    const auto& spots = cleaning.CleanedPositions();
    const Vec3 at = spots[count_ % spots.size()]; // deterministic: the spots in the order they were cleaned
    net::NetIdentity ni;
    net::SetNetArchetype(ni, "scene");
    world.CreateEntity(ni, Transform{at, Quaternion{}}, Tags{{"Contamination"}});
    ++count_;
    next_ = now + config_.interval;
    AETHER_LOG_INFO("Caretaker", "recontaminated a cleaned spot (%u of %u)", count_, config_.max_per_contract);
}

} // namespace ghostshift
