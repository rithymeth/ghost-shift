// A scripted headless player for the assembled server: joins, walks, cleans the three stains of the Willow House scene by
// position (clients only see replicated transforms, not tags), then extracts. Real time, real UDP.
// usage: ghostshift_playtest <host:port> [name]      prints GHOSTSHIFT_PLAYTEST {json}; exit 0 when the contract finished and paid.
#include "aether/net/replication.h"
#include "aether/server/dedicated_server.h"
#include "gs/cleaning.h"
#include "gs/contract.h"
#include "gs/haunting.h"
#include "gs/trap.h"

#include <chrono>
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using namespace aether;
using namespace aether::server;
using namespace ghostshift;
using Clock = std::chrono::steady_clock;

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: ghostshift_playtest <host:port> [name]\n"); return 2; }
    RegisterServerComponents();
    const auto addr = net::Address::Parse(argv[1]);
    if (!addr) { std::fprintf(stderr, "bad address\n"); return 2; }
    NetClientConfig cc;
    cc.server = *addr, cc.name = argc > 2 ? argv[2] : "Maya", cc.duration = 1e9, cc.send_rpc = false, cc.join_timeout = 15;
    NetClient client;
    std::string err;
    const auto t0 = Clock::now();
    auto seconds = [&] { return std::chrono::duration<f64>(Clock::now() - t0).count(); };
    if (!client.Start(cc, 0.0, &err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 2; }
    f64 next_tick = 0;
    auto frame = [&](f64 dt = 1.0 / 30.0) { // one 30 Hz frame, paced in real time
        next_tick += dt;
        std::this_thread::sleep_until(t0 + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<f64>(next_tick)));
        client.Tick(seconds());
    };
    World& w = client.GetWorld();
    auto pawn = [&]() -> Entity {
        for (const auto& [id, e] : client.Replication()->Entities()) {
            const net::NetIdentity* ni = w.IsAlive(e) ? w.GetComponent<net::NetIdentity>(e) : nullptr;
            if (ni && ni->locally_owned && w.GetComponent<GhostCleaner>(e)) return e;
        }
        return kNullEntity;
    };
    const bool debug = std::getenv("GS_DEBUG") != nullptr;
    auto say = [&](const char* m) {
        if (!debug) return;
        const auto& ms = client.Movement()->GetStats();
        std::fprintf(stderr, "[%.1fs] %s (entities %zu; predicted %u, acks %u, corrections %u, max error %.2f m)\n", seconds(), m,
                     client.Replication()->EntityCount(), ms.predicted, ms.acks, ms.corrections, ms.max_error);
    };
    say("joining");
    for (int i = 0; i < 30 * 15 && pawn() == kNullEntity; ++i) frame();
    say("have pawn");
    if (pawn() == kNullEntity) { std::printf("GHOSTSHIFT_PLAYTEST {\"error\":\"no pawn\"}\n"); return 1; }
    for (int i = 0; i < 30; ++i) frame();

    // The stains (from tools/build_lookdev_scene.py), found among the replicated scene entities by position.
    const Vec3 spots[3] = {{-2.4f, 0, -0.5f}, {2.6f, 0, 0.8f}, {1.8f, 0, -3.0f}};
    auto find = [&](const Vec3& p) -> Entity {
        for (const auto& [id, e] : client.Replication()->Entities()) {
            const Transform* t = w.IsAlive(e) ? w.GetComponent<Transform>(e) : nullptr;
            const net::NetIdentity* ni = w.IsAlive(e) ? w.GetComponent<net::NetIdentity>(e) : nullptr;
            if (t && ni && std::string(ni->archetype) == "scene" && std::fabs(t->position.x - p.x) < 0.05f && std::fabs(t->position.z - p.z) < 0.05f &&
                std::fabs(t->position.y) < 0.05f) return e;
        }
        return kNullEntity;
    };
    auto call = [&](const reflect::TypeInfo& type, const char* fn, void* comp, std::vector<reflect::Any> args) {
        RemoteCallResult r;
        CallFunction(w, pawn(), type, *type.FindFunction(fn), comp, args, nullptr, &r);
    };
    // From the spawn point only the two near stains are in reach (3 m); the ectoplasm is 3.5 m away. So: walk to the ectoplasm
    // (about 1.5 m north-east), clean it, walk back, then clean the other two.
    auto clean_spot = [&](const Vec3& spot, int& done) {
        for (int i = 0; i < 90; ++i) { // up to ~12 s per stain (4 frames per request)
            const Entity target = find(spot);
            if (target == kNullEntity) { ++done; return; } // gone: the server removed it
            GhostCleaner* gc = w.GetComponent<GhostCleaner>(pawn());
            call(reflect::Reflect<GhostCleaner>(), "Clean", gc, {static_cast<i32>(w.GetComponent<net::NetIdentity>(target)->net_id)});
            for (int k = 0; k < 4; ++k) frame();
            if (debug && i % 30 == 0) {
                const Transform* pt = w.GetComponent<Transform>(pawn());
                std::fprintf(stderr, "  request %d: pawn (%.2f, %.2f) -> stain (%.2f, %.2f) | requests %d accepted %d rejected %d\n", i, pt->position.x,
                             pt->position.z, spot.x, spot.z, gc->requests, gc->accepted, gc->rejected);
            }
        }
    };
    // A real client sends an input every frame, including "no movement" (the movement rule decelerates only when it is stepped).
    auto stand = [&](int frames) { for (int i = 0; i < frames; ++i) { client.Movement()->Predict(pawn(), 0.0f, 0.0f, false, 1.0f / 30.0f); frame(); } };
    int cleaned = 0;
    for (int i = 0; i < 20; ++i) { client.Movement()->Predict(pawn(), 0.5f, -0.866f, false, 1.0f / 30.0f); frame(); }
    stand(30);
    say("at the ectoplasm");
    clean_spot(spots[2], cleaned);
    for (int i = 0; i < 20; ++i) { client.Movement()->Predict(pawn(), -0.5f, 0.866f, false, 1.0f / 30.0f); frame(); }
    stand(30);
    say("back at the middle");
    clean_spot(spots[0], cleaned);
    clean_spot(spots[1], cleaned);
    for (int i = 0; i < 30; ++i) frame();
    say("cleaning done");
    GhostTools* tools = w.GetComponent<GhostTools>(pawn());
    call(reflect::Reflect<GhostTools>(), "Extract", tools, {});
    for (int i = 0; i < 60; ++i) frame();
    ContractResult result;
    w.ForEach<ContractResult>([&](ContractResult& r) { result = r; });
    const GhostCleaner* gc = w.GetComponent<GhostCleaner>(pawn());
    std::printf("GHOSTSHIFT_PLAYTEST {\"cleaned\":%d,\"state\":%d,\"grade\":%d,\"payout\":%d,\"cleaned_percent\":%d,\"accepted\":%d,\"rejected\":%d}\n",
                cleaned, result.state, result.grade, result.payout, result.cleaned_percent, gc ? gc->accepted : -1, gc ? gc->rejected : -1);
    return result.state != 0 && result.payout > 0 ? 0 : 1;
}
