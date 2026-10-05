// SPDX-License-Identifier: MIT
#include "experiment.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {
using ph::production::Json;
namespace research = ph::production::research;
using Number = std::int64_t;
int checks = 0;

void check(bool condition, const std::string& message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}

Number n(const Json& object, const char* key) { return object.at(key).get<Number>(); }

Json risk_zero() {
  return {{"delay_probability_bps", 0}, {"delay_age_increment_bps", 0}, {"delay_ticks", 1},
          {"output_loss_probability_bps", 0}, {"output_loss_age_increment_bps", 0}, {"output_loss_bps", 0}};
}

Json simple() {
  return {{"schema_version", "production.config.v1"}, {"time_unit", "research tick"},
          {"currency", "EUR"}, {"horizon_ticks", 8}, {"seed", 41}, {"scheduling_policy", "fifo"},
          {"resources", Json::array({{{"id", "input"}, {"unit", "input-unit"}, {"initial_quantity", 100}},
                                     {{"id", "output"}, {"unit", "output-unit"}, {"initial_quantity", 0}}})},
          {"labor", Json::array({{{"id", "labor"}, {"unit", "labor-unit"}, {"units_per_tick", 2},
                                  {"compensation_due_per_tick", 1000}}})},
          {"capacity", Json::array({{{"id", "capacity"}, {"unit", "capacity-unit"}, {"units_per_tick", 8}}})},
          {"processes", Json::array({{{"id", "process"}, {"stages", Json::array({
            {{"id", "stage"}, {"depends_on", Json::array()}, {"work_units", 4}, {"max_work_per_tick", 2},
             {"inputs", {{"input", 2}}}, {"outputs", {{"output", 1}}}, {"labor", {{"labor", 1}}},
             {"capacity", {{"capacity", 1}}}, {"risk", risk_zero()}}})}}})},
          {"jobs", Json::array({{{"id", "job"}, {"process", "process"}, {"release_tick", 1}, {"deadline_tick", 4}}})},
          {"shocks", Json::array()}, {"availability_risks", Json::array()}};
}

Json measurements(const Json& config) { return research::measure_run(ph::production::simulate(config)); }

const Json& stage(const Json& measures, const char* job_id = "job", const char* stage_id = "stage") {
  return measures.at("stages").at(job_id).at(stage_id);
}

void partition(const Json& measure, Number horizon) {
  check(n(measure, "pre_start_ticks") + n(measure, "progress_ticks") +
        n(measure, "post_start_no_progress_ticks") + n(measure, "already_completed_ticks") == horizon,
        "every stage tick belongs to exactly one observation category");
}

Json verified_case(const std::string& name, const Json& config = simple()) {
  return {{"id", name}, {"label", name}, {"config", config}, {"seeds", Json::array({0})},
          {"expectations", {{"fifo", {{"result", {{"status", "ok"}}}}},
                            {"earliest_deadline", {{"result", {{"status", "ok"}}}}}}}};
}

Json exploratory_case(const std::string& name, const Json& config = simple()) {
  auto result = verified_case(name, config);
  result["expectations"] = Json::object();
  return result;
}

Json plan() {
  return {{"schema_version", "production.experiment.config.v1"}, {"id", "test-plan"},
          {"description", "Synthetic verification of research measurements, not empirical evidence."},
          {"policies", Json::array({"fifo", "earliest_deadline"})},
          {"verification_cases", Json::array({verified_case("control")})},
          {"exploration_cases", Json::array()}, {"prefix_checks", Json::array()}};
}

