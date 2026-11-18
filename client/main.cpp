// ghostshift_client: a first-person client for ghostshift_server. Written against the Aether SDK (aether/player/game.h, scene_renderer.h,
// platform/window.h); the structure of the render loop follows the engine's aether_player (player/main.cpp).
//
//  * The cooked scene is loaded LOCALLY for what you see (models, lights, HUD). Every rule and every position comes from the server:
//    the client's own world holds the replicated entities (pawns, ghost, trap, contamination), and a presentation layer maps them onto
//    the local scene (a stain the server removed disappears, the ghost and the trap follow the server, other players get a proxy pawn).
//  * You move with predicted movement; Clean, tool, van, trap, stun, activate and extract are RPCs the server decides.
//
// Controls: WASD move, mouse look, hold left mouse to clean, 1/2/3 vacuum / UV scrubber / sprayer, F empty tank at the van,
//           T place trap, Q stun, R activate trap, X extract.
//
// usage: ghostshift_client --pak <file|dir> --server <ip:port> [--name <text>] [--size WxH] [--backend d3d12|vulkan]
//                          [--frames <n>] [--screenshot <file.bmp>] [--press <Key>]...
#include "aether/core/log.h"
#include "aether/gfx/rhi/device.h"
#include "aether/net/replication.h"
#include "aether/platform/window.h"
#include "aether/player/game.h"
#include "aether/player/scene_renderer.h"
#include "aether/scene/components.h"
#include "aether/scene/gameplay.h"
#include "aether/scene/script_component.h"
#include "aether/server/dedicated_server.h"
#include "aether/ui/widget_system.h"
#include "gs/cleaning.h"
#include "gs/contract.h"
#include "gs/haunting.h"
#include "gs/trap.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace aether;
using namespace aether::player;
using namespace ghostshift;
namespace rhi = aether::gfx::rhi;
using Clock = std::chrono::steady_clock;

namespace {

struct Options {
    std::vector<std::string> paks, pressed;
    std::string server, name = "Maya", backend, screenshot;
    i64 frames = -1;
    u32 width = 1280, height = 720;
    f32 yaw_degrees = 0.0f; // initial look direction (for scripted runs)
    i32 select_tool = -1;   // tool to select once joined (for scripted runs)
};

bool ParseArgs(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const bool v = i + 1 < argc;
        if (a == "--pak" && v) o.paks.push_back(argv[++i]);
        else if (a == "--server" && v) o.server = argv[++i];
        else if (a == "--name" && v) o.name = argv[++i];
        else if (a == "--backend" && v) o.backend = argv[++i];
        else if (a == "--frames" && v) o.frames = std::atoll(argv[++i]);
        else if (a == "--screenshot" && v) o.screenshot = argv[++i];
        else if (a == "--press" && v) o.pressed.push_back(argv[++i]);
        else if (a == "--yaw" && v) o.yaw_degrees = static_cast<f32>(std::atof(argv[++i]));
        else if (a == "--select" && v) o.select_tool = std::atoi(argv[++i]);
        else if (a == "--size" && v) { unsigned w = 0, h = 0; if (std::sscanf(argv[++i], "%ux%u", &w, &h) != 2 || w < 64 || h < 64) return false; o.width = w, o.height = h; }
        else return false;
    }
    return !o.paks.empty() && !o.server.empty();
}

void WriteU32(std::ofstream& f, u32 v) { f.write(reinterpret_cast<const char*>(&v), 4); }
void WriteU16(std::ofstream& f, u16 v) { f.write(reinterpret_cast<const char*>(&v), 2); }
bool SaveBmp(const std::string& path, u32 w, u32 h, const std::vector<u8>& rgba) {
    if (w == 0 || h == 0 || rgba.size() != static_cast<usize>(w) * h * 4) return false;
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    const u32 bytes = w * h * 4;
    f.put('B'); f.put('M');
    WriteU32(f, 54 + bytes); WriteU16(f, 0); WriteU16(f, 0); WriteU32(f, 54); WriteU32(f, 40);
    WriteU32(f, w); WriteU32(f, static_cast<u32>(-static_cast<i32>(h))); WriteU16(f, 1); WriteU16(f, 32);
    WriteU32(f, 0); WriteU32(f, bytes); WriteU32(f, 2835); WriteU32(f, 2835); WriteU32(f, 0); WriteU32(f, 0);
    for (usize i = 0; i < rgba.size(); i += 4) {
        const char bgra[4] = {static_cast<char>(rgba[i + 2]), static_cast<char>(rgba[i + 1]), static_cast<char>(rgba[i]), static_cast<char>(rgba[i + 3])};
        f.write(bgra, 4);
    }
    return f.good();
}

