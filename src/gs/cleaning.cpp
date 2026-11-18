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
        i32 kind;
    };
    std::vector<Target> targets;
    for (const Entity e : FindEntitiesWithTag(world, "Contamination")) {
        const net::NetIdentity* ni = world.GetComponent<net::NetIdentity>(e);
        const Transform* t = world.GetComponent<Transform>(e);
        i32 kind = static_cast<i32>(Kind::Any);
        if (const Tags* tags = world.GetComponent<Tags>(e)) {
            for (const std::string& tag : tags->names) {
                if (tag == "Kind:Ecto") kind = static_cast<i32>(Kind::Ecto);
                else if (tag == "Kind:Stain") kind = static_cast<i32>(Kind::Stain);
                else if (tag == "Kind:Dust") kind = static_cast<i32>(Kind::Dust);
            }
        }
        if (ni && ni->net_id != 0 && t) targets.push_back({e, ni->net_id, t->position, kind});
    }
    if (!started_) {
        if (targets.empty()) return; // wait until the scene's contamination has network ids
        started_ = true;
        total_ = remaining_ = static_cast<u32>(targets.size());
        for (const Target& t : targets) health_[t.net_id] = 1.0f, slot_[t.net_id] = next_slot_++;
        AETHER_LOG_INFO("Contract", "started: %u contamination to clean", total_);
    } else {
        for (const Target& t : targets) {
            if (health_.emplace(t.net_id, 1.0f).second) {
                slot_[t.net_id] = next_slot_++;
                ++total_, ++remaining_;
                AETHER_LOG_INFO("Contract", "new contamination appeared (%u left)", remaining_);
            }
        }
    }

    std::vector<Vec3> vans;
    for (const Entity v : FindEntitiesWithTag(world, "Van")) if (const Transform* vt = world.GetComponent<Transform>(v)) vans.push_back(vt->position);
    std::vector<Entity> cleaned;
    world.ForEach<net::NetIdentity, GhostCleaner, Transform>([&](net::NetIdentity&, GhostCleaner& player, Transform& pawn) {
        if (!player.resources_init) { // first sight of this player: full charge and canister
            player.battery = config_.battery_capacity, player.spray = config_.spray_capacity;
            player.resources_init = true;
        }
        if (player.tool_request >= 0) { // SelectTool: only the three real tools
            if (player.tool_request <= static_cast<i32>(Tool::Sprayer)) player.tool = player.tool_request;
            else ++player.rejected;
            player.tool_request = -1;
        }
        if (player.empty_requested) { // servicing the vacuum, the UV scrubber and the sprayer needs the van
            player.empty_requested = false;
            bool at_van = false;
            for (const Vec3& v : vans) {
                const f32 dx = v.x - pawn.position.x, dz = v.z - pawn.position.z;
                at_van = at_van || dx * dx + dz * dz <= config_.van_range * config_.van_range;
            }
            if (at_van && !player.downed) {
                player.tank = 0.0f, player.battery = config_.battery_capacity, player.spray = config_.spray_capacity;
                player.tank_full = false;
                ++player.empties;
            }
            else ++player.rejected;
        }
        for (i32 i = 0; i < player.inbox_n; ++i) {
            const u32 id = static_cast<u32>(player.inbox[i]);
            const auto target = std::find_if(targets.begin(), targets.end(), [&](const Target& t) { return t.net_id == id; });
            const auto health = health_.find(id);
            player.downed = now < player.down_until; // an incapacitated player cannot clean; it recovers by itself
            const f32 w = config_.work_per_request;
            const bool room_in_tank = player.tool == static_cast<i32>(Tool::Vacuum)       ? player.tank + w <= config_.tank_capacity + 1.0e-3f
                                      : player.tool == static_cast<i32>(Tool::UvScrubber) ? player.battery - w >= -1.0e-3f
                                                                                           : player.spray - w >= -1.0e-3f;
            if (!room_in_tank) player.tank_full = true;
            const bool right_tool = target == targets.end() || target->kind == static_cast<i32>(Kind::Any) || target->kind == player.tool;
            if (!right_tool) ++player.wrong_tool;
            bool ok = !player.downed && room_in_tank && right_tool && target != targets.end() && health != health_.end() && now - player.last_accept >= config_.min_interval;
            if (ok) {
                const f32 dx = target->position.x - pawn.position.x, dz = target->position.z - pawn.position.z;
                ok = dx * dx + dz * dz <= config_.range * config_.range;
            }
            if (!ok) {
                ++player.rejected;
                continue;
            }
            ++player.accepted;
            ++accepted_total_;
            if (player.tool == static_cast<i32>(Tool::Vacuum)) player.tank += w;
            else if (player.tool == static_cast<i32>(Tool::UvScrubber)) player.battery -= w;
            else player.spray -= w;
            player.tank_full = player.tool == static_cast<i32>(Tool::Vacuum)       ? player.tank + w > config_.tank_capacity + 1.0e-3f
                               : player.tool == static_cast<i32>(Tool::UvScrubber) ? player.battery - w < -1.0e-3f
                                                                                    : player.spray - w < -1.0e-3f;
            player.last_accept = now;
            health->second -= config_.work_per_request;
            if (health->second <= 1.0e-4f) {
                cleaned.push_back(target->entity);
                if (const auto sl = slot_.find(target->net_id); sl != slot_.end() && sl->second < 32) cleaned_mask_ |= 1u << sl->second;
                cleaned_positions_.push_back(target->position);
                health_.erase(health);
                targets.erase(target);
                --remaining_;
                AETHER_LOG_INFO("Contract", "cleaned one (%u left)", remaining_);
            }
        }
        player.inbox_n = 0;
    });
    for (const Entity e : cleaned) {
        // The glow goes with the stain: lights that share its "Glow:<name>" tag.
        if (const Tags* tags = world.GetComponent<Tags>(e)) {
            const std::vector<std::string> names = tags->names; // copy: the entity is destroyed below
            for (const std::string& tag : names) {
                if (tag.rfind("Glow:", 0) != 0) continue;
                for (const Entity light : FindEntitiesWithTag(world, tag)) if (light != e) world.DestroyEntity(light);
            }
        }
        world.DestroyEntity(e);
    }
    if (!cleaned.empty() && remaining_ == 0) AETHER_LOG_INFO("Contract", "COMPLETE: %u/%u cleaned", total_, total_);
}

} // namespace ghostshift