void delay_opportunity_and_completion_measurements() {
  auto config = simple();
  auto& risk = config["processes"][0]["stages"][0]["risk"];
  risk["delay_probability_bps"] = 10000;
  risk["delay_age_increment_bps"] = -10000;
  risk["delay_ticks"] = 2;
  const auto result = ph::production::simulate(config);
  const auto measured = research::measure_run(result);
  const auto& observed = stage(measured);
  partition(observed, 8);
  check(observed.at("start_tick") == 1 && observed.at("completed_tick") == 4,
        "a delayed start and the eventual completion remain visible");
  check(n(observed, "pre_start_ticks") == 0 && n(observed, "post_start_no_progress_ticks") == 2,
        "the initial delayed consumption counts as post-start no progress");
  check(observed.at("post_start_reasons").at("risk_delay") == 2,
        "a two-tick delay contains two paused ticks");
  check(n(observed, "delay_event_count") == 1 && n(observed, "delay_opportunities") == 3,
        "only the new trigger and two working ticks are delay opportunities");
  check(n(observed, "progress_ticks") == 2 && n(observed, "already_completed_ticks") == 4,
        "completion tick is progress and only later ticks are already completed");
  check(n(observed, "span_ticks") == 4 && n(observed, "unfinished_work_units") == 0,
        "completed span includes interruption without creating remaining work");
  check(measured.at("pool_totals").at("labor").at("labor").at("available") == 16 &&
        measured.at("pool_totals").at("labor").at("labor").at("used") == 4 &&
        measured.at("pool_totals").at("labor").at("labor").at("compensation_due") == 8000,
        "pool measurements include idle ticks without treating obligations as payments");
  check(measured.at("pool_totals").at("capacity").at("capacity").at("used") == 4,
        "capacity use aggregates actual progress");
}

void constrained_progress_and_observation_windows() {
  auto config = simple();
  config["labor"][0]["units_per_tick"] = 8;
  config["processes"][0]["stages"][0]["work_units"] = 5;
  config["processes"][0]["stages"][0]["max_work_per_tick"] = 3;
  const auto full = measurements(config);
  check(n(stage(full), "progress_ticks") == 2 && n(stage(full), "constrained_progress_ticks") == 0,
        "the smaller final work allocation is not constrained when it exhausts remaining work");
  config["labor"][0]["units_per_tick"] = 2;
  const auto limited = measurements(config);
  check(n(stage(limited), "progress_ticks") == 3 && n(stage(limited), "constrained_progress_ticks") == 2,
        "only the first two allocations are limited relative to feasible unconstrained progress");

  config = simple();
  config["horizon_ticks"] = 4;
  config["jobs"][0]["release_tick"] = 6;
  config["jobs"][0]["deadline_tick"] = 8;
  const auto future = measurements(config);
  const auto& pending = stage(future);
  partition(pending, 4);
  check(n(pending, "pre_start_ticks") == 4 && pending.at("pre_start_reasons").at("awaiting_release") == 4,
        "future release ticks remain an explicit pre-start category");
  check(pending.at("start_tick").is_null() && pending.at("completed_tick").is_null() &&
        pending.at("span_ticks").is_null() && n(pending, "observed_span_ticks") == 0,
        "a stage that never started has no completed or observed production span");
  check(future.at("jobs").at("job").at("elapsed_ticks_since_release").is_null() &&
        future.at("jobs").at("job").at("observed_ticks_since_release") == 0,
        "an unreleased job does not have a negative or fabricated elapsed duration");

  config["jobs"][0]["release_tick"] = 3;
  config["processes"][0]["stages"][0]["work_units"] = 5;
  const auto unfinished = measurements(config);
  const auto& active = stage(unfinished);
  partition(active, 4);
  check(active.at("span_ticks").is_null() && n(active, "observed_span_ticks") == 2 &&
        n(active, "unfinished_work_units") == 1 && n(active, "work_units_completed") == 4,
        "active unfinished work has an observed span but no invented completion span");
  check(unfinished.at("jobs").at("job").at("elapsed_ticks_since_release").is_null() &&
        unfinished.at("jobs").at("job").at("observed_ticks_since_release") == 2,
        "unfinished job observation duration is distinguished from completion duration");
}

void parallel_stages_and_process_local_identifiers() {
  auto config = simple();
  config["labor"][0]["units_per_tick"] = 8;
  auto second = config["processes"][0]["stages"][0];
  second["id"] = "parallel";
  config["processes"][0]["stages"].push_back(second);
  const auto parallel = measurements(config);
  check(n(stage(parallel), "progress_ticks") == 2 && n(stage(parallel, "job", "parallel"), "progress_ticks") == 2 &&
        parallel.at("jobs").at("job").at("elapsed_ticks_since_release") == 2,
        "parallel stage ticks do not become a summed job duration");
  partition(stage(parallel), 8);
  partition(stage(parallel, "job", "parallel"), 8);

  config = simple();
  config["labor"][0]["units_per_tick"] = 8;
  config["processes"][0]["stages"][0]["work_units"] = 5;
  config["processes"][0]["stages"][0]["max_work_per_tick"] = 3;
  second = config["processes"][0];
  second["id"] = "other-process";
  second["stages"][0]["max_work_per_tick"] = 5;
  config["processes"].push_back(second);
  config["jobs"].push_back({{"id", "other-job"}, {"process", "other-process"}, {"release_tick", 1}, {"deadline_tick", 4}});
  const auto local = measurements(config);
  check(n(stage(local), "progress_ticks") == 2 && n(stage(local, "other-job"), "progress_ticks") == 1 &&
        n(stage(local), "constrained_progress_ticks") == 0 && n(stage(local, "other-job"), "constrained_progress_ticks") == 0,
        "identical stage identifiers in different processes retain their own rate definitions");
}

