#include "gs/trap.h"

#include "aether/core/log.h"
#include "aether/scene/gameplay.h"
#include "gs/cleaning.h"

#include <cmath>

namespace ghostshift {

static f32 Dist2(const Vec3& a, const Vec3& b) { const f32 dx = a.x - b.x, dz = a.z - b.z; return dx * dx + dz * dz; }

void TrapSystem::Update(World& world, f64 now, HauntingSystem& haunting) {
    auto traps = FindEntitiesWithTag(world, "Trap");
    Entity trap = traps.empty() ? kNullEntity : traps.front();
    GhostBrain* ghost = nullptr;
    Transform* ghost_pos = nullptr;
    world.ForEach<GhostBrain, Transform>([&](GhostBrain& g, Transform& t) { if (!ghost) ghost = &g, ghost_pos = &t; });

    const bool ghost_stunned = ghost && ghost->state == static_cast<i32>(GhostState::Stunned);
    i32 players_up = 0;
    world.ForEach<GhostTools, GhostCleaner>([&](GhostTools& tools, GhostCleaner& cleaner) {
        if (!ghost_stunned) tools.did_stun = false; // the stun is over: nobody is 'the stunner' any more
        if (!cleaner.downed && now >= cleaner.down_until) ++players_up;
    });

    world.ForEach<GhostTools, Transform, GhostCleaner>([&](GhostTools& tools, Transform& pawn, GhostCleaner& cleaner) {
        for (i32 i = 0; i < tools.inbox_n; ++i) {
            const GhostTools::Request r = tools.inbox[i];
            bool ok = false;
            if (!cleaner.downed && now >= cleaner.down_until) { // a player who is down cannot use tools
                if (r.kind == GhostTools::kPlace) {
                    const Vec3 at{r.x, 0, r.z};
                    if (std::isfinite(r.x) && std::isfinite(r.z) && Dist2(at, pawn.position) <= config_.place_range * config_.place_range) {
                        if (trap == kNullEntity) {
                            net::NetIdentity ni;
                            net::SetNetArchetype(ni, "trap");
                            trap = world.CreateEntity(ni, Transform{at, Quaternion{}}, GhostTrap{}, Tags{{"Trap"}});
                        } else if (Transform* t = world.GetComponent<Transform>(trap)) {
                            t->position = at;
                        }
                        ++tools.placed;
                        ok = true;
                    }
                } else if (r.kind == GhostTools::kStun) {
                    if (ghost && now >= tools.stun_ready && Dist2(ghost_pos->position, pawn.position) <= config_.stun_range * config_.stun_range &&
                        ghost->state != static_cast<i32>(GhostState::Contained) &&
                        !(config_.stun_needs_awake && ghost->state == static_cast<i32>(GhostState::Dormant)) && haunting.Stun(world, now)) {
                        tools.stun_ready = now + config_.stun_cooldown;
                        tools.did_stun = true;
                        ++tools.stuns;
                        ok = true;
                    }
                } else if (r.kind == GhostTools::kActivate) {
                    const Transform* tt = trap != kNullEntity ? world.GetComponent<Transform>(trap) : nullptr;
                    const bool needs_partner = config_.require_second_player && players_up > 1 && tools.did_stun;
                    if (ghost && tt && !needs_partner && ghost->state == static_cast<i32>(GhostState::Stunned) &&
                        Dist2(ghost_pos->position, tt->position) <= config_.capture_radius * config_.capture_radius) {
                        ghost->state = static_cast<i32>(GhostState::Contained);
                        ghost->state_since = now;
                        ++tools.captures;
                        ok = true;
                        AETHER_LOG_INFO("Trap", "ghost contained");
                    }
                }
            }
            if (!ok) ++tools.failed;
        }
        tools.inbox_n = 0;
    });
}

} // namespace ghostshift
