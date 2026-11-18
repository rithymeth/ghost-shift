#pragma once
// Ghost Shift containment: place a trap, bait the ghost near it, stun it, activate. Server-authoritative.
// Rules from the design doc ("Containment trap: coordinate placement, baiting, and activation"; Stunned and Contained
// ghost states). A ghost resists capture unless it is stunned and inside the trap's radius, so it takes teamwork.
// All numbers are PLACEHOLDERS to be tuned in playtests.
#include "aether/ecs/world.h"
#include "aether/net/replication.h"
#include "aether/reflection/reflect_macros.h"
#include "aether/scene/components.h"
#include "gs/haunting.h"

namespace ghostshift {
using namespace aether;

// On every pawn: requests are queued by the RPCs and decided by TrapSystem. Counters replicate so the client sees verdicts.
struct GhostTools {
    static constexpr i32 kInbox = 8;
    enum Kind : i32 { kPlace = 0, kStun = 1, kActivate = 2 };
    struct Request {
        i32 kind;
        f32 x, z;
    };
    i32 placed = 0, stuns = 0, captures = 0, failed = 0; // replicated
    Request inbox[kInbox] = {};                          // server side only
    i32 inbox_n = 0;
    f64 stun_ready = -1.0e9;                             // server side only
    bool did_stun = false;                               // server side only: this player stunned the ghost that is still stunned
    bool extract_requested = false;                      // server side only: set by Extract, read by ContractSystem
    void Push(Request r) { if (inbox_n < kInbox) inbox[inbox_n++] = r; else ++failed; }
    void PlaceTrap(f32 x, f32 z) { Push({kPlace, x, z}); } // Fn_Server
    void StunGhost() { Push({kStun, 0, 0}); }              // Fn_Server
    void ActivateTrap() { Push({kActivate, 0, 0}); }       // Fn_Server
    void Extract() { extract_requested = true; }           // Fn_Server: leave the property (full result if ready, a retreat otherwise)
};

// On the trap entity (archetype "trap", tag "Trap").
struct GhostTrap {
    bool armed = true;
};

struct TrapConfig {
    f32 place_range = 3.0f;     // a trap can be put down this far from the player
    f32 stun_range = 5.0f;      // a player can stun a ghost this far away
    f64 stun_cooldown = 20.0;
    f32 capture_radius = 3.0f;  // a stunned ghost this close to the trap can be captured
    bool require_second_player = true; // with two or more players up, the player who stunned the ghost cannot also activate the trap: stun and trap take two people (solo play is unaffected)
    bool stun_needs_awake = true; // a Dormant ghost is not engaged and cannot be stunned: the team has to let the haunting wake it and bait it
};

class TrapSystem {
public:
    explicit TrapSystem(TrapConfig config = {}) : config_(config) {}
    // Call once per server tick, before HauntingSystem::Update.
    void Update(World& world, f64 now, HauntingSystem& haunting);

private:
    TrapConfig config_;
};

} // namespace ghostshift

AETHER_REFLECT(ghostshift::GhostTools, 1,
    AETHER_FIELD(placed, Field_Replicated),
    AETHER_FIELD(stuns, Field_Replicated),
    AETHER_FIELD(captures, Field_Replicated),
    AETHER_FIELD(failed, Field_Replicated),
    AETHER_METHOD(PlaceTrap, Fn_Server, {"x", "z"}),
    AETHER_METHOD(StunGhost, Fn_Server),
    AETHER_METHOD(ActivateTrap, Fn_Server),
    AETHER_METHOD(Extract, Fn_Server)
)
AETHER_REFLECT(ghostshift::GhostTrap, 1,
    AETHER_FIELD(armed, Field_Replicated)
)