void comparisons_and_unfinished_aggregation() {
  auto config = simple();
  config["horizon_ticks"] = 2;
  config["jobs"][0]["id"] = "job-late";
  config["jobs"][0]["deadline_tick"] = 8;
  config["jobs"].push_back({{"id", "job-early"}, {"process", "process"}, {"release_tick", 1}, {"deadline_tick", 2}});
  auto declared = plan();
  declared["verification_cases"][0] = verified_case("window", config);
  declared["verification_cases"][0]["seeds"] = Json::array({0, 1});
  const auto report = research::run_experiment(declared);
  check(report.at("schema_version") == "production.experiment.v1" && report.at("status") == "ok" &&
        report.at("runs").size() == 4 && report.at("case_summaries").size() == 2,
        "paired policies retain every declared seed run");
  for (const auto& summary : report.at("case_summaries")) {
    check(summary.at("sample_count") == 2, "aggregate sample count uses the full declared seed set");
    const bool fifo = summary.at("policy") == "fifo";
    const auto& finished = summary.at("jobs").at(fifo ? "job-late" : "job-early");
    const auto& pending = summary.at("jobs").at(fifo ? "job-early" : "job-late");
    check(finished.at("completed_count") == 2 && finished.at("unfinished_count") == 0 &&
          finished.at("completion_tick_counts").at("2") == 2,
          "completed frequency uses the declared denominator");
    check(pending.at("completed_count") == 0 && pending.at("unfinished_count") == 2 &&
          pending.at("completion_tick_counts").at("unfinished") == 2 && pending.at("samples").size() == 2,
          "unfinished samples remain in the distribution and sample records");
    for (const auto& sample : pending.at("samples"))
      check(sample.at("completed_tick").is_null() && sample.at("status") == "unfinished",
            "null unfinished completion is not imputed or omitted");
    check(pending.at("missed_deadline_count") == (fifo ? 2 : 0),
          "an unfinished job is overdue only once its own deadline is reached");
  }
  for (int seed = 0; seed < 2; ++seed) {
    Json first, second;
    for (const auto& run : report.at("runs")) if (run.at("seed") == seed) {
      auto current = run.at("result").at("config");
      const auto policy = current.at("scheduling_policy");
      current.erase("scheduling_policy");
      if (policy == "fifo") first = std::move(current); else second = std::move(current);
    }
    check(first.is_object() && first == second, "a policy pair changes scheduling_policy alone");
  }
  check(report == research::run_experiment(declared), "the experiment report replays exactly");
}

void verification_gates_and_prefixes() {
  auto declared = plan();
  declared["verification_cases"][0]["expectations"]["fifo"] = {{"result", {{"summary", {{"completed_jobs", 999}}}}}};
  declared["exploration_cases"].push_back(exploratory_case("followup"));
  const auto rejected = research::run_experiment(declared);
  check(rejected.at("status") == "verification_failed", "a failed hand calculation gates exploration");
  check(std::find(rejected.at("skipped_cases").begin(), rejected.at("skipped_cases").end(), Json("followup")) !=
        rejected.at("skipped_cases").end(), "the gated case is recorded as skipped");
  bool failure_retained = false;
  for (const auto& run : rejected.at("runs")) {
    check(run.at("case_id") != "followup", "no exploratory trajectory is produced after failed verification");
    failure_retained = failure_retained || run.at("expectation_passed") == false;
  }
  check(failure_retained, "the mismatched expectation remains visible in the receipt");

  declared = plan();
  auto short_config = simple(); short_config["horizon_ticks"] = 2;
  auto long_config = short_config; long_config["horizon_ticks"] = 4;
  declared["verification_cases"] = Json::array({verified_case("short", short_config), verified_case("long", long_config)});
  declared["prefix_checks"] = Json::array({{{"short_case", "short"}, {"long_case", "long"}}});
  declared["exploration_cases"].push_back(exploratory_case("followup"));
  const auto accepted = research::run_experiment(declared);
  check(accepted.at("status") == "ok" && accepted.at("runs").size() == 6 && accepted.at("skipped_cases").empty(),
        "successful verification and horizon-prefix checks allow declared exploration");
  check(!accepted.at("checks").empty(), "verification and prefix receipts are preserved");
  for (const auto& receipt : accepted.at("checks")) check(receipt.at("passed") == true, "all accepted checks pass");
}

