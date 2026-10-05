// SPDX-License-Identifier: MIT
#pragma once

#include "simulation.hpp"
#include <string>

namespace ph::production::research {
// Observations of a production.sim.v1 result, without changing model state.
Json measure_run(const Json& result);
// Strict, bounded experiment plans. All state and output remain local.
void validate_plan(const Json& plan);
Json run_experiment(const Json& plan);
// Presentation of the same observations; no additional economic inference.
std::string render_markdown(const Json& report);
}  // namespace ph::production::research