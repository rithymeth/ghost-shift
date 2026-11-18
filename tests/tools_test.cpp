// Tool types: each kind of contamination needs its tool; each tool has its own resource; the van services them all.
#include "aether/net/replication.h"
#include "aether/scene/gameplay.h"
#include "gs/cleaning.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace aether;
using namespace ghostshift;

static int g_failed = 0;
#define CHECK(c, msg) do { if (!(c)) { std::printf("FAIL: %s\n", msg); ++g_failed; } else std::printf("ok:   %s\n", msg); } while (0)

struct Room {
    World world;
    CleaningSystem cleaning;
    Entity pawn = kNullEntity;
    f64 now = 0;
    u32 next_id = 1;
    Room() {
        net::NetIdentity pawn_id;
        pawn = world.CreateEntity(pawn_id, Transform{Vec3{0, 0, 0}, Quaternion{}}, GhostCleaner{});
        net::NetIdentity van_id;
        world.CreateEntity(van_id, Transform{Vec3{0, 0, 1.0f}, Quaternion{}}, Tags{{"Van"}});
        Tick(0.1);
    }
    Entity Add(const char* kind, Vec3 at) {
        net::NetIdentity ni;
        net::SetNetArchetype(ni, "scene");
        std::vector<std::string> tags = {"Contamination"};
        if (kind) tags.push_back(kind);
        return world.CreateEntity(ni, Transform{at, Quaternion{}}, Tags{tags});
    }
    GhostCleaner& P() { return *world.GetComponent<GhostCleaner>(pawn); }
    void Tick(f64 dt = 1.0 / 30.0) {
        now += dt;
        world.ForEach<net::NetIdentity>([&](net::NetIdentity& ni) { if (ni.net_id == 0) ni.net_id = next_id++; });
        cleaning.Update(world, now);
    }
    void Select(Tool t) { P().SelectTool(static_cast<i32>(t)); Tick(); }
    // Scrub `target` n times (4 ticks apart, clear of the cooldown).
    void Scrub(Entity target, int n) {
        for (int i = 0; i < n && world.IsAlive(target); ++i) {
            GhostCleaner& gc = P();
            if (gc.inbox_n < GhostCleaner::kInbox) gc.inbox[gc.inbox_n++] = static_cast<i32>(world.GetComponent<net::NetIdentity>(target)->net_id);
            for (int t = 0; t < 4; ++t) Tick();
        }
    }
};

int main() {
    // 1. The right tool for the kind.
    {
        Room r;
        const Entity ecto = r.Add("Kind:Ecto", {1, 0, 0.5f});
        const Entity stain = r.Add("Kind:Stain", {1.5f, 0, 0.5f});
        const Entity dust = r.Add("Kind:Dust", {2, 0, 0.5f});
        r.Tick();
        CHECK(r.P().tool == static_cast<i32>(Tool::Vacuum), "a player starts with the vacuum");
        r.Scrub(stain, 20);
        CHECK(r.world.IsAlive(stain) && r.P().wrong_tool >= 20 && r.P().accepted == 0, "the vacuum cannot clean a cursed stain: refused and counted");
        r.Scrub(ecto, 15);
        CHECK(!r.world.IsAlive(ecto), "the vacuum cleans ectoplasm");
        r.Select(Tool::UvScrubber);
        CHECK(r.P().tool == static_cast<i32>(Tool::UvScrubber), "the player switches to the UV scrubber");
        r.Scrub(dust, 5);
        const i32 wrong = r.P().wrong_tool;
        CHECK(r.world.IsAlive(dust) && wrong >= 25, "the UV scrubber cannot clean dust");
        r.Scrub(stain, 15);
        CHECK(!r.world.IsAlive(stain), "the UV scrubber cleans cursed stains");
        r.Select(Tool::Sprayer);
        r.Scrub(dust, 15);
        CHECK(!r.world.IsAlive(dust), "the sprayer cleans haunted dust");
        r.P().SelectTool(7); r.Tick();
        CHECK(r.P().tool == static_cast<i32>(Tool::Sprayer), "an invalid tool index is refused and the tool stays");
    }
    // 2. Contamination with no kind can be cleaned with any tool.
    {
        Room r;
        const Entity any = r.Add(nullptr, {1, 0, 0.5f});
        r.Select(Tool::Sprayer);
        r.Scrub(any, 15);
        CHECK(!r.world.IsAlive(any), "contamination without a kind is cleaned by whichever tool is selected");
    }
    // 3. Each tool has its own resource; the van services them all.
    {
        Room r;
        for (int i = 0; i < 4; ++i) r.Add("Kind:Stain", {1.0f + 0.1f * static_cast<f32>(i), 0, 0.5f});
        r.Select(Tool::UvScrubber);
        CHECK(r.P().battery == 2.0f && r.P().tank == 0.0f && r.P().spray == 4.0f, "the three resources start full, empty tank, full battery, full canister");
        for (int i = 0; i < 80; ++i) {
            const auto stains = FindEntitiesWithTag(r.world, "Kind:Stain");
            if (stains.empty()) break;
            r.Scrub(stains.front(), 1);
        }
        std::printf("info: tool %d accepted %d tank %.3f battery %.3f spray %.3f full %d wrong %d rejected %d\n", r.P().tool, r.P().accepted, r.P().tank, r.P().battery, r.P().spray, r.P().tank_full ? 1 : 0, r.P().wrong_tool, r.P().rejected);
        CHECK(r.P().accepted == 30 && r.P().tank_full && r.P().battery < 0.01f, "the UV scrubber's battery lasts 2 units (30 requests), then the tool is blocked");
        CHECK(r.P().tank == 0.0f && r.P().spray == 4.0f, "using the UV scrubber did not touch the vacuum's tank or the sprayer's canister");
        r.Select(Tool::UvScrubber);
        r.P().EmptyTank(); r.Tick();
        CHECK(r.P().battery == 2.0f && !r.P().tank_full && r.P().empties == 1, "at the van the battery is recharged");
        r.Scrub(FindEntitiesWithTag(r.world, "Kind:Stain").front(), 5);
        CHECK(r.P().accepted > 30, "and the player can scrub again");
    }
    std::printf("%s (%d failed)\n", g_failed ? "FAILED" : "PASSED", g_failed);
    return g_failed ? 1 : 0;
}