void exact_array_expectations() {
  auto declared = plan();
  const auto jobs = ph::production::simulate(simple()).at("summary").at("jobs");
  for (const auto& policy : declared.at("policies"))
    declared["verification_cases"][0]["expectations"][policy.get<std::string>()] =
      {{"result", {{"summary", {{"jobs", jobs}}}}}};
  check(research::run_experiment(declared).at("status") == "ok",
        "an expected array containing complete exact job records passes");
  for (const auto& policy : declared.at("policies"))
    declared["verification_cases"][0]["expectations"][policy.get<std::string>()] =
      {{"result", {{"summary", {{"jobs", Json::array({{{"id", "job"}}})}}}}}};
  const auto partial = research::run_experiment(declared);
  check(partial.at("status") == "verification_failed",
        "array expectations do not accept partial nested objects even when their length matches");
  for (const auto& run : partial.at("runs"))
    check(run.at("expectation_passed") == false, "each incomplete expected job array fails explicitly");
  for (const auto& policy : declared.at("policies"))
    declared["verification_cases"][0]["expectations"][policy.get<std::string>()] =
      {{"result", {{"config", {{"shocks", Json::array()}}}}}};
  check(research::run_experiment(declared).at("status") == "ok",
        "an exact empty array remains a meaningful prediction rather than a vacuous object");
}

void waiting_reason_distributions_and_markdown() {
  auto config = simple();
  config["processes"][0]["stages"][0]["work_units"] = 6;
  auto other = config["processes"][0];
  other["id"] = "early-process";
  other["stages"][0]["work_units"] = 4;
  config["processes"].push_back(other);
  config["jobs"][0]["id"] = "job-late";
  config["jobs"][0]["deadline_tick"] = 8;
  config["jobs"].push_back({{"id", "job-early"}, {"process", "early-process"}, {"release_tick", 2}, {"deadline_tick", 2}});
  auto declared = plan();
  declared["verification_cases"][0] = verified_case("pause", config);
  declared["verification_cases"][0]["seeds"] = Json::array({0, 1, 2});
  const auto report = research::run_experiment(declared);
  check(report.at("status") == "ok", "the pause/wait reporting fixture passes its verification gate");
  for (const auto& summary : report.at("case_summaries")) {
    check(summary.at("sample_count") == 3, "waiting distributions retain every declared seed");
    const auto& reasons = summary.at("jobs").at("job-early").at("reason_distributions").at("stage").at("pre_start_reasons");
    check(reasons.at("awaiting_release").at("1") == 3,
          "unreleased time stays separate in every sample");
    const bool fifo = summary.at("policy") == "fifo";
    check(reasons.at("awaiting_labor").at(fifo ? "2" : "0") == 3,
          "labour waiting retains zero counts for the policy without that waiting");
    for (const auto& job : summary.at("jobs"))
      for (const auto& observed : job.at("reason_distributions"))
        for (const auto& category : observed)
          for (const auto& histogram : category) {
            Number denominator = 0;
            for (const auto& frequency : histogram) denominator += frequency.get<Number>();
            check(denominator == 3, "every reason histogram has the full sample denominator, including zeros");
          }
  }
  const auto markdown = research::render_markdown(report);
  check(markdown.find("Recorded waiting reasons") != std::string::npos &&
        markdown.find(R"(| pre\_start\_reasons | awaiting\_release | 1: 3 |)") != std::string::npos &&
        markdown.find(R"(| pre\_start\_reasons | awaiting\_labor | 2: 3 |)") != std::string::npos,
        "the rendered reason table exposes release and labour waiting separately with their seed frequencies");
}

