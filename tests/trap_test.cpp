// Containment: trap placement, stun and capture rules (isolated), then the full coordinated sequence with two real clients.
#include "aether/net/replication.h"
#include "aether/scene/gameplay.h"
#include "aether/server/dedicated_server.h"
#include "gs/cleaning.h"
#include "gs/haunting.h"
#include "gs/trap.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace aether;
using namespace aether::server;
using namespace ghostshift;

static int g_failed = 0;
#define CHECK(c, msg) do { if (!(c)) { std::printf("FAIL: %s\n", msg); ++g_failed; } else std::printf("ok:   %s\n", msg); } while (0)

static Entity MakePawn(World& w, Vec3 p) {
    net::NetIdentity ni;
    return w.CreateEntity(ni, Transform{p, Quaternion{}}, GhostCleaner{}, GhostTools{});
}
static Entity MakeGhost(World& w, Vec3 p, GhostState s) {
    net::NetIdentity ni;
    GhostBrain g;
    g.state = static_cast<i32>(s);
    return w.CreateEntity(ni, Transform{p, Quaternion{}}, g);
}
static usize TrapCount(World& w) { return FindEntitiesWithTag(w, "Trap").size(); }

int main() {
    RegisterServerComponents();

    // ---------- Isolated rules ----------
    {
        World w;
        const Entity pawn = MakePawn(w, {0, 0, 0});
        const Entity ghost = MakeGhost(w, {0, 0, -4}, GhostState::Hunting);
        GhostTools* tools = w.GetComponent<GhostTools>(pawn);
        GhostBrain* brain = w.GetComponent<GhostBrain>(ghost);
        HauntingSystem haunting;
        TrapSystem traps;
        f64 now = 100.0;
        auto tick = [&](f64 dt = 1.0 / 30.0) { now += dt; traps.Update(w, now, haunting); };

        tools->PlaceTrap(50.0f, 50.0f); tick();
        CHECK(TrapCount(w) == 0 && tools->failed == 1, "placing a trap far from the player is rejected");
        tools->PlaceTrap(std::numeric_limits<float>::quiet_NaN(), 0.0f); tick();
        CHECK(TrapCount(w) == 0 && tools->failed == 2, "a NaN placement is rejected");
        tools->PlaceTrap(0.0f, -2.0f); tick();
        CHECK(TrapCount(w) == 1 && tools->placed == 1, "placing a trap within range creates it");
        tools->PlaceTrap(1.0f, -1.0f); tick();
        CHECK(TrapCount(w) == 1 && tools->placed == 2, "placing again moves the one trap instead of adding another");
        tools->PlaceTrap(0.0f, -2.0f); tick();

        tools->ActivateTrap(); tick();
        CHECK(brain->state == static_cast<i32>(GhostState::Hunting) && tools->captures == 0, "activating the trap on a ghost that is not stunned fails (it resists)");

        tools->StunGhost(); tick();
        CHECK(brain->state == static_cast<i32>(GhostState::Stunned) && tools->stuns == 1, "stunning a ghost within range works");
        tools->StunGhost(); tick();
        CHECK(tools->stuns == 1, "a second stun inside the cooldown is rejected");

        // A stunned ghost that is NOT near the trap cannot be captured.
        w.GetComponent<Transform>(ghost)->position = {0, 0, -20};
        tools->ActivateTrap(); tick();
        CHECK(brain->state == static_cast<i32>(GhostState::Stunned) && tools->captures == 0, "a stunned ghost far from the trap is not captured");

        // Near the trap and stunned: captured.
        w.GetComponent<Transform>(ghost)->position = {0, 0, -3.0f};
        tools->ActivateTrap(); tick();
        CHECK(brain->state == static_cast<i32>(GhostState::Contained) && tools->captures == 1, "a stunned ghost inside the trap radius is contained");
        CHECK(HauntingSystem::AllContained(w), "every ghost in the world is contained");

        // A contained ghost ends the haunting: intensity drops to zero and stays there with players present.
        haunting.SetIntensity(75.0f);
        for (int i = 0; i < 300; ++i) { now += 1.0 / 30.0; haunting.Update(w, now, 0); }
        CHECK(haunting.Intensity() == 0.0f, "with the ghost contained the haunting stays at zero while players are present");
        CHECK(brain->state == static_cast<i32>(GhostState::Contained), "a contained ghost stays contained");
    }
    {
        World w; // a downed player cannot use tools
        const Entity pawn = MakePawn(w, {0, 0, 0});
        MakeGhost(w, {0, 0, -2}, GhostState::Hunting);
        GhostTools* tools = w.GetComponent<GhostTools>(pawn);
        GhostCleaner* cleaner = w.GetComponent<GhostCleaner>(pawn);
        HauntingSystem haunting;
        TrapSystem traps;
        cleaner->downed = true, cleaner->down_until = 1.0e9;
        tools->PlaceTrap(0.0f, -1.0f); tools->StunGhost();
        traps.Update(w, 10.0, haunting);
        CHECK(TrapCount(w) == 0 && tools->stuns == 0 && tools->failed == 2, "a downed player cannot place a trap or stun");
    }

    // ---------- End to end: two real clients, the coordinated sequence ----------
    World world;
    net::NetIdentity gid, did;
    net::SetNetArchetype(gid, "ghost");
    net::SetNetArchetype(did, "director");
    const Entity ghost = world.CreateEntity(gid, Transform{Vec3{0, 0, -9.0f}, Quaternion{}}, GhostBrain{});
    const Entity director = world.CreateEntity(did, Transform{Vec3{0, 0, 0}, Quaternion{}}, Haunting{});
    std::vector<Entity> stains;
    for (const Vec3 p : {Vec3{1.0f, 0, 0.5f}, Vec3{2.0f, 0, 1.0f}, Vec3{1.5f, 0, 0.0f}}) {
        net::NetIdentity ni;
        net::SetNetArchetype(ni, kSceneArchetype);
        stains.push_back(world.CreateEntity(ni, Transform{p, Quaternion{}}, Tags{{"Contamination"}}));
    }
    ServerConfig sc;
    sc.port = 0, sc.lan_advertise = false, sc.status_interval = 0;
    DedicatedServer server;
    std::string err;
    if (!server.Start(sc, world, &err)) { std::printf("server: %s\n", err.c_str()); return 2; }
    CleaningSystem cleaning;
    HauntingSystem haunting;
    TrapSystem traps;
    f64 now = 0;
    struct Player {
        std::unique_ptr<NetClient> client = std::make_unique<NetClient>();
        Entity Pawn() {
            for (const auto& [id, e] : client->Replication()->Entities()) {
                World& w = client->GetWorld();
                const net::NetIdentity* ni = w.IsAlive(e) ? w.GetComponent<net::NetIdentity>(e) : nullptr;
                if (ni && ni->locally_owned && w.GetComponent<GhostTools>(e)) return e;
            }
            return kNullEntity;
        }
    };
    std::vector<std::unique_ptr<Player>> players;
    auto step = [&](int n) {
        for (int i = 0; i < n; ++i) {
            now += 1.0 / 30.0;
            server.Tick(now);
            traps.Update(world, now, haunting);
            cleaning.Update(world, now);
            haunting.Update(world, now, cleaning.Total() - cleaning.Remaining());
            for (auto& p : players) p->client->Tick(now);
        }
    };
    for (const char* name : {"Maya", "Felix"}) {
        auto p = std::make_unique<Player>();
        NetClientConfig cc;
        cc.server = net::Address::Loopback(server.Port()), cc.name = name, cc.duration = 1e9, cc.send_rpc = false;
        if (!p->client->Start(cc, now, &err)) return 2;
        players.push_back(std::move(p));
        for (int i = 0; i < 300 && players.back()->Pawn() == kNullEntity; ++i) step(1);
    }
    step(30);
    auto tool = [&](Player& p, const char* fn, std::vector<reflect::Any> args) {
        const Entity pawn = p.Pawn();
        GhostTools* gt = p.client->GetWorld().GetComponent<GhostTools>(pawn);
        const reflect::TypeInfo& type = reflect::Reflect<GhostTools>();
        RemoteCallResult r;
        CallFunction(p.client->GetWorld(), pawn, type, *type.FindFunction(fn), gt, args, nullptr, &r);
    };
    auto tools_of = [&](Player& p) { return p.client->GetWorld().GetComponent<GhostTools>(p.Pawn()); };
    Player& maya = *players[0];
    Player& felix = *players[1];

    haunting.SetIntensity(70.0f); // Dangerous: the ghost hunts
    step(2);
    tool(maya, "PlaceTrap", {f32{0.0f}, f32{-1.0f}});
    step(6);
    CHECK(TrapCount(world) == 1 && tools_of(maya)->placed == 1, "Maya places the trap (the client sees the verdict)");
    tool(maya, "ActivateTrap", {});
    step(6);
    CHECK(world.GetComponent<GhostBrain>(ghost)->state != static_cast<i32>(GhostState::Contained) && tools_of(maya)->failed >= 1,
          "activating too early fails: the ghost is not stunned");

    // Bait: Maya stays near the trap; the hunting ghost closes in. Felix stuns it once it is within the trap's radius.
    bool stunned = false;
    for (int t = 0; t < 30 * 20 && !stunned; ++t) {
        step(1);
        const Transform* g = world.GetComponent<Transform>(ghost);
        const f32 dz = g->position.z + 1.0f; // the trap is at (0, -1)
        if (g->position.x * g->position.x + dz * dz <= 3.0f * 3.0f && world.GetComponent<GhostBrain>(ghost)->state == static_cast<i32>(GhostState::Hunting)) {
            tool(felix, "StunGhost", {});
            step(6);
            stunned = world.GetComponent<GhostBrain>(ghost)->state == static_cast<i32>(GhostState::Stunned);
        }
    }
    CHECK(stunned, "the baited ghost is stunned by Felix once it is inside the trap radius");
    tool(maya, "ActivateTrap", {});
    step(10);
    CHECK(world.GetComponent<GhostBrain>(ghost)->state == static_cast<i32>(GhostState::Contained), "Maya activates the trap and the ghost is contained");
    step(10);
    i32 seen_state = -1, seen_trap = 0;
    players[1]->client->GetWorld().ForEach<GhostBrain>([&](GhostBrain& g) { seen_state = g.state; });
    players[1]->client->GetWorld().ForEach<GhostTrap>([&](GhostTrap&) { ++seen_trap; });
    CHECK(seen_state == static_cast<i32>(GhostState::Contained) && seen_trap == 1, "the other client sees the ghost contained and the trap (replicated)");
    CHECK(tools_of(maya)->captures == 1 && tools_of(felix)->stuns == 1, "the verdict counters replicate: one capture for Maya, one stun for Felix");
    step(30 * 5);
    CHECK(world.GetComponent<Haunting>(director)->intensity == 0.0f, "the haunting is over (intensity 0)");

    // Extraction needs both: every ghost contained AND every contamination cleaned.
    const auto ready = [&] { return cleaning.Completed() && HauntingSystem::AllContained(world); };
    CHECK(!ready(), "the team cannot extract yet: contamination remains");
    for (int round = 0; round < 900 && !cleaning.Completed(); ++round) {
        for (const Entity s : stains) if (world.IsAlive(s)) {
            const u32 id = world.GetComponent<net::NetIdentity>(s)->net_id;
            for (auto& p : players) {
                const Entity pawn = p->Pawn();
                GhostCleaner* gc = p->client->GetWorld().GetComponent<GhostCleaner>(pawn);
                const reflect::TypeInfo& type = reflect::Reflect<GhostCleaner>();
                std::vector<reflect::Any> args = {static_cast<i32>(id)};
                RemoteCallResult r;
                CallFunction(p->client->GetWorld(), pawn, type, *type.FindFunction("Clean"), gc, args, nullptr, &r);
            }
            break;
        }
        step(4);
    }
    step(10);
    CHECK(ready(), "once the ghost is contained and the property is clean, the team can extract");

    std::printf("%s (%d failed)\n", g_failed ? "FAILED" : "PASSED", g_failed);
    return g_failed ? 1 : 0;
}
