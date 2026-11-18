// Haunting intensity, ghost states and counterplay, server-authoritative, with real clients over loopback UDP.
#include "aether/net/replication.h"
#include "aether/server/dedicated_server.h"
#include "gs/cleaning.h"
#include "gs/haunting.h"

#include <cstdio>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

using namespace aether;
using namespace aether::server;
using namespace ghostshift;

static int g_failed = 0;
#define CHECK(c, msg) do { if (!(c)) { std::printf("FAIL: %s\n", msg); ++g_failed; } else std::printf("ok:   %s\n", msg); } while (0)

static Entity Spawn(World& w, const char* archetype, Vec3 p, bool contamination, auto... extra) {
    net::NetIdentity ni;
    net::SetNetArchetype(ni, archetype);
    Entity e = w.CreateEntity(ni, Transform{p, Quaternion{}}, extra...);
    if (contamination) AddTag(w, e, "Contamination");
    return e;
}

struct Player {
    std::unique_ptr<NetClient> client = std::make_unique<NetClient>();
    Entity Pawn() {
        for (const auto& [id, e] : client->Replication()->Entities()) {
            World& w = client->GetWorld();
            const net::NetIdentity* ni = w.IsAlive(e) ? w.GetComponent<net::NetIdentity>(e) : nullptr;
            if (ni && ni->locally_owned && w.GetComponent<GhostCleaner>(e)) return e;
        }
        return kNullEntity;
    }
};

