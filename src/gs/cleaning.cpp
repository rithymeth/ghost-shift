#include "gs/cleaning.h"

#include "aether/core/log.h"

#include <algorithm>
#include <vector>

namespace ghostshift {

void CleaningSystem::Update(World& world, f64 now) {
    struct Target {
        Entity entity;
        u32 net_id;
        Vec3 position;
    };
    std::vector<Target> targets;
    for (const Entity e : FindEntitiesWithTag(world, "Contamination")) {
        const net::NetIdentity* ni = world.GetComponent<net::NetIdentity>(e);
        const Transform* t = world.GetComponent<Transform>(e);
        if (ni && ni->net_id != 0 && t) targets.push_back({e, ni->net_id, t->position});
    }
    if (!started_) {
        if (targets.empty()) return; // wait until the scene's contamination has network ids
        started_ = true;
        total_ = remaining_ = static_cast<u32>(targets.size());
        for (const Target& t : targets) health_[t.net_id] = 1.0f;
        AETHER_LOG_INFO("Contract", "started: %u contamination to clean", total_);
    }

    std::vector<Entity> cleaned;
    world.ForEach<net::NetIdentity, GhostCleaner, Transform>([&](net::NetIdentity&, GhostCleaner& player, Transform& pawn) {
        for (i32 i = 0; i < player.inbox_n; ++i) {
            const u32 id = static_cast<u32>(player.inbox[i]);
            const auto target = std::find_if(targets.begin(), targets.end(), [&](const Target& t) { return t.net_id == id; });
            const auto health = health_.find(id);
            bool ok = target != targets.end() && health != health_.end() && now - player.last_accept >= config_.min_interval;
            if (ok) {
                const f32 dx = target->position.x - pawn.position.x, dz = target->position.z - pawn.position.z;
                ok = dx * dx + dz * dz <= config_.range * config_.range;
            }
            if (!ok) {
                ++player.rejected;
                continue;
            }
            ++player.accepted;
            player.last_accept = now;
            health->second -= config_.work_per_request;
            if (health->second <= 1.0e-4f) {
                cleaned.push_back(target->entity);
                health_.erase(health);
                targets.erase(target);
                --remaining_;
                AETHER_LOG_INFO("Contract", "cleaned one (%u left)", remaining_);
            }
        }
        player.inbox_n = 0;
    });
    for (const Entity e : cleaned) world.DestroyEntity(e);
    if (!cleaned.empty() && remaining_ == 0) AETHER_LOG_INFO("Contract", "COMPLETE: %u/%u cleaned", total_, total_);
}

} // namespace ghostshift
