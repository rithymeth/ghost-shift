#pragma once
// Ghost Shift haunting: intensity (five stages) and the Prankster-style ghost AI, server-authoritative.
// Rules from the design doc: intensity rises while players are in the property and falls as rooms are cleaned; every
// dangerous event has a counteraction; ghost attacks incapacitate a player instead of eliminating them.
// The numbers below are PLACEHOLDERS to be tuned in playtests, not design values.
#include "aether/ecs/world.h"
#include "aether/net/replication.h"
#include "aether/reflection/reflect_macros.h"
#include "aether/scene/components.h"

namespace ghostshift {
using namespace aether;

enum class Stage : i32 { Quiet = 0, Stirring, Active, Dangerous, Critical };
enum class GhostState : i32 { Dormant = 0, Investigating, Pranking, Hunting, Stunned, Contained };

// On the director entity (archetype "director"): replicated so every client can show the haunting stage.
struct Haunting {
    f32 intensity = 0.0f; // 0..100
    i32 stage = 0;        // Stage
};

// On a ghost entity (archetype "ghost").
struct GhostBrain {
    i32 state = 0;     // GhostState (replicated)
    i32 pranks = 0;    // prank events so far (replicated)
    i32 attacks = 0;   // players incapacitated so far (replicated)
    f64 state_since = 0.0;   // server side only
    f64 next_attack = 0.0;
    f64 next_prank = 0.0;
    f64 stunned_until = -1.0;
};

struct HauntingConfig {
    f32 rise_per_second = 0.8f;
    f32 relief_per_clean = 12.0f;
    f32 stage_edges[4] = {20.0f, 40.0f, 60.0f, 80.0f};
    f32 hunt_speed = 2.5f, stalk_speed = 0.8f, stalk_distance = 3.0f;
    f32 reach = 1.0f;            // metres: a hunting ghost this close to a player incapacitates it
    f64 downed_seconds = 5.0;
    f64 protect_seconds = 3.0;   // after recovering, a player cannot be incapacitated again for this long
    f64 attack_cooldown = 2.0;
    f64 prank_interval = 3.0;
    f64 stun_seconds = 5.0;
};

class HauntingSystem {
public:
    explicit HauntingSystem(HauntingConfig config = {}) : config_(config) {}
    // `cleaned_total`: contamination cleaned so far this contract (each new one lowers the intensity).
    void Update(World& world, f64 now, u32 cleaned_total);
    // The counteraction to a hunting ghost (a tool will call this): stunned ghosts stop for stun_seconds.
    bool Stun(World& world, f64 now);
    float Intensity() const { return intensity_; }
    Stage CurrentStage() const;

private:
    HauntingConfig config_;
    float intensity_ = 0.0f;
    f64 last_now_ = -1.0;
    u32 last_cleaned_ = 0;
};

} // namespace ghostshift

AETHER_REFLECT(ghostshift::Haunting, 1,
    AETHER_FIELD(intensity, Field_Replicated),
    AETHER_FIELD(stage, Field_Replicated)
)
AETHER_REFLECT(ghostshift::GhostBrain, 1,
    AETHER_FIELD(state, Field_Replicated),
    AETHER_FIELD(pranks, Field_Replicated),
    AETHER_FIELD(attacks, Field_Replicated)
)
