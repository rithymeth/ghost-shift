// The vacuum's tank and the van: the tank fills as a player cleans, a full tank blocks cleaning, and it is emptied at the van.
#include "aether/net/replication.h"
#include "aether/scene/gameplay.h"
#include "gs/cleaning.h"

#include <cstdio>
#include <vector>

using namespace aether;
using namespace ghostshift;

static int g_failed = 0;
#define CHECK(c, msg) do { if (!(c)) { std::printf("FAIL: %s\n", msg); ++g_failed; } else std::printf("ok:   %s\n", msg); } while (0)

struct Room {
    World world;
    CleaningSystem cleaning;
    std::vector<Entity> pawns;
    f64 now = 0;
    u32 next_id = 1;
    Room() {
        for (int i = 0; i < 6; ++i) Stain({1.0f + 0.2f * static_cast<f32>(i), 0, 0.5f});
        AddPawn({0, 0, 0});
        Tick(0.1);
    }
    Entity AddPawn(Vec3 at) {
        net::NetIdentity ni;
        pawns.push_back(world.CreateEntity(ni, Transform{at, Quaternion{}}, GhostCleaner{}));
        return pawns.back();
    }
    void Stain(Vec3 at) {
        net::NetIdentity ni;
        net::SetNetArchetype(ni, "scene");
        world.CreateEntity(ni, Transform{at, Quaternion{}}, Tags{{"Contamination"}});
    }
    GhostCleaner& P(usize i = 0) { return *world.GetComponent<GhostCleaner>(pawns[i]); }
    void Tick(f64 dt = 1.0 / 30.0) {
        now += dt;
        world.ForEach<net::NetIdentity>([&](net::NetIdentity& ni) { if (ni.net_id == 0) ni.net_id = next_id++; });
        cleaning.Update(world, now);
    }
    // One scrub request at the first live contamination, then 4 ticks (clear of the cooldown).
    void Scrub(usize pawn = 0) {
        for (const Entity e : FindEntitiesWithTag(world, "Contamination")) {
            GhostCleaner& gc = P(pawn);
            if (gc.inbox_n < GhostCleaner::kInbox) gc.inbox[gc.inbox_n++] = static_cast<i32>(world.GetComponent<net::NetIdentity>(e)->net_id);
            break;
        }
        for (int t = 0; t < 4; ++t) Tick();
    }
    void Empty(usize pawn = 0) { P(pawn).EmptyTank(); Tick(); }
};

int main() {
    // 1. The tank fills while cleaning, and a full tank blocks further cleaning.
    {
        Room r;
        for (int i = 0; i < 20; ++i) r.Scrub();
        CHECK(r.P().tank > 1.2f && r.P().tank < 1.4f && !r.P().tank_full, "the tank fills as the player cleans (20 requests = 1.33 units)");
        for (int i = 0; i < 60; ++i) r.Scrub();
        const i32 accepted = r.P().accepted;
        CHECK(accepted == 45 && r.P().tank > 2.99f && r.P().tank_full, "the tank holds 3 units (45 requests) and then reports itself full");
        for (int i = 0; i < 10; ++i) r.Scrub();
        CHECK(r.P().accepted == accepted && r.cleaning.Remaining() == 3, "a full tank blocks cleaning: nothing more is accepted or removed");

        // 2. Emptying needs the van.
        r.Empty();
        CHECK(r.P().tank_full && r.P().empties == 0, "with no van anywhere the tank cannot be emptied");
        net::NetIdentity van_id;
        r.world.CreateEntity(van_id, Transform{Vec3{20, 0, 20}, Quaternion{}}, Tags{{"Van"}});
        r.Empty();
        CHECK(r.P().tank_full && r.P().empties == 0, "standing far from the van does not empty it");
        r.world.GetComponent<Transform>(r.pawns[0])->position = {19.0f, 0, 19.0f};
        r.Empty();
        CHECK(r.P().tank == 0.0f && !r.P().tank_full && r.P().empties == 1, "at the van the tank is emptied");
        // Cleaning resumes (the pawn is back near the stains; range is 3 m).
        r.world.GetComponent<Transform>(r.pawns[0])->position = {0, 0, 0};
        const i32 before = r.P().accepted;
        for (int i = 0; i < 10; ++i) r.Scrub();
        CHECK(r.P().accepted > before, "after emptying, cleaning resumes");
    }
    // 3. A downed player cannot empty a tank; tanks are per player.
    {
        Room r;
        net::NetIdentity van_id;
        r.world.CreateEntity(van_id, Transform{Vec3{0, 0, 0.5f}, Quaternion{}}, Tags{{"Van"}});
        r.AddPawn({0.5f, 0, 0});
        r.Tick();
        for (int i = 0; i < 30; ++i) r.Scrub(0);
        CHECK(r.P(0).tank > 1.9f && r.P(1).tank == 0.0f, "each player has their own tank");
        r.P(0).downed = true, r.P(0).down_until = 1.0e9;
        r.Empty(0);
        CHECK(r.P(0).tank > 1.9f && r.P(0).empties == 0, "a player who is down cannot empty the tank");
        r.P(0).downed = false, r.P(0).down_until = -1.0;
        r.Empty(0);
        CHECK(r.P(0).tank == 0.0f && r.P(0).empties == 1, "once up again, the same player can");
    }
    std::printf("%s (%d failed)\n", g_failed ? "FAILED" : "PASSED", g_failed);
    return g_failed ? 1 : 0;
}
