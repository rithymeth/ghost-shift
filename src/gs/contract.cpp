#include "gs/contract.h"

#include "aether/core/log.h"

#include <algorithm>
#include <cmath>

namespace ghostshift {

f32 Score(const Performance& p, const ContractConfig& c) {
    const f32 cleaned = std::clamp(p.cleaned_ratio, 0.0f, 1.0f);
    const f32 safety = 1.0f - std::min(1.0f, static_cast<f32>(std::max(0, p.attacks)) / 4.0f);
    const f32 pace = p.seconds <= c.par_seconds ? 1.0f : std::max(0.0f, 1.0f - static_cast<f32>((p.seconds - c.par_seconds) / c.par_seconds));
    return 40.0f * cleaned + 30.0f * (p.contained ? 1.0f : 0.0f) + 20.0f * safety + 10.0f * pace;
}

Grade GradeFor(f32 score) {
    return score >= 95.0f ? Grade::S : score >= 85.0f ? Grade::A : score >= 70.0f ? Grade::B : score >= 55.0f ? Grade::C : Grade::D;
}

const char* GradeName(Grade g) {
    static const char* names[] = {"D", "C", "B", "A", "S"};
    return names[static_cast<int>(g)];
}

i32 Payout(Grade grade, bool completed, const Performance& p, const ContractConfig& c, i32 penalties) {
    if (!completed) // a retreat: a share of the base, scaled by what was cleaned
        return static_cast<i32>(std::lround(c.base_pay * c.difficulty * c.retreat_fraction * std::clamp(p.cleaned_ratio, 0.0f, 1.0f)));
    static const f32 multiplier[] = {0.5f, 0.75f, 1.0f, 1.25f, 1.5f};
    const i32 pay = static_cast<i32>(std::lround(c.base_pay * c.difficulty * multiplier[static_cast<int>(grade)]));
    return std::max(0, pay - std::max(0, penalties));
}

bool Company::Load(std::string* message) {
    data_ = CompanySave{};
    writable_ = true;
    if (!save_.Exists(slot_)) {
        if (message) *message = "no saved company: starting a new one";
        return true;
    }
    const save::SaveResult r = save_.Load(slot_, data_);
    if (r.ok) {
        if (message) *message = r.warnings.empty() ? "company loaded" : "company loaded from the backup: " + r.warnings.front();
        return true;
    }
    data_ = CompanySave{};
    writable_ = false; // saving now would overwrite a progress file we could not read
    if (message) *message = "company save unreadable (" + r.message + "): not overwriting it";
    return false;
}

bool Company::Award(i32 contract_id, i32 payout, Grade grade, bool completed, std::string* message) {
    if (!writable_) { if (message) *message = "company save is unreadable: not paying into it"; return false; }
    if (contract_id <= data_.last_contract) { if (message) *message = "contract already paid"; return false; }
    CompanySave next = data_;
    next.money += std::max(0, payout);
    next.last_contract = contract_id;
    if (completed) {
        ++next.contracts_completed;
        next.reputation += 5 + 3 * static_cast<i32>(grade);
        next.best_grade = std::max(next.best_grade, static_cast<i32>(grade));
        next.level = 1 + next.contracts_completed / 3; // placeholder: a level every three completed contracts
    } else {
        ++next.contracts_retreated;
    }
    const save::SaveResult r = save_.Save(slot_, next);
    if (!r.ok) { if (message) *message = "company save failed: " + r.message; return false; } // not paid: memory and file stay in step
    data_ = next;
    if (message) *message = "paid";
    return true;
}

void ContractSystem::Update(World& world, f64 now, const CleaningSystem& cleaning, Company& company) {
    bool any_player = false, extract = false;
    world.ForEach<GhostTools>([&](GhostTools& t) {
        any_player = true;
        if (t.extract_requested) extract = true;
        t.extract_requested = false; // consumed: later requests on a finished contract are ignored below
    });
    if (state_ != ContractState::Active) return;
    if (started_ < 0.0 && any_player) started_ = now;
    if (!extract) return;

    Performance perf;
    perf.cleaned_ratio = cleaning.Total() == 0 ? 1.0f : static_cast<f32>(cleaning.Total() - cleaning.Remaining()) / static_cast<f32>(cleaning.Total());
    perf.contained = HauntingSystem::AllContained(world);
    world.ForEach<GhostBrain>([&](GhostBrain& g) { perf.attacks += g.attacks; });
    perf.seconds = started_ < 0.0 ? 0.0 : now - started_;
    const bool completed = cleaning.Completed() && perf.contained;
    const Grade grade = completed ? GradeFor(Score(perf, config_)) : Grade::D; // a retreat is never better than D
    const i32 payout = Payout(grade, completed, perf, config_);
    state_ = completed ? ContractState::Completed : ContractState::Retreated;

    std::string message;
    const i32 contract_id = company.Data().last_contract + 1;
    const bool paid = company.Award(contract_id, payout, grade, completed, &message);
    AETHER_LOG_INFO("Contract", "%s: grade %s, payout %d (%s)", completed ? "COMPLETED" : "RETREAT", GradeName(grade), payout, message.c_str());
    (void)paid;
    world.ForEach<ContractResult>([&](ContractResult& r) {
        r.state = static_cast<i32>(state_);
        r.grade = static_cast<i32>(grade);
        r.payout = payout;
        r.cleaned_percent = static_cast<i32>(std::lround(perf.cleaned_ratio * 100.0f));
        r.contained = perf.contained ? 1 : 0;
        r.attacks = perf.attacks;
        r.elapsed_seconds = static_cast<i32>(perf.seconds);
    });
}

} // namespace ghostshift
