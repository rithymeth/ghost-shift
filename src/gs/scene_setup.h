#pragma once
// Maps the authored scene onto the replicated Ghost Shift entities. Run once after the scene is loaded and before the
// server starts replicating (the server gives every other scene entity a generic "scene" identity).
//   tag "Ghost"      -> archetype "ghost" with a GhostBrain (type from an optional "GhostType:<Name>" tag; default Prankster)
//   tag "Director"   -> archetype "director" with Haunting and ContractResult
//   tag "Contamination" -> archetype "scene" (it is cleaned away, so clients must see it go)
// The C++ systems own the ghost's and the director's logic on the server, so their Luau scripts (the offline game's version
// of the same rules) are removed there, as is the template player's controller (server pawns come from the session).
#include "aether/ecs/world.h"

namespace ghostshift {

struct SceneSetupReport {
    unsigned ghosts = 0, directors = 0, contamination = 0, scripts_removed = 0;
};

SceneSetupReport PrepareScene(aether::World& world);

} // namespace ghostshift
