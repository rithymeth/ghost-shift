// Spawn points: each joiner takes the first point nobody is standing at; a leaver's point is reused; with every point taken the
// engine's old rule (x = number of players x 2) still applies. Real server and clients over loopback UDP.
#include "aether/net/replication.h"
#include "aether/server/dedicated_server.h"
#include "gs/cleaning.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace aether;
using namespace aether::server;
using namespace ghostshift;

static int g_failed = 0;
#define CHECK(c, msg) do { if (!(c)) { std::printf("FAIL: %s\n", msg); ++g_failed; } else std::printf("ok:   %s\n", msg); } while (0)

int main() {
    RegisterServerComponents();
    World world;
    ServerConfig sc;
    sc.port = 0, sc.lan_advertise = false, sc.status_interval = 0;
    DedicatedServer server;
    const std::vector<Vec3> points = {{0.0f, 0, 3.2f}, {1.4f, 0, 3.2f}, {-1.4f, 0, 2.2f}};
    server.SetSpawnPoints(points);
    std::string err;
    if (!server.Start(sc, world, &err)) { std::printf("server: %s\n", err.c_str()); return 2; }
    f64 now = 0;
    std::vector<std::unique_ptr<NetClient>> clients;
    auto step = [&](int n) { for (int i = 0; i < n; ++i) { now += 1.0 / 30.0; server.Tick(now); for (auto& c : clients) if (!c->Done()) c->Tick(now); } };
    auto join = [&](const char* name, f64 stay) {
        auto c = std::make_unique<NetClient>();
        NetClientConfig cc;
        cc.server = net::Address::Loopback(server.Port()), cc.name = name, cc.duration = stay, cc.send_rpc = false;
        if (!c->Start(cc, now, &err)) { std::printf("client: %s\n", err.c_str()); std::exit(2); }
        clients.push_back(std::move(c));
        step(60);
    };
    auto pawns = [&]() { // server-side pawn positions, in creation order
        std::vector<Vec3> out;
        world.ForEach<GhostCleaner, Transform>([&](GhostCleaner&, Transform& t) { out.push_back(t.position); });
        return out;
    };
    auto same = [](const Vec3& a, const Vec3& b) { return std::fabs(a.x - b.x) < 0.01f && std::fabs(a.z - b.z) < 0.01f; };

    join("Maya", 1e9);
    join("Felix", 8.0); // leaves 8 s after joining: after the next two have joined
    join("Nora", 1e9);
    auto p = pawns();
    CHECK(p.size() == 3 && same(p[0], points[0]) && same(p[1], points[1]) && same(p[2], points[2]), "three joiners take the three spawn points in order");
    join("Buster", 1e9); // every point is taken: the engine's old rule applies
    p = pawns();
    CHECK(p.size() == 4 && same(p[3], Vec3{6.0f, 0, 0}), "with every point taken the fourth player falls back to the engine's rule (x = 3 x 2, z = 0)");

    // Felix leaves (8 s after his join): his point is free again and the next joiner takes it.
    step(200);
    p = pawns();
    CHECK(p.size() == 3, "a player who left is removed (3 pawns)");
    join("Boris", 1e9);
    p = pawns();
    bool took_felixs_point = false;
    for (const Vec3& v : p) took_felixs_point = took_felixs_point || same(v, points[1]);
    CHECK(p.size() == 4 && took_felixs_point, "the next joiner takes the point the leaver freed");
    std::printf("%s (%d failed)\n", g_failed ? "FAILED" : "PASSED", g_failed);
    return g_failed ? 1 : 0;
}
