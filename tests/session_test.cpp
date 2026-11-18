// Four clients, one vanishing mid-contract (no goodbye: a pulled cable), and a reconnect.
// Links: 40 ms one-way latency, up to 20 ms jitter, 3% loss each way.
#include "aether/net/replication.h"
#include "aether/server/dedicated_server.h"
#include "gs/cleaning.h"
#include "netem_relay.h"

#include <cstdio>
#include <memory>
#include <string>
#include <unordered_map>

using namespace aether;
using namespace aether::server;

static int g_failed = 0;
#define CHECK(c, msg) do { if (!(c)) { std::printf("FAIL: %s\n", msg); ++g_failed; } else std::printf("ok:   %s\n", msg); } while (0)

static Entity AddContamination(World& w, Vec3 p) {
    net::NetIdentity ni;
    net::SetNetArchetype(ni, kSceneArchetype);
    return w.CreateEntity(ni, Transform{p, Quaternion{}}, Tags{{"Contamination"}});
}

struct Rig {
    std::unique_ptr<NetClient> client = std::make_unique<NetClient>();
    gs_test::NetemRelay relay;
    bool alive = true; // false: the cable is pulled, nothing is ticked or relayed any more
    Entity Pawn() {
        for (const auto& [id, e] : client->Replication()->Entities()) {
            World& w = client->GetWorld();
            const net::NetIdentity* ni = w.IsAlive(e) ? w.GetComponent<net::NetIdentity>(e) : nullptr;
            if (ni && ni->locally_owned && w.GetComponent<ghostshift::GhostCleaner>(e)) return e;
        }
        return kNullEntity;
    }
};