void validation() {
  const auto invalid = [](const std::function<void(Json&)>& edit, const std::string& message) {
    auto declared = plan(); edit(declared);
    bool rejected = false;
    try { research::validate_plan(declared); } catch (const std::exception&) { rejected = true; }
    check(rejected, message);
  };
  invalid([](Json& p) { p["extra"] = true; }, "unknown plan fields are rejected");
  invalid([](Json& p) { p.erase("description"); }, "missing plan fields are rejected");
  invalid([](Json& p) { p["schema_version"] = "production.experiment.config.v2"; }, "unknown plan versions are rejected");
  invalid([](Json& p) { p["policies"] = Json::array(); }, "a plan needs a policy");
  invalid([](Json& p) { p["policies"] = Json::array({"fifo", "fifo"}); }, "duplicate policies are rejected");
  invalid([](Json& p) { p["policies"] = Json::array({"optimal"}); }, "unsupported policies are rejected");
  invalid([](Json& p) { p["verification_cases"] = Json::array(); }, "exploration needs a verification gate");
  invalid([](Json& p) { p["verification_cases"][0]["unknown"] = 1; }, "case fields are strict");
  invalid([](Json& p) { p["verification_cases"][0]["id"] = "has spaces"; }, "case identifiers follow the contract");
  invalid([](Json& p) { p["verification_cases"][0]["label"] = std::string(257, 'a'); }, "case labels are bounded");
  invalid([](Json& p) { p["description"] = std::string(2049, 'a'); }, "plan description is bounded");
  invalid([](Json& p) { p["verification_cases"][0]["seeds"] = Json::array(); }, "each case needs a seed");
  invalid([](Json& p) { p["verification_cases"][0]["seeds"] = Json::array({0, 0}); }, "duplicate seeds are rejected");
  invalid([](Json& p) { p["verification_cases"][0]["seeds"] = Json::array({-1}); }, "negative seeds are rejected");
  invalid([](Json& p) { p["verification_cases"][0]["seeds"] = Json::array({4294967296ULL}); }, "seeds exceed neither uint32 nor its parser range");
  invalid([](Json& p) { p["verification_cases"][0]["seeds"] = Json::array({1.0}); }, "floating-point seed tokens are rejected");
  invalid([](Json& p) { p["verification_cases"][0]["expectations"]["fifo"] = Json::object(); }, "verification expectations must be nonempty");
  invalid([](Json& p) { p["verification_cases"][0]["expectations"]["fifo"] = {{"result", Json::object()}}; },
          "nested empty objects cannot act as vacuous verification predictions");
  invalid([](Json& p) { p["verification_cases"][0]["expectations"].erase("fifo"); }, "each declared policy needs an expectation");
  invalid([](Json& p) { p["exploration_cases"].push_back(verified_case("followup")); }, "exploration must not carry pass/fail expectations");
  invalid([](Json& p) { p["exploration_cases"].push_back(exploratory_case("control")); }, "case IDs are unique across phases");
  invalid([](Json& p) { p["verification_cases"][0]["config"]["horizon_ticks"] = 0; }, "production configurations are validated");
  invalid([](Json& p) {
    for (int i = 1; i < 65; ++i) p["verification_cases"][0]["seeds"].push_back(i);
  }, "per-case seed bounds are enforced");
  invalid([](Json& p) {
    p["verification_cases"] = Json::array();
    for (int i = 0; i < 65; ++i) p["verification_cases"].push_back(verified_case("case-" + std::to_string(i)));
  }, "verification case count is bounded");
  invalid([](Json& p) {
    p["verification_cases"] = Json::array();
    for (int i = 0; i < 4; ++i) {
      auto one = verified_case("case-" + std::to_string(i)); one["seeds"] = Json::array();
      for (int seed = 0; seed < 33; ++seed) one["seeds"].push_back(seed);
      p["verification_cases"].push_back(std::move(one));
    }
  }, "aggregate trajectory count is bounded independently of case bounds");
  invalid([](Json& p) {
    p["policies"] = Json::array({"fifo"});
    p["verification_cases"][0]["expectations"].erase("earliest_deadline");
    p["verification_cases"][0]["seeds"] = Json::array({0, 1});
    auto& config = p["verification_cases"][0]["config"]; config["horizon_ticks"] = 1000;
    for (int i = 1; i < 128; ++i) {
      auto one = config["jobs"][0]; one["id"] = "job-" + std::to_string(i); config["jobs"].push_back(std::move(one));
    }
  }, "aggregate stage-tick budget accounts for seeds and policies");
  invalid([](Json& p) {
    auto& config = p["verification_cases"][0]["config"];
    config["horizon_ticks"] = 1000;
    for (int i = 2; i < 128; ++i)
      config["resources"].push_back({{"id", "stock-" + std::to_string(i)}, {"unit", "unit"}, {"initial_quantity", 0}});
    for (int i = 1; i < 32; ++i) {
      config["labor"].push_back({{"id", "labor-" + std::to_string(i)}, {"unit", "unit"},
                                  {"units_per_tick", 1}, {"compensation_due_per_tick", 0}});
      config["capacity"].push_back({{"id", "capacity-" + std::to_string(i)}, {"unit", "unit"}, {"units_per_tick", 1}});
    }
    ph::production::validate_config(config);
  }, "aggregate stock and pool observations are bounded even when the stage-tick budget would pass");

  const auto bad_prefix = [](const std::function<void(Json&)>& edit, const std::string& message) {
    auto declared = plan(); auto shorter = simple(); shorter["horizon_ticks"] = 2;
    auto longer = shorter; longer["horizon_ticks"] = 4;
    declared["verification_cases"] = Json::array({verified_case("short", shorter), verified_case("long", longer)});
    declared["prefix_checks"] = Json::array({{{"short_case", "short"}, {"long_case", "long"}}});
    edit(declared); bool rejected = false;
    try { research::validate_plan(declared); } catch (const std::exception&) { rejected = true; }
    check(rejected, message);
  };
  bad_prefix([](Json& p) { p["prefix_checks"][0]["long_case"] = "missing"; }, "prefix cases must exist");
  bad_prefix([](Json& p) { p["verification_cases"][1]["config"]["resources"][0]["initial_quantity"] = 99; },
             "prefix checks cannot change opening resources");
  bad_prefix([](Json& p) { p["verification_cases"][1]["seeds"] = Json::array({1}); }, "prefix checks must use the same seeds");
  bad_prefix([](Json& p) { p["verification_cases"][1]["config"]["horizon_ticks"] = 2; }, "prefix horizons must differ");
  bad_prefix([](Json& p) { p["prefix_checks"][0] = {{"short_case", "long"}, {"long_case", "short"}}; },
             "prefix ordering must run from shorter to longer horizon");
  bad_prefix([](Json& p) { p["prefix_checks"].push_back(p["prefix_checks"][0]); }, "duplicate prefix checks are rejected");
  bad_prefix([](Json& p) {
    p["verification_cases"].erase(p["verification_cases"].begin() + 1);
    auto longer = simple(); longer["horizon_ticks"] = 4;
    p["exploration_cases"].push_back(exploratory_case("long", longer));
  }, "prefix gates may refer only to verification cases");
}

