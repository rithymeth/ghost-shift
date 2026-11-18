// The Caretaker ghost: re-dirties cleaned spots, with limits and counterplay. Isolated systems (no network), 30 Hz.
#include "aether/net/replication.h"
#include "aether/scene/gameplay.h"
#include "gs/caretaker.h"
#include "gs/cleaning.h"
#include "gs/haunting.h"

#include <cstdio>
#include <vector>

using namespace aether;
using namespace ghostshift;

static int g_failed = 0;
#define CHECK(c, msg) do { if (!(c)) { std::printf("FAIL: %s\n", msg); ++g_failed; } else std::printf("ok:   %s\n", msg); } while (0)

struct Room {
    World world;
    Entity pawn = kNullEntity, ghost = kNullEntity;
    CleaningSystem cleaning;
    CaretakerSystem caretaker;
    f64 now = 0;
    u32 next_id = 1;

    Room(GhostType type, GhostState state, u32 contamination = 1) {
        net::NetIdentity pawn_id;
        pawn = world.CreateEntity(pawn_id, Transform{Vec3{0, 0, 0}, Quaternion{}}, GhostCleaner{});
        net::NetIdentity ghost_id;
        GhostBrain brain;
        brain.type = static_cast<i32>(type);
        brain.state = static_cast<i32>(state);
        ghost = world.CreateEntity(ghost_id, Transform{Vec3{0, 0, -6}, Quaternion{}}, brain);
        for (u32 i = 0; i < contamination; ++i) Add({1.0f + static_cast<f32>(i), 0, 0.5f});
        Tick(0.1);
    }
    Entity Add(Vec3 at) {
        net::NetIdentity ni;
        net::SetNetArchetype(ni, "scene");
        return world.CreateEntity(ni, Transform{at, Quaternion{}}, Tags{{"Contamination"}});
    }
    GhostBrain& Brain() { return *world.GetComponent<GhostBrain>(ghost); }
    // One server tick: the network would assign ids to new replicated entities; then the systems run.
    void Tick(f64 dt = 1.0 / 30.0) {
        now += dt;
        world.ForEach<net::NetIdentity>([&](net::NetIdentity& ni) { if (ni.net_id == 0) ni.net_id = next_id++; });
        cleaning.Update(world, now);
        caretaker.Update(world, now, cleaning);
    }
    void Run(f64 seconds) { for (int i = 0; i < static_cast<int>(seconds * 30); ++i) Tick(); }
    // The pawn scrubs everything within reach until nothing is left (15 accepted requests per piece, 0.1 s apart).
    void CleanAll() {
        for (int guard = 0; guard < 4000 && cleaning.Remaining() > 0; ++guard) {
            for (const Entity e : FindEntitiesWithTag(world, "Contamination")) {
                GhostCleaner* gc = world.GetComponent<GhostCleaner>(pawn);
                if (gc->inbox_n < GhostCleaner::kInbox) gc->inbox[gc->inbox_n++] = static_cast<i32>(world.GetComponent<net::NetIdentity>(e)->net_id);
                break;
            }
            for (int t = 0; t < 4; ++t) Tick(); // 0.133 s: clear of the cooldown
        }
    }
};

int main() {
    // 1. A Caretaker that is active re-dirties a cleaned spot after its interval, and not before.
    {
        Room r(GhostType::Caretaker, GhostState::Pranking);
        r.CleanAll();
        CHECK(r.cleaning.Remaining() == 0 && r.cleaning.CleanedPositions().size() == 1, "the team cleans the room; the spot is remembered");
        const u32 before = r.caretaker.Recontaminated();
        r.Run(5.0);
        CHECK(r.caretaker.Recontaminated() == before, "nothing is re-dirtied before the interval has passed");
        r.Run(10.0);
        CHECK(r.caretaker.Recontaminated() == 1 && r.cleaning.Remaining() == 1 && r.cleaning.Total() == 2, "after the interval a cleaned spot is dirty again and the contract counts it");
        const auto spots = FindEntitiesWithTag(r.world, "Contamination");
        const Transform* t = spots.empty() ? nullptr : r.world.GetComponent<Transform>(spots.front());
        CHECK(t && t->position.x == 1.0f && t->position.z == 0.5f, "and it is the spot that was cleaned");
        // 2. The limit: it can only do this 3 times, so the team can always win.
        for (int i = 0; i < 6; ++i) { r.CleanAll(); r.Run(13.0); }
        CHECK(r.caretaker.Recontaminated() == 3, "it recontaminates at most 3 times per contract");
        r.CleanAll();
        r.Run(30.0);
        CHECK(r.cleaning.Remaining() == 0 && r.cleaning.Completed(), "after the limit the team can finish cleaning for good");
    }
    // 3. Other ghosts do not do this.
    {
        Room r(GhostType::Prankster, GhostState::Pranking);
        r.CleanAll();
        r.Run(40.0);
        CHECK(r.caretaker.Recontaminated() == 0 && r.cleaning.Remaining() == 0, "a Prankster does not recontaminate");
    }
    // 4. Counterplay: a stunned or contained Caretaker stops, and its clock restarts when it is active again.
    {
        Room r(GhostType::Caretaker, GhostState::Pranking);
        r.CleanAll();
        r.Brain().state = static_cast<i32>(GhostState::Stunned);
        r.Run(30.0);
        CHECK(r.caretaker.Recontaminated() == 0, "a stunned Caretaker does not recontaminate");
        r.Brain().state = static_cast<i32>(GhostState::Pranking);
        r.Run(5.0);
        CHECK(r.caretaker.Recontaminated() == 0, "its interval starts over when it recovers (no instant re-dirtying)");
        r.Brain().state = static_cast<i32>(GhostState::Contained);
        r.Run(40.0);
        CHECK(r.caretaker.Recontaminated() == 0, "a contained Caretaker never recontaminates");
    }
    // 5. A calm Caretaker (Dormant) is harmless until the haunting wakes it.
    {
        Room r(GhostType::Caretaker, GhostState::Dormant);
        r.CleanAll();
        r.Run(40.0);
        CHECK(r.caretaker.Recontaminated() == 0, "a dormant Caretaker does nothing");
    }
    std::printf("%s (%d failed)\n", g_failed ? "FAILED" : "PASSED", g_failed);
    return g_failed ? 1 : 0;
}