int main() {
    RegisterServerComponents();
    World world;
    ghostshift::CleaningConfig cfg;
    cfg.tank_capacity = 1000.0f;
    cfg.range = 5.0f; // the four pawns spawn 2 m apart (x = 0, 2, 4, 6); the targets sit in the middle
    std::vector<Entity> targets;
    for (const Vec3 p : {Vec3{3.0f, 0, -1.0f}, Vec3{2.0f, 0, 1.0f}, Vec3{4.0f, 0, 0.5f}, Vec3{3.0f, 0, 2.0f}}) targets.push_back(AddContamination(world, p));

    ServerConfig sc;
    sc.port = 0, sc.lan_advertise = false, sc.status_interval = 0;
    DedicatedServer server;
    std::string err;
    if (!server.Start(sc, world, &err)) { std::printf("server: %s\n", err.c_str()); return 2; }

    net::LinkConditions link;
    link.latency = 0.04, link.jitter = 0.02, link.loss = 0.03f;
    std::vector<std::unique_ptr<Rig>> rigs;
    f64 now = 0;
    auto join = [&](const char* name, u32 seed) {
        auto r = std::make_unique<Rig>();
        if (!r->relay.Open(net::Address::Loopback(server.Port()), link, seed)) return std::unique_ptr<Rig>();
        NetClientConfig cc;
        cc.server = r->relay.FrontAddress(), cc.name = name, cc.duration = 1e9, cc.send_rpc = false, cc.join_timeout = 30;
        if (!r->client->Start(cc, now, &err)) { std::printf("client: %s\n", err.c_str()); return std::unique_ptr<Rig>(); }
        return r;
    };

    ghostshift::CleaningSystem cleaning(cfg);
    auto step = [&](int n) {
        for (int i = 0; i < n; ++i) {
            now += 1.0 / 30.0;
            for (auto& r : rigs) if (r->alive) r->relay.Pump(now);
            server.Tick(now);
            cleaning.Update(world, now);
            for (auto& r : rigs) if (r->alive) { r->client->Tick(now); r->relay.Pump(now); }
        }
    };
    std::unordered_map<Entity, u32> ids;
    auto idOf = [&](Entity e) { if (ids.find(e) == ids.end()) ids[e] = world.GetComponent<net::NetIdentity>(e)->net_id; return ids[e]; };
    auto clean = [&](Rig& r, u32 id) {
        const Entity p = r.Pawn();
        if (p == kNullEntity) return;
        ghostshift::GhostCleaner* gc = r.client->GetWorld().GetComponent<ghostshift::GhostCleaner>(p);
        const reflect::TypeInfo& type = reflect::Reflect<ghostshift::GhostCleaner>();
        std::vector<reflect::Any> args = {static_cast<i32>(id)};
        RemoteCallResult res;
        CallFunction(r.client->GetWorld(), p, type, *type.FindFunction("Clean"), gc, args, nullptr, &res);
    };
    auto firstLive = [&]() -> Entity { for (const Entity e : targets) if (world.IsAlive(e)) return e; return kNullEntity; };

    // 1. Four clients join.
    const char* names[4] = {"Maya", "Felix", "Nora", "Buster"};
    for (int i = 0; i < 4; ++i) { rigs.push_back(join(names[i], 500u + static_cast<u32>(i))); if (!rigs.back()) return 2; }
    for (int i = 0; i < 600 && server.Stats().players < 4; ++i) step(1);
    for (int i = 0; i < 90; ++i) step(1);
    bool all_pawns = true;
    for (auto& r : rigs) all_pawns = all_pawns && r->Pawn() != kNullEntity;
    CHECK(server.Stats().players == 4 && all_pawns, "four players join and each sees its own pawn");
    for (const Entity e : targets) idOf(e);
    CHECK(cleaning.Total() == 4, "server counted 4 contamination");

    // 2. All four clean; after 20 accepted requests the second player's cable is pulled.
    bool pulled = false;
    for (int round = 0; round < 1500 && !cleaning.Completed(); ++round) {
        const Entity pick = firstLive();
        if (pick != kNullEntity) for (auto& r : rigs) if (r->alive) clean(*r, idOf(pick));
        step(4);
        if (!pulled && cleaning.Accepted() >= 20) {
            rigs[1]->alive = false; // Felix vanishes: no Leave, no more packets
            pulled = true;
            std::printf("info: Felix's cable pulled at t=%.1f s after %u accepted requests\n", now, cleaning.Accepted());
        }
    }
    step(30);
    CHECK(pulled, "a player vanished in the middle of the contract");
    CHECK(cleaning.Completed() && cleaning.Remaining() == 0, "the other three finished the contract");
    CHECK(cleaning.Accepted() == 60, "work is conserved across the disconnect: exactly 60 accepted requests (4 x 15)");

    // 3. The server notices the silence and removes the pawn.
    for (int i = 0; i < 600 && server.Stats().players > 3; ++i) step(1);
    CHECK(server.Stats().players == 3, "the server timed the vanished player out (3 players left)");
    std::printf("info: players=%u peak=%u joins=%llu leaves=%llu\n", server.Stats().players, server.Stats().peak_players,
                (unsigned long long)server.Stats().joins, (unsigned long long)server.Stats().leaves);

    // 4. Felix comes back (a new connection with the same name); new contamination appears (a Caretaker re-dirtying the room).
    rigs.push_back(join("Felix", 900u));
    if (!rigs.back()) return 2;
    for (int i = 0; i < 600 && rigs.back()->Pawn() == kNullEntity; ++i) step(1);
    step(30);
    CHECK(rigs.back()->Pawn() != kNullEntity && server.Stats().players == 4, "the same player rejoins on a new connection and gets a new pawn");
    const Entity fresh = AddContamination(world, {3.0f, 0, 0.0f});
    step(10);
    CHECK(cleaning.Remaining() == 1 && cleaning.Total() == 5, "contamination that appears after the contract started is registered");
    const u32 before = cleaning.Accepted();
    for (int round = 0; round < 600 && world.IsAlive(fresh); ++round) { clean(*rigs.back(), idOf(fresh)); step(4); }
    step(20);
    CHECK(!world.IsAlive(fresh) && cleaning.Completed(), "the rejoined player cleans it and the contract completes again");
    CHECK(cleaning.Accepted() - before == 15, "exactly 15 accepted requests for the new contamination");

    std::printf("%s (%d failed)\n", g_failed ? "FAILED" : "PASSED", g_failed);
    return g_failed ? 1 : 0;
}
