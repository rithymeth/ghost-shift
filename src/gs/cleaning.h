#pragma once
// Ghost Shift networked cleaning. A client asks, through an RPC on its own pawn, to scrub a contamination
// (named by its replicated net id). The server owns the contamination's state and decides: the request is
// accepted only for a live contamination, within range of the pawn, and not faster than the cooldown.
#include "aether/ecs/world.h"
#include "aether/net/replication.h"
#include "aether/reflection/reflect_macros.h"
#include "aether/scene/components.h"
#include "aether/scene/gameplay.h"

#include <unordered_map>

namespace ghostshift {
using namespace aether;

// On every player pawn. `requests`, `accepted` and `rejected` replicate, so a client sees the server's verdict.
struct GhostCleaner {
    static constexpr i32 kInbox = 8;
    i32 requests = 0, accepted = 0, rejected = 0;
    bool downed = false;      // incapacitated by a ghost: cannot clean until it recovers (replicated)
    f64 down_until = -1.0e9;  // server side only
    f64 protected_until = -1.0e9; // server side only: a recovered player cannot be re-attacked straight away
    i32 inbox[kInbox] = {};   // server side only: net ids asked for since the last Update
    i32 inbox_n = 0;
    f64 last_accept = -1.0e9; // server side only
    void Clean(i32 net_id) {  // runs on the server (Fn_Server)
        ++requests;
        if (inbox_n < kInbox) inbox[inbox_n++] = net_id;
        else ++rejected;
    }
};

struct CleaningConfig {
    f32 range = 3.0f;            // metres from the pawn
    f64 min_interval = 0.1;      // seconds between accepted requests of one player
    f32 work_per_request = 1.0f / 15.0f; // 15 accepted requests (1.5 s at the cooldown) remove one contamination
};

class CleaningSystem {
public:
    explicit CleaningSystem(CleaningConfig config = {}) : config_(config) {}
    // Contamination that appears later (a ghost re-contaminating a room) is picked up and counted as it appears.
    // Call once per server tick, after the network has delivered this tick's RPCs.
    void Update(World& world, f64 now);
    u32 Total() const { return total_; }
    u32 Remaining() const { return remaining_; }
    bool Completed() const { return started_ && remaining_ == 0; }
    u32 Accepted() const { return accepted_total_; } // accepted requests over the whole session (survives players leaving)

private:
    CleaningConfig config_;
    std::unordered_map<u32, f32> health_; // net id -> 1 (dirty) .. 0 (clean)
    bool started_ = false;
    u32 total_ = 0, remaining_ = 0, accepted_total_ = 0;
};

} // namespace ghostshift

AETHER_REFLECT(ghostshift::GhostCleaner, 1,
    AETHER_FIELD(requests, Field_Replicated),
    AETHER_FIELD(accepted, Field_Replicated),
    AETHER_FIELD(rejected, Field_Replicated),
    AETHER_FIELD(downed, Field_Replicated),
    AETHER_METHOD(Clean, Fn_Server, {"net_id"})
)
