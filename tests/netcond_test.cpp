// Two clients on bad links (60 ms one-way latency + up to 20 ms jitter + 5% loss each way: ~120-160 ms RTT)
// against one server: prediction/reconciliation under impairment, and two clients cleaning the same targets.
#include "aether/net/replication.h"
#include "aether/server/dedicated_server.h"
#include "gs/cleaning.h"
#include "netem_relay.h"

#include <cmath>
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
    // All within 3 m of both pawns' spawn slots (x = 0 and x = 2).
    const Entity targets[3] = {AddContamination(world, {0.5f, 0, -1.0f}), AddContamination(world, {1.5f, 0, 1.0f}),
                               AddContamination(world, {1.0f, 0, -0.5f})};
    ServerConfig sc;
    sc.port = 0, sc.lan_advertise = false, sc.status_interval = 0;
    DedicatedServer server;
    std::string err;
    if (!server.Start(sc, world, &err)) { std::printf("server: %s\n", err.c_str()); return 2; }

    net::LinkConditions bad;
    bad.latency = 0.06, bad.jitter = 0.02, bad.loss = 0.05f;
    Rig rigs[2];
    const char* names[2] = {"Maya", "Felix"};
    for (int i = 0; i < 2; ++i) {
        if (!rigs[i].relay.Open(net::Address::Loopback(server.Port()), bad, 1234u + static_cast<u32>(i))) return 2;
        NetClientConfig cc;
        cc.server = rigs[i].relay.FrontAddress(), cc.name = names[i], cc.duration = 1e9, cc.send_rpc = false, cc.join_timeout = 30;
        if (!rigs[i].client->Start(cc, 0.0, &err)) { std::printf("client: %s\n", err.c_str()); return 2; }
    }

    ghostshift::CleaningConfig roomy;
    roomy.tank_capacity = 1000.0f;
    ghostshift::CleaningSystem cleaning(roomy);
    f64 now = 0;
    auto step = [&](int n) {
        for (int i = 0; i < n; ++i) {
            now += 1.0 / 30.0;
            for (Rig& r : rigs) r.relay.Pump(now);
            server.Tick(now);
            cleaning.Update(world, now);
            for (Rig& r : rigs) { r.client->Tick(now); r.relay.Pump(now); }
        }
    };
    std::unordered_map<Entity, u32> ids;
    auto clean = [&](Rig& r, Entity target) {
        const Entity p = r.Pawn();
        if (p == kNullEntity || ids.find(target) == ids.end()) return;
        ghostshift::GhostCleaner* gc = r.client->GetWorld().GetComponent<ghostshift::GhostCleaner>(p);
        const reflect::TypeInfo& type = reflect::Reflect<ghostshift::GhostCleaner>();
        std::vector<reflect::Any> args = {static_cast<i32>(ids[target])};
        RemoteCallResult res;
        CallFunction(r.client->GetWorld(), p, type, *type.FindFunction("Clean"), gc, args, nullptr, &res);
    };
    auto serverPawn = [&](int index) -> Transform* { // pawns are created in join order; find by x slot is unreliable, so use order
        std::vector<Transform*> all;
        world.ForEach<net::NetIdentity, ghostshift::GhostCleaner, Transform>([&](net::NetIdentity&, ghostshift::GhostCleaner&, Transform& t) { all.push_back(&t); });
        return index < static_cast<int>(all.size()) ? all[static_cast<usize>(index)] : nullptr;
    };

    // Joining over a lossy, laggy link.
    for (int i = 0; i < 300 && !(rigs[0].Pawn() != kNullEntity && rigs[1].Pawn() != kNullEntity); ++i) step(1);
    CHECK(rigs[0].Pawn() != kNullEntity && rigs[1].Pawn() != kNullEntity, "both clients join and see their pawns over a lossy, laggy link");
    step(30);
    for (const Entity e : targets) ids[e] = world.GetComponent<net::NetIdentity>(e)->net_id;
    CHECK(cleaning.Total() == 3, "server counted 3 contamination");

    // Prediction under impairment: Maya walks east 1 s, then west 1 s; Felix stands still.
    for (int i = 0; i < 30; ++i) { rigs[0].client->Movement()->Predict(rigs[0].Pawn(), 1.0f, 0.0f, false, 1.0f / 30.0f); step(1); }
    for (int i = 0; i < 30; ++i) { rigs[0].client->Movement()->Predict(rigs[0].Pawn(), -1.0f, 0.0f, false, 1.0f / 30.0f); step(1); }
    step(90); // settle: 3 s for the last acks and corrections to arrive
    const Transform* sp = serverPawn(0);
    const Transform* cp = rigs[0].client->GetWorld().GetComponent<Transform>(rigs[0].Pawn());
    const auto& ms = rigs[0].client->Movement()->GetStats();
    std::printf("info: Maya predicted %u inputs, %u acks, %u corrections, max error %.3f m\n", ms.predicted, ms.acks, ms.corrections, ms.max_error);
    CHECK(sp && cp && std::abs(sp->position.x - cp->position.x) < 0.05f && std::abs(sp->position.z - cp->position.z) < 0.05f,
          "after settling, Maya's predicted pawn and the server's agree within 5 cm despite loss");
    CHECK(sp && sp->position.x > -3.0f && sp->position.x < 3.0f, "the server moved Maya out and back (inputs survived 5% loss)");

    // Two clients clean the same targets at once. Work must be conserved: 3 targets x 15 accepted requests = 45, no more, no less.
    int rounds = 0;
    for (; rounds < 900 && !cleaning.Completed(); ++rounds) {
        for (int c = 0; c < 2; ++c) {
            Entity pick = kNullEntity; // both start from the first live target, so they collide
            for (const Entity e : targets) if (world.IsAlive(e)) { pick = e; break; }
            if (pick != kNullEntity) clean(rigs[c], pick);
        }
        step(4);
    }
    step(30);
    i32 accepted = 0, rejected = 0, requests = 0;
    world.ForEach<net::NetIdentity, ghostshift::GhostCleaner, Transform>([&](net::NetIdentity&, ghostshift::GhostCleaner& g, Transform&) {
        accepted += g.accepted, rejected += g.rejected, requests += g.requests;
    });
    std::printf("info: %d rounds, %d requests reached the server, %d accepted, %d rejected; relays dropped %llu/%llu and %llu/%llu packets\n",
                rounds, requests, accepted, rejected, (unsigned long long)rigs[0].relay.Dropped(),
                (unsigned long long)(rigs[0].relay.Dropped() + rigs[0].relay.Forwarded()), (unsigned long long)rigs[1].relay.Dropped(),
                (unsigned long long)(rigs[1].relay.Dropped() + rigs[1].relay.Forwarded()));
    CHECK(rigs[0].relay.Dropped() > 0 && rigs[1].relay.Dropped() > 0, "the links really dropped packets (the test is not running on a perfect link)");
    CHECK(cleaning.Completed() && cleaning.Remaining() == 0, "the contract completes with two clients cleaning at once");
    CHECK(!world.IsAlive(targets[0]) && !world.IsAlive(targets[1]) && !world.IsAlive(targets[2]), "all three contamination are gone");
    CHECK(accepted == 45, "work is conserved: exactly 45 accepted requests (no double credit, none lost)");

    std::printf("%s (%d failed)\n", g_failed ? "FAILED" : "PASSED", g_failed);
    return g_failed ? 1 : 0;
}
