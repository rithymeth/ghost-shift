// PrepareScene: maps the authored scene onto the replicated ghost and director entities.
#include "aether/net/replication.h"
#include "aether/scene/gameplay.h"
#include "aether/scene/script_component.h"
#include "gs/contract.h"
#include "gs/haunting.h"
#include "gs/scene_setup.h"

#include <cstdio>
#include <cstring>

using namespace aether;
using namespace ghostshift;

static int g_failed = 0;
#define CHECK(c, msg) do { if (!(c)) { std::printf("FAIL: %s\n", msg); ++g_failed; } else std::printf("ok:   %s\n", msg); } while (0)

int main() {
    World w;
    const Entity ghost = w.CreateEntity(Transform{Vec3{0, 0, -4}, Quaternion{}}, Tags{{"Lookdev", "Ghost", "GhostType:Caretaker"}}, ScriptComponent{});
    const Entity plain_ghost = w.CreateEntity(Transform{Vec3{1, 0, -4}, Quaternion{}}, Tags{{"Ghost"}});
    const Entity director = w.CreateEntity(Transform{Vec3{0, 0, 0}, Quaternion{}}, Tags{{"Director"}}, ScriptComponent{});
    const Entity player = w.CreateEntity(Transform{Vec3{0, 1.7f, 4}, Quaternion{}}, Tags{{"Player"}}, ScriptComponent{});
    const Entity stain = w.CreateEntity(Transform{Vec3{2, 0, 1}, Quaternion{}}, Tags{{"Contamination"}});
    const Entity prop = w.CreateEntity(Transform{Vec3{5, 0, 5}, Quaternion{}}, Tags{{"Lookdev"}});

    const SceneSetupReport r = PrepareScene(w);
    CHECK(r.ghosts == 2 && r.directors == 1 && r.contamination == 1, "the report counts 2 ghosts, 1 director and 1 contamination");
    const net::NetIdentity* gi = w.GetComponent<net::NetIdentity>(ghost);
    CHECK(gi && std::strcmp(gi->archetype, "ghost") == 0 && w.GetComponent<GhostBrain>(ghost) != nullptr, "a Ghost entity becomes the replicated 'ghost' archetype with a GhostBrain");
    CHECK(w.GetComponent<GhostBrain>(ghost)->type == static_cast<i32>(GhostType::Caretaker), "its GhostType tag selects the Caretaker");
    CHECK(w.GetComponent<GhostBrain>(plain_ghost)->type == static_cast<i32>(GhostType::Prankster), "a ghost with no type tag is a Prankster");
    const net::NetIdentity* di = w.GetComponent<net::NetIdentity>(director);
    CHECK(di && std::strcmp(di->archetype, "director") == 0 && w.GetComponent<Haunting>(director) && w.GetComponent<ContractResult>(director),
          "the Director becomes the replicated 'director' archetype with Haunting and ContractResult");
    CHECK(!w.HasComponent<ScriptComponent>(ghost) && !w.HasComponent<ScriptComponent>(director) && !w.HasComponent<ScriptComponent>(player) && r.scripts_removed == 3,
          "the offline Luau scripts of the ghost, director and template player are removed (C++ owns that logic here)");
    const net::NetIdentity* si = w.GetComponent<net::NetIdentity>(stain);
    CHECK(si && std::strcmp(si->archetype, "scene") == 0, "contamination gets a replicated identity (it is cleaned away, so clients must see it go)");
    CHECK(!w.HasComponent<net::NetIdentity>(prop), "a static prop is NOT replicated");
    const SceneSetupReport again = PrepareScene(w);
    CHECK(again.scripts_removed == 0 && w.GetComponent<GhostBrain>(ghost)->type == static_cast<i32>(GhostType::Caretaker), "running it twice changes nothing");
    std::printf("%s (%d failed)\n", g_failed ? "FAILED" : "PASSED", g_failed);
    return g_failed ? 1 : 0;
}