int main() {
    RegisterServerComponents();
    World world;
    const Entity director = Spawn(world, "director", {0, 0, 0}, false, Haunting{});
    const Entity ghost = Spawn(world, "ghost", {0.0f, 0, -8.0f}, false, GhostBrain{});
    std::vector<Entity> stains;
    for (const Vec3 p : {Vec3{1.0f, 0, -0.5f}, Vec3{2.0f, 0, 1.0f}, Vec3{1.5f, 0, 0.5f}}) stains.push_back(Spawn(world, "scene", p, true));

    ServerConfig sc;
    sc.port = 0, sc.lan_advertise = false, sc.status_interval = 0;
    DedicatedServer server;
    std::string err;
    if (!server.Start(sc, world, &err)) { std::printf("server: %s\n", err.c_str()); return 2; }
    CleaningConfig roomy;
    roomy.tank_capacity = 1000.0f;
    CleaningSystem cleaning(roomy);
    HauntingSystem haunting;
    f64 now = 0;
    std::vector<std::unique_ptr<Player>> players;
    auto step = [&](int n) {
        for (int i = 0; i < n; ++i) {
            now += 1.0 / 30.0;
            server.Tick(now);
            cleaning.Update(world, now);
            haunting.Update(world, now, cleaning.Total() - cleaning.Remaining());
            for (auto& p : players) p->client->Tick(now);
        }
    };
    auto join = [&](const char* name) {
        auto p = std::make_unique<Player>();
        NetClientConfig cc;
        cc.server = net::Address::Loopback(server.Port()), cc.name = name, cc.duration = 1e9, cc.send_rpc = false;
        if (!p->client->Start(cc, now, &err)) { std::printf("client: %s\n", err.c_str()); std::exit(2); }
        players.push_back(std::move(p));
        for (int i = 0; i < 300 && players.back()->Pawn() == kNullEntity; ++i) step(1);
    };
    std::unordered_map<Entity, u32> ids;
    auto clean = [&](Player& p, Entity target) {
        const Entity pawn = p.Pawn();
        if (pawn == kNullEntity || !world.IsAlive(target)) return;
        if (ids.find(target) == ids.end()) ids[target] = world.GetComponent<net::NetIdentity>(target)->net_id;
        GhostCleaner* gc = p.client->GetWorld().GetComponent<GhostCleaner>(pawn);
        const reflect::TypeInfo& type = reflect::Reflect<GhostCleaner>();
        std::vector<reflect::Any> args = {static_cast<i32>(ids[target])};
        RemoteCallResult r;
        CallFunction(p.client->GetWorld(), pawn, type, *type.FindFunction("Clean"), gc, args, nullptr, &r);
    };
    const auto brain = [&]() -> const GhostBrain& { return *world.GetComponent<GhostBrain>(ghost); };
    const auto haunt = [&]() -> const Haunting& { return *world.GetComponent<Haunting>(director); };

    // A. Nobody in the property: nothing rises.
    step(300);
    CHECK(haunt().intensity == 0.0f && brain().state == static_cast<i32>(GhostState::Dormant), "with nobody in the property the haunting stays Quiet and the ghost Dormant");

    // B. Two players join; nobody cleans. The haunting escalates through every stage, in order.
    join("Maya");
    join("Felix");
    for (const Entity s : stains) world.GetComponent<net::NetIdentity>(s); // ids are assigned by the first server tick
    std::vector<i32> stages_seen = {haunt().stage}, states_seen = {brain().state};
    i32 first_attack_tick = -1, pranks_before_hunt = 0;
    for (int t = 0; t < 30 * 140 && brain().attacks == 0; ++t) {
        step(1);
        if (haunt().stage != stages_seen.back()) stages_seen.push_back(haunt().stage);
        if (brain().state != states_seen.back()) states_seen.push_back(brain().state);
        if (brain().state == static_cast<i32>(GhostState::Pranking)) pranks_before_hunt = brain().pranks;
        if (brain().attacks > 0 && first_attack_tick < 0) first_attack_tick = t;
    }
    std::printf("info: stages seen:"); for (i32 s : stages_seen) std::printf(" %d", s);
    std::printf(" | ghost states seen:"); for (i32 s : states_seen) std::printf(" %d", s);
    std::printf(" | first attack at t=%.0f s, intensity %.1f\n", now, haunt().intensity);
    CHECK(stages_seen == std::vector<i32>({0, 1, 2, 3}), "intensity climbs Quiet, Stirring, Active, Dangerous in order");
    CHECK(states_seen == std::vector<i32>({0, 1, 2, 3}), "the ghost follows: Dormant, Investigating, Pranking, Hunting");
    CHECK(pranks_before_hunt > 0, "the ghost pranked while Active");
    CHECK(brain().attacks >= 1, "a hunting ghost reached a player and incapacitated them");

    // Replication: a client sees the stage, the ghost's state and who is down.
    step(10);
    const World& cw = players[0]->client->GetWorld();
    i32 seen_stage = -1, seen_state = -1;
    const_cast<World&>(cw).ForEach<Haunting>([&](Haunting& h) { seen_stage = h.stage; });
    const_cast<World&>(cw).ForEach<GhostBrain>([&](GhostBrain& g) { seen_state = g.state; });
    CHECK(seen_stage >= 3 && seen_state == static_cast<i32>(GhostState::Hunting), "clients see the haunting stage and the ghost's state (replicated)");

    // C. A downed player cannot clean; the other can; the ghost cannot re-attack a recovered player straight away.
    Player* downed = nullptr; Player* up = nullptr;
    for (auto& p : players) {
        const GhostCleaner* gc = p->client->GetWorld().GetComponent<GhostCleaner>(p->Pawn());
        (gc && gc->downed ? downed : up) = p.get();
    }
    CHECK(downed != nullptr, "the client of the incapacitated player sees it is downed (replicated)");
    if (downed) {
        const i32 acc = downed->client->GetWorld().GetComponent<GhostCleaner>(downed->Pawn())->accepted;
        for (int i = 0; i < 6; ++i) { clean(*downed, stains[0]); step(4); }
        const GhostCleaner* gc = downed->client->GetWorld().GetComponent<GhostCleaner>(downed->Pawn());
        CHECK(gc->downed && gc->accepted == acc, "a downed player's 6 clean requests were not accepted while down");
    }

    // D. Counterplay: the team cleans. Contamination gives relief, the intensity falls, the ghost stops hunting.
    const i32 attacks_when_cleaning_began = brain().attacks;
    for (int round = 0; round < 900 && !cleaning.Completed(); ++round) {
        for (const Entity s : stains) if (world.IsAlive(s)) { for (auto& p : players) clean(*p, s); break; }
        step(4);
    }
    step(30);
    std::printf("info: contract %s at t=%.0f s; intensity %.1f; ghost state %d; attacks %d (%d during the cleanup)\n",
                cleaning.Completed() ? "completed" : "NOT completed", now, haunt().intensity, brain().state, brain().attacks,
                brain().attacks - attacks_when_cleaning_began);
    CHECK(cleaning.Completed(), "a hunted team can still finish the contract (attacks incapacitate, they do not eliminate)");
    CHECK(brain().state != static_cast<i32>(GhostState::Hunting), "cleaning relief calmed the ghost out of Hunting");
    step(30 * 8);
    bool anyDowned = false;
    for (auto& p : players) anyDowned = anyDowned || p->client->GetWorld().GetComponent<GhostCleaner>(p->Pawn())->downed;
    CHECK(!anyDowned, "after a few quiet seconds nobody is still down");

    // E. The counteraction a tool will provide: Stun freezes a hunting ghost for stun_seconds, then it recovers.
    {
        World w2; // a clean, isolated check of Stun on a hunting ghost
        net::NetIdentity ni;
        const Entity g2 = w2.CreateEntity(ni, Transform{Vec3{0, 0, -5}, Quaternion{}}, GhostBrain{});
        w2.GetComponent<GhostBrain>(g2)->state = static_cast<i32>(GhostState::Hunting);
        HauntingSystem h2;
        const bool stunned = h2.Stun(w2, 100.0);
        CHECK(stunned && w2.GetComponent<GhostBrain>(g2)->state == static_cast<i32>(GhostState::Stunned), "Stun puts a hunting ghost into Stunned");
        h2.Update(w2, 102.0, 0);
        CHECK(w2.GetComponent<GhostBrain>(g2)->state == static_cast<i32>(GhostState::Stunned), "a stunned ghost stays stunned for its duration");
        h2.Update(w2, 106.0, 0);
        CHECK(w2.GetComponent<GhostBrain>(g2)->state != static_cast<i32>(GhostState::Stunned), "and recovers afterwards");
    }

    // F. Protection after recovery: a hunting ghost standing next to a player who just got up must not re-attack at once.
    {
        World w3;
        net::NetIdentity pawn_id;
        w3.CreateEntity(pawn_id, Transform{Vec3{0, 0, 0}, Quaternion{}}, GhostCleaner{});
        net::NetIdentity ghost_id;
        const Entity g3 = w3.CreateEntity(ghost_id, Transform{Vec3{0.4f, 0, 0}, Quaternion{}}, GhostBrain{});
        HauntingConfig hc;
        hc.rise_per_second = 100.0f; // straight to Critical
        HauntingSystem h3(hc);
        GhostCleaner* pc = nullptr;
        w3.ForEach<GhostCleaner>([&](GhostCleaner& c) { pc = &c; });
        f64 t = 0;
        auto tick = [&](f64 seconds) { // 30 Hz; mirrors the server loop: the cleaner's downed flag follows its timer
            for (int i = 0; i < static_cast<int>(seconds * 30); ++i) { t += 1.0 / 30.0; pc->downed = t < pc->down_until; h3.Update(w3, t, 0); }
        };
        tick(1.0);
        const i32 first = w3.GetComponent<GhostBrain>(g3)->attacks;
        CHECK(first == 1 && pc->downed, "a hunting ghost next to a player incapacitates them once");
        tick(5.0 + 1.0); // downed for 5 s, then 1 s into the protection window
        CHECK(!pc->downed && w3.GetComponent<GhostBrain>(g3)->attacks == 1, "the recovered player is not re-attacked during the protection window");
        tick(3.0);       // the window (3 s) has now ended
        CHECK(w3.GetComponent<GhostBrain>(g3)->attacks >= 2, "once the protection window ends the ghost can attack again");
    }

    std::printf("%s (%d failed)\n", g_failed ? "FAILED" : "PASSED", g_failed);
    return g_failed ? 1 : 0;
}