// A local scene entity the server owns the truth about.
struct Stain {
    Entity entity = kNullEntity;
    Vec3 position{};
    std::vector<Entity> lights;
    u32 net_id = 0;       // the replicated entity that matches it (found by position), 0 until seen
    bool removed = false; // the server removed it: it is gone here too
};

Quaternion LookRotation(f32 yaw, f32 pitch) {
    const f32 hy = yaw / 2, hp = pitch / 2;
    Quaternion q;
    q.x = std::cos(hy) * std::sin(hp), q.y = std::sin(hy) * std::cos(hp), q.z = -std::sin(hy) * std::sin(hp), q.w = std::cos(hy) * std::cos(hp);
    return q;
}

const char* ToolName(i32 t) { return t == 0 ? "VACUUM" : t == 1 ? "UV SCRUBBER" : "SPRAYER"; }
const char* StageName(i32 s) { static const char* n[] = {"QUIET", "STIRRING", "ACTIVE", "DANGEROUS", "CRITICAL"}; return n[std::clamp(s, 0, 4)]; }

} // namespace

int main(int argc, char** argv) {
    Options opt;
    if (!ParseArgs(argc, argv, opt)) {
        std::fprintf(stderr, "usage: ghostshift_client --pak <file|dir> --server <ip:port> [--name <text>] [--size WxH] [--backend d3d12|vulkan]\n"
                             "                         [--frames <n>] [--screenshot <file.bmp>] [--press <Key>]... [--yaw <deg>] [--select <0|1|2>]\n");
        return 2;
    }
    const auto address = net::Address::Parse(opt.server);
    if (!address) { std::fprintf(stderr, "ghostshift_client: --server must be ip:port\n"); return 2; }
    server::RegisterServerComponents();

    // --- the package and the local scene ---
    GamePackage package;
    std::string error;
    int mounted = 0;
    for (const std::string& p : opt.paks) {
        std::vector<std::string> files;
        std::error_code ec;
        if (std::filesystem::is_directory(p, ec)) files = GamePackage::FindPaks(p); else files.push_back(p);
        for (const std::string& f : files) {
            if (!package.Mount(f, mounted++, &error)) { std::fprintf(stderr, "ghostshift_client: %s\n", error.c_str()); return 1; }
        }
    }
    if (!package.LoadManifest(&error)) { std::fprintf(stderr, "ghostshift_client: %s\n", error.c_str()); return 1; }
    Game game(package);
    if (!game.LoadStartupScene(&error)) { std::fprintf(stderr, "ghostshift_client: %s\n", error.c_str()); return 1; }
    World& world = game.GetWorld();
    // The server owns every rule: the scene's offline Luau scripts are not run here.
    for (const Entity e : FindEntitiesWithTag(world, "Lookdev")) if (world.HasComponent<ScriptComponent>(e)) world.RemoveComponent<ScriptComponent>(e);
    for (const char* tag : {"Ghost", "Director", "Player"})
        for (const Entity e : FindEntitiesWithTag(world, tag)) if (world.HasComponent<ScriptComponent>(e)) world.RemoveComponent<ScriptComponent>(e);

    std::vector<Stain> stains;
    for (const Entity e : FindEntitiesWithTag(world, "Contamination")) {
        Stain s;
        s.entity = e;
        s.position = world.GetComponent<Transform>(e)->position;
        if (const Tags* tags = world.GetComponent<Tags>(e))
            for (const std::string& tag : tags->names)
                if (tag.rfind("Glow:", 0) == 0)
                    for (const Entity l : FindEntitiesWithTag(world, tag)) if (l != e) s.lights.push_back(l);
        stains.push_back(std::move(s));
    }
    const usize total_stains = stains.size();
    auto first_with = [&](const char* tag) { const auto v = FindEntitiesWithTag(world, tag); return v.empty() ? kNullEntity : v.front(); };
    const Entity local_ghost = first_with("Ghost"), local_director = first_with("Director"), proxy_trap = first_with("ProxyTrap");
    const std::vector<Entity> proxy_pawns = FindEntitiesWithTag(world, "ProxyPawn");
    Entity camera = kNullEntity;
    for (const Entity e : FindEntitiesWithTag(world, "Player")) if (world.HasComponent<Camera>(e)) camera = e;
    if (camera == kNullEntity) { std::fprintf(stderr, "ghostshift_client: the scene has no Player camera\n"); return 1; }

    // --- the window and the renderer ---
    std::unique_ptr<Window> window;
    std::unique_ptr<rhi::IDevice> device;
    std::unique_ptr<rhi::ISwapChain> swap_chain;
    std::vector<std::unique_ptr<rhi::ICommandList>> command_lists;
    std::vector<u64> frame_fences;
    std::unique_ptr<SceneRenderer> renderer;
    {
        WindowDesc desc;
        desc.title = "Ghost Shift";
        desc.width = opt.width, desc.height = opt.height;
        window = std::make_unique<Window>(desc);
        if (!window->IsValid()) { std::fprintf(stderr, "ghostshift_client: no window could be opened\n"); return 1; }
#if defined(_WIN32)
        const rhi::Backend backend = opt.backend == "vulkan" ? rhi::Backend::Vulkan : rhi::Backend::D3D12;
        void* native = window->NativeHandle();
#else
        const rhi::Backend backend = rhi::Backend::Vulkan;
        void* native = window->PlatformWindow();
#endif
        device = rhi::CreateDevice(backend, false);
        if (!device) { std::fprintf(stderr, "ghostshift_client: no graphics device\n"); return 1; }
        swap_chain = device->CreateSwapChain(native, window->Width(), window->Height(), 2);
        const u32 frames = std::max(swap_chain->BufferCount(), 1u);
        frame_fences.assign(frames, 0);
        for (u32 i = 0; i < frames; ++i) command_lists.push_back(device->CreateCommandList());
        renderer = std::make_unique<SceneRenderer>(*device, *swap_chain, package);
        window->SetCursorMode(CursorMode::Locked);
    }
    std::vector<input::Key> held;
    for (const std::string& name : opt.pressed) {
        const input::Key k = input::KeyFromName(name);
        if (k == input::Key::None) { std::fprintf(stderr, "ghostshift_client: no key named '%s'\n", name.c_str()); return 2; }
        held.push_back(k);
    }

    // --- the network ---
    server::NetClientConfig cfg;
    cfg.server = *address, cfg.name = opt.name, cfg.duration = std::numeric_limits<f64>::max(), cfg.send_rpc = false, cfg.join_timeout = 15;
    server::NetClient net;
    if (!net.Start(cfg, 0.0, &error)) { std::fprintf(stderr, "ghostshift_client: %s\n", error.c_str()); return 1; }
    World& nw = net.GetWorld();
    const auto t0 = Clock::now();
    auto seconds = [&] { return std::chrono::duration<f64>(Clock::now() - t0).count(); };

    game.BeginPlay();
    f32 yaw = opt.yaw_degrees * 3.14159265f / 180.0f, pitch = 0;
    bool selected_once = opt.select_tool < 0;
    std::map<input::Key, bool> was_down;
    f64 next_clean = 0;
    u32 slot = 0;
    bool shot_done = false;
    auto last = Clock::now();

    auto call = [&](Entity pawn, const reflect::TypeInfo& type, const char* fn, void* comp, std::vector<reflect::Any> args) {
        RemoteCallResult r;
        CallFunction(nw, pawn, type, *type.FindFunction(fn), comp, args, nullptr, &r);
    };

    for (i64 frame = 0; opt.frames < 0 || frame < opt.frames; ++frame) {
        if (!window->PumpMessages()) break;
        ApplyWindowEvents(window->TakeEvents(), game.Input());
        for (const input::Key k : held) game.Input().SetButton(k, true);
        const auto now = Clock::now();
        const f32 real_dt = std::chrono::duration<f32>(now - last).count();
        const f32 dt = std::clamp(real_dt, 0.001f, 0.1f); // a long frame never sends the server a longer step than it allows
        last = now;
        net.Tick(seconds());
        if (net.Done()) { std::fprintf(stderr, "ghostshift_client: the session ended: %s\n", net.Result().join_result.c_str()); break; }

        // Our pawn (the replicated entity this client owns) and the replicated director and ghost.
        Entity pawn = kNullEntity, net_ghost = kNullEntity, net_director = kNullEntity, net_trap = kNullEntity;
        std::vector<Entity> other_pawns;
        for (const auto& [id, e] : net.Replication()->Entities()) {
            const net::NetIdentity* ni = nw.IsAlive(e) ? nw.GetComponent<net::NetIdentity>(e) : nullptr;
            if (!ni) continue;
            const std::string arch = ni->archetype;
            if (arch == server::kPlayerArchetype) { if (ni->locally_owned) pawn = e; else other_pawns.push_back(e); }
            else if (arch == "ghost") net_ghost = e;
            else if (arch == "director") net_director = e;
            else if (arch == "trap") net_trap = e;
        }
        GhostCleaner* me = pawn != kNullEntity ? nw.GetComponent<GhostCleaner>(pawn) : nullptr;
        GhostTools* tools = pawn != kNullEntity ? nw.GetComponent<GhostTools>(pawn) : nullptr;
        const Transform* my_t = pawn != kNullEntity ? nw.GetComponent<Transform>(pawn) : nullptr;

        // --- input ---
        input::InputState& in = game.Input();
        yaw -= in.Value(input::Key::MouseX) * 0.0021f;
        pitch = std::clamp(pitch - in.Value(input::Key::MouseY) * 0.0021f, -1.4f, 1.4f);
        const auto pressed = [&](input::Key k) { const bool down = in.IsDown(k), edge = down && !was_down[k]; was_down[k] = down; return edge; };
        if (me && tools && my_t && net.Movement()) {
            f32 fwd = (in.IsDown(input::Key::W) ? 1.0f : 0.0f) - (in.IsDown(input::Key::S) ? 1.0f : 0.0f);
            f32 right = (in.IsDown(input::Key::D) ? 1.0f : 0.0f) - (in.IsDown(input::Key::A) ? 1.0f : 0.0f);
            f32 len = std::sqrt(fwd * fwd + right * right);
            if (len > 1.0f) fwd /= len, right /= len;
            const f32 sy = std::sin(yaw), cy = std::cos(yaw);
            net.Movement()->Predict(pawn, -sy * fwd + cy * right, -cy * fwd - sy * right, false, dt); // sent every frame, including "no movement"
            const Vec3 p = my_t->position;
            const f32 fx = -sy, fz = -cy;
            const reflect::TypeInfo& cleaner_type = reflect::Reflect<GhostCleaner>();
            const reflect::TypeInfo& tools_type = reflect::Reflect<GhostTools>();
            if (!selected_once) { call(pawn, cleaner_type, "SelectTool", me, {static_cast<i32>(opt.select_tool)}); selected_once = true; }
            if (in.IsDown(input::Key::MouseLeft) && seconds() >= next_clean) { // scrub the nearest stain in front of us
                Stain* best = nullptr;
                f32 best_d = 3.0f;
                for (Stain& s : stains) {
                    if (s.removed || s.net_id == 0) continue;
                    const f32 dx = s.position.x - p.x, dz = s.position.z - p.z, d = std::sqrt(dx * dx + dz * dz);
                    if (d <= best_d && d > 0.01f && (dx * fx + dz * fz) / d > 0.5f) best = &s, best_d = d;
                }
                if (best) { call(pawn, cleaner_type, "Clean", me, {static_cast<i32>(best->net_id)}); next_clean = seconds() + 0.14; }
            }
            if (pressed(input::Key::Num1)) call(pawn, cleaner_type, "SelectTool", me, {i32{0}});
            if (pressed(input::Key::Num2)) call(pawn, cleaner_type, "SelectTool", me, {i32{1}});
            if (pressed(input::Key::Num3)) call(pawn, cleaner_type, "SelectTool", me, {i32{2}});
            if (pressed(input::Key::F)) call(pawn, cleaner_type, "EmptyTank", me, {});
            if (pressed(input::Key::T)) call(pawn, tools_type, "PlaceTrap", tools, {f32{p.x + fx * 1.5f}, f32{p.z + fz * 1.5f}});
            if (pressed(input::Key::Q)) call(pawn, tools_type, "StunGhost", tools, {});
            if (pressed(input::Key::R)) call(pawn, tools_type, "ActivateTrap", tools, {});
            if (pressed(input::Key::X)) call(pawn, tools_type, "Extract", tools, {});
        }

        // --- the presentation layer: the server's world onto the local scene ---
        // Which stains are cleaned: an explicit mask the server replicates on the director. (Never "missing from the snapshot": a client with
        // no acknowledged baseline, at start-up or when it reads packets slowly, gets partial snapshots and would see everything flicker.)
        const ContractResult* contract = net_director != kNullEntity ? nw.GetComponent<ContractResult>(net_director) : nullptr;
        const u32 cleaned_mask = contract ? static_cast<u32>(contract->cleaned_mask) : 0u;
        for (usize i = 0; i < stains.size(); ++i) {
            Stain& s = stains[i];
            if (s.removed) continue;
            if (s.net_id == 0) { // the replicated entity at this spot, needed to name it in a Clean request (clients see transforms, not tags)
                for (const auto& [id, e] : net.Replication()->Entities()) {
                    const Transform* t = nw.IsAlive(e) ? nw.GetComponent<Transform>(e) : nullptr;
                    const net::NetIdentity* ni = nw.IsAlive(e) ? nw.GetComponent<net::NetIdentity>(e) : nullptr;
                    if (t && ni && std::string(ni->archetype) == "scene" && std::fabs(t->position.x - s.position.x) < 0.05f &&
                        std::fabs(t->position.z - s.position.z) < 0.05f && std::fabs(t->position.y - s.position.y) < 0.05f) { s.net_id = id; break; }
                }
            }
            if (i < 32 && ((cleaned_mask >> i) & 1u)) { // the server cleaned it: remove it and its glow here
                for (const Entity l : s.lights) if (world.IsAlive(l)) world.DestroyEntity(l);
                if (world.IsAlive(s.entity)) world.DestroyEntity(s.entity);
                s.removed = true;
            }
        }
        if (local_ghost != kNullEntity && net_ghost != kNullEntity)
            if (Transform* lt = world.GetComponent<Transform>(local_ghost)) if (const Transform* nt = nw.GetComponent<Transform>(net_ghost)) lt->position = {nt->position.x, 0.4f, nt->position.z};
        if (proxy_trap != kNullEntity)
            if (Transform* lt = world.GetComponent<Transform>(proxy_trap)) {
                const Transform* nt = net_trap != kNullEntity ? nw.GetComponent<Transform>(net_trap) : nullptr;
                lt->position = nt ? Vec3{nt->position.x, 0.0f, nt->position.z} : Vec3{0, -100, 0};
            }
        for (usize i = 0; i < proxy_pawns.size(); ++i)
            if (Transform* lt = world.GetComponent<Transform>(proxy_pawns[i])) {
                const Transform* nt = i < other_pawns.size() ? nw.GetComponent<Transform>(other_pawns[i]) : nullptr;
                lt->position = nt ? Vec3{nt->position.x, 0.0f, nt->position.z} : Vec3{0, -100, 0};
            }
        if (my_t)
            if (Transform* ct = world.GetComponent<Transform>(camera)) { ct->position = {my_t->position.x, my_t->position.y + 1.7f, my_t->position.z}; ct->rotation = LookRotation(yaw, pitch); }

        // --- the HUD, from the replicated state ---
        if (local_director != kNullEntity) {
            usize left = 0;
            for (const Stain& s : stains) left += s.removed ? 0 : 1;
            const usize cleaned = total_stains - left;
            const Haunting* h = net_director != kNullEntity ? nw.GetComponent<Haunting>(net_director) : nullptr;
            const ContractResult* r = net_director != kNullEntity ? nw.GetComponent<ContractResult>(net_director) : nullptr;
            const GhostBrain* gb = net_ghost != kNullEntity ? nw.GetComponent<GhostBrain>(net_ghost) : nullptr;
            char buf[160];
            std::snprintf(buf, sizeof(buf), "WILLOW HOUSE   HAUNTING %s", h ? StageName(h->stage) : "...");
            UI::SetText(local_director, "Title", buf);
            std::snprintf(buf, sizeof(buf), "CONTAMINATION  %zu / %zu", cleaned, total_stains);
            UI::SetText(local_director, "ContamText", buf);
            UI::SetValue(local_director, "CleanBar", total_stains ? static_cast<f32>(cleaned) / static_cast<f32>(total_stains) : 1.0f);
            std::snprintf(buf, sizeof(buf), "TIME  %02d:%02d", static_cast<int>(seconds()) / 60, static_cast<int>(seconds()) % 60);
            UI::SetText(local_director, "TimerText", buf);
            if (me) {
                const f32 value = me->tool == 0 ? me->tank : me->tool == 1 ? me->battery : me->spray;
                const f32 cap = me->tool == 0 ? 3.0f : me->tool == 1 ? 2.0f : 4.0f;
                std::snprintf(buf, sizeof(buf), "%s  %s %.1f / %.1f%s%s%s", ToolName(me->tool), me->tool == 0 ? "TANK" : me->tool == 1 ? "BATTERY" : "CANISTER",
                              value, cap, me->tank_full ? "  (SERVICE AT THE VAN: F)" : "", me->downed ? "  YOU ARE DOWN" : "",
                              gb && gb->state == static_cast<i32>(GhostState::Contained) ? "  GHOST CONTAINED" : "");
                UI::SetText(local_director, "Prompt", buf);
            } else {
                UI::SetText(local_director, "Prompt", "CONNECTING...");
            }
            if (r && r->state != 0) {
                std::snprintf(buf, sizeof(buf), "%s  GRADE %s  $%d", r->state == 1 ? "CONTRACT COMPLETE" : "RETREAT", r->grade >= 0 ? GradeName(static_cast<Grade>(r->grade)) : "-", r->payout);
                UI::SetText(local_director, "Message", buf);
            }
            UI::SetText(local_director, "Hint", "WASD MOVE   MOUSE LOOK   HOLD LEFT MOUSE CLEAN   1 2 3 TOOL   F VAN   T TRAP   Q STUN   R ACTIVATE   X EXTRACT");
        }

        game.Tick(dt);

        // --- draw ---
        device->WaitForFence(frame_fences[slot]);
        swap_chain->AcquireNextImage();
        rhi::ICommandList& cmd = *command_lists[slot];
        cmd.Reset();
        renderer->Draw(game, cmd, slot);
        cmd.Close();
        frame_fences[slot] = device->Submit(cmd, swap_chain.get());
        if (!opt.screenshot.empty() && !shot_done && opt.frames >= 0 && frame + 1 >= opt.frames) {
            std::vector<u8> rgba;
            shot_done = true;
            if (swap_chain->ReadBack(rgba) && SaveBmp(opt.screenshot, swap_chain->Width(), swap_chain->Height(), rgba)) std::fprintf(stderr, "ghostshift_client: saved %s\n", opt.screenshot.c_str());
            else { std::fprintf(stderr, "ghostshift_client: could not save the screenshot\n"); return 4; }
        }
        swap_chain->Present(false);
        slot = (slot + 1) % static_cast<u32>(frame_fences.size());
    }
    for (u64 f : frame_fences) device->WaitForFence(f);
    game.EndPlay();
    if (net.Movement()) {
        const auto& ms = net.Movement()->GetStats();
        std::fprintf(stderr, "ghostshift_client: done (predicted %u, acks %u, corrections %u, max error %.2f m)\n", ms.predicted, ms.acks, ms.corrections, ms.max_error);
    }
    return 0;
}
