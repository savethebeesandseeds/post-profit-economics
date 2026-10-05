// SPDX-License-Identifier: MIT
#include "simulation.hpp"
#include "default_config.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

namespace ph::production {
namespace {
using Quantity = std::int64_t;
using Amounts = std::map<std::string, Quantity>;
constexpr Quantity bps = 10000;
constexpr Quantity stage_tick_limit = 200000;

void require(bool condition, const std::string& message) {
  if (!condition) throw std::invalid_argument(message);
}
void keys(const Json& value, std::initializer_list<const char*> expected,
          const std::string& where) {
  require(value.is_object() && value.size() == expected.size(),
          where + " has missing or unknown fields");
  for (const auto* key : expected) require(value.contains(key), where + " is missing " + key);
}
Quantity integer(const Json& value, Quantity low, Quantity high, const std::string& where) {
  require(value.is_number_integer(), where + " must be an integer token");
  if (value.is_number_unsigned()) {
    const auto number = value.get<std::uint64_t>();
    require(number <= static_cast<std::uint64_t>(high), where + " exceeds its bound");
    require(low <= 0 || number >= static_cast<std::uint64_t>(low), where + " is below its bound");
    return static_cast<Quantity>(number);
  }
  const auto number = value.get<Quantity>();
  require(number >= low && number <= high, where + " is outside its bounds");
  return number;
}
std::string text(const Json& value, const std::string& where, std::size_t limit = 64) {
  require(value.is_string(), where + " must be text");
  const auto result = value.get<std::string>();
  require(!result.empty() && result.size() <= limit, where + " has invalid length");
  for (unsigned char character : result) {
    require(character >= 32 && character != 127, where + " contains a control character");
  }
  // Direct typed callers must meet the same UTF-8 contract as parsed callers.
  try { static_cast<void>(value.dump()); }
  catch (const Json::exception&) { throw std::invalid_argument(where + " has invalid UTF-8"); }
  return result;
}
std::string id(const Json& value, const std::string& where) {
  const auto result = text(value, where);
  for (unsigned char character : result) {
    require((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                (character >= '0' && character <= '9') || character == '-' || character == '_' || character == '.',
            where + " must use letters, digits, dash, underscore or dot");
  }
  return result;
}
void array(const Json& value, std::size_t low, std::size_t high, const std::string& where) {
  require(value.is_array() && value.size() >= low && value.size() <= high,
          where + " has invalid array size");
}
std::set<std::string> identities(const Json& values, const std::string& where) {
  std::set<std::string> result;
  for (const auto& value : values) {
    require(value.is_object() && value.contains("id"), where + " needs an id");
    require(result.insert(id(value.at("id"), where + ".id")).second, where + " has duplicate ids");
  }
  return result;
}
void amounts(const Json& value, const std::set<std::string>& allowed, Quantity low,
             Quantity high, const std::string& where) {
  require(value.is_object(), where + " must be an object");
  for (auto entry = value.begin(); entry != value.end(); ++entry) {
    require(allowed.count(entry.key()) != 0, where + " references unknown id " + entry.key());
    integer(entry.value(), low, high, where + "." + entry.key());
  }
}
Amounts quantities(const Json& value) {
  Amounts result;
  for (auto entry = value.begin(); entry != value.end(); ++entry) result.emplace(entry.key(), entry.value().get<Quantity>());
  return result;
}
Quantity number(const Json& value, const char* key) { return value.at(key).get<Quantity>(); }

std::uint64_t mix(std::uint64_t value) {
  value = (value ^ (value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
  value = (value ^ (value >> 27)) * UINT64_C(0x94d049bb133111eb);
  return value ^ (value >> 31);
}
bool occurrence(std::uint64_t seed, Quantity tick, Quantity probability,
                std::initializer_list<std::string> parts) {
  if (probability == 0) return false;
  if (probability == bps) return true;
  // Length-delimited identities avoid ambiguous job/stage/risk concatenations.
  std::uint64_t hash = UINT64_C(14695981039346656037);
  for (const auto& part : parts) {
    hash ^= part.size(); hash *= UINT64_C(1099511628211);
    for (unsigned char character : part) { hash ^= character; hash *= UINT64_C(1099511628211); }
  }
  const auto draw = mix(hash ^ mix(seed) ^ mix(static_cast<std::uint64_t>(tick)));
  return draw % static_cast<std::uint64_t>(bps) < static_cast<std::uint64_t>(probability);
}
Quantity probability(const Json& risk, const char* base, const char* increment, Quantity age) {
  return std::clamp(number(risk, base) + age * number(risk, increment), Quantity{0}, bps);
}
struct StageState {
  Quantity work = 0, start = 0, completed = 0, blocked_until = 0;
};
struct JobState {
  std::vector<StageState> stages;
  Quantity completed = 0;
};
struct Candidate {
  std::size_t job, stage;
};
}  // namespace

Json parse_json(const std::string& input) {
  require(input.size() <= 2 * 1024 * 1024, "JSON input exceeds 2 MiB");
  // A raw NUL is never valid JSON. Reject it before the lexer can mistake it
  // for end-of-input and accept a valid prefix while ignoring trailing bytes.
  require(input.find('\0') == std::string::npos, "JSON input contains a raw null byte");
  std::vector<std::set<std::string>> objects;
  const auto callback = [&](int depth, Json::parse_event_t event, Json& parsed) {
    require(depth <= 32, "JSON nesting exceeds 32");
    if (event == Json::parse_event_t::object_start) objects.emplace_back();
    if (event == Json::parse_event_t::key) {
      require(!objects.empty() && objects.back().insert(parsed.get<std::string>()).second,
              "duplicate JSON key");
    }
    if (event == Json::parse_event_t::object_end) objects.pop_back();
    return true;
  };
  try { return Json::parse(input, callback); }
  catch (const Json::exception&) { throw std::invalid_argument("invalid JSON encoding or syntax"); }
}

void validate_config(const Json& config) {
  keys(config, {"schema_version", "time_unit", "currency", "horizon_ticks", "seed", "scheduling_policy",
                "resources", "labor", "capacity", "processes", "jobs", "shocks", "availability_risks"}, "configuration");
  require(config.at("schema_version") == "production.config.v1", "unsupported configuration schema");
  text(config.at("time_unit"), "time_unit", 32);
  const auto currency = text(config.at("currency"), "currency", 3);
  require(currency.size() == 3 && std::all_of(currency.begin(), currency.end(), [](char c) { return c >= 'A' && c <= 'Z'; }),
          "currency must be three uppercase letters");
  const auto horizon = integer(config.at("horizon_ticks"), 1, 1000, "horizon_ticks");
  integer(config.at("seed"), 0, 4294967295LL, "seed");
  require(config.at("scheduling_policy") == "fifo" || config.at("scheduling_policy") == "earliest_deadline",
          "unknown scheduling policy");
  array(config.at("resources"), 1, 128, "resources");
  array(config.at("labor"), 0, 32, "labor");
  array(config.at("capacity"), 0, 32, "capacity");
  array(config.at("processes"), 1, 32, "processes");
  array(config.at("jobs"), 1, 128, "jobs");
  array(config.at("shocks"), 0, 128, "shocks");
  array(config.at("availability_risks"), 0, 128, "availability_risks");
  const auto resource_ids = identities(config.at("resources"), "resources");
  const auto labor_ids = identities(config.at("labor"), "labor");
  const auto capacity_ids = identities(config.at("capacity"), "capacity");
  const auto process_ids = identities(config.at("processes"), "processes");
  identities(config.at("jobs"), "jobs");
  identities(config.at("shocks"), "shocks");
  identities(config.at("availability_risks"), "availability_risks");
  for (const auto& resource : config.at("resources")) {
    keys(resource, {"id", "unit", "initial_quantity"}, "resource");
    text(resource.at("unit"), "resource.unit", 32);
    integer(resource.at("initial_quantity"), 0, 1000000000000LL, "initial_quantity");
  }
  for (const auto& pool : config.at("labor")) {
    keys(pool, {"id", "unit", "units_per_tick", "compensation_due_per_tick"}, "labor pool");
    text(pool.at("unit"), "labor.unit", 32);
    integer(pool.at("units_per_tick"), 0, 1000000000, "labor.units_per_tick");
    integer(pool.at("compensation_due_per_tick"), 0, 1000000000, "compensation_due_per_tick");
  }
  for (const auto& pool : config.at("capacity")) {
    keys(pool, {"id", "unit", "units_per_tick"}, "capacity pool");
    text(pool.at("unit"), "capacity.unit", 32);
    integer(pool.at("units_per_tick"), 0, 1000000000, "capacity.units_per_tick");
  }
  std::map<std::string, std::size_t> stage_counts;
  for (const auto& process : config.at("processes")) {
    keys(process, {"id", "stages"}, "process");
    const auto& stages = process.at("stages");
    array(stages, 1, 16, "stages");
    const auto stage_ids = identities(stages, "stages");
    std::map<std::string, std::vector<std::string>> dependencies;
    for (const auto& stage : stages) {
      keys(stage, {"id", "depends_on", "work_units", "max_work_per_tick", "inputs", "outputs", "labor", "capacity", "risk"}, "stage");
      integer(stage.at("work_units"), 1, 1000000, "work_units");
      integer(stage.at("max_work_per_tick"), 1, 1000000, "max_work_per_tick");
      amounts(stage.at("inputs"), resource_ids, 1, 1000000000, "stage.inputs");
      amounts(stage.at("outputs"), resource_ids, 1, 1000000000, "stage.outputs");
      amounts(stage.at("labor"), labor_ids, 1, 1000000, "stage.labor");
      amounts(stage.at("capacity"), capacity_ids, 1, 1000000, "stage.capacity");
      array(stage.at("depends_on"), 0, 15, "depends_on");
      std::set<std::string> seen;
      auto& list = dependencies[stage.at("id").get<std::string>()];
      for (const auto& dependency : stage.at("depends_on")) {
        const auto name = id(dependency, "dependency");
        require(stage_ids.count(name) && seen.insert(name).second, "unknown or duplicate stage dependency");
        list.push_back(name);
      }
      const auto& risk = stage.at("risk");
      keys(risk, {"delay_probability_bps", "delay_age_increment_bps", "delay_ticks", "output_loss_probability_bps",
                  "output_loss_age_increment_bps", "output_loss_bps"}, "stage.risk");
      integer(risk.at("delay_probability_bps"), 0, bps, "delay_probability_bps");
      integer(risk.at("delay_age_increment_bps"), -bps, bps, "delay_age_increment_bps");
      integer(risk.at("delay_ticks"), 1, 1000, "delay_ticks");
      integer(risk.at("output_loss_probability_bps"), 0, bps, "output_loss_probability_bps");
      integer(risk.at("output_loss_age_increment_bps"), -bps, bps, "output_loss_age_increment_bps");
      integer(risk.at("output_loss_bps"), 0, bps, "output_loss_bps");
    }
    std::map<std::string, int> colors;
    std::function<void(const std::string&)> visit = [&](const std::string& name) {
      require(colors[name] != 1, "production stage dependencies contain a cycle");
      if (colors[name] == 2) return;
      colors[name] = 1;
      for (const auto& predecessor : dependencies.at(name)) visit(predecessor);
      colors[name] = 2;
    };
    for (const auto& name : stage_ids) visit(name);
    stage_counts.emplace(process.at("id").get<std::string>(), stages.size());
  }
  Quantity stage_count = 0;
  for (const auto& job : config.at("jobs")) {
    keys(job, {"id", "process", "release_tick", "deadline_tick"}, "job");
    const auto name = id(job.at("process"), "job.process");
    require(process_ids.count(name) != 0, "job references unknown process");
    const auto release = integer(job.at("release_tick"), 1, 1000000, "release_tick");
    if (!job.at("deadline_tick").is_null()) integer(job.at("deadline_tick"), release, 1000000, "deadline_tick");
    stage_count += static_cast<Quantity>(stage_counts.at(name));
  }
  require(stage_count * horizon <= stage_tick_limit, "configuration exceeds 200000 stage-tick records");
  for (const auto& shock : config.at("shocks")) {
    keys(shock, {"id", "start_tick", "end_tick", "labor_factors", "capacity_factors", "resource_losses"}, "shock");
    const auto start = integer(shock.at("start_tick"), 1, 1000000, "shock.start_tick");
    integer(shock.at("end_tick"), start, 1000000, "shock.end_tick");
    amounts(shock.at("labor_factors"), labor_ids, 0, bps, "shock.labor_factors");
    amounts(shock.at("capacity_factors"), capacity_ids, 0, bps, "shock.capacity_factors");
    amounts(shock.at("resource_losses"), resource_ids, 1, 1000000000, "shock.resource_losses");
  }
  for (const auto& risk : config.at("availability_risks")) {
    keys(risk, {"id", "target_kind", "target_id", "probability_bps", "duration_ticks", "start_tick", "end_tick"}, "availability risk");
    require(risk.at("target_kind") == "labor" || risk.at("target_kind") == "capacity", "unknown availability target kind");
    const auto& allowed = risk.at("target_kind") == "labor" ? labor_ids : capacity_ids;
    require(allowed.count(id(risk.at("target_id"), "risk.target_id")) != 0, "unknown availability target");
    integer(risk.at("probability_bps"), 0, bps, "risk.probability_bps");
    integer(risk.at("duration_ticks"), 1, 1000, "risk.duration_ticks");
    const auto start = integer(risk.at("start_tick"), 1, 1000000, "risk.start_tick");
    integer(risk.at("end_tick"), start, 1000000, "risk.end_tick");
  }
}

Json default_config() { return parse_json(ph_production_default_config); }

Json simulate(const Json& config) {
  validate_config(config);
  const auto horizon = number(config, "horizon_ticks");
  const auto seed = config.at("seed").get<std::uint64_t>();
  Amounts stock, initial, consumed, produced, lost, unrealized;
  Amounts labor_base, capacity_base, labor_used_total, capacity_used_total;
  Amounts labor_blocked, capacity_blocked;
  Quantity compensation_per_tick = 0;
  for (const auto& resource : config.at("resources")) {
    const auto name = resource.at("id").get<std::string>();
    initial[name] = stock[name] = number(resource, "initial_quantity");
    consumed[name] = produced[name] = lost[name] = unrealized[name] = 0;
  }
  for (const auto& pool : config.at("labor")) {
    const auto name = pool.at("id").get<std::string>();
    labor_base[name] = number(pool, "units_per_tick"); labor_used_total[name] = 0;
    compensation_per_tick += number(pool, "compensation_due_per_tick");
  }
  for (const auto& pool : config.at("capacity")) {
    const auto name = pool.at("id").get<std::string>();
    capacity_base[name] = number(pool, "units_per_tick"); capacity_used_total[name] = 0;
  }
  const auto& jobs = config.at("jobs");
  std::map<std::string, const Json*> processes;
  for (const auto& process : config.at("processes")) processes.emplace(process.at("id").get<std::string>(), &process.at("stages"));
  std::vector<JobState> state(jobs.size());
  std::vector<std::map<std::string, std::size_t>> stage_index(jobs.size());
  for (std::size_t j = 0; j < jobs.size(); ++j) {
    const auto& stages = *processes.at(jobs[j].at("process").get<std::string>());
    state[j].stages.resize(stages.size());
    for (std::size_t s = 0; s < stages.size(); ++s) stage_index[j].emplace(stages[s].at("id").get<std::string>(), s);
  }
  Json rows = Json::array(), events = Json::array();
  for (Quantity tick = 1; tick <= horizon; ++tick) {
    Amounts labor_available = labor_base, capacity_available = capacity_base;
    // Overlapping deterministic shocks use the minimum declared factor. Their
    // effect is independent of array order and never expands workload limits.
    Amounts labor_factor, capacity_factor;
    for (const auto& entry : labor_base) labor_factor[entry.first] = bps;
    for (const auto& entry : capacity_base) capacity_factor[entry.first] = bps;
    for (const auto& shock : config.at("shocks")) {
      if (tick == number(shock, "start_tick")) {
        for (const auto& entry : quantities(shock.at("resource_losses"))) {
          const auto loss = std::min(stock.at(entry.first), entry.second);
          stock.at(entry.first) -= loss; lost.at(entry.first) += loss;
          events.push_back({{"tick", tick}, {"type", "resource_loss"}, {"shock", shock.at("id")},
                            {"resource", entry.first}, {"requested", entry.second}, {"actual", loss}});
        }
      }
      if (tick < number(shock, "start_tick") || tick > number(shock, "end_tick")) continue;
      for (const auto& entry : quantities(shock.at("labor_factors"))) labor_factor.at(entry.first) = std::min(labor_factor.at(entry.first), entry.second);
      for (const auto& entry : quantities(shock.at("capacity_factors"))) capacity_factor.at(entry.first) = std::min(capacity_factor.at(entry.first), entry.second);
    }
    for (const auto& risk : config.at("availability_risks")) {
      if (tick < number(risk, "start_tick") || tick > number(risk, "end_tick")) continue;
      if (!occurrence(seed, tick, number(risk, "probability_bps"), {"availability", risk.at("id").get<std::string>()})) continue;
      const auto kind = risk.at("target_kind").get<std::string>(), target = risk.at("target_id").get<std::string>();
      auto& blocked = kind == "labor" ? labor_blocked[target] : capacity_blocked[target];
      blocked = std::max(blocked, tick + number(risk, "duration_ticks") - 1);
      events.push_back({{"tick", tick}, {"type", "availability_interruption"}, {"risk", risk.at("id")},
                        {"target_kind", kind}, {"target_id", target}, {"blocked_until", blocked}});
    }
    for (auto& entry : labor_available) {
      entry.second = labor_blocked[entry.first] >= tick ? 0 : entry.second * labor_factor.at(entry.first) / bps;
    }
    for (auto& entry : capacity_available) {
      entry.second = capacity_blocked[entry.first] >= tick ? 0 : entry.second * capacity_factor.at(entry.first) / bps;
    }
    const auto labor_opening = labor_available, capacity_opening = capacity_available;
    std::vector<Candidate> candidates;
    std::vector<std::vector<std::string>> reasons(jobs.size());
    std::vector<std::vector<Quantity>> work(jobs.size());
    for (std::size_t j = 0; j < jobs.size(); ++j) {
      const auto& stages = *processes.at(jobs[j].at("process").get<std::string>());
      reasons[j].resize(stages.size()); work[j].resize(stages.size());
      for (std::size_t s = 0; s < stages.size(); ++s) {
        if (state[j].stages[s].completed) { reasons[j][s] = "completed"; continue; }
        if (tick < number(jobs[j], "release_tick")) { reasons[j][s] = "awaiting_release"; continue; }
        bool ready = true;
        for (const auto& dependency : stages[s].at("depends_on")) {
          ready = ready && state[j].stages[stage_index[j].at(dependency.get<std::string>())].completed != 0;
        }
        if (!ready) { reasons[j][s] = "awaiting_dependencies"; continue; }
        candidates.push_back({j, s});
      }
    }
    const bool deadline_policy = config.at("scheduling_policy") == "earliest_deadline";
    const auto priority = [&](const Candidate& candidate) {
      const auto& job = jobs[candidate.job];
      const Quantity deadline = job.at("deadline_tick").is_null() ? std::numeric_limits<Quantity>::max() : number(job, "deadline_tick");
      return std::make_tuple(deadline_policy ? deadline : Quantity{0}, number(job, "release_tick"), candidate.job, candidate.stage);
    };
    std::sort(candidates.begin(), candidates.end(), [&](const Candidate& a, const Candidate& b) { return priority(a) < priority(b); });
    // Plan every allocation before producing any output. Dependencies and new
    // output stocks become available at the next tick, regardless of job order.
    for (const auto& candidate : candidates) {
      const auto j = candidate.job, s = candidate.stage;
      const auto& stage = (*processes.at(jobs[j].at("process").get<std::string>()))[s];
      auto& current = state[j].stages[s];
      auto& reason = reasons[j][s];
      if (current.blocked_until >= tick) { reason = "risk_delay"; continue; }
      const auto input = quantities(stage.at("inputs"));
      if (!current.start && std::any_of(input.begin(), input.end(), [&](const auto& entry) { return stock.at(entry.first) < entry.second; })) {
        reason = "awaiting_resources"; continue;
      }
      const auto labor = quantities(stage.at("labor")), capacity = quantities(stage.at("capacity"));
      Quantity rate = std::min(number(stage, "max_work_per_tick"), number(stage, "work_units") - current.work);
      for (const auto& entry : labor) rate = std::min(rate, labor_available.at(entry.first) / entry.second);
      if (!rate) { reason = "awaiting_labor"; continue; }
      for (const auto& entry : capacity) rate = std::min(rate, capacity_available.at(entry.first) / entry.second);
      if (!rate) { reason = "awaiting_capacity"; continue; }
      if (!current.start) {
        current.start = tick;
        for (const auto& entry : input) { stock.at(entry.first) -= entry.second; consumed.at(entry.first) += entry.second; }
        events.push_back({{"tick", tick}, {"type", "stage_started"}, {"job", jobs[j].at("id")}, {"stage", stage.at("id")}, {"inputs_consumed", input}});
      }
      const auto& risk = stage.at("risk");
      const auto chance = probability(risk, "delay_probability_bps", "delay_age_increment_bps", tick - current.start);
      if (occurrence(seed, tick, chance, {"stage_delay", jobs[j].at("id").get<std::string>(), stage.at("id").get<std::string>()})) {
        current.blocked_until = tick + number(risk, "delay_ticks") - 1;
        reason = "risk_delay";
        events.push_back({{"tick", tick}, {"type", "stage_delay"}, {"job", jobs[j].at("id")}, {"stage", stage.at("id")},
                          {"probability_bps", chance}, {"blocked_until", current.blocked_until}});
        continue;
      }
      work[j][s] = rate; reason = "working";
      for (const auto& entry : labor) labor_available.at(entry.first) -= rate * entry.second;
      for (const auto& entry : capacity) capacity_available.at(entry.first) -= rate * entry.second;
    }
    Json stage_rows = Json::array();
    for (std::size_t j = 0; j < jobs.size(); ++j) {
      const auto& stages = *processes.at(jobs[j].at("process").get<std::string>());
      bool complete = true;
      for (std::size_t s = 0; s < stages.size(); ++s) {
        const auto& stage = stages[s]; auto& current = state[j].stages[s];
        current.work += work[j][s];
        if (!current.completed && current.work == number(stage, "work_units")) {
          current.completed = tick;
          const auto& risk = stage.at("risk");
          const auto chance = probability(risk, "output_loss_probability_bps", "output_loss_age_increment_bps", tick - current.start);
          const auto loss_factor = occurrence(seed, tick, chance,
              {"output_loss", jobs[j].at("id").get<std::string>(), stage.at("id").get<std::string>()}) ? number(risk, "output_loss_bps") : 0;
          Amounts actual, rejected;
          for (const auto& entry : quantities(stage.at("outputs"))) {
            const auto output = entry.second * (bps - loss_factor) / bps;
            actual[entry.first] = output; rejected[entry.first] = entry.second - output;
            stock.at(entry.first) += output; produced.at(entry.first) += output; unrealized.at(entry.first) += entry.second - output;
          }
          events.push_back({{"tick", tick}, {"type", "stage_completed"}, {"job", jobs[j].at("id")}, {"stage", stage.at("id")},
                            {"elapsed_ticks", tick - current.start + 1}, {"output_loss_probability_bps", chance},
                            {"output_loss_bps", loss_factor}, {"outputs_produced", actual}, {"outputs_unrealized", rejected}});
        }
        complete = complete && current.completed != 0;
        stage_rows.push_back({{"job", jobs[j].at("id")}, {"stage", stage.at("id")}, {"work_this_tick", work[j][s]},
                              {"work_completed", current.work}, {"work_required", stage.at("work_units")},
                              {"status", current.completed ? "completed" : (current.start ? "active" : "pending")},
                              {"reason", reasons[j][s]}, {"start_tick", current.start ? Json(current.start) : Json(nullptr)},
                              {"completed_tick", current.completed ? Json(current.completed) : Json(nullptr)}});
      }
      if (complete && !state[j].completed) {
        state[j].completed = tick;
        events.push_back({{"tick", tick}, {"type", "job_completed"}, {"job", jobs[j].at("id")}});
      }
    }
    Json labor_row = Json::object(), capacity_row = Json::object();
    for (const auto& pool : config.at("labor")) {
      const auto name = pool.at("id").get<std::string>();
      const auto used = labor_opening.at(name) - labor_available.at(name); labor_used_total.at(name) += used;
      labor_row[name] = {{"available", labor_opening.at(name)}, {"used", used}, {"compensation_due", pool.at("compensation_due_per_tick")}};
    }
    for (const auto& entry : capacity_opening) {
      const auto used = entry.second - capacity_available.at(entry.first); capacity_used_total.at(entry.first) += used;
      capacity_row[entry.first] = {{"available", entry.second}, {"used", used}};
    }
    rows.push_back({{"tick", tick}, {"resources", stock}, {"labor", labor_row}, {"capacity", capacity_row},
                    {"compensation_due", compensation_per_tick}, {"stages", std::move(stage_rows)}});
  }
  Json job_summary = Json::array(), resource_summary = Json::object();
  Quantity completed_jobs = 0, missed_deadlines = 0;
  for (std::size_t j = 0; j < jobs.size(); ++j) {
    const auto deadline = jobs[j].at("deadline_tick");
    const bool missed = !deadline.is_null() && (state[j].completed ? state[j].completed > deadline.get<Quantity>() : horizon >= deadline.get<Quantity>());
    completed_jobs += state[j].completed != 0; missed_deadlines += missed;
    job_summary.push_back({{"id", jobs[j].at("id")}, {"process", jobs[j].at("process")}, {"release_tick", jobs[j].at("release_tick")},
                           {"deadline_tick", deadline}, {"completed_tick", state[j].completed ? Json(state[j].completed) : Json(nullptr)},
                           {"status", state[j].completed ? "completed" : "unfinished"}, {"missed_deadline", missed}});
  }
  for (const auto& resource : config.at("resources")) {
    const auto name = resource.at("id").get<std::string>();
    require(stock.at(name) == initial.at(name) + produced.at(name) - consumed.at(name) - lost.at(name), "internal resource reconciliation failure");
    resource_summary[name] = {{"unit", resource.at("unit")}, {"opening", initial.at(name)}, {"produced", produced.at(name)},
                              {"consumed", consumed.at(name)}, {"lost", lost.at(name)}, {"closing", stock.at(name)}, {"unrealized_output", unrealized.at(name)}};
  }
  return {{"schema_version", "production.sim.v1"}, {"status", "ok"}, {"config", config}, {"rows", std::move(rows)}, {"events", std::move(events)},
          {"summary", {{"completed_jobs", completed_jobs}, {"unfinished_jobs", static_cast<Quantity>(jobs.size()) - completed_jobs},
                       {"missed_deadlines", missed_deadlines}, {"jobs", std::move(job_summary)}, {"resources", std::move(resource_summary)},
                       {"labor_used", labor_used_total}, {"capacity_used", capacity_used_total},
                       {"compensation_due", compensation_per_tick * horizon}, {"compensation_settlement", "not_modeled"}}}};
}

std::string run_json(const std::string& command) {
  try {
    const auto request = parse_json(command);
    require(request.is_object() && request.contains("op"), "command needs an op");
    if (request.at("op") == "defaults") {
      keys(request, {"op"}, "defaults command");
      return Json({{"schema_version", "production.sim.v1"}, {"status", "ok"}, {"config", default_config()}}).dump();
    }
    require(request.at("op") == "simulate", "unknown command");
    keys(request, {"op", "config"}, "simulate command");
    return simulate(request.at("config")).dump();
  } catch (const std::exception& error) {
    return Json({{"schema_version", "production.sim.v1"}, {"status", "error"},
                 {"error", {{"code", "invalid_production"}, {"message", error.what()}}}})
        .dump(-1, ' ', false, Json::error_handler_t::replace);
  }
}
}  // namespace ph::production
