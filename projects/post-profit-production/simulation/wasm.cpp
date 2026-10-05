// SPDX-License-Identifier: MIT
// Thin WebAssembly boundaries. All parsing, model state and measurements stay
// in the same C++ implementation used by the native command-line programs.
#include "simulation.hpp"
#include "experiment.hpp"

#include <exception>
#include <string>

extern "C" const char* production_run(const char* command) {
  static std::string response;
  response = ph::production::run_json(command ? command : "");
  return response.c_str();
}

extern "C" const char* production_experiment_run(const char* plan) {
  static std::string response;
  try {
    response = ph::production::research::run_experiment(
        ph::production::parse_json(plan ? plan : "")).dump();
  } catch (const std::exception& error) {
    using ph::production::Json;
    response = Json({{"schema_version", "production.experiment.v1"},
                     {"status", "error"},
                     {"error", {{"code", "invalid_experiment"}, {"message", error.what()}}}})
        .dump(-1, ' ', false, Json::error_handler_t::replace);
  }
  return response.c_str();
}
