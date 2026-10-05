// SPDX-License-Identifier: MIT
#include "simulation.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using ph::production::Json;
using Number = std::int64_t;
int checks = 0;
void check(bool condition, const std::string& message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}
Number n(const Json& value, const char* key) { return value.at(key).get<Number>(); }
Json risk_zero() {
  return {{"delay_probability_bps", 0}, {"delay_age_increment_bps", 0}, {"delay_ticks", 1},
          {"output_loss_probability_bps", 0}, {"output_loss_age_increment_bps", 0}, {"output_loss_bps", 0}};
}
Json simple() {
  auto config = ph::production::default_config();
  config["horizon_ticks"] = 8;
  config["shocks"] = Json::array(); config["availability_risks"] = Json::array();
  config["resources"] = Json::array({{{"id", "input"}, {"unit", "input-unit"}, {"initial_quantity", 100}},
                                    {{"id", "output"}, {"unit", "output-unit"}, {"initial_quantity", 0}}});
  config["labor"] = Json::array({{{"id", "labor"}, {"unit", "labor-unit"}, {"units_per_tick", 2}, {"compensation_due_per_tick", 100}}});
  config["capacity"] = Json::array({{{"id", "capacity"}, {"unit", "capacity-unit"}, {"units_per_tick", 2}}});
  const Json stage = {{"id", "stage"}, {"depends_on", Json::array()}, {"work_units", 4}, {"max_work_per_tick", 4},
                      {"inputs", {{"input", 6}}}, {"outputs", {{"output", 3}}}, {"labor", {{"labor", 1}}},
                      {"capacity", {{"capacity", 1}}}, {"risk", risk_zero()}};
  config["processes"] = Json::array({{{"id", "process"}, {"stages", Json::array({stage})}}});
  config["jobs"] = Json::array({{{"id", "job"}, {"process", "process"}, {"release_tick", 1}, {"deadline_tick", 4}}});
  return config;
}
Json run(const Json& config) { return ph::production::simulate(config); }
Number finish(const Json& result, std::size_t index = 0) {
  const auto& value = result.at("summary").at("jobs")[index].at("completed_tick");
  return value.is_null() ? 0 : value.get<Number>();
}
void reject(const Json& config, const std::string& message) {
  const auto result = Json::parse(ph::production::run_json(Json({{"op", "simulate"}, {"config", config}}).dump()));
  check(result.at("status") == "error" && !result.contains("rows"), message);
}
void balances(const Json& result) {
  const auto& config = result.at("config");
  check(result.at("rows").size() == config.at("horizon_ticks").get<std::size_t>(), "declared horizon retained");
  for (const auto& resource : result.at("summary").at("resources")) {
    check(n(resource, "opening") + n(resource, "produced") - n(resource, "consumed") - n(resource, "lost") == n(resource, "closing"),
          "resource quantities reconcile independently");
    check(n(resource, "closing") >= 0, "stocks remain nonnegative");
  }
  for (const auto& row : result.at("rows")) {
    for (const auto& resource : row.at("resources")) check(resource.get<Number>() >= 0, "tick stock remains nonnegative");
    for (const auto& pool : row.at("labor")) check(n(pool, "used") >= 0 && n(pool, "used") <= n(pool, "available"), "labor respects available capacity");
    for (const auto& pool : row.at("capacity")) check(n(pool, "used") >= 0 && n(pool, "used") <= n(pool, "available"), "capacity is not double allocated");
    for (const auto& stage : row.at("stages")) check(n(stage, "work_completed") <= n(stage, "work_required"), "work never exceeds requirements");
  }
  check(result.at("summary").at("compensation_settlement") == "not_modeled", "obligations do not imply payments");
}
void hand_calculated_timeline() {
  const auto config = simple(), result = run(config); balances(result);
  check(finish(result) == 2, "four work units finish in two ticks with two labor/capacity units");
  check(result.at("rows")[0].at("resources").at("input") == 94 && result.at("rows")[0].at("resources").at("output") == 0,
        "inputs consumed once on start; output waits for completion");
  check(result.at("rows")[1].at("resources").at("output") == 3, "completion produces configured output");
  check(result.at("summary").at("labor_used").at("labor") == 4 && result.at("summary").at("capacity_used").at("capacity") == 4,
        "total labor and capacity use follow work requirements");
  check(n(result.at("summary"), "compensation_due") == 800, "compensation commitments cover the declared horizon");
  auto stretched = config; stretched["labor"][0]["units_per_tick"] = 1;
  const auto slow = run(stretched); balances(slow);
  check(finish(slow) == 4 && n(slow.at("summary"), "compensation_due") == 800, "labor scarcity stretches completion without cutting compensation");
  auto saturated = config; saturated["labor"][0]["units_per_tick"] = 8;
  check(finish(run(saturated)) == 2, "extra labor cannot bypass a capacity bottleneck");
  saturated["capacity"][0]["units_per_tick"] = 8;
  check(finish(run(saturated)) == 1, "concurrent work can use additional capacity");
  saturated["processes"][0]["stages"][0]["max_work_per_tick"] = 1;
  check(finish(run(saturated)) == 4, "process rate imposes saturation independently of labor");
  auto absent = config; absent["labor"][0]["units_per_tick"] = 0;
  const auto stalled = run(absent); balances(stalled);
  check(finish(stalled) == 0 && stalled.at("summary").at("resources").at("input").at("consumed") == 0,
        "unstartable work consumes no inputs");
  check(n(stalled.at("summary"), "missed_deadlines") == 1, "unfinished work exposes missed deadline");
}
void resources_dependencies_and_parallelism() {
  auto config = simple(); config["resources"][0]["initial_quantity"] = 5;
  const auto shortage = run(config); balances(shortage);
  check(finish(shortage) == 0 && shortage.at("rows")[0].at("stages")[0].at("reason") == "awaiting_resources", "resource shortage blocks start atomically");
  check(shortage.at("summary").at("resources").at("input").at("consumed") == 0, "shortage preserves stocks");
  config = simple(); auto stage = config["processes"][0]["stages"][0];
  stage["work_units"] = 1; stage["max_work_per_tick"] = 1; stage["id"] = "first";
  stage["outputs"] = {{"input", 2}};
  auto next = stage; next["id"] = "second"; next["depends_on"] = Json::array({"first"});
  next["inputs"] = {{"input", 2}}; next["outputs"] = {{"output", 3}};
  config["processes"][0]["stages"] = Json::array({next, stage});
  const auto sequential = run(config); balances(sequential);
  check(finish(sequential) == 2, "dependency order is independent of configuration order");
  check(sequential.at("rows")[0].at("stages")[0].at("work_this_tick") == 0, "successors wait for next-tick output");
  config["processes"][0]["stages"][0]["depends_on"] = Json::array();
  check(finish(run(config)) == 1, "independent stages can proceed in parallel");
  auto dual = simple(); auto second = dual["jobs"][0]; second["id"] = "second-job"; dual["jobs"].push_back(second);
  const auto shared = run(dual); balances(shared);
  check(finish(shared, 0) == 2 && finish(shared, 1) == 4, "jobs share finite capacity");
  check(shared.at("summary").at("resources").at("input").at("consumed") == 12, "each job consumes inputs once");
  auto producer = simple(); producer["resources"][0]["initial_quantity"] = 0;
  auto create = producer["processes"][0]["stages"][0];
  create["inputs"] = Json::object(); create["outputs"] = {{"input", 6}}; create["work_units"] = 1; create["max_work_per_tick"] = 1;
  producer["processes"].push_back({{"id", "source"}, {"stages", Json::array({create})}});
  const Json source_job = {{"id", "source-job"}, {"process", "source"}, {"release_tick", 1}, {"deadline_tick", nullptr}};
  producer["jobs"].insert(producer["jobs"].begin(), source_job);
  const auto delivered = run(producer); balances(delivered);
  check(delivered.at("rows")[0].at("stages")[1].at("work_this_tick") == 0 && finish(delivered, 1) == 3,
        "independent jobs cannot use newly produced stocks in the same tick");
  auto timed = simple(); timed["labor"] = Json::array(); timed["capacity"] = Json::array();
  timed["processes"][0]["stages"][0]["labor"] = Json::object();
  timed["processes"][0]["stages"][0]["capacity"] = Json::object();
  check(finish(run(timed)) == 1, "pure time-dependent stages require only their declared rate");
}
void scheduling_and_release() {
  auto config = simple(); auto second = config["jobs"][0]; config["jobs"][0]["deadline_tick"] = 8;
  second["id"] = "urgent"; second["deadline_tick"] = 2; config["jobs"].push_back(second);
  const auto fifo = run(config); balances(fifo);
  check(finish(fifo, 0) == 2 && finish(fifo, 1) == 4 && n(fifo.at("summary"), "missed_deadlines") == 1, "FIFO records missed deadline");
  config["scheduling_policy"] = "earliest_deadline";
  const auto deadline = run(config); balances(deadline);
  check(finish(deadline, 0) == 4 && finish(deadline, 1) == 2 && n(deadline.at("summary"), "missed_deadlines") == 0,
        "deadline scheduling is a distinct configurable policy");
  config = simple(); config["jobs"][0]["release_tick"] = 3;
  check(finish(run(config)) == 4, "work never precedes release");
  config["jobs"][0]["release_tick"] = 20; config["jobs"][0]["deadline_tick"] = nullptr;
  const auto future = run(config);
  check(finish(future) == 0 && n(future.at("summary"), "missed_deadlines") == 0, "future jobs are not falsely overdue");
}
void shock_and_risk() {
  auto config = simple();
  config["shocks"] = Json::array({{{"id", "shock"}, {"start_tick", 2}, {"end_tick", 3}, {"labor_factors", {{"labor", 0}}},
                                  {"capacity_factors", Json::object()}, {"resource_losses", {{"input", 200}}}}});
  const auto shocked = run(config); balances(shocked);
  check(finish(shocked) == 4, "scheduled shortage stretches work");
  check(shocked.at("summary").at("resources").at("input").at("lost") == 94 && shocked.at("rows")[2].at("resources").at("input") == 0,
        "loss occurs once, is stock-bounded, and leaves consumed inputs intact");
  auto extra = config["shocks"][0]; extra["id"] = "overlap"; extra["labor_factors"]["labor"] = 5000; extra["resource_losses"] = Json::object();
  config["shocks"].push_back(extra); auto reordered = config;
  std::reverse(reordered["shocks"].begin(), reordered["shocks"].end());
  check(run(config).at("rows") == run(reordered).at("rows"), "overlapping factors use order-independent minimum");
  config = simple();
  config["availability_risks"] = Json::array({{{"id", "absence"}, {"target_kind", "labor"}, {"target_id", "labor"},
    {"probability_bps", 10000}, {"duration_ticks", 2}, {"start_tick", 2}, {"end_tick", 2}}});
  const auto interrupted = run(config); balances(interrupted);
  check(finish(interrupted) == 4 && interrupted.at("rows")[2].at("labor").at("labor").at("available") == 0,
        "risk duration includes trigger tick and capacity returns afterward");
  check(n(interrupted.at("summary"), "compensation_due") == 800, "absence preserves compensation obligations");
  config = simple(); auto& risk = config["processes"][0]["stages"][0]["risk"];
  risk["delay_probability_bps"] = 10000; risk["delay_age_increment_bps"] = -10000; risk["delay_ticks"] = 2;
  const auto delay = run(config); balances(delay);
  check(finish(delay) == 4 && delay.at("rows")[0].at("labor").at("labor").at("used") == 0,
        "stage delay preserves progress and consumes no labor while blocked");
  check(delay.at("summary").at("resources").at("input").at("consumed") == 6, "delayed start consumes inputs once");
  config = simple(); auto& output_risk = config["processes"][0]["stages"][0]["risk"];
  output_risk["output_loss_age_increment_bps"] = 10000; output_risk["output_loss_bps"] = 5000;
  const auto deteriorated = run(config); balances(deteriorated);
  check(deteriorated.at("summary").at("resources").at("output").at("produced") == 1 &&
        deteriorated.at("summary").at("resources").at("output").at("unrealized_output") == 2,
        "elapsed exposure changes loss probability and output rounds down");
  auto fast = config; fast["labor"][0]["units_per_tick"] = 4; fast["capacity"][0]["units_per_tick"] = 4;
  check(run(fast).at("summary").at("resources").at("output").at("produced") == 3, "shorter timeline can reduce declared exposure");
  output_risk["output_loss_probability_bps"] = 10000; output_risk["output_loss_bps"] = 10000;
  const auto no_output = run(config);
  check(finish(no_output) == 2 && no_output.at("summary").at("resources").at("output").at("produced") == 0,
        "work completion is distinguished from output fulfillment");
}
void replay_and_boundaries() {
  const auto config = ph::production::default_config(), result = run(config); balances(result);
  check(result == run(config), "same configuration and seed replay exactly");
  auto shorter = config; shorter["horizon_ticks"] = 3; const auto prefix = run(shorter);
  for (std::size_t tick = 0; tick < 3; ++tick) check(prefix.at("rows")[tick] == result.at("rows")[tick], "shorter horizon preserves earlier trajectory");
  auto future = config; future["shocks"].push_back({{"id", "future"}, {"start_tick", 6}, {"end_tick", 8},
    {"labor_factors", {{"labor-a", 0}}}, {"capacity_factors", Json::object()}, {"resource_losses", Json::object()}});
  const auto later = run(future);
  for (std::size_t tick = 0; tick < 5; ++tick) check(later.at("rows")[tick] == result.at("rows")[tick], "future shocks preserve earlier work");
  for (int seed = 0; seed < 24; ++seed) {
    auto stochastic = simple(); stochastic["seed"] = seed;
    stochastic["processes"][0]["stages"][0]["risk"]["delay_probability_bps"] = 2500;
    stochastic["processes"][0]["stages"][0]["risk"]["output_loss_probability_bps"] = 5000;
    stochastic["processes"][0]["stages"][0]["risk"]["output_loss_bps"] = 5000;
    const auto sample = run(stochastic); balances(sample); check(sample == run(stochastic), "stochastic replay retains all events");
  }
  auto maximum = simple(); maximum["horizon_ticks"] = 1000; maximum["seed"] = 4294967295ULL;
  maximum["resources"][0]["initial_quantity"] = 1000000000000LL; maximum["labor"][0]["compensation_due_per_tick"] = 1000000000;
  const auto bounded = run(maximum); balances(bounded);
  check(n(bounded.at("summary"), "compensation_due") == 1000000000000LL, "bounded obligations retain integer precision");
}
void validation() {
  const auto invalid = [&](const std::function<void(Json&)>& edit, const std::string& message) {
    auto config = simple(); edit(config); reject(config, message);
  };
  invalid([](Json& c) { c["unknown"] = 1; }, "unknown fields rejected");
  invalid([](Json& c) { c.erase("currency"); }, "missing fields rejected");
  invalid([](Json& c) { c["seed"] = 1.0; }, "float integer token rejected");
  invalid([](Json& c) { c["seed"] = true; }, "boolean count rejected");
  invalid([](Json& c) { c["seed"] = 18446744073709551615ULL; }, "unsigned overflow rejected");
  invalid([](Json& c) { c["currency"] = "eur"; }, "currency convention validated");
  invalid([](Json& c) { c["horizon_ticks"] = 1001; }, "horizon bound enforced");
  invalid([](Json& c) { c["scheduling_policy"] = "maximize_profit"; }, "unsupported policy rejected");
  invalid([](Json& c) { c["resources"].push_back(c["resources"][0]); }, "duplicate resource ids rejected");
  invalid([](Json& c) { c["processes"][0]["stages"][0]["inputs"]["unknown"] = 1; }, "unknown resource reference rejected");
  invalid([](Json& c) { c["processes"][0]["stages"][0]["labor"]["labor"] = 0; }, "zero work ratio rejected");
  invalid([](Json& c) { c["processes"][0]["stages"][0]["work_units"] = 0; }, "zero-work stage rejected");
  invalid([](Json& c) { c["processes"][0]["stages"][0]["depends_on"] = Json::array({"stage"}); }, "self dependency rejected");
  invalid([](Json& c) { c["jobs"][0]["process"] = "unknown"; }, "unknown process rejected");
  invalid([](Json& c) { c["jobs"][0]["deadline_tick"] = 0; }, "invalid deadline rejected");
  invalid([](Json& c) { c["processes"][0]["stages"][0]["risk"]["delay_ticks"] = 0; }, "zero risk duration rejected");
  invalid([](Json& c) { c["processes"][0]["stages"][0]["risk"]["output_loss_bps"] = 10001; }, "invalid risk factor rejected");
  auto bad_utf = simple(); bad_utf["resources"][0]["unit"] = std::string("\xff");
  bool utf_rejected = false;
  try { static_cast<void>(run(bad_utf)); } catch (const std::exception&) { utf_rejected = true; }
  check(utf_rejected, "typed configuration validates UTF-8");
  auto cycle = simple(); auto other = cycle["processes"][0]["stages"][0];
  other["id"] = "other"; other["depends_on"] = Json::array({"stage"});
  cycle["processes"][0]["stages"][0]["depends_on"] = Json::array({"other"}); cycle["processes"][0]["stages"].push_back(other);
  reject(cycle, "multistage cycle rejected");
  auto excessive = simple(); excessive["horizon_ticks"] = 1000;
  for (int j = 1; j < 128; ++j) { auto job = excessive["jobs"][0]; job["id"] = "job-" + std::to_string(j); excessive["jobs"].push_back(job); }
  auto second = excessive["processes"][0]["stages"][0]; second["id"] = "second";
  excessive["processes"][0]["stages"].push_back(second); reject(excessive, "aggregate trajectory budget enforced");
  for (const auto& input : {std::string("{\"op\":\"defaults\",\"op\":\"defaults\"}"),
      std::string("{\"op\":\"defaults\",\"extra\":{\"a\":1,\"a\":2}}"),
      std::string("{\"op\":\"simulate\",\"config\":{}}"), std::string("[1e10000]"),
      std::string("{\"op\":\"") + static_cast<char>(0xff) + "\"}"}) {
    const auto error = Json::parse(ph::production::run_json(input));
    check(error.at("status") == "error" && !error.contains("rows"), "invalid command returns an error without results");
  }
  const auto deep = std::string(40, '[') + "0" + std::string(40, ']');
  check(Json::parse(ph::production::run_json(deep)).at("status") == "error", "excess nesting rejected");
  check(Json::parse(ph::production::run_json(std::string(2 * 1024 * 1024 + 1, ' '))).at("status") == "error", "input byte budget enforced");
  check(Json::parse(ph::production::run_json("{\"op\":\"defaults\"}")).at("config") == ph::production::default_config(), "defaults retain central configuration");

  const auto config_text = simple().dump();
  const auto command_text = Json({{"op", "simulate"}, {"config", simple()}}).dump();
  for (const auto& input : {
      std::string("{\"op\":\"defaults\"}") + '\0' + "trailing garbage",
      command_text + '\0' + "trailing garbage",
      config_text + '\0' + "trailing garbage",
      std::string("{\"field\":\"before") + '\0' + "after\"}"}) {
    bool null_rejected = false;
    try { static_cast<void>(ph::production::parse_json(input)); }
    catch (const std::invalid_argument&) { null_rejected = true; }
    check(null_rejected, "raw null bytes cannot terminate JSON parsing or occur inside a field");
    const auto error = Json::parse(ph::production::run_json(input));
    check(error.at("status") == "error" && !error.contains("rows"), "raw-null commands cannot produce partial-prefix results");
  }
  const auto escaped_null = ph::production::parse_json("{\"field\":\"before\\u0000after\"}");
  check(escaped_null.at("field") == std::string("before") + '\0' + "after", "an escaped null remains valid JSON syntax");
  auto control_unit = simple(); control_unit["time_unit"] = std::string("tick") + '\0' + "suffix";
  reject(control_unit, "escaped control characters remain rejected by configuration text validation");
}
}
int main() {
  try {
    hand_calculated_timeline(); resources_dependencies_and_parallelism(); scheduling_and_release();
    shock_and_risk(); replay_and_boundaries(); validation();
    std::cout << checks << " production research simulator checks passed\n"; return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
