#pragma once
// Ghost Shift contract results, payout and the company save.
// From the design doc: grades D to S reflect cleanliness, objectives, damage, containment and extraction; players may
// retreat for partial rewards; payout = base value x difficulty multiplier + bonuses - penalties; the company save must
// survive a restart; contract results must award currency exactly once.
// All numbers are PLACEHOLDERS to be tuned in playtests. Property damage is not modelled yet (penalty is always 0).
#include "aether/ecs/world.h"
#include "aether/reflection/reflect_macros.h"
#include "aether/save/save_system.h"
#include "gs/cleaning.h"
#include "gs/haunting.h"
#include "gs/trap.h"

#include <filesystem>
#include <string>

namespace ghostshift {
using namespace aether;

enum class Grade : i32 { D = 0, C, B, A, S };
enum class ContractState : i32 { Active = 0, Completed, Retreated };

struct ContractConfig {
    i32 base_pay = 1000;
    f32 difficulty = 1.0f;
    f64 par_seconds = 900.0;     // finishing within this earns the full time bonus
    f32 retreat_fraction = 0.4f; // a retreat pays this share of the base, scaled by how much was cleaned
};

struct Performance {
    f32 cleaned_ratio = 0.0f; // 0..1
    bool contained = false;
    i32 attacks = 0;
    f64 seconds = 0.0;
};

f32 Score(const Performance& p, const ContractConfig& c);          // 0..100
Grade GradeFor(f32 score);                                          // S >= 95, A >= 85, B >= 70, C >= 55, else D
i32 Payout(Grade grade, bool completed, const Performance& p, const ContractConfig& c, i32 penalties = 0);
const char* GradeName(Grade g);

// On the director entity (archetype "director"); replicated so every client sees the result.
struct ContractResult {
    i32 state = 0;            // ContractState
    i32 grade = -1;           // Grade, -1 until finished
    i32 payout = 0;
    i32 cleaned_percent = 0;
    i32 contained = 0;
    i32 attacks = 0;
    i32 elapsed_seconds = 0;
};

// What is saved between sessions (host-owned: the host's company, not each player's).
struct CompanySave {
    i32 money = 0;
    i32 level = 1;
    i32 reputation = 0;
    i32 contracts_completed = 0;
    i32 contracts_retreated = 0;
    i32 best_grade = -1;
    i32 last_contract = 0;    // the highest contract id already paid: a replayed result is not paid twice
};

class Company {
public:
    explicit Company(std::filesystem::path directory, std::string slot = "company") : save_(std::move(directory)), slot_(std::move(slot)) {}
    // Loads the saved company. Missing: a new one (ok). Damaged: the engine's backup is used when it is whole (a warning is
    // kept); if nothing readable remains this returns false, keeps the defaults in memory and NEVER overwrites the file.
    bool Load(std::string* message = nullptr);
    // Pays a finished contract once (ids at or below last_contract are ignored) and saves atomically. False: not paid.
    bool Award(i32 contract_id, i32 payout, Grade grade, bool completed, std::string* message = nullptr);
    const CompanySave& Data() const { return data_; }
    bool Writable() const { return writable_; } // false after a failed load of an existing file: saving would destroy it

private:
    save::SaveSystem save_;
    std::string slot_;
    CompanySave data_;
    bool writable_ = true;
};

class ContractSystem {
public:
    explicit ContractSystem(ContractConfig config = {}) : config_(config) {}
    // Call once per server tick. When a player asks to extract, finishes the contract once, pays the company and
    // writes the replicated ContractResult.
    void Update(World& world, f64 now, const CleaningSystem& cleaning, Company& company);
    ContractState State() const { return state_; }

private:
    ContractConfig config_;
    ContractState state_ = ContractState::Active;
    f64 started_ = -1.0;
};

} // namespace ghostshift

AETHER_REFLECT(ghostshift::ContractResult, 1,
    AETHER_FIELD(state, Field_Replicated),
    AETHER_FIELD(grade, Field_Replicated),
    AETHER_FIELD(payout, Field_Replicated),
    AETHER_FIELD(cleaned_percent, Field_Replicated),
    AETHER_FIELD(contained, Field_Replicated),
    AETHER_FIELD(attacks, Field_Replicated),
    AETHER_FIELD(elapsed_seconds, Field_Replicated)
)
AETHER_REFLECT(ghostshift::CompanySave, 1,
    AETHER_FIELD(money),
    AETHER_FIELD(level),
    AETHER_FIELD(reputation),
    AETHER_FIELD(contracts_completed),
    AETHER_FIELD(contracts_retreated),
    AETHER_FIELD(best_grade),
    AETHER_FIELD(last_contract)
)
