#include "gs/scene_setup.h"

#include "aether/net/replication.h"
#include "aether/scene/gameplay.h"
#include "aether/scene/script_component.h"
#include "gs/contract.h"
#include "gs/haunting.h"

#include <string>

namespace ghostshift {
using namespace aether;

static void StripScript(World& world, Entity e, SceneSetupReport& report) {
    if (world.HasComponent<ScriptComponent>(e)) {
        world.RemoveComponent<ScriptComponent>(e);
        ++report.scripts_removed;
    }
}

static void Identify(World& world, Entity e, const char* archetype) {
    if (world.HasComponent<net::NetIdentity>(e)) return;
    net::NetIdentity ni;
    net::SetNetArchetype(ni, archetype);
    world.AddComponent(e, ni);
}

SceneSetupReport PrepareScene(World& world) {
    SceneSetupReport report;
    for (const Entity e : FindEntitiesWithTag(world, "Ghost")) {
        if (!world.HasComponent<Transform>(e)) continue;
        Identify(world, e, "ghost");
        GhostBrain brain;
        static const char* names[] = {"Prankster", "Poltergeist", "Mimic", "Wailer", "Hoarder", "Caretaker"};
        if (const Tags* tags = world.GetComponent<Tags>(e)) {
            for (const std::string& tag : tags->names) {
                if (tag.rfind("GhostType:", 0) != 0) continue;
                for (int i = 0; i < 6; ++i) if (tag.substr(10) == names[i]) brain.type = i;
            }
        }
        if (!world.HasComponent<GhostBrain>(e)) world.AddComponent(e, brain);
        StripScript(world, e, report);
        ++report.ghosts;
    }
    for (const Entity e : FindEntitiesWithTag(world, "Director")) {
        if (!world.HasComponent<Transform>(e)) continue;
        Identify(world, e, "director");
        if (!world.HasComponent<Haunting>(e)) world.AddComponent(e, Haunting{});
        if (!world.HasComponent<ContractResult>(e)) world.AddComponent(e, ContractResult{});
        StripScript(world, e, report);
        ++report.directors;
    }
    for (const Entity e : FindEntitiesWithTag(world, "Player")) StripScript(world, e, report);
    for (const Entity e : FindEntitiesWithTag(world, "Contamination")) // dynamic: cleaned away, so they replicate (everything else static stays local)
        if (world.HasComponent<Transform>(e)) Identify(world, e, "scene");
    report.contamination = static_cast<unsigned>(FindEntitiesWithTag(world, "Contamination").size());
    return report;
}

} // namespace ghostshift
