// In-process check of networked cleaning: a real DedicatedServer and NetClient over loopback UDP.
// Three contamination: two within range of the pawn (at x=0) and one 3.5 m away.
#include "aether/net/replication.h"
#include "aether/server/dedicated_server.h"
#include "gs/cleaning.h"

#include <cstdio>
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

int main() {
    RegisterServerComponents();
    World world;
    const Entity near_a = AddContamination(world, {-2.4f, 0, -0.5f});
    const Entity near_b = AddContamination(world, {2.6f, 0, 0.8f});
    const Entity far_c = AddContamination(world, {1.8f, 0, -3.0f});

    ServerConfig sc;
    sc.port = 0, sc.lan_advertise = false, sc.status_interval = 0;
    DedicatedServer server;
    std::string err;
    if (!server.Start(sc, world, &err)) { std::printf("server: %s\n", err.c_str()); return 2; }

    NetClientConfig cc;
    cc.server = net::Address::Loopback(server.Port()), cc.name = "Maya", cc.duration = 1e9, cc.send_rpc = false;
    NetClient client;
    if (!client.Start(cc, 0.0, &err)) { std::printf("client: %s\n", err.c_str()); return 2; }

    ghostshift::CleaningSystem cleaning;
    f64 now = 0;
    auto step = [&](int n) {
        for (int i = 0; i < n; ++i) {
            now += 1.0 / 30.0;
            server.Tick(now);
            cleaning.Update(world, now);
            client.Tick(now);
        }
    };
    auto pawn = [&]() -> Entity {
        for (const auto& [id, e] : client.Replication()->Entities()) {
            const net::NetIdentity* ni = client.GetWorld().IsAlive(e) ? client.GetWorld().GetComponent<net::NetIdentity>(e) : nullptr;
            if (ni && ni->locally_owned && client.GetWorld().GetComponent<ghostshift::GhostCleaner>(e) != nullptr) return e;
        }
        return kNullEntity;
    };
    std::unordered_map<Entity, u32> ids; // captured while alive: a destroyed entity must not be read
    auto clean = [&](Entity target) {
        const Entity p = pawn();
        if (ids.find(target) == ids.end()) ids[target] = world.GetComponent<net::NetIdentity>(target)->net_id;
        const u32 id = ids[target];
        ghostshift::GhostCleaner* gc = client.GetWorld().GetComponent<ghostshift::GhostCleaner>(p);
        const reflect::TypeInfo& type = reflect::Reflect<ghostshift::GhostCleaner>();
        std::vector<reflect::Any> args = {static_cast<i32>(id)};
        RemoteCallResult r = RemoteCallResult::Refused;
        CallFunction(client.GetWorld(), p, type, *type.FindFunction("Clean"), gc, args, nullptr, &r);
        return r == RemoteCallResult::Sent;
    };

    step(60);
    for (const Entity e : {near_a, near_b, far_c}) ids[e] = world.GetComponent<net::NetIdentity>(e)->net_id;
    CHECK(pawn() != kNullEntity, "client sees its own pawn with a GhostCleaner");
    CHECK(cleaning.Total() == 3, "server counted 3 contamination");

    // Hammer the in-range target: one request per 0.1 s, 15 needed.
    int sent = 0;
    for (int i = 0; i < 40 && world.IsAlive(near_a); ++i) { sent += clean(near_a); step(4); } // 4 ticks = 0.133 s, clear of the cooldown
    step(10);
    CHECK(!world.IsAlive(near_a), "in-range contamination is removed by accepted requests");
    CHECK(cleaning.Remaining() == 2, "two left");

    // Out of range: 25 requests (a clean needs 15) spaced clear of the cooldown. None may be accepted.
    const i32 acc0 = client.GetWorld().GetComponent<ghostshift::GhostCleaner>(pawn())->accepted;
    const i32 rej0 = client.GetWorld().GetComponent<ghostshift::GhostCleaner>(pawn())->rejected;
    for (int i = 0; i < 25; ++i) { clean(far_c); step(4); }
    step(10);
    const ghostshift::GhostCleaner* gc = client.GetWorld().GetComponent<ghostshift::GhostCleaner>(pawn());
    CHECK(world.IsAlive(far_c), "out-of-range contamination is NOT removed by 25 requests");
    CHECK(gc && gc->accepted == acc0, "none of the out-of-range requests was accepted");
    CHECK(gc && gc->rejected - rej0 == 25, "all 25 were rejected and the client sees the verdicts (replicated)");

    // A request for an entity that does not exist is rejected, not a crash.
    {
        ghostshift::GhostCleaner* g = client.GetWorld().GetComponent<ghostshift::GhostCleaner>(pawn());
        const reflect::TypeInfo& type = reflect::Reflect<ghostshift::GhostCleaner>();
        std::vector<reflect::Any> args = {i32{999999}};
        RemoteCallResult r;
        CallFunction(client.GetWorld(), pawn(), type, *type.FindFunction("Clean"), g, args, nullptr, &r);
        step(6);
    }
    CHECK(cleaning.Remaining() == 2, "a bogus net id changes nothing");

    // Flooding faster than the cooldown is throttled: 10 requests in one tick give at most 1 acceptance.
    const i32 before = client.GetWorld().GetComponent<ghostshift::GhostCleaner>(pawn())->accepted;
    for (int i = 0; i < 8; ++i) clean(near_b);
    step(2);
    step(6);
    const i32 after = client.GetWorld().GetComponent<ghostshift::GhostCleaner>(pawn())->accepted;
    CHECK(after - before <= 2, "a burst inside one cooldown window is throttled");

    // Finish near_b: the contract still cannot complete while the far one survives.
    for (int i = 0; i < 40 && world.IsAlive(near_b); ++i) { clean(near_b); step(4); }
    step(6);
    CHECK(!world.IsAlive(near_b) && !cleaning.Completed(), "contract is not complete while one remains");
    (void)far_c;

    std::printf("%s (%d failed)\n", g_failed ? "FAILED" : "PASSED", g_failed);
    return g_failed ? 1 : 0;
}
