// Contract results, payout and the company save: pure rules, the save across restarts and damage, and two end-to-end runs
// with real clients (a full completion and a retreat).
#include "aether/net/replication.h"
#include "aether/scene/gameplay.h"
#include "aether/server/dedicated_server.h"
#include "gs/cleaning.h"
#include "gs/contract.h"
#include "gs/haunting.h"
#include "gs/trap.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <unistd.h>
#include <vector>

using namespace aether;
using namespace aether::server;
using namespace ghostshift;
namespace fs = std::filesystem;

static int g_failed = 0;
#define CHECK(c, msg) do { if (!(c)) { std::printf("FAIL: %s\n", msg); ++g_failed; } else std::printf("ok:   %s\n", msg); } while (0)

static std::string Slurp(const fs::path& p) { std::ifstream f(p, std::ios::binary); return {std::istreambuf_iterator<char>(f), {}}; }
static void Spit(const fs::path& p, const std::string& s) { std::ofstream f(p, std::ios::binary | std::ios::trunc); f << s; }

struct Outcome { ContractResult result; CompanySave company; bool second_extract_paid_again = false; };

// One full session on a real server: 2 clients, 2 stains. `clean_all`: clean both (else only the first), `contain`: the ghost is contained.
static Outcome RunScenario(bool clean_all, bool contain, const fs::path& dir) {
    World world;
    net::NetIdentity did, gid;
    net::SetNetArchetype(did, "director");
    net::SetNetArchetype(gid, "ghost");
    const Entity director = world.CreateEntity(did, Transform{Vec3{0, 0, 0}, Quaternion{}}, Haunting{}, ContractResult{});
    const Entity ghost = world.CreateEntity(gid, Transform{Vec3{0, 0, -8.0f}, Quaternion{}}, GhostBrain{});
    std::vector<Entity> stains;
    for (const Vec3 p : {Vec3{1.0f, 0, 0.5f}, Vec3{2.0f, 0, 1.0f}}) {
        net::NetIdentity ni;
        net::SetNetArchetype(ni, kSceneArchetype);
        stains.push_back(world.CreateEntity(ni, Transform{p, Quaternion{}}, Tags{{"Contamination"}}));
    }
    ServerConfig sc;
    sc.port = 0, sc.lan_advertise = false, sc.status_interval = 0;
    DedicatedServer server;
    std::string err;
    if (!server.Start(sc, world, &err)) { std::printf("server: %s\n", err.c_str()); std::exit(2); }
    Company company(dir);
    company.Load();
    CleaningConfig roomy;
    roomy.tank_capacity = 1000.0f;
    CleaningSystem cleaning(roomy);
    HauntingSystem haunting;
    TrapSystem traps;
    ContractSystem contract;
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
            haunting.SetIntensity(0.0f); // keep this run about results, not about the ghost hunting the players
            haunting.Update(world, now, cleaning.Total() - cleaning.Remaining());
            contract.Update(world, now, cleaning, company);
            for (auto& p : players) p->client->Tick(now);
        }
    };
    for (const char* name : {"Maya", "Felix"}) {
        auto p = std::make_unique<Player>();
        NetClientConfig cc;
        cc.server = net::Address::Loopback(server.Port()), cc.name = name, cc.duration = 1e9, cc.send_rpc = false;
        if (!p->client->Start(cc, now, &err)) std::exit(2);
        players.push_back(std::move(p));
        for (int i = 0; i < 300 && players.back()->Pawn() == kNullEntity; ++i) step(1);
    }
    step(30);
    auto clean = [&](Player& p, Entity target) {
        const Entity pawn = p.Pawn();
        if (!world.IsAlive(target)) return;
        GhostCleaner* gc = p.client->GetWorld().GetComponent<GhostCleaner>(pawn);
        const reflect::TypeInfo& type = reflect::Reflect<GhostCleaner>();
        std::vector<reflect::Any> args = {static_cast<i32>(world.GetComponent<net::NetIdentity>(target)->net_id)};
        RemoteCallResult r;
        CallFunction(p.client->GetWorld(), pawn, type, *type.FindFunction("Clean"), gc, args, nullptr, &r);
    };
    auto extract = [&](Player& p) {
        const Entity pawn = p.Pawn();
        GhostTools* gt = p.client->GetWorld().GetComponent<GhostTools>(pawn);
        const reflect::TypeInfo& type = reflect::Reflect<GhostTools>();
        std::vector<reflect::Any> args;
        RemoteCallResult r;
        CallFunction(p.client->GetWorld(), pawn, type, *type.FindFunction("Extract"), gt, args, nullptr, &r);
    };
    const usize to_clean = clean_all ? stains.size() : 1;
    for (usize s = 0; s < to_clean; ++s)
        for (int round = 0; round < 600 && world.IsAlive(stains[s]); ++round) { for (auto& p : players) clean(*p, stains[s]); step(4); }
    step(20);
    if (contain) world.GetComponent<GhostBrain>(ghost)->state = static_cast<i32>(GhostState::Contained);
    step(5);
    extract(*players[0]);
    extract(*players[1]); // both press it
    step(30);
    const i32 money_after_first = company.Data().money;
    extract(*players[0]); // and again after the contract has finished
    step(30);
    Outcome out;
    out.result = *world.GetComponent<ContractResult>(director);
    out.second_extract_paid_again = company.Data().money != money_after_first;
    out.company = company.Data();
    // What a client sees (replicated).
    ContractResult seen;
    players[1]->client->GetWorld().ForEach<ContractResult>([&](ContractResult& r) { seen = r; });
    out.result.grade = seen.grade == out.result.grade && seen.payout == out.result.payout && seen.state == out.result.state ? out.result.grade : -99;
    return out;
}

