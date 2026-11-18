// A scripted headless player for the assembled server: joins, walks, cleans the three stains of the Willow House scene by
// position (clients only see replicated transforms, not tags), then extracts. Real time, real UDP.
// usage: ghostshift_playtest <host:port> [name] [--full]   prints GHOSTSHIFT_PLAYTEST {json}; exit 0 when the contract finished and paid.
// --full also contains the ghost first (place the trap beside it, stun it, activate), so the contract completes instead of being a retreat.
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
    bool full = false;
    for (int i = 2; i < argc; ++i) if (std::string(argv[i]) == "--full") full = true;
    cc.server = *addr, cc.name = argc > 2 && std::string(argv[2]) != "--full" ? argv[2] : "Maya", cc.duration = 1e9, cc.send_rpc = false, cc.join_timeout = 15;
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
    const std::vector<Vec3> spots = {{-2.4f, 0, -0.5f}, {2.6f, 0, 0.8f}, {1.8f, 0, -3.0f}, {-4.0f, 0, -2.0f}, {4.0f, 0, -1.8f},
                                     {-3.2f, 0, 3.0f}, {3.2f, 0, 3.4f}, {0.0f, 0, -4.5f}, {-5.0f, 0, 0.8f},
                                     {-1.5f, 0, 2.0f}, {1.0f, 0, 3.8f}, {4.8f, 0, 1.6f}};
    // Which tool each spot needs (the scene's "Kind:" tags): ectoplasm = vacuum, cursed stains = UV scrubber, dust = sprayer.
    auto tool_for = [&](const Vec3& p) -> i32 {
        auto is = [&](float x, float z) { return std::fabs(p.x - x) < 0.01f && std::fabs(p.z - z) < 0.01f; };
        if (is(1.8f, -3.0f) || is(-4.0f, -2.0f) || is(-3.2f, 3.0f) || is(-5.0f, 0.8f)) return 0; // ectoplasm
        if (is(-1.5f, 2.0f) || is(1.0f, 3.8f) || is(4.8f, 1.6f)) return 2;                          // dust
        return 1;                                                                                    // cursed stains
    };
    const Vec3 van = {-3.5f, 0, 4.7f}; // where the tank is emptied
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
    int trips = 0, switches = 0; // times a tool was serviced at the van / switched
    // A real client sends an input every frame, including "no movement" (the movement rule decelerates only when it is stepped).
    auto stand = [&](int frames) { for (int i = 0; i < frames; ++i) { client.Movement()->Predict(pawn(), 0.0f, 0.0f, false, 1.0f / 30.0f); frame(); } };
    auto position = [&]() { return w.GetComponent<Transform>(pawn())->position; };
    // Steer toward (x, z) until within `radius` (at most ~8 s), then stop.
    auto walk_to = [&](const Vec3& to, f32 radius) {
        for (int i = 0; i < 30 * 8; ++i) {
            const Vec3 p = position();
            const f32 dx = to.x - p.x, dz = to.z - p.z, d = std::sqrt(dx * dx + dz * dz);
            if (d < radius) break;
            client.Movement()->Predict(pawn(), dx / d, dz / d, false, 1.0f / 30.0f);
            frame();
        }
        stand(12);
    };
    // Scrub one stain until it is gone. Returns false if the tank filled first (the caller goes to the van and comes back).
    auto clean_spot = [&](const Vec3& spot) {
        for (int i = 0; i < 90; ++i) { // up to ~12 s per stain (4 frames per request)
            const Entity target = find(spot);
            if (target == kNullEntity) return true; // gone: the server removed it
            GhostCleaner* gc = w.GetComponent<GhostCleaner>(pawn());
            if (gc->tank_full) return false;
            call(reflect::Reflect<GhostCleaner>(), "Clean", gc, {static_cast<i32>(w.GetComponent<net::NetIdentity>(target)->net_id)});
            for (int k = 0; k < 4; ++k) frame();
        }
        return find(spot) == kNullEntity;
    };
    auto empty_at_van = [&]() {
        walk_to(van, 1.5f);
        GhostCleaner* gc = w.GetComponent<GhostCleaner>(pawn());
        call(reflect::Reflect<GhostCleaner>(), "EmptyTank", gc, {});
        stand(12);
        trips++;
        say("emptied the tank");
    };

    // --full: contain the ghost first. The Prankster starts about 4.2 m from the spawn point: place the trap beside it (within 3 m of
    // us); a Dormant ghost cannot be stunned, so wait (standing by the trap) until the haunting wakes it and it comes to us; stun it
    // (within 5 m of us); activate (the stunned ghost is inside the trap's 3 m radius).
    i32 ghost_state = -1;
    auto ghost_now = [&]() { i32 st = -1; w.ForEach<GhostBrain>([&](GhostBrain& g) { st = g.state; }); return st; };
    if (full) {
        GhostTools* gt = w.GetComponent<GhostTools>(pawn());
        const reflect::TypeInfo& tt = reflect::Reflect<GhostTools>();
        walk_to({-0.9f, 0, -1.0f}, 0.6f); // the spawn point is by the van: walk into the room, within 3 m of where the trap goes
        call(tt, "PlaceTrap", gt, {f32{-1.2f}, f32{-2.5f}});
        const f64 waiting_since = seconds();
        for (int i = 0; i < 30 * 60 && ghost_now() < 1 && seconds() - waiting_since < 60.0; ++i) stand(1);
        std::fprintf(stderr, "[%.1fs] the ghost woke after %.1f s (state %d)\n", seconds(), seconds() - waiting_since, ghost_now());
        stand(45); // let it settle beside the trap
        call(tt, "StunGhost", gt, {});
        stand(12);
        say("ghost stunned?");
        call(tt, "ActivateTrap", gt, {});
        stand(20);
        say("trap activated");
    }

    // Clean all nine: always the nearest one next; empty the tank at the van whenever it fills.
    std::vector<Vec3> pending = spots;
    int cleaned = 0;
    for (int guard = 0; guard < 60 && !pending.empty(); ++guard) {
        usize best = 0;
        f32 best_d = 1.0e30f;
        const Vec3 p = position();
        for (usize i = 0; i < pending.size(); ++i) {
            const f32 d = (pending[i].x - p.x) * (pending[i].x - p.x) + (pending[i].z - p.z) * (pending[i].z - p.z);
            if (d < best_d) best_d = d, best = i;
        }
        walk_to(pending[best], 2.0f);
        if (w.GetComponent<GhostCleaner>(pawn())->tool != tool_for(pending[best])) {
            GhostCleaner* gc = w.GetComponent<GhostCleaner>(pawn());
            call(reflect::Reflect<GhostCleaner>(), "SelectTool", gc, {static_cast<i32>(tool_for(pending[best]))});
            stand(6);
            ++switches;
        }
        if (clean_spot(pending[best])) { ++cleaned; pending.erase(pending.begin() + static_cast<std::ptrdiff_t>(best)); }
        else empty_at_van(); // the tank filled: go and empty it, then come back to the same stain
    }
    for (int i = 0; i < 30; ++i) frame();
    say("cleaning done");
    GhostTools* tools = w.GetComponent<GhostTools>(pawn());
    call(reflect::Reflect<GhostTools>(), "Extract", tools, {});
    for (int i = 0; i < 60; ++i) frame();
    ContractResult result;
    w.ForEach<ContractResult>([&](ContractResult& r) { result = r; });
    const GhostCleaner* gc = w.GetComponent<GhostCleaner>(pawn());
    ghost_state = ghost_now();
    std::printf("GHOSTSHIFT_PLAYTEST {\"switches\":%d,\"trips\":%d,\"ghost_state\":%d,\"cleaned\":%d,\"state\":%d,\"grade\":%d,\"payout\":%d,\"cleaned_percent\":%d,\"accepted\":%d,\"rejected\":%d}\n",
                switches, trips, ghost_state, cleaned, result.state, result.grade, result.payout, result.cleaned_percent, gc ? gc->accepted : -1, gc ? gc->rejected : -1);
    return result.state != 0 && result.payout > 0 ? 0 : 1;
}