void declared_protocol() {
  std::ifstream input(PH_SCHEDULING_PLAN_PATH, std::ios::binary);
  check(static_cast<bool>(input), "the reviewed scheduling protocol fixture is readable");
  const auto declared = ph::production::parse_json(std::string(std::istreambuf_iterator<char>(input), {}));
  research::validate_plan(declared);
  const auto report = research::run_experiment(declared);
  check(report.at("status") == "ok" && report.at("runs").size() == 102 && report.at("skipped_cases").empty(),
        "all corrected deterministic and declared stochastic protocol runs finish the research runner");
  for (const auto& receipt : report.at("checks")) check(receipt.at("passed") == true, "the protocol retains passing verification receipts");
  for (const auto& run : report.at("runs")) {
    const auto horizon = n(run.at("result").at("config"), "horizon_ticks");
    for (const auto& job : run.at("measurements").at("stages"))
      for (const auto& observed : job) partition(observed, horizon);
  }
  const auto markdown = research::render_markdown(report);
  check(!markdown.empty(), "a reviewable research report accompanies the full JSON receipt");
}
}  // namespace

int main() {
  try {
    delay_opportunity_and_completion_measurements();
    constrained_progress_and_observation_windows();
    parallel_stages_and_process_local_identifiers();
    comparisons_and_unfinished_aggregation();
    verification_gates_and_prefixes();
    exact_array_expectations();
    waiting_reason_distributions_and_markdown();
    validation();
    declared_protocol();
    std::cout << checks << " production experiment checks passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