int main() {
    RegisterServerComponents();
    ContractConfig cfg;

    // ---------- Pure rules ----------
    {
        Performance best{1.0f, true, 0, 600.0};
        CHECK(Score(best, cfg) == 100.0f && GradeFor(Score(best, cfg)) == Grade::S, "a perfect, fast, clean contract scores 100 and grades S");
        CHECK(Payout(Grade::S, true, best, cfg) == 1500, "an S pays 1.5x the base");
        CHECK(GradeFor(95.0f) == Grade::S && GradeFor(94.9f) == Grade::A && GradeFor(85.0f) == Grade::A && GradeFor(84.9f) == Grade::B &&
              GradeFor(70.0f) == Grade::B && GradeFor(69.9f) == Grade::C && GradeFor(55.0f) == Grade::C && GradeFor(54.9f) == Grade::D,
              "grade boundaries are S 95, A 85, B 70, C 55");
        Performance hurt = best; hurt.attacks = 3;
        Performance slow = best; slow.seconds = 1800.0;
        CHECK(Score(hurt, cfg) < Score(best, cfg) && Score(slow, cfg) < Score(best, cfg), "attacks and slowness each lower the score");
        Performance uncontained = best; uncontained.contained = false;
        CHECK(GradeFor(Score(uncontained, cfg)) == Grade::B, "cleaning everything without containing the ghost is a B at best");
        Performance half = best; half.cleaned_ratio = 0.5f;
        const i32 retreat = Payout(Grade::D, false, half, cfg);
        CHECK(retreat > 0 && retreat < Payout(Grade::D, true, half, cfg), "a retreat pays something, but less than finishing");
        CHECK(Payout(Grade::D, false, Performance{0.0f, false, 0, 10.0}, cfg) == 0, "a retreat with nothing cleaned pays nothing");
        CHECK(Payout(Grade::D, true, best, cfg, 99999) == 0, "penalties never push a payout below zero");
    }

    // ---------- The company save ----------
    const fs::path base = fs::temp_directory_path() / ("gs_contract_test_" + std::to_string(::getpid()));
    fs::remove_all(base);
    {
        const fs::path dir = base / "save";
        std::string msg;
        { Company c(dir); CHECK(c.Load(&msg) && c.Data().money == 0, "a missing save starts a new company"); }
        { Company c(dir); c.Load(); CHECK(c.Award(1, 1200, Grade::A, true, &msg) && c.Data().money == 1200, "a contract pays the company");
          CHECK(c.Data().contracts_completed == 1 && c.Data().reputation == 5 + 3 * 3 && c.Data().best_grade == 3, "reputation and best grade follow the grade"); }
        { Company c(dir); CHECK(c.Load(&msg) && c.Data().money == 1200 && c.Data().contracts_completed == 1 && c.Data().last_contract == 1, "after a restart the company is as it was"); }
        { Company c(dir); c.Load();
          CHECK(!c.Award(1, 1200, Grade::A, true, &msg) && c.Data().money == 1200, "replaying a paid contract id pays nothing");
          CHECK(c.Award(2, 800, Grade::C, true, &msg) && c.Data().money == 2000, "the next contract pays"); }
        // Damage the main save: the engine's backup (the state before the last write) is used and says so.
        fs::path main_file;
        for (const auto& e : fs::directory_iterator(dir)) if (e.path().extension() != ".bak" && e.path().string().find("company") != std::string::npos) main_file = e.path();
        const std::string good = Slurp(main_file);
        Spit(main_file, good.substr(0, good.size() / 2));
        { Company c(dir); const bool ok = c.Load(&msg);
          CHECK(ok && c.Data().money == 1200, "a damaged save is recovered from the backup (the last contract's pay is lost, not everything)");
          CHECK(msg.find("backup") != std::string::npos, "and the recovery is reported"); }
        // Damage both: nothing readable. The company must not be silently reset and the files must not be overwritten.
        fs::path bak = main_file; bak += ".bak";
        Spit(main_file, "garbage"); Spit(bak, "garbage too");
        { Company c(dir); const bool ok = c.Load(&msg);
          CHECK(!ok && !c.Writable(), "with nothing readable the load fails and the company becomes read-only");
          CHECK(!c.Award(3, 500, Grade::B, true, &msg), "paying into an unreadable save is refused");
          CHECK(Slurp(main_file) == "garbage" && Slurp(bak) == "garbage too", "the damaged files are left exactly as found (not overwritten)"); }
    }

    // ---------- End to end ----------
    {
        const Outcome full = RunScenario(true, true, base / "full");
        std::printf("info: full run: state %d grade %d payout %d cleaned %d%% attacks %d, company money %d\n", full.result.state, full.result.grade,
                    full.result.payout, full.result.cleaned_percent, full.result.attacks, full.company.money);
        CHECK(full.result.state == static_cast<i32>(ContractState::Completed) && full.result.cleaned_percent == 100 && full.result.contained == 1, "everything cleaned and the ghost contained: the contract completes");
        CHECK(full.result.grade >= 0 && full.result.payout > 0, "clients see a grade and a payout (replicated)");
        CHECK(full.company.money == full.result.payout && full.company.contracts_completed == 1, "the company is paid exactly the payout, once, although two players pressed Extract");
        CHECK(!full.second_extract_paid_again, "a later Extract on a finished contract pays nothing");
        const Outcome retreat = RunScenario(false, false, base / "retreat");
        std::printf("info: retreat run: state %d grade %d payout %d cleaned %d%%, company money %d\n", retreat.result.state, retreat.result.grade,
                    retreat.result.payout, retreat.result.cleaned_percent, retreat.company.money);
        CHECK(retreat.result.state == static_cast<i32>(ContractState::Retreated) && retreat.result.grade == static_cast<i32>(Grade::D), "extracting early is a retreat, graded D");
        CHECK(retreat.result.payout > 0 && retreat.result.payout < full.result.payout, "a retreat pays a partial reward");
        CHECK(retreat.company.contracts_retreated == 1 && retreat.company.contracts_completed == 0 && retreat.company.money == retreat.result.payout,
              "the company records the retreat and is paid the partial reward once");
    }
    fs::remove_all(base);

    std::printf("%s (%d failed)\n", g_failed ? "FAILED" : "PASSED", g_failed);
    return g_failed ? 1 : 0;
}
