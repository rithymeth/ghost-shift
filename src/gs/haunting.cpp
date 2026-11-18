#include "gs/haunting.h"

#include "aether/core/log.h"
#include "gs/cleaning.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace ghostshift {

Stage HauntingSystem::CurrentStage() const {
    int s = 0;
    for (const f32 edge : config_.stage_edges) if (intensity_ >= edge) ++s;
    return static_cast<Stage>(s);
}

bool HauntingSystem::Stun(World& world, f64 now) {
    bool any = false;
    world.ForEach<GhostBrain>([&](GhostBrain& g) {
        if (g.state == static_cast<i32>(GhostState::Contained)) return;
        g.state = static_cast<i32>(GhostState::Stunned);
        g.state_since = now, g.stunned_until = now + config_.stun_seconds;
        any = true;
    });
    return any;
}

bool HauntingSystem::AllContained(const World& world) {
    u32 total = 0, contained = 0;
    world.ForEach<GhostBrain>([&](GhostBrain& g) { ++total; contained += g.state == static_cast<i32>(GhostState::Contained); });
    return total > 0 && contained == total;
}

void HauntingSystem::Update(World& world, f64 now, u32 cleaned_total) {
    const f64 dt = last_now_ < 0 ? 0.0 : std::max(0.0, now - last_now_);
    last_now_ = now;

    struct Pawn {
        Vec3 position;
        GhostCleaner* cleaner;
    };
    std::vector<Pawn> pawns;
    world.ForEach<net::NetIdentity, GhostCleaner, Transform>([&](net::NetIdentity&, GhostCleaner& c, Transform& t) { pawns.push_back({t.position, &c}); });

    // Intensity: rises only while someone is in the property; each cleaned contamination gives relief.
    const bool all_contained = AllContained(world);
    if (!pawns.empty() && !all_contained) intensity_ += config_.rise_per_second * static_cast<f32>(dt);
    if (cleaned_total > last_cleaned_) intensity_ -= config_.relief_per_clean * static_cast<f32>(cleaned_total - last_cleaned_);
    last_cleaned_ = std::max(last_cleaned_, cleaned_total);
    if (all_contained) intensity_ = 0.0f; // a contained ghost no longer haunts the property
    intensity_ = std::clamp(intensity_, 0.0f, 100.0f);
    const Stage stage = CurrentStage();
    world.ForEach<Haunting>([&](Haunting& h) { h.intensity = intensity_, h.stage = static_cast<i32>(stage); });

    // Ghost state follows the stage unless it is stunned or contained.
    const GhostState wanted = stage == Stage::Quiet ? GhostState::Dormant
                              : stage == Stage::Stirring ? GhostState::Investigating
                              : stage == Stage::Active ? GhostState::Pranking
                                                       : GhostState::Hunting;
    world.ForEach<GhostBrain, Transform>([&](GhostBrain& g, Transform& t) {
        const GhostState current = static_cast<GhostState>(g.state);
        if (current == GhostState::Contained) return;
        if (current == GhostState::Stunned) {
            if (now < g.stunned_until) return; // frozen: the counteraction worked
            g.state = static_cast<i32>(GhostState::Investigating); // recovers, then re-evaluates below
            g.state_since = now;
        } else if (current != wanted) {
            AETHER_LOG_INFO("Haunting", "ghost state %d -> %d (intensity %.1f)", g.state, static_cast<i32>(wanted), intensity_);
            g.state = static_cast<i32>(wanted);
            g.state_since = now;
        }
        const GhostState s = static_cast<GhostState>(g.state);
        // Nearest player who is not already down.
        Pawn* target = nullptr;
        f32 best = 1.0e30f;
        for (Pawn& p : pawns) {
            if (p.cleaner->downed || now < p.cleaner->protected_until) continue; // down or still protected: not a target
            const f32 dx = p.position.x - t.position.x, dz = p.position.z - t.position.z, d = dx * dx + dz * dz;
            if (d < best) best = d, target = &p;
        }
        if (s == GhostState::Pranking && now >= g.next_prank) {
            ++g.pranks; // a prank event (item theft and swaps come with the tools)
            g.next_prank = now + config_.prank_interval;
        }
        if (!target || (s != GhostState::Investigating && s != GhostState::Hunting)) return;
        const f32 dist = std::sqrt(best);
        const f32 speed = s == GhostState::Hunting ? config_.hunt_speed : config_.stalk_speed;
        const bool close_enough = s == GhostState::Investigating && dist <= config_.stalk_distance;
        if (!close_enough && dist > 1.0e-3f) {
            const f32 step = std::min(speed * static_cast<f32>(dt), dist);
            t.position.x += (target->position.x - t.position.x) / dist * step;
            t.position.z += (target->position.z - t.position.z) / dist * step;
        }
        if (s == GhostState::Hunting && dist <= config_.reach && now >= g.next_attack) {
            target->cleaner->downed = true;
            target->cleaner->down_until = now + config_.downed_seconds;
            target->cleaner->protected_until = target->cleaner->down_until + config_.protect_seconds;
            g.next_attack = now + config_.attack_cooldown;
            ++g.attacks;
            AETHER_LOG_INFO("Haunting", "a player was incapacitated (attack %d)", g.attacks);
        }
    });
}

} // namespace ghostshift
