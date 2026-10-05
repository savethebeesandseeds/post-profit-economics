// SPDX-License-Identifier: MIT
#include "experiment.hpp"
#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace ph::production::research {
namespace {
using Number = std::int64_t;
using RunKey = std::tuple<std::string, std::string, std::uint32_t>;
void require(bool ok, const std::string& message) { if (!ok) throw std::invalid_argument(message); }
void keys(const Json& value, std::initializer_list<const char*> fields, const std::string& where) {
  require(value.is_object() && value.size() == fields.size(), where + " has missing or unknown fields");
  for (const auto* field : fields) require(value.contains(field), where + " is missing " + field);
}
std::string text(const Json& value, std::size_t limit, const std::string& where) {
  require(value.is_string(), where + " must be text");
  const auto result = value.get<std::string>();
  require(!result.empty() && result.size() <= limit, where + " has invalid length");
  for (unsigned char c : result) require(c >= 32 && c != 127, where + " contains controls");
  try { static_cast<void>(value.dump()); }
  catch (const Json::exception&) { throw std::invalid_argument(where + " contains invalid UTF-8"); }
  return result;
}
std::string identity(const Json& value, const std::string& where) {
  const auto result = text(value, 64, where);
  for (unsigned char c : result) require((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
    (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.', where + " has invalid identifier characters");
  return result;
}
std::uint32_t seed(const Json& value) {
  require(value.is_number_integer(), "seed must be an integer token");
  if (value.is_number_unsigned()) require(value.get<std::uint64_t>() <= UINT32_MAX, "seed exceeds uint32");
  else require(value.get<Number>() >= 0 && value.get<Number>() <= UINT32_MAX, "seed is outside uint32");
  return value.get<std::uint32_t>();
}
Number n(const Json& value, const char* field) { return value.at(field).get<Number>(); }
void count(Json& object, const std::string& key) {
  if (!object.contains(key)) object[key] = 0;
  object[key] = object[key].get<Number>() + 1;
}
const Json& stages_for(const Json& config, const Json& job) {
  for (const auto& process : config.at("processes")) if (process.at("id") == job.at("process")) return process.at("stages");
  throw std::invalid_argument("job process is missing");
}
const Json& stage_for(const Json& config, const Json& job, const std::string& id) {
  for (const auto& stage : stages_for(config, job)) if (stage.at("id") == id) return stage;
  throw std::invalid_argument("stage definition is missing");
}
bool has_prediction(const Json& expected) {
  // An exact array, even an empty one, asserts something. An empty object
  // subset asserts nothing and must not open the exploration gate by itself.
  if (!expected.is_object()) return true;
  for (const auto& child : expected) if (has_prediction(child)) return true;
  return false;
}
bool subset(const Json& expected, const Json& actual, const std::string& path, Json& mismatches) {
  bool passed = true;
  if (expected.is_object() && actual.is_object()) {
    for (auto it = expected.begin(); it != expected.end(); ++it) {
      const auto next = path + "/" + it.key();
      if (!actual.contains(it.key())) {
        mismatches.push_back({{"path", next}, {"expected", it.value()}, {"actual", nullptr}, {"missing", true}});
        passed = false;
      } else if (!subset(it.value(), actual.at(it.key()), next, mismatches)) passed = false;
    }
    return passed;
  }
  // Objects allow explicit subsets; arrays retain exact order and contents.
  // In particular, a partial object inside an array is not a subset match.
  if (expected != actual) {
    mismatches.push_back({{"path", path}, {"expected", expected}, {"actual", actual}, {"missing", false}});
    return false;
  }
  return true;
}
Json invariant_errors(const Json& result) {
  Json errors = Json::array();
  const auto add = [&](bool ok, const std::string& condition) { if (!ok) errors.push_back(condition); };
  const auto& config = result.at("config"); const auto& summary = result.at("summary");
  Number compensation = 0;
  for (const auto& pool : config.at("labor")) compensation += n(pool, "compensation_due_per_tick");
  add(summary.at("compensation_settlement") == "not_modeled", "compensation settlement remains unmodeled");
  add(n(summary, "compensation_due") == compensation * n(config, "horizon_ticks"), "full-horizon compensation identity");
  for (const auto& resource : summary.at("resources"))
    add(n(resource, "closing") == n(resource, "opening") + n(resource, "produced") -
      n(resource, "consumed") - n(resource, "lost"), "resource reconciliation");
  std::map<std::pair<std::string, std::string>, Number> previous;
  for (const auto& row : result.at("rows")) {
    add(n(row, "compensation_due") == compensation, "per-tick compensation identity");
    for (const auto& stock : row.at("resources")) add(stock.get<Number>() >= 0, "nonnegative stock");
    for (const auto* kind : {"labor", "capacity"}) for (const auto& pool : row.at(kind))
      add(n(pool, "used") >= 0 && n(pool, "used") <= n(pool, "available"), "pool use bounded by availability");
    for (const auto& stage : row.at("stages")) {
      const auto key = std::make_pair(stage.at("job").get<std::string>(), stage.at("stage").get<std::string>());
      const auto work = n(stage, "work_this_tick");
      add(work >= 0 && n(stage, "work_completed") == previous[key] + work &&
        n(stage, "work_completed") <= n(stage, "work_required"), "work progress identity");
      add((stage.at("reason") == "working") == (work > 0), "working reason agrees with positive progress");
      previous[key] = n(stage, "work_completed");
    }
  }
  return errors;
}
Json availability_events(const Json& result) {
  Json events = Json::array();
  for (const auto& event : result.at("events")) if (event.at("type") == "availability_interruption") events.push_back(event);
  return events;
}
Json availability_series(const Json& result) {
  Json rows = Json::array();
  for (const auto& row : result.at("rows")) {
    Json observed = {{"tick", row.at("tick")}, {"labor", Json::object()}, {"capacity", Json::object()}};
    for (const auto* kind : {"labor", "capacity"}) for (auto pool = row.at(kind).begin(); pool != row.at(kind).end(); ++pool)
      observed[kind][pool.key()] = pool.value().at("available");
    rows.push_back(std::move(observed));
  }
  return rows;
}
std::string cell(const std::string& value) {
  std::string out;
  for (char c : value) {
    if (c == '|' || c == '\\' || c == '*' || c == '_' || c == char(96) || c == '[' || c == ']') out += '\\';
    if (c == '<') out += "&lt;"; else if (c == '>') out += "&gt;"; else out += c;
  }
  return out;
}
std::string tick_text(const Json& value) { return value.is_null() ? "unfinished" : value.dump(); }
}  // namespace

void validate_plan(const Json& plan) {
  keys(plan, {"schema_version","id","description","policies","verification_cases","exploration_cases","prefix_checks"}, "experiment plan");
  require(plan.at("schema_version") == "production.experiment.config.v1", "unsupported experiment schema");
  identity(plan.at("id"), "plan id"); text(plan.at("description"), 2048, "description");
  const auto& policies = plan.at("policies");
  require(policies.is_array() && !policies.empty() && policies.size() <= 2, "policies needs 1-2 supported policies");
  std::set<std::string> policy_ids;
  for (const auto& policy : policies) {
    require(policy == "fifo" || policy == "earliest_deadline", "unsupported experiment policy");
    require(policy_ids.insert(policy.get<std::string>()).second, "duplicate policy");
  }
  require(plan.at("verification_cases").is_array() && !plan.at("verification_cases").empty() &&
    plan.at("verification_cases").size() <= 64, "verification_cases needs 1-64 cases");
  require(plan.at("exploration_cases").is_array() && plan.at("exploration_cases").size() <= 64, "exploration_cases exceeds 64 cases");
  std::set<std::string> case_ids;
  std::map<std::string, const Json*> verification_by_id;
  Number runs = 0, stage_ticks = 0, observation_entries = 0;
  for (const auto* field : {"verification_cases", "exploration_cases"}) for (const auto& item : plan.at(field)) {
    keys(item, {"id","label","config","seeds","expectations"}, "case");
    const auto case_id = identity(item.at("id"), "case id");
    require(case_ids.insert(case_id).second, "duplicate case id"); text(item.at("label"), 256, "case label");
    validate_config(item.at("config"));
    const auto& seeds = item.at("seeds");
    require(seeds.is_array() && !seeds.empty() && seeds.size() <= 64, "seeds needs 1-64 values");
    std::set<std::uint32_t> unique;
    for (const auto& value : seeds) require(unique.insert(seed(value)).second, "duplicate seed");
    const auto& expectations = item.at("expectations");
    require(expectations.is_object(), "expectations must be an object");
    if (std::string(field) == "verification_cases") {
      require(expectations.size() == policy_ids.size(), "verification expectations must cover every policy");
      for (const auto& policy : policy_ids) require(expectations.contains(policy) && expectations.at(policy).is_object() &&
        !expectations.at(policy).empty() && has_prediction(expectations.at(policy)),
        "policy expectation needs a concrete scalar or exact-array prediction");
      verification_by_id.emplace(case_id, &item);
    } else require(expectations.empty(), "exploration expectations must be empty");
    Number stages = 0;
    for (const auto& job : item.at("config").at("jobs")) stages += static_cast<Number>(stages_for(item.at("config"), job).size());
    const auto repetitions = static_cast<Number>(policies.size() * seeds.size());
    runs += repetitions; stage_ticks += stages * n(item.at("config"), "horizon_ticks") * repetitions;
    const auto& config = item.at("config");
    const auto row_entries = stages + static_cast<Number>(config.at("resources").size() +
      config.at("labor").size() + config.at("capacity").size());
    observation_entries += row_entries * n(config, "horizon_ticks") * repetitions;
    require(runs <= 256, "experiment exceeds 256 run trajectories");
    require(stage_ticks <= 200000, "experiment exceeds 200000 aggregate stage-tick records");
    require(observation_entries <= 200000, "experiment exceeds 200000 aggregate stage/resource/pool observations");
  }
  const auto& prefixes = plan.at("prefix_checks");
  require(prefixes.is_array() && prefixes.size() <= 64, "prefix_checks exceeds 64 pairs");
  std::set<std::pair<std::string, std::string>> prefix_ids;
  for (const auto& pair : prefixes) {
    keys(pair, {"short_case","long_case"}, "prefix check");
    const auto shorter = identity(pair.at("short_case"), "short_case"), longer = identity(pair.at("long_case"), "long_case");
    require(verification_by_id.count(shorter) && verification_by_id.count(longer), "prefix check references unknown verification case");
    require(prefix_ids.emplace(shorter, longer).second, "duplicate prefix check");
    const auto& short_item = *verification_by_id.at(shorter); const auto& long_item = *verification_by_id.at(longer);
    auto short_config = short_item.at("config"), long_config = long_item.at("config");
    require(n(short_config, "horizon_ticks") < n(long_config, "horizon_ticks"), "prefix horizon must increase");
    short_config.erase("horizon_ticks"); long_config.erase("horizon_ticks");
    require(short_config == long_config && short_item.at("seeds") == long_item.at("seeds"),
      "prefix cases must differ only in horizon and retain identical seeds");
  }
}

Json measure_run(const Json& result) {
  require(result.is_object() && result.at("schema_version") == "production.sim.v1" && result.at("status") == "ok",
    "measure_run requires a successful production.sim.v1 result");
  const auto& config = result.at("config"); validate_config(config);
  const auto horizon = n(config, "horizon_ticks");
  require(result.at("rows").is_array() && static_cast<Number>(result.at("rows").size()) == horizon, "observation rows do not match horizon");
  Json measured = {{"jobs",Json::object()},{"stages",Json::object()},
    {"pool_totals",{{"labor",Json::object()},{"capacity",Json::object()}}}};
  std::map<std::string, const Json*> job_definitions;
  Number stage_count = 0;
  for (const auto& job : config.at("jobs")) {
    const auto job_id = job.at("id").get<std::string>(); job_definitions.emplace(job_id, &job);
    measured["stages"][job_id] = Json::object();
    for (const auto& stage : stages_for(config, job)) {
      ++stage_count;
      measured["stages"][job_id][stage.at("id").get<std::string>()] = {
        {"pre_start_ticks",0},{"pre_start_reasons",Json::object()},{"progress_ticks",0},{"constrained_progress_ticks",0},
        {"work_units_completed",0},{"post_start_no_progress_ticks",0},{"post_start_reasons",Json::object()},
        {"already_completed_ticks",0},{"start_tick",nullptr},{"completed_tick",nullptr},{"span_ticks",nullptr},
        {"observed_span_ticks",0},{"unfinished_work_units",stage.at("work_units")},{"delay_opportunities",0},
        {"delay_event_count",0},{"reason_counts",Json::object()}};
    }
  }
  for (const auto* kind : {"labor","capacity"}) for (const auto& pool : config.at(kind)) {
    Json totals = {{"available",0},{"used",0}};
    if (std::string(kind) == "labor") totals["compensation_due"] = 0;
    measured["pool_totals"][kind][pool.at("id").get<std::string>()] = std::move(totals);
  }
  const std::set<std::string> reasons = {"awaiting_release","awaiting_dependencies","risk_delay","awaiting_resources",
    "awaiting_labor","awaiting_capacity","working","completed"};
  Number expected_tick = 1;
  for (const auto& row : result.at("rows")) {
    require(n(row,"tick") == expected_tick++, "observation ticks must be consecutive");
    std::set<std::pair<std::string,std::string>> seen;
    for (const auto& observed : row.at("stages")) {
      const auto job_id = observed.at("job").get<std::string>(), stage_id = observed.at("stage").get<std::string>();
      require(seen.emplace(job_id,stage_id).second, "duplicate stage observation");
      auto& stage = measured.at("stages").at(job_id).at(stage_id);
      const auto reason = observed.at("reason").get<std::string>();
      require(reasons.count(reason) != 0, "unknown stage observation reason"); count(stage["reason_counts"],reason);
      if (observed.at("start_tick").is_null()) {
        stage["pre_start_ticks"] = n(stage,"pre_start_ticks")+1; count(stage["pre_start_reasons"],reason);
      } else if (reason == "working") {
        stage["progress_ticks"] = n(stage,"progress_ticks")+1; stage["delay_opportunities"] = n(stage,"delay_opportunities")+1;
        const auto remaining = n(observed,"work_required")-(n(observed,"work_completed")-n(observed,"work_this_tick"));
        const auto& definition = stage_for(config,*job_definitions.at(job_id),stage_id);
        if (n(observed,"work_this_tick") < std::min(n(definition,"max_work_per_tick"),remaining))
          stage["constrained_progress_ticks"] = n(stage,"constrained_progress_ticks")+1;
      } else if (reason == "completed") stage["already_completed_ticks"] = n(stage,"already_completed_ticks")+1;
      else {
        require(n(observed,"work_this_tick") == 0, "non-working observation has positive work");
        stage["post_start_no_progress_ticks"] = n(stage,"post_start_no_progress_ticks")+1; count(stage["post_start_reasons"],reason);
      }
      stage["start_tick"] = observed.at("start_tick"); stage["completed_tick"] = observed.at("completed_tick");
      stage["work_units_completed"] = observed.at("work_completed");
      stage["unfinished_work_units"] = n(observed,"work_required")-n(observed,"work_completed");
    }
    require(static_cast<Number>(seen.size()) == stage_count, "missing stage observation");
    for (const auto* kind : {"labor","capacity"}) for (auto pool = row.at(kind).begin(); pool != row.at(kind).end(); ++pool) {
      auto& total = measured.at("pool_totals").at(kind).at(pool.key());
      total["available"] = n(total,"available")+n(pool.value(),"available"); total["used"] = n(total,"used")+n(pool.value(),"used");
      if (std::string(kind) == "labor") total["compensation_due"] = n(total,"compensation_due")+n(pool.value(),"compensation_due");
    }
  }
  for (const auto& event : result.at("events")) if (event.at("type") == "stage_delay") {
    auto& stage = measured.at("stages").at(event.at("job").get<std::string>()).at(event.at("stage").get<std::string>());
    stage["delay_event_count"] = n(stage,"delay_event_count")+1; stage["delay_opportunities"] = n(stage,"delay_opportunities")+1;
  }
  for (auto& job : measured["stages"]) for (auto& stage : job) {
    require(n(stage,"pre_start_ticks")+n(stage,"progress_ticks")+n(stage,"post_start_no_progress_ticks")+
      n(stage,"already_completed_ticks") == horizon, "stage categories do not partition horizon");
    if (!stage.at("start_tick").is_null()) {
      const auto start = stage.at("start_tick").get<Number>();
      const auto end = stage.at("completed_tick").is_null()?horizon:stage.at("completed_tick").get<Number>();
      stage["observed_span_ticks"] = end-start+1;
      if (!stage.at("completed_tick").is_null()) stage["span_ticks"] = end-start+1;
    }
  }
  for (const auto& job : result.at("summary").at("jobs")) {
    const auto completion = job.at("completed_tick"); const auto end = completion.is_null()?horizon:completion.get<Number>();
    measured["jobs"][job.at("id").get<std::string>()] = {{"status",job.at("status")},{"release_tick",job.at("release_tick")},
      {"deadline_tick",job.at("deadline_tick")},{"completed_tick",completion},{"missed_deadline",job.at("missed_deadline")},
      {"elapsed_ticks_since_release",completion.is_null()?Json(nullptr):Json(end-n(job,"release_tick")+1)},
      {"observed_ticks_since_release",std::max(Number{0},end-n(job,"release_tick")+1)}};
  }
  return measured;
}

Json run_experiment(const Json& plan) {
  validate_plan(plan);
  Json runs=Json::array(),checks=Json::array(),skipped=Json::array();
  std::map<RunKey,std::size_t> indexes; bool verified=true;
  const auto record_check = [&](Json check) { if (!check.at("passed").get<bool>()) verified=false; checks.push_back(std::move(check)); };
  const auto run_case = [&](const Json& item,const std::string& phase) {
    const auto case_id=item.at("id").get<std::string>();
    for (const auto& value:item.at("seeds")) {
      const auto current_seed=seed(value);
      for (const auto& policy_value:plan.at("policies")) {
        const auto policy=policy_value.get<std::string>(); auto config=item.at("config");
        config["seed"]=current_seed; config["scheduling_policy"]=policy;
        auto result=simulate(config); auto measurements=measure_run(result); auto errors=invariant_errors(result);
        const bool replay=result==simulate(config);
        record_check({{"kind","invariants"},{"case_id",case_id},{"policy",policy},{"seed",current_seed},
          {"passed",errors.empty()},{"errors",std::move(errors)}});
        record_check({{"kind","replay"},{"case_id",case_id},{"policy",policy},{"seed",current_seed},{"passed",replay}});
        Json expectation_passed=nullptr;
        if (phase=="verification") {
          Json mismatches=Json::array();
          const bool passed=subset(item.at("expectations").at(policy),Json({{"result",result},{"measurements",measurements}}),"",mismatches);
          expectation_passed=passed;
          record_check({{"kind","expectation"},{"case_id",case_id},{"policy",policy},{"seed",current_seed},
            {"passed",passed},{"mismatches",std::move(mismatches)}});
        }
        indexes.emplace(RunKey{case_id,policy,current_seed},runs.size());
        runs.push_back({{"case_id",case_id},{"phase",phase},{"policy",policy},{"seed",current_seed},
          {"result",std::move(result)},{"measurements",std::move(measurements)},{"expectation_passed",expectation_passed}});
      }
      if (plan.at("policies").size()>1) {
        const auto reference_policy=plan.at("policies")[0].get<std::string>();
        const auto& reference=runs.at(indexes.at({case_id,reference_policy,current_seed})).at("result");
        for (std::size_t p=1;p<plan.at("policies").size();++p) {
          const auto other_policy=plan.at("policies")[p].get<std::string>();
          const auto& other=runs.at(indexes.at({case_id,other_policy,current_seed})).at("result");
          auto ref_config=reference.at("config"),other_config=other.at("config");
          ref_config.erase("scheduling_policy");other_config.erase("scheduling_policy");
          const bool same_inputs=ref_config==other_config;
          const bool same_compensation=reference.at("summary").at("compensation_due")==other.at("summary").at("compensation_due");
          const bool same_availability=availability_events(reference)==availability_events(other)&&availability_series(reference)==availability_series(other);
          record_check({{"kind","paired_controls"},{"case_id",case_id},{"seed",current_seed},
            {"policies",Json::array({reference_policy,other_policy})},{"passed",same_inputs&&same_compensation&&same_availability},
            {"only_policy_differs",same_inputs},{"equal_compensation_due",same_compensation},
            {"matched_availability_events_and_capacity",same_availability}});
        }
      }
    }
  };
  for (const auto& item:plan.at("verification_cases")) run_case(item,"verification");
  for (const auto& pair:plan.at("prefix_checks")) {
    const auto shorter=pair.at("short_case").get<std::string>(),longer=pair.at("long_case").get<std::string>();
    const Json* short_case=nullptr;
    for (const auto& item:plan.at("verification_cases")) if (item.at("id")==shorter) short_case=&item;
    for (const auto& value:short_case->at("seeds")) for (const auto& policy:plan.at("policies")) {
      const auto current_seed=seed(value);
      const auto& short_result=runs.at(indexes.at({shorter,policy.get<std::string>(),current_seed})).at("result");
      const auto& long_result=runs.at(indexes.at({longer,policy.get<std::string>(),current_seed})).at("result");
      bool rows_match=true;
      for (std::size_t t=0;t<short_result.at("rows").size();++t) rows_match=rows_match&&short_result.at("rows")[t]==long_result.at("rows")[t];
      Json event_prefix=Json::array();const auto horizon=n(short_result.at("config"),"horizon_ticks");
      for (const auto& event:long_result.at("events")) if (n(event,"tick")<=horizon) event_prefix.push_back(event);
      record_check({{"kind","prefix"},{"short_case",shorter},{"long_case",longer},{"policy",policy},{"seed",current_seed},
        {"passed",rows_match&&event_prefix==short_result.at("events")}});
    }
  }
  if (verified) for (const auto& item:plan.at("exploration_cases")) run_case(item,"exploration");
  else for (const auto& item:plan.at("exploration_cases")) skipped.push_back(item.at("id"));
  Json summaries=Json::array();
  for (const auto* phase:{"verification_cases","exploration_cases"}) for (const auto& item:plan.at(phase)) for (const auto& policy:plan.at("policies")) {
    Json summary={{"case_id",item.at("id")},{"phase",std::string(phase)=="verification_cases"?"verification":"exploration"},
      {"policy",policy},{"sample_count",0},{"jobs",Json::object()}};
    for (const auto& run:runs) {
      if (run.at("case_id")!=item.at("id")||run.at("policy")!=policy) continue;
      summary["sample_count"]=n(summary,"sample_count")+1;
      for (auto job=run.at("measurements").at("jobs").begin();job!=run.at("measurements").at("jobs").end();++job) {
        if (!summary["jobs"].contains(job.key())) summary["jobs"][job.key()]={{"completed_count",0},{"unfinished_count",0},
          {"missed_deadline_count",0},{"completion_tick_counts",{{"unfinished",0}}},{"stage_distributions",Json::object()},
          {"reason_distributions",Json::object()},{"samples",Json::array()}};
        auto& collected=summary["jobs"][job.key()];const auto& observed=job.value();
        count(collected["completion_tick_counts"],tick_text(observed.at("completed_tick")));
        const char* field=observed.at("completed_tick").is_null()?"unfinished_count":"completed_count";
        collected[field]=n(collected,field)+1;
        if (observed.at("missed_deadline").get<bool>()) collected["missed_deadline_count"]=n(collected,"missed_deadline_count")+1;
        const auto& stages=run.at("measurements").at("stages").at(job.key());
        for (auto stage=stages.begin();stage!=stages.end();++stage) for (const auto* metric:{"pre_start_ticks","progress_ticks",
          "constrained_progress_ticks","post_start_no_progress_ticks","unfinished_work_units","delay_opportunities","delay_event_count",
          "work_units_completed","already_completed_ticks","span_ticks","observed_span_ticks"}) {
          auto& histogram=collected["stage_distributions"][stage.key()][metric];
          if (histogram.is_null()) histogram=Json::object();
          count(histogram,tick_text(stage.value().at(metric)));
        }
        const std::map<std::string,std::vector<std::string>> reason_kinds = {
          {"pre_start_reasons",{"awaiting_release","awaiting_dependencies","awaiting_resources","awaiting_labor","awaiting_capacity"}},
          {"post_start_reasons",{"risk_delay","awaiting_labor","awaiting_capacity"}}};
        for (auto stage=stages.begin();stage!=stages.end();++stage) for (const auto& kind:reason_kinds)
          for (const auto& reason:kind.second) {
            const auto& observed_reasons = stage.value().at(kind.first);
            const auto ticks = observed_reasons.contains(reason) ? observed_reasons.at(reason).get<Number>() : Number{0};
            auto& histogram = collected["reason_distributions"][stage.key()][kind.first][reason];
            if (histogram.is_null()) histogram=Json::object();
            count(histogram,std::to_string(ticks));
        }
        collected["samples"].push_back({{"seed",run.at("seed")},{"status",observed.at("status")},{"completed_tick",observed.at("completed_tick")},
          {"missed_deadline",observed.at("missed_deadline")},{"stages",stages}});
      }
    }
    if (n(summary,"sample_count")!=0) summaries.push_back(std::move(summary));
  }
  return {{"schema_version","production.experiment.v1"},{"status",verified?"ok":"verification_failed"},{"plan",plan},
    {"runs",std::move(runs)},{"checks",std::move(checks)},{"case_summaries",std::move(summaries)},{"skipped_cases",std::move(skipped)},
    {"interpretation",Json::array({
      "Synthetic observations under declared assumptions; risk parameters are uncalibrated.",
      "Recorded reasons are first blocking conditions, not a complete blocker set or unique causal attribution.",
      "Post-start no-progress counts include a zero-work starting tick; unreleased time is separately classified before start.",
      "Completion frequencies retain unfinished outcomes and the full sample denominator; no policy ranking or completion-only average is supplied.",
      "Compensation equality and resource reconciliation are implementation checks, not economic findings; settlement remains not_modeled.",
      "Stage-tick counts cannot be summed into job elapsed time. Results establish no empirical adequacy, consent, worker benefit or viability."})}};
}

std::string render_markdown(const Json& report) {
  require(report.at("schema_version")=="production.experiment.v1","unsupported experiment report");
  std::ostringstream out;
  out<<"# Production scheduling observations\n\nPlan: "<<cell(report.at("plan").at("id").get<std::string>())
     <<". Status: **"<<cell(report.at("status").get<std::string>())<<"**. Recorded policy runs: "<<report.at("runs").size()<<".\n\n"
     <<cell(report.at("plan").at("description").get<std::string>())<<"\n\n";
  Number passed=0;for(const auto& check:report.at("checks"))passed+=check.at("passed").get<bool>();
  out<<"Verification and consistency checks: "<<passed<<"/"<<report.at("checks").size()
     <<" passed. These include expectations, replay, accounting/capacity bounds, paired controls and horizon prefixes.\n\n";
  for(const auto& limitation:report.at("interpretation"))out<<"- "<<limitation.get<std::string>()<<'\n';
  out << "\n## Configured cases\n\n| Case | Description | Phase | Seeds | Horizon ticks |\n"
      << "| --- | --- | --- | ---: | ---: |\n";
  for (const auto* phase : {"verification_cases", "exploration_cases"})
    for (const auto& item : report.at("plan").at(phase))
      out << "| " << cell(item.at("id").get<std::string>()) << " | "
          << cell(item.at("label").get<std::string>()) << " | "
          << (std::string(phase) == "verification_cases" ? "verification" : "exploration") << " | "
          << item.at("seeds").size() << " | " << item.at("config").at("horizon_ticks") << " |\n";
  out<<"\n## Outcomes by configured case and policy\n\n"
     <<"| Case | Phase | Policy | Job | Samples | Completed | Unfinished | Deadline misses | Completion tick frequencies |\n"
     <<"| --- | --- | --- | --- | ---: | ---: | ---: | ---: | --- |\n";
  for(const auto& summary:report.at("case_summaries"))for(auto job=summary.at("jobs").begin();job!=summary.at("jobs").end();++job){
    out<<"| "<<cell(summary.at("case_id").get<std::string>())<<" | "<<summary.at("phase").get<std::string>()<<" | "
       <<cell(summary.at("policy").get<std::string>())<<" | "<<cell(job.key())<<" | "<<summary.at("sample_count")<<" | "
       <<job.value().at("completed_count")<<" | "<<job.value().at("unfinished_count")<<" | "<<job.value().at("missed_deadline_count")<<" | ";
    bool first=true;for(auto freq=job.value().at("completion_tick_counts").begin();freq!=job.value().at("completion_tick_counts").end();++freq){
      if (!first) out << "; ";
      out << cell(freq.key()) << ": " << freq.value();
      first = false;
    }out<<" |\n";
  }
  out<<"\n## Waiting, progress and delay opportunities\n\n"
     <<"Frequencies include every declared seed. Units are stage ticks or stage events, not job elapsed time. "
     <<"A delay opportunity is a working stage tick or newly triggered stage-delay event, not each blocked tick.\n\n"
     <<"| Case | Policy | Job | Stage | Measure | Value frequencies |\n| --- | --- | --- | --- | --- | --- |\n";
  for(const auto& summary:report.at("case_summaries"))for(auto job=summary.at("jobs").begin();job!=summary.at("jobs").end();++job)
    for(auto stage=job.value().at("stage_distributions").begin();stage!=job.value().at("stage_distributions").end();++stage)
      for(auto metric=stage.value().begin();metric!=stage.value().end();++metric){
        out<<"| "<<cell(summary.at("case_id").get<std::string>())<<" | "<<cell(summary.at("policy").get<std::string>())<<" | "
           <<cell(job.key())<<" | "<<cell(stage.key())<<" | "<<cell(metric.key())<<" | ";
        bool first=true;for(auto freq=metric.value().begin();freq!=metric.value().end();++freq){
          if (!first) out << "; ";
          out << freq.key() << ": " << freq.value();
          first = false;
        }out<<" |\n";
      }
  out << "\n## Recorded waiting reasons\n\n"
      << "Only reasons observed in at least one sample are shown. Frequencies retain zero counts for other seeds. "
      << "Unreleased time is separate from resource or pool waiting. These are first blocking checks, not exhaustive causes.\n\n"
      << "| Case | Policy | Job | Stage | Time category | Recorded reason | Tick-count frequencies |\n"
      << "| --- | --- | --- | --- | --- | --- | --- |\n";
  for (const auto& summary : report.at("case_summaries"))
    for (auto job=summary.at("jobs").begin();job!=summary.at("jobs").end();++job)
      for (auto stage=job.value().at("reason_distributions").begin();stage!=job.value().at("reason_distributions").end();++stage)
        for (auto kind=stage.value().begin();kind!=stage.value().end();++kind)
          for (auto reason=kind.value().begin();reason!=kind.value().end();++reason) {
            bool observed = false;
            for (auto frequency=reason.value().begin();frequency!=reason.value().end();++frequency)
              observed = observed || (frequency.key() != "0" && frequency.value().get<Number>() > 0);
            if (!observed) continue;
            out << "| " << cell(summary.at("case_id").get<std::string>()) << " | "
                << cell(summary.at("policy").get<std::string>()) << " | " << cell(job.key()) << " | "
                << cell(stage.key()) << " | " << cell(kind.key()) << " | " << cell(reason.key()) << " | ";
            bool first = true;
            for (auto frequency=reason.value().begin();frequency!=reason.value().end();++frequency) {
              if (!first) out << "; ";
              out << frequency.key() << ": " << frequency.value();
              first = false;
            }
            out << " |\n";
          }
  if(!report.at("skipped_cases").empty())out<<"\nSkipped exploration cases: "<<report.at("skipped_cases").dump()<<".\n";
  if(passed!=static_cast<Number>(report.at("checks").size())){
    out<<"\n## Failed checks\n\n";for(const auto& check:report.at("checks"))if(!check.at("passed").get<bool>())out<<"- "<<cell(check.dump())<<'\n';
  }
  out<<"\nThe JSON report retains the complete plan, configurations, trajectories, events and per-seed observations. "
     <<"This presentation reports synthetic observations and checks; funded operation remains outside the model.\n";
  return out.str();
}
}  // namespace ph::production::research
